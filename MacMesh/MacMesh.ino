/*
  Mac Mesh  -  a Macintosh Plus on a Heltec WiFi LoRa 32 V3 (ESP32-S3), in your browser or over VNC.

  - 68000 CPU + Mac Plus hardware (umac by Matt Evans, Musashi by Karl Stenerud) on CPU core 1
  - Wi-Fi, web page with the live screen + sound (port 80, WebSocket 81), VNC (5900) on core 0
  - Your own Mac Plus v3 ROM and a startup disk image, uploaded from the web page into flash
  - The ESP32-S3 has ~512 KB of RAM, so this Mac has 128-256 KB (the ROM is patched to accept it)
  - OLED shows the address; PRG button: tap = mini Mac screen on the OLED, hold 2 s = restart the Mac

  Board: "Heltec WiFi LoRa 32(V3)" (esp32 Arduino core 3.x). partitions.csv in this folder makes
  a 1.9 MB app + ROM area + ~5.6 MB disk area.
*/
#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <ESPmDNS.h>
#include <Preferences.h>
#include <Wire.h>
#include <esp_heap_caps.h>
#include "macmachine.h"
#include "mackeys.h"
#include "macvnc.h"
#include "webview.h"
#include "espflash.h"
#include "webui.h"
#include "macfiles_js.h"
#include "oledfont.h"

// ---------------- Heltec V3 pins
#define PIN_VEXT 36      // LOW = power to OLED
#define PIN_OLED_RST 21
#define PIN_OLED_SDA 17
#define PIN_OLED_SCL 18
#define PIN_LED 35
#define PIN_BUTTON 0     // PRG button

static const char* AP_SSID = "MacMesh-Setup";
static const char* AP_PASS = "macmesh1";
static const char* HOSTNAME = "macmesh";
static const size_t RUN_RESERVE = 56 * 1024;    // heap to keep free for Wi-Fi traffic, web page, VNC (Wi-Fi itself is already up)

static PartitionArea romPart, diskPart;
static VncServer vnc;
static WebView webView;
static WebServer web(80);
static WiFiServer vncListener(5900), wsListener(81);
static Preferences prefs;

static bool apMode = false;
static std::string startError;
static volatile int speedPct = 0;
static volatile int oledPage = 0;
static volatile bool cmdReset = false, pauseReq = false, paused = false, macReady = false;
static bool safeMode = false;
static uint32_t restartAt = 0;

// ---------------- OLED (SSD1306 128x64)
struct Oled {
  uint8_t buf[1024];
  bool ok = false;
  void cmd(uint8_t c) { Wire.beginTransmission(0x3C); Wire.write(0x00); Wire.write(c); Wire.endTransmission(); }
  void begin() {
    pinMode(PIN_VEXT, OUTPUT); digitalWrite(PIN_VEXT, LOW);
    delay(50);
    pinMode(PIN_OLED_RST, OUTPUT);
    digitalWrite(PIN_OLED_RST, LOW); delay(20); digitalWrite(PIN_OLED_RST, HIGH); delay(20);
    Wire.begin(PIN_OLED_SDA, PIN_OLED_SCL, 400000);
    Wire.beginTransmission(0x3C);
    ok = Wire.endTransmission() == 0;
    if (!ok) return;
    const uint8_t init[] = {0xAE, 0xD5, 0x80, 0xA8, 0x3F, 0xD3, 0x00, 0x40, 0x8D, 0x14, 0x20, 0x00, 0xA1, 0xC8,
                            0xDA, 0x12, 0x81, 0xCF, 0xD9, 0xF1, 0xDB, 0x40, 0xA4, 0xA6, 0xAF};
    for (uint8_t c : init) cmd(c);
    clear(); show();
  }
  void clear() { memset(buf, 0, sizeof buf); }
  void pset(int x, int y) { if (x >= 0 && x < 128 && y >= 0 && y < 64) buf[(y >> 3) * 128 + x] |= 1 << (y & 7); }
  void text(int col, int row, const char* s, bool inv = false) {   // 21 columns x 8 rows
    for (; *s && col < 21; s++, col++) {
      int ch = (unsigned char)*s;
      if (ch >= 'a' && ch <= 'z') ch -= 32;
      if (ch < 32 || ch > 95) ch = '?';
      for (int x = 0; x < 6; x++) {
        uint8_t bits = x < 5 ? OLED_FONT[ch - 32][x] : 0;
        if (inv) bits = ~bits & 0x7F;
        for (int y = 0; y < 8; y++) if (bits & (1 << y)) pset(col * 6 + x, row * 8 + y);
      }
    }
  }
  void show() {
    if (!ok) return;
    cmd(0x21); cmd(0); cmd(127); cmd(0x22); cmd(0); cmd(7);
    for (int i = 0; i < 1024; i += 16) {
      Wire.beginTransmission(0x3C);
      Wire.write(0x40);
      Wire.write(buf + i, 16);
      Wire.endTransmission();
    }
  }
} oled;

