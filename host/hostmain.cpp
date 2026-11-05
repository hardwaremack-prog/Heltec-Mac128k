// PC test harness for Heltec-Mac: same emulator, VNC and browser-view code as the ESP32 build.
//   ./machost --rom plus.rom --disk system.img     import (like the web page) and run
//   ./machost --testrom                            run a tiny built-in 68000 test program
//   ./machost --frames 600 --dump out.pbm          headless: run N frames, save the screen
// VNC on :5900, browser WebSocket on :5981, flash images kept in ./flash-rom.bin / ./flash-disk.bin
#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/socket.h>
#include <unistd.h>
#include <errno.h>
#include <chrono>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <thread>
#include <atomic>
#include <vector>
#include <string>
#include "../Heltec-Mac/macmachine.h"
#include "../Heltec-Mac/mackeys.h"
#include "../Heltec-Mac/macvnc.h"
#include "../Heltec-Mac/webview.h"
extern "C" {
#include "../Heltec-Mac/src/core/m68k.h"
#include "../Heltec-Mac/src/core/umac.h"
extern uint8_t* _ram_base;
}
#include <ctime>
// This computer's local time as Mac time (seconds since 1 Jan 1904, local)
static uint32_t macNow() {
  time_t t = time(nullptr); struct tm lt; localtime_r(&t, &lt);
  return (uint32_t)(t + lt.tm_gmtoff + 2082844800LL);
}
#define PROGMEM
#include "../Heltec-Mac/webui.h"
#include "../Heltec-Mac/macfiles_js.h"

// ---------------------------------------------------------------- flash stand-in (a file)
struct FileArea : FlashArea {
  std::vector<uint8_t> d;
  std::string path;
  FileArea(const std::string& p, size_t n) : d(n, 0xFF), path(p) {
    FILE* f = fopen(p.c_str(), "rb");
    if (f) { size_t r = fread(d.data(), 1, n, f); (void)r; fclose(f); }
  }
  void save() { FILE* f = fopen(path.c_str(), "wb"); fwrite(d.data(), 1, d.size(), f); fclose(f); }
  size_t size() const override { return d.size(); }
  bool read(uint32_t off, void* buf, size_t n) override {
    if (off + n > d.size()) return false;
    memcpy(buf, d.data() + off, n); return true;
  }
  bool write(uint32_t off, const void* buf, size_t n) override {
    if (off + n > d.size()) return false;
    const uint8_t* s = (const uint8_t*)buf;
    for (size_t i = 0; i < n; i++) {
      if ((d[off + i] & s[i]) != s[i]) { fprintf(stderr, "flash: write to unerased byte at %u\n", (unsigned)(off + i)); return false; }
      d[off + i] &= s[i];
    }
    return true;
  }
  bool erase(uint32_t off, size_t n) override {
    if ((off & 4095) || (n & 4095) || off + n > d.size()) { fprintf(stderr, "flash: bad erase %u+%u\n", (unsigned)off, (unsigned)n); return false; }
    memset(d.data() + off, 0xFF, n); return true;
  }
  const uint8_t* map(uint32_t off, size_t) override { return d.data() + off; }
  void unmap() override {}
};

// ---------------------------------------------------------------- sockets
struct SockConn : VncConn {
  int fd;
  bool ok = true;
  explicit SockConn(int f) : fd(f) {
    fcntl(fd, F_SETFL, fcntl(fd, F_GETFL) | O_NONBLOCK);
    int one = 1;
    setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof one);
    if (getenv("SMALLSNDBUF")) { int sz = 4096; setsockopt(fd, SOL_SOCKET, SO_SNDBUF, &sz, sizeof sz); }   // test: act like the board's small buffers
  }
  int read(uint8_t* b, int n) override {
    int r = ::recv(fd, b, n, 0);
    if (r == 0) { ok = false; return 0; }
    if (r < 0) { if (errno != EAGAIN && errno != EWOULDBLOCK) ok = false; return 0; }
    return r;
  }
  int writeSome(const uint8_t* b, int n) override {
    int w = ::send(fd, b, n, MSG_NOSIGNAL | MSG_DONTWAIT);
    if (w >= 0) return w;
    if (errno == EAGAIN || errno == EWOULDBLOCK) return 0;
    ok = false; return -1;
  }
  void idle() override { usleep(1000); }
  bool write(const uint8_t* b, int n) override {
    while (n > 0) {
      int w = ::send(fd, b, n, MSG_NOSIGNAL);
      if (w < 0) {
        if (errno == EAGAIN) { usleep(1000); continue; }
        ok = false; return false;
      }
      b += w; n -= w;
    }
    return true;
  }
  bool connected() override { return ok; }
  void close() override { ::close(fd); }
};

