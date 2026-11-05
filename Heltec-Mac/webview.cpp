#include "webview.h"
#include <string.h>

// ---------------------------------------------------------------- SHA-1 + base64 (for the handshake)
static inline uint32_t rol(uint32_t v, int s) { return (v << s) | (v >> (32 - s)); }

void sha1(const uint8_t* data, size_t len, uint8_t out[20]) {
  uint32_t h[5] = {0x67452301, 0xEFCDAB89, 0x98BADCFE, 0x10325476, 0xC3D2E1F0};
  uint64_t bits = (uint64_t)len * 8;
  size_t total = ((len + 8) / 64 + 1) * 64;
  uint8_t blk[64];
  for (size_t off = 0; off < total; off += 64) {
    for (int i = 0; i < 64; i++) {
      size_t p = off + i;
      if (p < len) blk[i] = data[p];
      else if (p == len) blk[i] = 0x80;
      else if (p >= total - 8) blk[i] = (uint8_t)(bits >> (8 * (total - 1 - p)));
      else blk[i] = 0;
    }
    uint32_t w[80];
    for (int i = 0; i < 16; i++) w[i] = ((uint32_t)blk[i * 4] << 24) | (blk[i * 4 + 1] << 16) | (blk[i * 4 + 2] << 8) | blk[i * 4 + 3];
    for (int i = 16; i < 80; i++) w[i] = rol(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);
    uint32_t a = h[0], b = h[1], c = h[2], d = h[3], e = h[4];
    for (int i = 0; i < 80; i++) {
      uint32_t f, k;
      if (i < 20) { f = (b & c) | (~b & d); k = 0x5A827999; }
      else if (i < 40) { f = b ^ c ^ d; k = 0x6ED9EBA1; }
      else if (i < 60) { f = (b & c) | (b & d) | (c & d); k = 0x8F1BBCDC; }
      else { f = b ^ c ^ d; k = 0xCA62C1D6; }
      uint32_t t = rol(a, 5) + f + e + k + w[i];
      e = d; d = c; c = rol(b, 30); b = a; a = t;
    }
    h[0] += a; h[1] += b; h[2] += c; h[3] += d; h[4] += e;
  }
  for (int i = 0; i < 5; i++) {
    out[i * 4] = h[i] >> 24; out[i * 4 + 1] = h[i] >> 16; out[i * 4 + 2] = h[i] >> 8; out[i * 4 + 3] = h[i];
  }
}

std::string base64(const uint8_t* d, size_t n) {
  static const char* t = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  std::string o;
  for (size_t i = 0; i < n; i += 3) {
    uint32_t v = d[i] << 16;
    if (i + 1 < n) v |= d[i + 1] << 8;
    if (i + 2 < n) v |= d[i + 2];
    o += t[(v >> 18) & 63];
    o += t[(v >> 12) & 63];
    o += i + 1 < n ? t[(v >> 6) & 63] : '=';
    o += i + 2 < n ? t[v & 63] : '=';
  }
  return o;
}

// ---------------------------------------------------------------- connection
void WebView::attach(VncConn* c) {
  if (conn) drop();
  conn = c;
  open = false;
  in.clear(); out.clear();
  ready = false; full = true;
  lastFrame = 0xFFFFFFFF;
  memset(tileHash, 0, sizeof tileHash);
}

void WebView::drop() {
  if (!conn) return;
  conn->close();
  delete conn;
  conn = nullptr;
  bool was = open;
  open = false;
  if (audioOn && onAudio) onAudio(false);
  audioOn = false;
  if (was && onDisconnect) onDisconnect();
}

void WebView::poll() {
  if (!conn) return;
  if (!conn->connected()) { drop(); return; }
  uint8_t buf[512];
  for (int guard = 0; guard < 16; guard++) {
    int n = conn->read(buf, sizeof buf);
    if (n <= 0) break;
    in.insert(in.end(), buf, buf + n);
  }
  if (!open) {
    if (!handshake()) return;
  }
  while (conn && readFrame()) {}
  if (conn && pongDue) { pongDue = false; uint8_t pong = 'p'; sendFrame(0x2, &pong, 1); }
  if (conn && open && audioOn && nextAudio) sendAudio();
  if (conn && open && nextPrint) sendPrint();
  if (conn && open && ready) sendUpdate();
}