static String ipText() { return apMode ? String("192.168.4.1") : WiFi.localIP().toString(); }

static const char* stateText() {
  if (safeMode) return "Safe mode";
  if (!mac.running()) return mac.meta().romOk ? "Not running" : "Needs a ROM";
  if (mac.crashed()) return "Stopped (error)";
  if (paused) return "Paused";
  return "Running";
}

static void drawOled() {
  oled.clear();
  const uint8_t* fb = mac.framebuffer();
  if (oledPage == 1 && fb) {   // mini Mac screen: a pixel is lit where the block is mostly black
    for (int oy = 0; oy < 64; oy++) {
      int y0 = oy * MAC_H / 64, y1 = (oy + 1) * MAC_H / 64;
      for (int ox = 0; ox < 128; ox++) {
        int black = 0, total = 0;
        for (int y = y0; y < y1; y++) {
          uint8_t b = fb[y * 64 + ox / 2];
          uint8_t nib = (ox & 1) ? (b & 0x0F) : (b >> 4);
          black += __builtin_popcount(nib);
          total += 4;
        }
        if (black * 2 > total) oled.pset(ox, oy);
      }
    }
  } else {
    char line[32];
    oled.text(0, 0, "      MAC MESH       ", true);
    if (apMode) {
      oled.text(0, 2, "WIFI SETUP:");
      oled.text(0, 3, AP_SSID);
      oled.text(0, 4, "PASSWORD: MACMESH1");
      oled.text(0, 5, "OPEN 192.168.4.1");
    } else {
      oled.text(0, 2, "OPEN IN A BROWSER:");
      oled.text(0, 3, ipText().c_str());
      oled.text(0, 4, WiFi.status() == WL_CONNECTED ? "VNC: SAME IP :5900" : "WIFI LOST...");
    }
    snprintf(line, sizeof line, "%s %uK %d%%", stateText(), (unsigned)(mac.ramBytes() / 1024), speedPct);
    oled.text(0, 6, line);
    snprintf(line, sizeof line, "VIEW: %s%s", webView.active() ? "WEB " : "", vnc.active() ? "VNC" : (webView.active() ? "" : "NONE"));
    oled.text(0, 7, line);
  }
  oled.show();
}

// ---------------- network transport over WiFiClient
struct WifiConn : VncConn {
  WiFiClient c;
  explicit WifiConn(WiFiClient cl) : c(cl) { c.setNoDelay(true); }
  int read(uint8_t* b, int n) override {
    int a = c.available();
    if (a <= 0) return 0;
    return c.read(b, a < n ? a : n);
  }
  bool write(const uint8_t* b, int n) override {
    int idle = 0;
    while (n > 0) {
      size_t w = c.write(b, n);
      if (w == 0) {
        if (!c.connected() || ++idle > 3000) return false;
        delay(1);
        continue;
      }
      idle = 0; b += w; n -= w;
    }
    return true;
  }
  bool connected() override { return c.connected(); }
  void close() override { c.stop(); }
};

// ---------------- emulator task (core 1)
static void emuTask(void*) {
  while (!macReady) vTaskDelay(20);
  for (;;) {
    if (pauseReq) {
      mac.flushDisk();
      paused = true;
      while (pauseReq) vTaskDelay(5);
      paused = false;
    }
    if (cmdReset) { cmdReset = false; mac.reset(); }
    int64_t s = mac.step(esp_timer_get_time());
    if (s > 1500) vTaskDelay(pdMS_TO_TICKS(s / 1000 > 0 ? s / 1000 : 1));
    else if (s > 0) taskYIELD();
  }
}

static void pauseEmu() {
  pauseReq = true;
  for (int i = 0; i < 400 && !paused; i++) delay(5);
}
static void resumeEmu() { pauseReq = false; }

