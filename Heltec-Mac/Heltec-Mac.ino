/*
  Heltec-Mac  -  a Macintosh Plus on a Heltec WiFi LoRa 32 V3 (ESP32-S3), in your browser or over VNC.

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
#include <time.h>
#include <esp_sleep.h>
#include <esp_wifi.h>
#include <WiFiUdp.h>
#include <lwip/sockets.h>
#include <errno.h>
#include "heltec_config.h"
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

static const char* AP_SSID = BOARD_AP_SSID;
static const char* AP_PASS = "heltecmac";
static const char* HOSTNAME = BOARD_HOSTNAME;
#ifdef HELTEC_V4
static const size_t RUN_RESERVE = 128 * 1024;   // PSRAM to keep for the network buffers that land there
#else
static const size_t RUN_RESERVE = 56 * 1024;    // heap to keep free for Wi-Fi traffic, web page, VNC (Wi-Fi itself is already up)
#endif

static PartitionArea romPart, diskPart;
static VncServer vnc;
static WebView webView;
static WebServer web(80);
static WiFiServer vncListener(5900), wsListener(81);
static Preferences prefs;

static bool apMode = false;
static String savedSsid, savedPass;
static WiFiUDP dnsUdp;

// ---------------- tiny DNS answerer for hotspot mode
// Every name points at the board, so phones and PCs see a working "internet check" (the board
// answers those too) and stay on the hotspot instead of hopping to a network with internet.
// Polled from the network task: no extra task, one socket, a 512-byte buffer on the stack.
static void dnsPoll() {
  int n = dnsUdp.parsePacket();
  if (n <= 0) return;
  uint8_t q[512];
  if (n < 17 || n > (int)sizeof q - 16) { dnsUdp.flush(); return; }
  dnsUdp.read(q, n);
  if (q[2] & 0x80 || ((q[4] << 8) | q[5]) != 1) return;   // only plain queries with one question
  // read the name, so Windows' own check gets the address it expects
  char name[96]; int ni = 0, p = 12;
  while (p < n && q[p]) {
    int l = q[p++];
    if (l > 63 || p + l > n) return;
    for (int k = 0; k < l && ni < 94; k++) name[ni++] = tolower(q[p + k]);
    if (ni < 94) name[ni++] = '.';
    p += l;
  }
  if (p + 5 > n) return;
  name[ni ? ni - 1 : 0] = 0;
  int qend = p + 5;                                  // zero byte, type, class
  uint16_t qtype = (q[p + 1] << 8) | q[p + 2];
  q[2] = 0x84 | (q[2] & 0x01); q[3] = 0x80;          // response, authoritative, recursion available
  q[6] = q[7] = q[8] = q[9] = q[10] = q[11] = 0;     // no answers / authority / extra (for now)
  int len = qend;
  if (qtype == 1) {                                  // A record
    IPAddress ip = strcmp(name, "dns.msftncsi.com") == 0 ? IPAddress(131, 107, 255, 255) : WiFi.softAPIP();
    const uint8_t a[] = {0xC0, 0x0C, 0, 1, 0, 1, 0, 0, 0, 60, 0, 4, ip[0], ip[1], ip[2], ip[3]};
    memcpy(q + len, a, sizeof a); len += sizeof a;
    q[7] = 1;
  }
  dnsUdp.beginPacket(dnsUdp.remoteIP(), dnsUdp.remotePort());
  dnsUdp.write(q, len);
  dnsUdp.endPacket();
}

// Pick the hotspot channel (1, 6 or 11) with the least Wi-Fi around it
static int quietestChannel() {
  int n = WiFi.scanNetworks();
  long score[3] = {0, 0, 0};
  const int chans[3] = {1, 6, 11};
  for (int i = 0; i < n; i++) {
    int ch = WiFi.channel(i), w = 100 + WiFi.RSSI(i);   // a strong neighbour counts more
    if (w < 5) w = 5;
    for (int c = 0; c < 3; c++) if (abs(ch - chans[c]) < 4) score[c] += w * (4 - abs(ch - chans[c]));
  }
  WiFi.scanDelete();
  int best = 1;   // channel 6 first if it's tied
  for (int c = 0; c < 3; c++) if (score[c] < score[best]) best = c;
  Serial.printf("Hotspot channel %d (busy-ness 1:%ld 6:%ld 11:%ld)\n", chans[best], score[0], score[1], score[2]);
  return chans[best];
}
static std::string startError;
static volatile int speedPct = 0;
static volatile int oledPage = 0;
static volatile bool cmdReset = false, pauseReq = false, paused = false, macReady = false;
// After a restart asked for from the web page, the Mac waits (up to 25 s) for that browser to
// reconnect before it powers on, so the startup sound plays there. Survives ESP.restart().
RTC_NOINIT_ATTR static uint32_t holdForViewer;
// Restarted from the hotspot? Come straight back as the hotspot, on the same channel
RTC_NOINIT_ATTR static uint32_t apResume;   // 0xA9C40000 | channel
static int apChannel = 6;
static bool wifiChanged = false;   // new network saved: try it after the restart
static volatile bool holdMac = false;
static uint32_t holdUntil = 0, viewerSince = 0;
static bool safeMode = false;
static uint32_t restartAt = 0, sleepAt = 0;

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
  if (holdMac && mac.running()) return "Waiting for your browser";
  if (!mac.running()) return mac.meta().romOk ? "Not running" : "Needs a ROM";
  if (mac.crashed()) return "Stopped (error)";
  if (paused) return "Paused";
  return "Running";
}

// ---------------- battery (Heltec V3: divider on GPIO1, switched on by GPIO37)
// V3.0/3.1 boards switch the divider on with GPIO37 LOW, V3.2 with HIGH; we try LOW first and
// flip once if it reads nothing. Same scaling as Meshtastic's heltec_v3 variant.
#define PIN_VBAT 1
#define PIN_ADC_CTRL 37
static int batMv = -1;           // -1 = not read yet, 0 = no battery
static uint8_t adcOn = LOW; static bool adcTried = false;
static int readBatteryOnce() {
  pinMode(PIN_ADC_CTRL, OUTPUT); digitalWrite(PIN_ADC_CTRL, adcOn);
  delay(5);
  uint32_t sum = 0;
  for (int i = 0; i < 16; i++) sum += analogReadMilliVolts(PIN_VBAT);
  digitalWrite(PIN_ADC_CTRL, !adcOn);   // divider off again: it drains the battery
  return (int)(sum / 16 * 4.9f * 1.045f);
}
static void readBattery() {
  int mv = readBatteryOnce();
  if (mv < 1000 && !adcTried) { adcTried = true; adcOn = !adcOn; mv = readBatteryOnce(); if (mv < 1000) adcOn = !adcOn; }
  if (mv < 2700) mv = 0;   // nothing (or a dead cell) on the battery connector
  batMv = batMv > 0 && mv > 0 ? (batMv * 3 + mv) / 4 : mv;   // smooth out ADC noise
}
static int batteryPct(int mv) {   // single-cell LiPo, resting voltage
  static const int16_t v[] = {3300, 3500, 3600, 3650, 3700, 3750, 3800, 3900, 4000, 4100, 4200};
  static const int8_t p[] = {0, 5, 12, 20, 30, 40, 50, 65, 80, 90, 100};
  if (mv <= v[0]) return 0;
  for (int i = 1; i < 11; i++) if (mv <= v[i]) return p[i - 1] + (p[i] - p[i - 1]) * (mv - v[i - 1]) / (v[i] - v[i - 1]);
  return 100;
}
static String batteryText(bool shortForm) {
  if (batMv < 0) return "";
  if (batMv == 0) return shortForm ? "NO BAT" : "none (USB power)";
  char b[32];
  if (batMv > 4300) snprintf(b, sizeof b, shortForm ? "USB" : "USB power (%.2f V)", batMv / 1000.0f);
  else if (shortForm) snprintf(b, sizeof b, "%.1fV %d%%", batMv / 1000.0f, batteryPct(batMv));
  else snprintf(b, sizeof b, "%d%% (%.2f V)", batteryPct(batMv), batMv / 1000.0f);
  return b;
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
    snprintf(line, sizeof line, "%-11s%10s", BOARD_OLED, batteryText(true).c_str());
    oled.text(0, 0, line, true);
    if (apMode) {
      oled.text(0, 2, "WIFI SETUP:");
      oled.text(0, 3, AP_SSID);
      oled.text(0, 4, "PASSWORD: HELTECMAC");
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
  explicit WifiConn(WiFiClient cl) : c(cl) { c.setNoDelay(true); c.setTimeout(1); }
  int read(uint8_t* b, int n) override {
    int a = c.available();
    if (a <= 0) return 0;
    return c.read(b, a < n ? a : n);
  }
  // The library's write() can wait up to 10 s on a full connection; send() with
  // MSG_DONTWAIT returns at once, so the network task keeps moving.
  int writeSome(const uint8_t* b, int n) override {
    int fd = c.fd();
    if (fd < 0 || !c.connected()) return -1;
    int r = send(fd, b, n, MSG_DONTWAIT);
    if (r > 0) return r;
    if (r < 0 && (errno == EAGAIN || errno == EWOULDBLOCK || errno == ENOMEM)) return 0;
    return r == 0 ? 0 : -1;
  }
  bool write(const uint8_t* b, int n) override {
    int waited = 0;
    while (n > 0) {
      int w = writeSome(b, n);
      if (w < 0) return false;
      if (w == 0) {
        if (++waited > 2000) return false;   // stuck for ~2 s: drop the viewer, it reconnects
        delay(1);
        continue;
      }
      waited = 0; b += w; n -= w;
    }
    return true;
  }
  void idle() override { delay(1); }
  bool connected() override { return c.connected(); }
  void close() override { c.stop(); }
};

// ---------------- emulator task (core 1)
static void emuTask(void*) {
  while (!macReady) vTaskDelay(20);
  while (holdMac) {
    // start once a browser has been connected for 0.4 s (time for it to switch sound on)
    if (webView.active()) { if (!viewerSince) viewerSince = millis() | 1; }
    else viewerSince = 0;
    if ((viewerSince && millis() - viewerSince > 400) || (int32_t)(millis() - holdUntil) >= 0) holdMac = false;
    else vTaskDelay(20);
  }
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
static void refreshDiskInfo();
// Switching disks restarts only the Mac (like its reset switch), not the board, so Wi-Fi and
// the hotspot stay up and the browser stays connected. Call with the Mac paused.
static void restartMacOnly() {
  refreshDiskInfo();
  cmdReset = true;   // the emulator task resets the Mac with the new disk in the drive
  resumeEmu();
}

// Deep sleep: save the disk, switch off the screen, radio and LED, and wait for the PRG button
// (or RST). Waking up is a normal start; the Mac then waits for a browser so you hear the chime.
static void goToSleep() {
  pauseEmu();
  mac.flushDisk();
  Serial.println("Going to sleep - press PRG or RST to wake");
  oled.clear(); oled.text(0, 2, "     SLEEPING...     "); oled.text(0, 4, " PRESS PRG TO WAKE"); oled.show();
  delay(1500);
  if (oled.ok) { oled.cmd(0x8D); oled.cmd(0x10); oled.cmd(0xAE); }   // charge pump off, display off
  digitalWrite(PIN_VEXT, HIGH);                                         // cut power to the OLED
  analogWrite(PIN_LED, 0);
  WiFi.disconnect(true); WiFi.mode(WIFI_OFF);
  while (digitalRead(PIN_BUTTON) == LOW) delay(10);   // don't wake straight away if PRG is held
  holdForViewer = 0xB007CAFE;
  esp_sleep_enable_ext0_wakeup(GPIO_NUM_0, 0);         // PRG button = GPIO0, active low
  esp_deep_sleep_start();
}

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
static String describeDisk(uint32_t off, uint32_t size) {
  bool boot; std::string fs, vol;
  inspectDisk(&diskPart, off, size, boot, fs, vol);
  String s = fs.c_str();
  if (vol.size()) s += String(" \"") + vol.c_str() + "\"";
  if (size) s += boot ? (s.length() ? ", startup disk" : "startup disk") : (s.length() ? ", not a startup disk" : "not a startup disk");
  return s;
}
static String libInfo[LIB_MAX];
static void refreshDiskInfo() {
  const MacMeta& m = mac.meta();
  for (int i = 0; i < LIB_MAX; i++) libInfo[i] = m.lib[i].size ? describeDisk(m.lib[i].off, m.lib[i].size) : String();
  diskInfo = m.active >= 0 ? libInfo[m.active] : String();
}

static bool uploadOk = false;
static String uploadMsg;

static void setupWeb() {
  web.on("/", HTTP_GET, [] { web.sendHeader("Cache-Control", "no-cache"); web.send_P(200, "text/html", INDEX_HTML); });
  web.on("/macfiles.js", HTTP_GET, [] {
    web.sendHeader("Cache-Control", "max-age=31536000, immutable");   // URL changes with the content
    web.send_P(200, "application/javascript", MACFILES_JS);
  });
  web.on("/sit-arsenic.js", HTTP_GET, [] {
    web.sendHeader("Cache-Control", "max-age=31536000, immutable");
    web.send_P(200, "application/javascript", SIT_ARSENIC_JS);
  });
  web.on("/imagewriter.js", HTTP_GET, [] {
    web.sendHeader("Cache-Control", "max-age=31536000, immutable");
    web.send_P(200, "application/javascript", IMAGEWRITER_JS);
  });

  web.on("/api/status", HTTP_GET, [] {
    const MacMeta& m = mac.meta();
    uint8_t ck4[8] = {(uint8_t)(m.romChecksum >> 24), (uint8_t)(m.romChecksum >> 16), (uint8_t)(m.romChecksum >> 8), (uint8_t)m.romChecksum, 0, 0, 0, 0};
    RomInfo ri = identifyRom(ck4, 8);
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
    j += "\"active\":" + String(m.active) + ",\"libFree\":" + String(mac.libraryFree()) + ",\"lib\":[";
    bool first = true;
    for (int i = 0; i < LIB_MAX; i++) {
      if (!m.lib[i].size) continue;
      j += String(first ? "" : ",") + "{\"i\":" + String(i) + ",\"n\":\"" + jsonEsc(m.lib[i].name) + "\",\"s\":" + String(m.lib[i].size) + ",\"d\":\"" + jsonEsc(libInfo[i]) + "\"}";
      first = false;
    }
    j += "],";
    { static const char* rr[] = {"unknown", "power on", "RST button", "restart", "crash", "watchdog", "watchdog", "watchdog", "woke from sleep",
                                 "brownout (power dip)", "SDIO", "USB", "JTAG", "efuse", "power glitch", "CPU lockup"};
      int r = (int)esp_reset_reason(); j += "\"uptime\":" + String(millis() / 1000) + ",\"reset\":\"" + String(r >= 0 && r < 16 ? rr[r] : "other") + "\","; }
    j += "\"bat\":" + String(batMv) + ",\"batPct\":" + String(batMv > 0 ? batteryPct(batMv) : -1) + ",\"batText\":\"" + batteryText(false) + "\",";
    j += "\"heapBlock\":" + String((unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)) + "}";
    web.send(200, "application/json", j);
  });

  web.on("/api/reboot", HTTP_POST, [] { web.send(200, "text/plain", "ok"); pauseEmu(); restartAt = millis() + 400; });
  web.on("/api/sleep", HTTP_POST, [] { sleepAt = millis() + 400; web.send(200, "text/plain", "ok"); });
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
    restartMacOnly();
  });
  web.on("/api/eject", HTTP_POST, [] {
    pauseEmu();
    mac.ejectDisk();
    web.send(200, "text/plain", "ok");
    restartMacOnly();
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
    refreshDiskInfo();
    // the Mac's RAM was released for the upload; restart unless more disks are coming (?restart=0)
    if (web.arg("restart") != "0" || !uploadOk) restartAt = millis() + 800;
  }, [] {
    HTTPUpload& u = web.upload();
    if (u.status == UPLOAD_FILE_START) {
      pauseEmu();
      // ?replace=N overwrites library disk N; ?activate=0 adds without switching to it
      int rep = web.hasArg("replace") ? web.arg("replace").toInt() : -1;
      mac.beginDiskUpload(u.filename.c_str(), rep, web.arg("activate") != "0");
    }
    else if (u.status == UPLOAD_FILE_WRITE) mac.diskUploadChunk(u.buf, u.currentSize);
  });

  // ---- disk library
  web.on("/api/use", HTTP_POST, [] {
    pauseEmu();
    bool ok = mac.useDisk(web.arg("i").toInt());
    web.send(ok ? 200 : 400, "text/plain", ok ? "ok" : "No such disk");
    if (ok) restartMacOnly(); else resumeEmu();
  });
  web.on("/api/libdel", HTTP_POST, [] {
    int i = web.arg("i").toInt();
    bool wasActive = i == mac.meta().active;
    pauseEmu();                       // other disks may slide down in flash
    bool ok = mac.removeDisk(i);
    refreshDiskInfo();
    web.send(ok ? 200 : 400, "text/plain", ok ? "ok" : "No such disk");
    if (wasActive) restartMacOnly(); else resumeEmu();
  });

  // ---- disk download (the Mac pauses while it streams)
  web.on("/api/disk.img", HTTP_GET, [] {
    const MacMeta& mm = mac.meta();
    int i = web.hasArg("i") ? web.arg("i").toInt() : mm.active;
    if (i < 0 || i >= LIB_MAX || !mm.lib[i].size) { web.send(404, "text/plain", "No disk"); return; }
    uint32_t size = mm.lib[i].size, base = mm.lib[i].off;
    pauseEmu();
    mac.flushDisk();
    String name = mm.lib[i].name[0] ? String(mm.lib[i].name) : String("heltec-mac-disk.img");
    web.sendHeader("Content-Disposition", "attachment; filename=\"" + name + "\"");
    web.setContentLength(size);
    web.send(200, "application/octet-stream", "");
    uint8_t* buf = (uint8_t*)malloc(2048);
    if (!buf) { resumeEmu(); return; }
    const uint32_t B = 2048;
    WiFiClient cl = web.client();
    for (uint32_t off = 0; off < size && cl.connected(); off += B) {
      uint32_t n = size - off < B ? size - off : B;
      diskPart.read(base + off, buf, n);
      if (cl.write(buf, n) != n) break;
    }
    free(buf);
    resumeEmu();
  });

  // ---- Wi-Fi
  web.on("/api/scan", HTTP_GET, [] {
    if (apMode) WiFi.mode(WIFI_AP_STA);   // scanning needs the station side for a moment
    int n = WiFi.scanNetworks();
    if (apMode) WiFi.mode(WIFI_AP);
    String j = "[";
    for (int i = 0; i < n; i++) j += String(i ? "," : "") + "{\"s\":\"" + jsonEsc(WiFi.SSID(i)) + "\",\"r\":" + String(WiFi.RSSI(i)) + "}";
    j += "]";
    WiFi.scanDelete();
    web.send(200, "application/json", j);
  });
  web.on("/api/wifi", HTTP_POST, [] {
    prefs.putString("ssid", web.arg("ssid"));
    prefs.putString("pass", web.arg("pass"));
    wifiChanged = true;
    web.send(200, "text/plain", "ok");
    pauseEmu();
    restartAt = millis() + 600;
  });
  // phone/PC "am I online?" checks: say yes, so the hotspot isn't treated as a sign-in portal
  web.on("/hotspot-detect.html", [] { web.send(200, "text/html", "<HTML><HEAD><TITLE>Success</TITLE></HEAD><BODY>Success</BODY></HTML>"); });
  web.on("/library/test/success.html", [] { web.send(200, "text/html", "<HTML><HEAD><TITLE>Success</TITLE></HEAD><BODY>Success</BODY></HTML>"); });
  web.on("/generate_204", [] { web.send(204); });
  web.on("/gen_204", [] { web.send(204); });
  web.on("/connecttest.txt", [] { web.send(200, "text/plain", "Microsoft Connect Test"); });
  web.on("/success.txt", [] { web.send(200, "text/plain", "success\n"); });
  web.on("/ncsi.txt", [] { web.send(200, "text/plain", "Microsoft NCSI"); });
  web.onNotFound([] {
    if (apMode && web.hostHeader() != "192.168.4.1" && web.hostHeader().indexOf("heltec-mac") < 0) {
      if (web.uri() == "/") { web.sendHeader("Location", "http://192.168.4.1/"); web.send(302); }   // typed a name by hand
      else web.send(404, "text/plain", "Not found");
      return;
    }
    web.send(404, "text/plain", "Not found");
  });
  web.begin();
}

// ---------------- network task (core 0)
static void netTask(void*) {
  uint32_t lastOled = 0, lastSpeed = millis(), lastLoops = mac.loops;
  uint32_t btnDown = 0;
  bool btnHeld = false, cleanMarked = false;
  uint32_t lastNtp = 0;
  for (;;) {
    if (apMode) dnsPoll();
    // internet time: once it arrives, and every 6 hours (only if we know your time zone)
    if (!apMode && mac.timezoneKnown() && (!lastNtp || millis() - lastNtp > 6UL * 3600 * 1000) && (millis() & 0x3FF) < 2) {
      time_t utc = time(nullptr);
      if (utc > 1700000000) {
        mac.setClock((uint32_t)(utc + 2082844800LL + mac.timezoneMin() * 60), mac.timezoneMin(), false);
        lastNtp = millis() ? millis() : 1;
        Serial.println("Clock set from internet time");
      }
    }
    if (!cleanMarked && millis() > 60000) { cleanMarked = true; mac.bootWasClean(); }
    web.handleClient();
    WiFiClient nc = vncListener.accept();
    if (nc) vnc.attach(new WifiConn(nc));
    WiFiClient wc = wsListener.accept();
    if (wc) webView.attach(new WifiConn(wc));
    vnc.poll();
    webView.poll();
    {   // LED: a short, dim blink every 3 s while someone is watching
      static int ledLevel = -1;
      int want = (vnc.active() || webView.active()) && millis() % 3000 < 60 ? 18 : 0;
      if (want != ledLevel) { analogWrite(PIN_LED, want); ledLevel = want; }
    }

    uint32_t now = millis();
    if (restartAt && (int32_t)(now - restartAt) >= 0) { mac.flushDisk(); holdForViewer = 0xB007CAFE; apResume = apMode && !wifiChanged ? 0xA9C40000u | apChannel : 0; ESP.restart(); }
    if (sleepAt && (int32_t)(now - sleepAt) >= 0) goToSleep();
    static uint32_t lastBat = 0;
    if (!lastBat || now - lastBat >= 10000) { readBattery(); lastBat = now | 1; }
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
  oled.clear(); { char t[22]; snprintf(t, sizeof t, "%-21s", BOARD_OLED); oled.text(0, 0, t, true); } oled.text(0, 3, "JOINING WIFI..."); oled.show();

  // Wi-Fi: saved network, else hotspot
  String ssid = prefs.getString("ssid", ""), pass = prefs.getString("pass", "");
  WiFi.setHostname(HOSTNAME);
  bool resumeAP = (apResume & 0xFFFF0000u) == 0xA9C40000u && esp_reset_reason() == ESP_RST_SW;
  int resumeCh = apResume & 0xFF;
  apResume = 0;
  if (ssid.length() && !resumeAP) {
    WiFi.mode(WIFI_STA);
    WiFi.setSleep(false);
    WiFi.begin(ssid.c_str(), pass.c_str());
    for (int i = 0; i < 150 && WiFi.status() != WL_CONNECTED; i++) delay(100);
  }
  savedSsid = ssid; savedPass = pass;
  if (WiFi.status() != WL_CONNECTED) {
    apMode = true;
    // Stop the station side from hunting for the saved network: every retry hops channels,
    // which knocks phones and laptops off the hotspot and stalls the web page.
    WiFi.setAutoReconnect(false);
    WiFi.disconnect(false, false);
    WiFi.mode(WIFI_STA);
    int ch = resumeAP && resumeCh >= 1 && resumeCh <= 13 ? resumeCh : quietestChannel();
    apChannel = ch;
    WiFi.mode(WIFI_AP);   // hotspot only: no station side to wander off-channel (Scan turns it on briefly)
    WiFi.setSleep(false);
    WiFi.softAP(AP_SSID, AP_PASS, ch, 0, 2);
    // A little below full power: a strong signal, without the biggest current spikes
    WiFi.setTxPower(WIFI_POWER_17dBm);
    // Keep a quiet phone or PC on the hotspot for 10 minutes instead of the usual 5
    esp_wifi_set_inactive_time(WIFI_IF_AP, 600);
    dnsUdp.begin(53);
  } else {
    WiFi.setAutoReconnect(true);
  }
  if (!apMode && MDNS.begin(HOSTNAME)) {
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
  webView.audioPending = [] { return mac.audioQueued(); };
  webView.nextPrint = [](uint8_t* b, int max) { return mac.printerRead(b, max); };
  webView.onClock = [](uint32_t t, int tz) { mac.setClock(t, tz, true); Serial.printf("Clock set from the browser (UTC%+d min)\n", tz); };
  // Internet time as a fallback when no browser is open (uses the time zone the browser last told us)
  if (!apMode) configTime(0, 0, "pool.ntp.org", "time.google.com");

  vncListener.begin(); vncListener.setNoDelay(true);
  wsListener.begin(); wsListener.setNoDelay(true);
  setupWeb();

  xTaskCreatePinnedToCore(emuTask, "mac", 8192, nullptr, 1, nullptr, 1);
  xTaskCreatePinnedToCore(netTask, "net", 12288, nullptr, 2, nullptr, 0);

  // Now the Mac gets what's left, after Wi-Fi, the servers and the tasks have taken their share.
  // If the board crashed three boots in a row, stay in safe mode (no Mac) so the page still works.
  safeMode = mac.meta().crashCount >= 3;
#ifdef HELTEC_V4
  // V4: the Mac's memory comes from the PSRAM, leaving the internal RAM to Wi-Fi
  size_t freeH = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
  size_t largest = heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM);
  Serial.printf("PSRAM free %u (largest %u)\n", (unsigned)freeH, (unsigned)largest);
#else
  size_t freeH = heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  size_t largest = heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
#endif
  Serial.printf("Heap before Mac: free %u, largest block %u\n", (unsigned)freeH, (unsigned)largest);
  if (safeMode) {
    startError = "The board crashed 3 times in a row starting the Mac. Pick a smaller Mac RAM below and click Apply.";
    Serial.println("Safe mode: Mac not started");
  } else if (romPart.p && diskPart.p && mac.start(freeH, largest, RUN_RESERVE, startError)) {
    Serial.printf("Mac started with %u KB RAM, free heap now %u\n", (unsigned)(mac.ramBytes() / 1024), (unsigned)ESP.getFreeHeap());
  } else {
    Serial.printf("Mac not started: %s\n", startError.c_str());
  }
  if (holdForViewer == 0xB007CAFE && (esp_reset_reason() == ESP_RST_SW || esp_reset_reason() == ESP_RST_DEEPSLEEP)) { holdMac = true; holdUntil = millis() + 25000; }
  holdForViewer = 0;
  macReady = true;
}

void loop() { vTaskDelete(nullptr); }