static int listenOn(int port) {
  int s = socket(AF_INET, SOCK_STREAM, 0);
  int one = 1;
  setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);
  sockaddr_in a{};
  a.sin_family = AF_INET; a.sin_port = htons(port); a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  if (bind(s, (sockaddr*)&a, sizeof a) < 0 || listen(s, 2) < 0) { perror("listen"); exit(1); }
  fcntl(s, F_SETFL, O_NONBLOCK);
  return s;
}

static std::vector<uint8_t> loadFile(const std::string& p) {
  std::ifstream f(p, std::ios::binary);
  return std::vector<uint8_t>((std::istreambuf_iterator<char>(f)), {});
}

static void dumpPBM(const char* path) {
  const uint8_t* fb = mac.framebuffer();
  FILE* f = fopen(path, "wb");
  fprintf(f, "P4 %d %d\n", MAC_W, MAC_H);
  if (fb) fwrite(fb, 1, MAC_W / 8 * MAC_H, f);
  fclose(f);
}

// A tiny 68000 program: fills the screen buffer with an incrementing byte pattern, forever.
static std::vector<uint8_t> testRom(uint32_t ramBytes, bool sound) {
  std::vector<uint8_t> r(0x20000, 0);
  uint32_t fbAddr = 0x600000 + ramBytes - (512 * 342 / 8 + 0x380);
  auto w16 = [&](int o, uint16_t v) { r[o] = v >> 8; r[o + 1] = v; };
  auto w32 = [&](int o, uint32_t v) { w16(o, v >> 16); w16(o + 2, v); };
  w32(0, 0x00610000);        // initial SSP (RAM at 0x600000 while the overlay is on)
  w32(4, 0x00400008);        // initial PC (ROM at 0x400000)
  int p = 8;
  if (sound) {  // sawtooth into the sound buffer, volume 7, sound on
    w16(p, 0x43F9); w32(p + 2, 0x600000 + ramBytes - 0x300); p += 6;   // lea snd,a1
    w16(p, 0x343C); w16(p + 2, 369); p += 4;                            // move.w #369,d2
    w16(p, 0x7600); p += 2;                                             // moveq #0,d3
    int l2 = p;
    w16(p, 0x1283); p += 2;                                             // move.b d3,(a1)
    w16(p, 0x5489); p += 2;                                             // addq.l #2,a1
    w16(p, 0x5E03); p += 2;                                             // addq.b #7,d3
    w16(p, 0x51CA); w16(p + 2, (uint16_t)(l2 - (p + 2))); p += 4;       // dbra d2,l2
    w16(p, 0x13FC); w16(p + 2, 0x0080); w32(p + 4, 0xEFE1FE); p += 8;   // move.b #$80,VIA ORB
    w16(p, 0x13FC); w16(p + 2, 0x0000); w32(p + 4, 0xEFE1FE); p += 8;   // move.b #$00,VIA ORB (sound on)
    w16(p, 0x13FC); w16(p + 2, 0x0007); w32(p + 4, 0xEFE3FE); p += 8;   // move.b #$07,VIA ORA (volume 7)
  }
  w16(p, 0x41F9); w32(p + 2, fbAddr); p += 6;  // lea fb,a0
  w16(p, 0x303C); w16(p + 2, 21887); p += 4;   // move.w #21887,d0
  w16(p, 0x7200); p += 2;                       // moveq #0,d1
  int loop = p;
  w16(p, 0x10C1); p += 2;                       // move.b d1,(a0)+
  w16(p, 0x5201); p += 2;                       // addq.b #1,d1
  w16(p, 0x51C8); w16(p + 2, (uint16_t)(loop - (p + 2))); p += 4;  // dbra d0,loop
  w16(p, 0x60FE);                               // bra.s *
  return r;
}