// ---------------- web API
static String jsonEsc(const String& s) {
  String o;
  for (char ch : s) {
    if (ch == '"' || ch == '\\') { o += '\\'; o += ch; }
    else if ((uint8_t)ch < 0x20) o += ' ';
    else o += ch;
  }
  return o;
}

static String diskInfo;
static void refreshDiskInfo() {
  bool boot; std::string fs, vol;
  inspectDisk(&diskPart, mac.meta().diskSize, boot, fs, vol);
  diskInfo = fs.c_str();
  if (vol.size()) diskInfo += String(" \"") + vol.c_str() + "\"";
  if (mac.meta().diskSize) diskInfo += boot ? (diskInfo.length() ? ", bootable" : "bootable") : (diskInfo.length() ? ", not bootable" : "not bootable");
}

static bool uploadOk = false;
static String uploadMsg;

static void setupWeb() {
  web.on("/", HTTP_GET, [] { web.send_P(200, "text/html", INDEX_HTML); });
  web.on("/macfiles.js", HTTP_GET, [] {
    web.sendHeader("Cache-Control", "max-age=86400");
    web.send_P(200, "application/javascript", MACFILES_JS);
  });

  web.on("/api/status", HTTP_GET, [] {
    const MacMeta& m = mac.meta();
    RomInfo ri = {m.romChecksum, true, "Mac Plus ROM v3", true};
    String j = "{";
    j += "\"ip\":\"" + ipText() + "\",\"mode\":\"" + String(apMode ? "ap" : "sta") + "\",";
    j += "\"ssid\":\"" + jsonEsc(WiFi.SSID()) + "\",\"rssi\":" + String(WiFi.RSSI()) + ",";
    j += "\"state\":\"" + jsonEsc(String(stateText()) + (startError.size() && !mac.running() ? String(" - ") + startError.c_str() : "")) + "\",";
    j += "\"ram\":" + String(mac.ramBytes()) + ",\"ramSetting\":" + String(m.ramSetting) + ",";
    j += "\"speed\":" + String(speedPct) + ",\"turbo\":" + String(m.turbo ? 1 : 0) + ",";
    j += "\"vnc\":" + String(vnc.active() ? 1 : 0) + ",\"web\":" + String(webView.active() ? 1 : 0) + ",";
    j += "\"romOk\":" + String(m.romOk ? 1 : 0) + ",\"romName\":\"" + String(ri.name) + "\",";
    j += "\"disk\":" + String(m.diskSize) + ",\"diskName\":\"" + jsonEsc(m.diskName) + "\",\"diskInfo\":\"" + jsonEsc(diskInfo) + "\",";
    j += "\"diskRO\":" + String(m.diskRO ? 1 : 0) + ",\"heap\":" + String(ESP.getFreeHeap()) + ",";
    j += "\"heapBlock\":" + String((unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)) + "}";
    web.send(200, "application/json", j);
  });

  web.on("/api/reset", HTTP_POST, [] { cmdReset = true; web.send(200, "text/plain", "ok"); });
  web.on("/api/turbo", HTTP_POST, [] { mac.setTurbo(web.arg("on") == "1"); web.send(200, "text/plain", "ok"); });
  web.on("/api/ram", HTTP_POST, [] {
    pauseEmu();
    mac.setRamSettingClear(web.arg("kb").toInt());
    web.send(200, "text/plain", "ok");
    restartAt = millis() + 500;
  });
  web.on("/api/diskro", HTTP_POST, [] {
    pauseEmu();
    mac.setDiskRO(web.arg("on") == "1");
    web.send(200, "text/plain", "ok");
    restartAt = millis() + 500;
  });
  web.on("/api/eject", HTTP_POST, [] {
    pauseEmu();
    mac.ejectDisk();
    web.send(200, "text/plain", "ok");
    restartAt = millis() + 500;
  });

  // ---- ROM upload: streamed into flash, checked, then the board restarts
  web.on("/api/rom", HTTP_POST, [] {
    std::string m;
    uploadOk = mac.finishRomUpload(m);
    web.send(uploadOk ? 200 : 400, "text/plain", m.c_str());
    restartAt = millis() + 800;
  }, [] {
    HTTPUpload& u = web.upload();
    if (u.status == UPLOAD_FILE_START) { pauseEmu(); mac.beginRomUpload(); }
    else if (u.status == UPLOAD_FILE_WRITE) mac.romUploadChunk(u.buf, u.currentSize);
  });

  // ---- disk upload
  web.on("/api/disk", HTTP_POST, [] {
    std::string m;
    uploadOk = mac.finishDiskUpload(m);
    web.send(uploadOk ? 200 : 400, "text/plain", m.c_str());
    restartAt = millis() + 800;
  }, [] {
    HTTPUpload& u = web.upload();
    if (u.status == UPLOAD_FILE_START) { pauseEmu(); mac.beginDiskUpload(u.filename.c_str()); }
    else if (u.status == UPLOAD_FILE_WRITE) mac.diskUploadChunk(u.buf, u.currentSize);
  });

  // ---- disk download (the Mac pauses while it streams)
  web.on("/api/disk.img", HTTP_GET, [] {
    uint32_t size = mac.meta().diskSize;
    if (!size) { web.send(404, "text/plain", "No disk"); return; }
    pauseEmu();
    String name = mac.meta().diskName[0] ? String(mac.meta().diskName) : String("macmesh-disk.img");
    web.sendHeader("Content-Disposition", "attachment; filename=\"" + name + "\"");
    web.setContentLength(size);
    web.send(200, "application/octet-stream", "");
    uint8_t* buf = (uint8_t*)malloc(2048);
    if (!buf) { resumeEmu(); return; }
    const uint32_t B = 2048;
    WiFiClient cl = web.client();
    for (uint32_t off = 0; off < size && cl.connected(); off += B) {
      uint32_t n = size - off < B ? size - off : B;
      diskPart.read(off, buf, n);
      if (cl.write(buf, n) != n) break;
    }
    free(buf);
    resumeEmu();
  });

  // ---- Wi-Fi
  web.on("/api/scan", HTTP_GET, [] {
    int n = WiFi.scanNetworks();
    String j = "[";
    for (int i = 0; i < n; i++) j += String(i ? "," : "") + "{\"s\":\"" + jsonEsc(WiFi.SSID(i)) + "\",\"r\":" + String(WiFi.RSSI(i)) + "}";
    j += "]";
    WiFi.scanDelete();
    web.send(200, "application/json", j);
  });
  web.on("/api/wifi", HTTP_POST, [] {
    prefs.putString("ssid", web.arg("ssid"));
    prefs.putString("pass", web.arg("pass"));
    web.send(200, "text/plain", "ok");
    pauseEmu();
    restartAt = millis() + 600;
  });
  web.onNotFound([] {
    if (apMode) { web.sendHeader("Location", "http://192.168.4.1/"); web.send(302); return; }  // captive-ish
    web.send(404, "text/plain", "Not found");
  });
  web.begin();
}

