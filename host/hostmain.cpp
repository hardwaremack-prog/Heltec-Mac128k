// PC test harness for Mac Mesh: same emulator, VNC and browser-view code as the ESP32 build.
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
#include <cstdio>
#include <cstring>
#include <fstream>
#include <thread>
#include <atomic>
#include <vector>
#include <string>
#include "../MacMesh/macmachine.h"
#include "../MacMesh/mackeys.h"
#include "../MacMesh/macvnc.h"
#include "../MacMesh/webview.h"
#define PROGMEM
#include "../MacMesh/webui.h"

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
  }
  int read(uint8_t* b, int n) override {
    int r = ::recv(fd, b, n, 0);
    if (r == 0) { ok = false; return 0; }
    if (r < 0) { if (errno != EAGAIN && errno != EWOULDBLOCK) ok = false; return 0; }
    return r;
  }
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

int main(int argc, char** argv) {
  std::string romFile, diskFile, dump;
  int frames = 0, ramKB = 0;
  bool test = false, fresh = false, sound = false;
  for (int i = 1; i < argc; i++) {
    std::string a = argv[i];
    if (a == "--rom" && i + 1 < argc) romFile = argv[++i];
    else if (a == "--disk" && i + 1 < argc) diskFile = argv[++i];
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
  if (!diskFile.empty()) {
    auto d = loadFile(diskFile);
    std::string n = diskFile.substr(diskFile.find_last_of('/') + 1);
    mac.beginDiskUpload(n);
    for (size_t o = 0; o < d.size(); o += 1436) mac.diskUploadChunk(d.data() + o, std::min<size_t>(1436, d.size() - o));
    bool ok = mac.finishDiskUpload(msg);
    printf("Disk: %s %s\n", ok ? "OK" : "REJECTED", msg.c_str());
  }

  std::vector<uint8_t> trom;
  if (test) {
    uint32_t bytes = (ramKB ? ramKB : 208) * 1024;
    trom = testRom(bytes, sound);
    mac.startTest(trom.data(), bytes);
    printf("Running test ROM with %u KB RAM\n", bytes / 1024);
  } else {
    // pretend we're an ESP32-S3 with ~360 KB free before Wi-Fi
    if (!mac.start(360 * 1024, 300 * 1024, 110 * 1024, msg)) { printf("Can't start: %s\n", msg.c_str()); }
    else printf("Mac started with %u KB RAM\n", mac.ramBytes() / 1024);
  }

  VncServer vnc;
  WebView web;
  vnc.fb = web.fb = [] { return mac.framebuffer(); };
  vnc.frame = web.frame = [] { return (uint32_t)mac.frameCount; };
  vnc.onKey = web.onKey = [](uint32_t k, bool d) { macKeys.push(k, d); };
  vnc.onPointer = web.onPointer = [](int x, int y, bool b) { mac.mouse(x, y, b); };
  vnc.onPaste = web.onPaste = [](const std::string& t) { macKeys.typeText(t.c_str()); };
  vnc.onDisconnect = web.onDisconnect = [] { macKeys.releaseAll(); };
  web.onAudio = [](bool on) { mac.audioWanted = on; };
  web.nextAudio = [](uint8_t* a) { return mac.popAudio(a); };

  auto t0 = std::chrono::steady_clock::now();
  auto now = [&] { return (int64_t)std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - t0).count(); };

  if (frames && !mac.running()) { romArea.save(); diskArea.save(); return 0; }
  if (frames) {   // headless
    while ((int)mac.frameCount < frames) {
      int64_t s = mac.step(now());
      if (s > 0) usleep(s);
    }
    if (mac.crashed()) printf("Mac stopped (emulator error)\n");
    printf("Ran %u frames, %u slices\n", (unsigned)mac.frameCount, (unsigned)mac.loops);
    if (!dump.empty()) dumpPBM(dump.c_str());
    mac.flushDisk();
    romArea.save(); diskArea.save();
    return 0;
  }

  std::atomic<bool> quit{false};
  std::thread emu([&] {
    while (!quit) {
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
      if (r.find("GET /api/status") == 0) {
        char j[512];
        snprintf(j, sizeof j, "{\"ip\":\"127.0.0.1\",\"mode\":\"sta\",\"ssid\":\"host\",\"rssi\":0,\"state\":\"%s\",\"ram\":%u,\"ramSetting\":0,"
                 "\"speed\":100,\"turbo\":0,\"vnc\":%d,\"web\":%d,\"romOk\":%d,\"romName\":\"Mac Plus ROM v3\",\"disk\":%u,"
                 "\"diskName\":\"%s\",\"diskInfo\":\"\",\"diskRO\":0,\"heap\":100000}",
                 mac.crashed() ? "Stopped (error)" : "Running", mac.ramBytes(), vnc.active(), web.active(), mac.meta().romOk,
                 mac.meta().diskSize, mac.meta().diskName);
        body = j; type = "application/json";
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
    if (now() - lastT > 5000000) {
      uint32_t l = mac.loops;
      printf("speed %d%%  vnc:%d web:%d\n", (int)((l - lastLoops) * 5000.0 * 100 / (now() - lastT)), vnc.active(), web.active());
      lastLoops = l; lastT = now();
      romArea.save(); diskArea.save();
    }
    usleep(2000);
  }
}