static std::atomic<int> pendingUse{-99};   // /api/use from the web thread, done in the main loop

int main(int argc, char** argv) {
  std::string romFile, dump;
  std::vector<std::string> diskFiles;
  int useIdx = -1, removeIdx = -1;
  int frames = 0, ramKB = 0;
  bool test = false, fresh = false, sound = false;
  for (int i = 1; i < argc; i++) {
    std::string a = argv[i];
    if (a == "--rom" && i + 1 < argc) romFile = argv[++i];
    else if (a == "--disk" && i + 1 < argc) diskFiles.push_back(argv[++i]);
    else if (a == "--use" && i + 1 < argc) useIdx = atoi(argv[++i]);
    else if (a == "--remove" && i + 1 < argc) removeIdx = atoi(argv[++i]);
    else if (a == "--frames" && i + 1 < argc) frames = atoi(argv[++i]);
    else if (a == "--dump" && i + 1 < argc) dump = argv[++i];
    else if (a == "--ram" && i + 1 < argc) ramKB = atoi(argv[++i]);
    else if (a == "--testrom") test = true;
    else if (a == "--testsound") test = sound = true;
    else if (a == "--fresh") fresh = true;
    else { fprintf(stderr, "unknown option %s\n", a.c_str()); return 1; }
  }
  if (fresh) { unlink("flash-rom.bin"); unlink("flash-disk.bin"); }
  FileArea romArea("flash-rom.bin", ROM_AREA_SIZE);
  FileArea diskArea("flash-disk.bin", 0x5A8000);
  mac.begin(&romArea, &diskArea);
  if (ramKB) mac.setRamSetting(ramKB);

  std::string msg;
  if (!romFile.empty()) {
    auto d = loadFile(romFile);
    mac.beginRomUpload();
    for (size_t o = 0; o < d.size(); o += 1436) mac.romUploadChunk(d.data() + o, std::min<size_t>(1436, d.size() - o));
    bool ok = mac.finishRomUpload(msg);
    printf("ROM: %s %s\n", ok ? "OK" : "REJECTED", msg.c_str());
  }
  for (size_t di = 0; di < diskFiles.size(); di++) {
    const std::string& diskFile = diskFiles[di];
    auto d = loadFile(diskFile);
    std::string n = diskFile.substr(diskFile.find_last_of('/') + 1);
    mac.beginDiskUpload(n, -1, di == 0);
    for (size_t o = 0; o < d.size(); o += 1436) mac.diskUploadChunk(d.data() + o, std::min<size_t>(1436, d.size() - o));
    bool ok = mac.finishDiskUpload(msg);
    printf("Disk: %s %s (library slot %d)\n", ok ? "OK" : "REJECTED", msg.c_str(), mac.lastUploadIndex);
  }
  if (useIdx >= 0) printf("Use disk %d: %s\n", useIdx, mac.useDisk(useIdx) ? "OK" : "no such disk");
  if (removeIdx >= 0) printf("Remove disk %d: %s\n", removeIdx, mac.removeDisk(removeIdx) ? "OK" : "no such disk");
  {
    const MacMeta& mm = mac.meta();
    for (int i = 0; i < LIB_MAX; i++) if (mm.lib[i].size) printf("  library %d%s: %s  %u bytes at %u\n", i, i == mm.active ? " (in use)" : "", mm.lib[i].name, mm.lib[i].size, mm.lib[i].off);
    printf("  free: %u KB\n", mac.libraryFree() / 1024);
    if (getenv("DUMPLIB")) for (int i = 0; i < LIB_MAX; i++) if (mm.lib[i].size) {
      char p[64]; snprintf(p, sizeof p, "lib%d.raw", i); FILE* f = fopen(p, "wb");
      fwrite(diskArea.d.data() + mm.lib[i].off, 1, mm.lib[i].size, f); fclose(f); }
  }

  std::vector<uint8_t> trom;
  if (test) {
    uint32_t bytes = (ramKB ? ramKB : 208) * 1024;
    trom = testRom(bytes, sound);
    mac.startTest(trom.data(), bytes);
    printf("Running test ROM with %u KB RAM\n", bytes / 1024);
  } else {
    // pretend we're an ESP32-S3 with ~360 KB free before Wi-Fi
    if (!mac.start(getenv("HEAP_KB") ? atoi(getenv("HEAP_KB")) * 1024 : 16 << 20, getenv("HEAP_KB") ? atoi(getenv("HEAP_KB")) * 1024 : 16 << 20, 56 * 1024, msg)) { printf("Can't start: %s\n", msg.c_str()); }
    else printf("Mac started with %u KB RAM\n", mac.ramBytes() / 1024);
    umac_rtc_set(macNow());
  }
  if (getenv("PRINTFILE")) {   // test hook: printer port bytes to a file
    static FILE* pf = fopen(getenv("PRINTFILE"), "wb");
    umac_set_printer([] { return 1; }, [](uint8_t b) { fputc(b, pf); fflush(pf); });
  }

  VncServer vnc;
  WebView web;
  vnc.fb = web.fb = [] { return mac.framebuffer(); };
  vnc.frame = web.frame = [] { return (uint32_t)mac.frameCount; };
  vnc.onKey = web.onKey = [](uint32_t k, bool d) { macKeys.push(k, d); };
  vnc.onPointer = web.onPointer = [](int x, int y, bool b) { mac.mouse(x, y, b); if (getenv("MOUSELOG")) { printf("mouse %d,%d %lld\n", x, y, (long long)std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count()); fflush(stdout); } };
  vnc.onPaste = web.onPaste = [](const std::string& t) { macKeys.typeText(t.c_str()); };
  vnc.onDisconnect = web.onDisconnect = [] { macKeys.releaseAll(); };
  web.onAudio = [](bool on) { mac.audioWanted = on; };
  web.nextAudio = [](uint8_t* a) { return mac.popAudio(a); };
  web.audioPending = [] { return mac.audioQueued(); };
  web.nextPrint = [](uint8_t* b, int max) { return mac.printerRead(b, max); };
  web.onClock = [](uint32_t t, int tz) { mac.setClock(t, tz, true); printf("Clock set from the browser (UTC%+d min)\n", tz); };

  auto t0 = std::chrono::steady_clock::now();
  auto now = [&] { return (int64_t)std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - t0).count(); };

  if (frames && !mac.running()) { romArea.save(); diskArea.save(); return 0; }
  if (frames) {   // headless
    uint32_t lastTrace = 0;
    bool sndCount = getenv("SNDCOUNT"); int sndFrames = 0, firstSnd = -1; uint8_t ab[MacMachine::AUDIO_BYTES];
    if (sndCount) mac.audioWanted = true;
    while ((int)mac.frameCount < frames) {
      if (sndCount) while (mac.popAudio(ab)) if (ab[0] != 0xFF) { sndFrames++; if (firstSnd < 0) firstSnd = mac.frameCount; }
      if (getenv("TRACE") && mac.frameCount / 60 != lastTrace) {
        lastTrace = mac.frameCount / 60;
        printf("t=%us PC=%06X SR=%04X D0=%08X A0=%08X\n", lastTrace, m68k_get_reg(NULL, M68K_REG_PC),
               m68k_get_reg(NULL, M68K_REG_SR), m68k_get_reg(NULL, M68K_REG_D0), m68k_get_reg(NULL, M68K_REG_A0));
      }
      int64_t s = mac.step(now());
      if (s > 0) usleep(s);
    }
    if (mac.crashed()) printf("Mac stopped (emulator error)\n");
    if (getenv("CLOCKCHECK") && _ram_base) {
      uint32_t t = (_ram_base[0x20c] << 24) | (_ram_base[0x20d] << 16) | (_ram_base[0x20e] << 8) | _ram_base[0x20f];
      time_t u = (time_t)t - 2082844800LL; struct tm g; gmtime_r(&u, &g); char b[64]; strftime(b, sizeof b, "%Y-%m-%d %H:%M:%S", &g);
      printf("Mac clock (Time global): %s   clock chip: %u   PC local: %u\n", b, umac_rtc_get(), macNow());
    }
    printf("Ran %u frames, %u slices\n", (unsigned)mac.frameCount, (unsigned)mac.loops);
    if (sndCount) printf("Sound: %d frames with sound, first at frame %d (%.2f s)\n", sndFrames, firstSnd, firstSnd / 60.0);
    if (!dump.empty()) dumpPBM(dump.c_str());
    mac.flushDisk();
    romArea.save(); diskArea.save();
    return 0;
  }

  std::atomic<bool> quit{false};
  std::thread emu([&] {
    if (getenv("HOLD")) {   // like the board after a page restart: wait for a browser, then 0.4 s
      int64_t since = 0;
      while (!quit) { if (web.active()) { if (!since) since = now(); if (now() - since > 400000) break; } else since = 0; usleep(20000); }
      printf("Browser connected: starting the Mac\n"); fflush(stdout);
    }
    while (!quit) {
      int u = pendingUse.exchange(-99);
      if (u != -99) { bool ok = mac.useDisk(u); if (ok) mac.reset(); printf("Use disk %d: %s, Mac reset\n", u, ok ? "OK" : "no such disk"); fflush(stdout); }
      int64_t s = mac.step(now());
      if (s > 0) usleep(s > 2000 ? 2000 : s);
    }
  });
  int ls = listenOn(5900), lw = listenOn(5981), lh = listenOn(8080);
  // tiny HTTP server for the web page (WebSocket port rewritten to 5981) and /api/status
  std::thread httpd([&] {
    std::string page = INDEX_HTML;
    size_t p = page.find(":81/");
    if (p != std::string::npos) page.replace(p, 4, ":5981/");
    for (;;) {
      int c = accept(lh, nullptr, nullptr);
      if (c < 0) { usleep(20000); continue; }
      fcntl(c, F_SETFL, 0);
      char req[2048]; int n = recv(c, req, sizeof req - 1, 0); req[n > 0 ? n : 0] = 0;
      std::string r(req), body, type = "text/html";
      if (r.find("GET /macfiles.js") == 0) { body = MACFILES_JS; type = "application/javascript"; }
      else if (r.find("GET /sit-arsenic.js") == 0) { body = SIT_ARSENIC_JS; type = "application/javascript"; }
      else if (r.find("GET /imagewriter.js") == 0) { body = IMAGEWRITER_JS; type = "application/javascript"; }
      else if (r.find("POST /api/use?i=") == 0) { pendingUse = atoi(r.c_str() + 16); body = "ok"; type = "text/plain"; }
      else if (r.find("GET /api/status") == 0) {
        // same fields as the board's /api/status (no Wi-Fi or heap on a PC)
        const MacMeta& mm = mac.meta();
        auto esc = [](std::string v) { std::string o; for (char ch : v) { if (ch == '"' || ch == '\\') o += '\\'; o += ch; } return o; };
        auto describe = [&](uint32_t off, uint32_t size) {
          bool boot; std::string fs, vol; inspectDisk(&diskArea, off, size, boot, fs, vol);
          std::string d = fs; if (vol.size()) d += " \"" + vol + "\"";
          d += boot ? (d.size() ? ", startup disk" : "startup disk") : (d.size() ? ", not a startup disk" : "not a startup disk");
          return d; };
        uint8_t ck[8] = {(uint8_t)(mm.romChecksum >> 24), (uint8_t)(mm.romChecksum >> 16), (uint8_t)(mm.romChecksum >> 8), (uint8_t)mm.romChecksum};
        std::string lib; bool first = true;
        for (int i = 0; i < LIB_MAX; i++) if (mm.lib[i].size) {
          lib += std::string(first ? "" : ",") + "{\"i\":" + std::to_string(i) + ",\"n\":\"" + esc(mm.lib[i].name) + "\",\"s\":" + std::to_string(mm.lib[i].size) +
                 ",\"d\":\"" + esc(describe(mm.lib[i].off, mm.lib[i].size)) + "\"}";
          first = false; }
        body = std::string("{\"ip\":\"localhost\",\"mode\":\"sta\",\"ssid\":\"(PC test build)\",\"rssi\":0,\"state\":\"") + (mac.crashed() ? "Stopped (error)" : "Running") +
               "\",\"ram\":" + std::to_string(mac.ramBytes()) + ",\"ramSetting\":0,\"speed\":100,\"turbo\":0,\"vnc\":" + std::to_string(vnc.active()) +
               ",\"web\":" + std::to_string(web.active()) + ",\"romOk\":" + std::to_string(mm.romOk) + ",\"romName\":\"" + identifyRom(ck, 8).name +
               "\",\"disk\":" + std::to_string(mm.diskSize) + ",\"diskName\":\"" + esc(mm.diskName) + "\",\"diskInfo\":\"\",\"diskRO\":" + std::to_string(mm.diskRO) +
               ",\"heap\":0,\"heapBlock\":0,\"active\":" + std::to_string(mm.active) + ",\"libFree\":" + std::to_string(mac.libraryFree()) + ",\"lib\":[" + lib + "]}";
        type = "application/json";
      } else body = page;
      std::string h = "HTTP/1.1 200 OK\r\nContent-Type: " + type + "\r\nContent-Length: " + std::to_string(body.size()) + "\r\nConnection: close\r\n\r\n";
      send(c, h.data(), h.size(), MSG_NOSIGNAL); send(c, body.data(), body.size(), MSG_NOSIGNAL);
      close(c);
    }
  });
  printf("Web page http://localhost:8080, VNC localhost:5900, WebSocket :5981\n");
  uint32_t lastLoops = 0; int64_t lastT = now();
  for (;;) {
    int c = accept(ls, nullptr, nullptr);
    if (c >= 0) vnc.attach(new SockConn(c));
    c = accept(lw, nullptr, nullptr);
    if (c >= 0) web.attach(new SockConn(c));
    vnc.poll();
    web.poll();
    if (getenv("DUMPRAM") && access(getenv("DUMPRAM"), F_OK) == 0 && _ram_base) {   // test hook: dump Mac RAM
      std::string out = std::string(getenv("DUMPRAM")) + ".ram"; FILE* f = fopen(out.c_str(), "wb");
      fwrite(_ram_base, 1, mac.ramBytes(), f); fclose(f); unlink(getenv("DUMPRAM"));
    }
    if (now() - lastT > 5000000) {
      uint32_t l = mac.loops;
      if (getenv("CLOCKCHECK") && _ram_base) {
        uint32_t t = (_ram_base[0x20c] << 24) | (_ram_base[0x20d] << 16) | (_ram_base[0x20e] << 8) | _ram_base[0x20f];
        time_t u = (time_t)t - 2082844800LL; struct tm g; gmtime_r(&u, &g); char b[64]; strftime(b, sizeof b, "%Y-%m-%d %H:%M:%S", &g);
        printf("Mac clock: %s\n", b); fflush(stdout);
      }
      printf("speed %d%%  vnc:%d web:%d\n", (int)((l - lastLoops) * 5000.0 * 100 / (now() - lastT)), vnc.active(), web.active());
      lastLoops = l; lastT = now();
      romArea.save(); diskArea.save();
    }
    usleep(2000);
  }
}