// ---------------- network task (core 0)
static void netTask(void*) {
  uint32_t lastOled = 0, lastSpeed = millis(), lastLoops = mac.loops;
  uint32_t btnDown = 0;
  bool btnHeld = false, cleanMarked = false;
  for (;;) {
    if (!cleanMarked && millis() > 60000) { cleanMarked = true; mac.bootWasClean(); }
    web.handleClient();
    WiFiClient nc = vncListener.accept();
    if (nc) vnc.attach(new WifiConn(nc));
    WiFiClient wc = wsListener.accept();
    if (wc) webView.attach(new WifiConn(wc));
    vnc.poll();
    webView.poll();
    digitalWrite(PIN_LED, (vnc.active() || webView.active()) ? HIGH : LOW);

    uint32_t now = millis();
    if (restartAt && (int32_t)(now - restartAt) >= 0) { mac.flushDisk(); ESP.restart(); }
    if (now - lastSpeed >= 1000) {
      uint32_t l = mac.loops;
      speedPct = (int)((l - lastLoops) * 5.0 * 100 / (now - lastSpeed) + 0.5);   // each loop = 5 ms of Mac time
      lastLoops = l; lastSpeed = now;
    }
    // PRG button: tap = OLED view, hold 2 s = restart the Mac
    bool down = digitalRead(PIN_BUTTON) == LOW;
    if (down && !btnDown) { btnDown = now ? now : 1; btnHeld = false; }
    if (down && btnDown && !btnHeld && now - btnDown > 2000) { btnHeld = true; cmdReset = true; }
    if (!down && btnDown) {
      if (!btnHeld && now - btnDown > 40) oledPage ^= 1;
      btnDown = 0;
      lastOled = 0;
    }
    if (now - lastOled > (oledPage ? 200u : 1000u)) { drawOled(); lastOled = now; }
    delay(1);
  }
}