static std::string headerValue(const std::string& req, const char* name) {
  std::string lower = req;
  for (auto& ch : lower) ch = tolower(ch);
  std::string key = std::string("\n") + name + ":";
  for (auto& ch : key) ch = tolower(ch);
  size_t p = lower.find(key);
  if (p == std::string::npos) return "";
  p += key.size();
  size_t e = req.find('\r', p);
  std::string v = req.substr(p, e == std::string::npos ? std::string::npos : e - p);
  while (!v.empty() && v[0] == ' ') v.erase(0, 1);
  while (!v.empty() && (v.back() == ' ' || v.back() == '\n')) v.pop_back();
  return v;
}

bool WebView::handshake() {
  if (in.size() > 4096) { drop(); return false; }
  std::string req(in.begin(), in.end());
  size_t end = req.find("\r\n\r\n");
  if (end == std::string::npos) return false;
  std::string key = headerValue(req, "Sec-WebSocket-Key");
  if (key.empty()) {
    const char* r = "HTTP/1.1 400 Bad Request\r\nContent-Length: 0\r\n\r\n";
    conn->write((const uint8_t*)r, strlen(r));
    drop();
    return false;
  }
  std::string acc = key + "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";
  uint8_t dig[20];
  sha1((const uint8_t*)acc.data(), acc.size(), dig);
  std::string resp = "HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Accept: " +
                     base64(dig, 20) + "\r\n\r\n";
  in.erase(in.begin(), in.begin() + end + 4);
  if (!conn->write((const uint8_t*)resp.data(), resp.size())) { drop(); return false; }
  open = true;
  ready = true;
  full = true;
  return true;
}

// Parses one client frame (always masked). Returns true if one was consumed.
bool WebView::readFrame() {
  if (in.size() < 2) return false;
  uint8_t op = in[0] & 0x0F;
  bool masked = in[1] & 0x80;
  uint64_t len = in[1] & 0x7F;
  size_t hdr = 2;
  if (len == 126) { if (in.size() < 4) return false; len = (in[2] << 8) | in[3]; hdr = 4; }
  else if (len == 127) { drop(); return false; }   // nothing we accept is that big
  if (len > 70000) { drop(); return false; }
  size_t need = hdr + (masked ? 4 : 0) + len;
  if (in.size() < need) return false;
  uint8_t* p = in.data() + hdr + (masked ? 4 : 0);
  if (masked) {
    const uint8_t* mk = in.data() + hdr;
    for (uint64_t i = 0; i < len; i++) p[i] ^= mk[i & 3];
  }
  if (op == 0x8) { if (!inSend) sendFrame(0x8, nullptr, 0); drop(); return false; }   // close
  if (op == 0x9) { if (!inSend) sendFrame(0xA, p, len); }               // ping -> pong
  else if (op == 0x1 || op == 0x2) handleMessage(p, len);
  if (conn) in.erase(in.begin(), in.begin() + need);
  return conn != nullptr;
}

void WebView::handleMessage(const uint8_t* p, size_t n) {
  if (!n) return;
  switch (p[0]) {
    case 'A': ready = true; break;
    case 'p': if (inSend) pongDue = true; else { uint8_t pong = 'p'; sendFrame(0x2, &pong, 1); } break;   // keep-alive
    case 'F': ready = true; full = true; break;
    case 'M':
      if (n >= 6 && onPointer) onPointer((p[1] << 8) | p[2], (p[3] << 8) | p[4], p[5] != 0);
      break;
    case 'K':
      if (n >= 6 && onKey) onKey(((uint32_t)p[2] << 24) | (p[3] << 16) | (p[4] << 8) | p[5], p[1] != 0);
      break;
    case 'C':
      if (n >= 7 && onClock) onClock(((uint32_t)p[1] << 24) | (p[2] << 16) | (p[3] << 8) | p[4], (int16_t)((p[5] << 8) | p[6]));
      break;
    case 'a':
      audioOn = n >= 2 && p[1];
      if (onAudio) onAudio(audioOn);
      break;
    case 'P':
      if (onPaste) onPaste(std::string((const char*)p + 1, n - 1));
      break;
  }
}

// Header and payload go out in ONE write: separate tiny writes flood the board's small
// network queue, and a full queue makes writes wait (freezing mouse and keyboard).
void WebView::sendFrame(uint8_t opcode, const uint8_t* p, size_t n) {
  if (!conn || n > 125) return;   // only small control frames come through here
  uint8_t b[129];
  b[0] = 0x80 | opcode; b[1] = n;
  if (n) memcpy(b + 2, p, n);
  if (!conn->write(b, 2 + n)) { drop(); return; }
  bytesSent += 2 + n;
}