// ---------------- setup
void setup() {
  Serial.begin(115200);
  pinMode(PIN_LED, OUTPUT);
  pinMode(PIN_BUTTON, INPUT_PULLUP);
  prefs.begin("macmesh", false);

  if (!romPart.open("macrom") || !diskPart.open("macdisk")) {
    Serial.println("Partitions missing: flash with this sketch's partitions.csv");
  }
  mac.begin(&romPart, &diskPart);
  esp_reset_reason_t rr = esp_reset_reason();
  if (rr == ESP_RST_PANIC || rr == ESP_RST_INT_WDT || rr == ESP_RST_TASK_WDT || rr == ESP_RST_WDT) {
    Serial.println("Last run crashed: using a smaller Mac in Auto mode");
    mac.lastBootCrashed();
  }
  refreshDiskInfo();

  oled.begin();
  oled.clear(); oled.text(0, 0, "      MAC MESH       ", true); oled.text(0, 3, "JOINING WIFI..."); oled.show();

  // Wi-Fi: saved network, else hotspot
  String ssid = prefs.getString("ssid", ""), pass = prefs.getString("pass", "");
  WiFi.setHostname(HOSTNAME);
  if (ssid.length()) {
    WiFi.mode(WIFI_STA);
    WiFi.setSleep(false);
    WiFi.begin(ssid.c_str(), pass.c_str());
    for (int i = 0; i < 150 && WiFi.status() != WL_CONNECTED; i++) delay(100);
  }
  if (WiFi.status() != WL_CONNECTED) {
    apMode = true;
    WiFi.mode(WIFI_AP_STA);
    WiFi.softAP(AP_SSID, AP_PASS);
  } else {
    WiFi.setAutoReconnect(true);
  }
  if (MDNS.begin(HOSTNAME)) {
    MDNS.addService("http", "tcp", 80);
    MDNS.addService("rfb", "tcp", 5900);
  }
  Serial.printf("Wi-Fi up: http://%s  free heap %u\n", ipText().c_str(), (unsigned)ESP.getFreeHeap());

  // viewers share the same hooks
  auto fb = [] { return mac.framebuffer(); };
  auto frame = [] { return (uint32_t)mac.frameCount; };
  auto key = [](uint32_t k, bool d) { macKeys.push(k, d); };
  auto ptr = [](int x, int y, bool b) { mac.mouse(x, y, b); };
  auto paste = [](const std::string& t) { macKeys.typeText(t.c_str()); };
  auto gone = [] { macKeys.releaseAll(); };
  vnc.fb = fb; vnc.frame = frame; vnc.onKey = key; vnc.onPointer = ptr; vnc.onPaste = paste; vnc.onDisconnect = gone;
  webView.fb = fb; webView.frame = frame; webView.onKey = key; webView.onPointer = ptr; webView.onPaste = paste; webView.onDisconnect = gone;
  webView.onAudio = [](bool on) { mac.audioWanted = on; };
  webView.nextAudio = [](uint8_t* a) { return mac.popAudio(a); };

  vncListener.begin(); vncListener.setNoDelay(true);
  wsListener.begin(); wsListener.setNoDelay(true);
  setupWeb();

  xTaskCreatePinnedToCore(emuTask, "mac", 8192, nullptr, 1, nullptr, 1);
  xTaskCreatePinnedToCore(netTask, "net", 12288, nullptr, 2, nullptr, 0);

  // Now the Mac gets what's left, after Wi-Fi, the servers and the tasks have taken their share.
  // If the board crashed three boots in a row, stay in safe mode (no Mac) so the page still works.
  safeMode = mac.meta().crashCount >= 3;
  size_t freeH = heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  size_t largest = heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  Serial.printf("Heap before Mac: free %u, largest block %u\n", (unsigned)freeH, (unsigned)largest);
  if (safeMode) {
    startError = "The board crashed 3 times in a row starting the Mac. Pick a smaller Mac RAM below and click Apply.";
    Serial.println("Safe mode: Mac not started");
  } else if (romPart.p && diskPart.p && mac.start(freeH, largest, RUN_RESERVE, startError)) {
    Serial.printf("Mac started with %u KB RAM, free heap now %u\n", (unsigned)(mac.ramBytes() / 1024), (unsigned)ESP.getFreeHeap());
  } else {
    Serial.printf("Mac not started: %s\n", startError.c_str());
  }
  macReady = true;
}

void loop() { vTaskDelete(nullptr); }