void WebView::sendBuf(uint8_t opcode, std::vector<uint8_t>& v) {
  if (!conn) return;
  size_t n = v.size() - 4;
  uint8_t* start;
  if (n < 126) { start = v.data() + 2; start[0] = 0x80 | opcode; start[1] = n; }
  else { start = v.data(); start[0] = 0x80 | opcode; start[1] = 126; start[2] = n >> 8; start[3] = n; }
  size_t len = v.data() + v.size() - start;
  // Send without blocking. While the connection is full (a slow or busy Wi-Fi link), keep
  // reading the browser's mouse and keyboard, so they never freeze behind a screen update.
  size_t done = 0; int waited = 0;
  inSend = true;
  while (conn && done < len) {
    int w = conn->writeSome(start + done, len - done);
    if (w < 0) break;
    if (w > 0) { done += w; waited = 0; continue; }
    if (++waited > 2000) break;          // stuck for ~2 s: drop the viewer, it reconnects
    pumpInput();
    if (conn) conn->idle();
  }
  inSend = false;
  if (!conn) return;
  if (done < len) { drop(); return; }
  bytesSent += len;
}

// Read whatever the browser has sent and act on it (used while a send waits)
void WebView::pumpInput() {
  uint8_t buf[256];
  int n = conn->read(buf, sizeof buf);
  if (n > 0) in.insert(in.end(), buf, buf + n);
  while (conn && readFrame()) {}
}

// Sound goes out in bundles of 3+ frames (about 50 ms), and bundles that are all silence
// aren't sent, so idle sound costs nothing on the network.
void WebView::sendAudio() {
  if (audioPending && audioPending() < 3) return;
  uint8_t a[372];
  aout.assign(4, 0);
  aout.push_back('S'); aout.push_back(0);
  int count = 0; bool any = false;
  while (count < 6 && nextAudio(a)) {
    aout.push_back(a[0]);
    if (a[0] != 0xFF) { aout.insert(aout.end(), a + 1, a + 371); any = true; }
    count++;
  }
  if (!count || !any) return;
  aout[5] = count;
  sendBuf(0x2, aout);
}

// Printer output: 'R' + up to 512 bytes of ImageWriter data
void WebView::sendPrint() {
  uint8_t b[512];
  int n = nextPrint(b, sizeof b);
  if (n <= 0) return;
  aout.assign(4, 0);
  aout.push_back('R');
  aout.insert(aout.end(), b, b + n);
  sendBuf(0x2, aout);
}

void WebView::sendUpdate() {
  uint32_t fc = frame ? frame() : 0;
  if (!full && fc == lastFrame) return;
  lastFrame = fc;
  const uint8_t* f = fb ? fb() : nullptr;

  // one message per batch of up to 64 tiles (34 bytes each) keeps frames under 64 KB
  out.clear();
  int count = 0, sent = 0;
  auto start = [&]() { out.assign(4, 0); out.push_back('T'); out.push_back(0); out.push_back(0); count = 0; };
  auto finish = [&]() {
    if (!count) return;
    out[5] = count >> 8; out[6] = count;
    sendBuf(0x2, out);
    sent += count;
  };
  start();
  for (int ty = 0; ty < TROWS && conn; ty++) {
    int rows = MAC_H - ty * 16;
    if (rows > 16) rows = 16;
    for (int tx = 0; tx < TCOLS; tx++) {
      uint16_t t[16];
      uint32_t h = 2166136261u;
      for (int r = 0; r < 16; r++) {
        uint16_t v = 0;
        if (r < rows) {
          if (f) { const uint8_t* q = f + (ty * 16 + r) * (MAC_W / 8) + tx * 2; v = (q[0] << 8) | q[1]; }
          else v = (r & 1) ? 0xAAAA : 0x5555;
        }
        t[r] = v;
        h = (h ^ (v & 0xFF)) * 16777619u;
        h = (h ^ (v >> 8)) * 16777619u;
      }
      int i = ty * TCOLS + tx;
      if (!full && h == tileHash[i]) continue;
      tileHash[i] = h;
      out.push_back(tx); out.push_back(ty);
      for (int r = 0; r < 16; r++) { out.push_back(t[r] >> 8); out.push_back(t[r]); }
      if (++count == 64) { finish(); start(); }
    }
  }
  finish();
  full = false;
  if (sent) ready = false;   // wait for the browser to ask again
}
