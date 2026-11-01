#include "macvnc.h"
#include <string.h>

// ---------------------------------------------------------------- connection lifecycle
void VncServer::attach(VncConn* c) {
  if (conn) drop();
  conn = c;
  state = WAIT_VERSION;
  in.clear(); out.clear();
  bpp = 4; bigEndian = false; trueColour = true; hextile = false;
  pal[0] = 0xFFFFFF; pal[1] = 0x000000;
  updateRequested = fullRequested = false;
  lastFrame = 0xFFFFFFFF;
  lastButtons = 0;
  memset(tileHash, 0, sizeof tileHash);
  for (int i = 0; i < TCOLS * TROWS; i++) tileDirty[i] = true;
  const char* ver = "RFB 003.008\n";
  conn->write((const uint8_t*)ver, 12);
}

void VncServer::drop() {
  if (!conn) return;
  conn->close();
  delete conn;
  conn = nullptr;
  if (onDisconnect) onDisconnect();
}

void VncServer::poll() {
  if (!conn) return;
  if (!conn->connected()) { drop(); return; }
  uint8_t buf[512];
  for (int guard = 0; guard < 16; guard++) {
    int n = conn->read(buf, sizeof buf);
    if (n <= 0) break;
    in.insert(in.end(), buf, buf + n);
  }
  while (conn && handleMessage()) {}
  if (conn && state == NORMAL) maybeSendUpdate();
}

// ---------------------------------------------------------------- client messages
bool VncServer::handleMessage() {
  switch (state) {
    case WAIT_VERSION: {
      if (in.size() < 12) return false;
      minor = (in[8] - '0') * 100 + (in[9] - '0') * 10 + (in[10] - '0');
      in.erase(in.begin(), in.begin() + 12);
      if (minor >= 7) {
        uint8_t m[2] = {1, 1};  // one security type: None
        conn->write(m, 2);
        state = WAIT_SECTYPE;
      } else {
        minor = 3;
        uint8_t m[4] = {0, 0, 0, 1};
        conn->write(m, 4);
        state = WAIT_CLIENTINIT;
      }
      return true;
    }
    case WAIT_SECTYPE: {
      if (in.size() < 1) return false;
      in.erase(in.begin());
      if (minor >= 8) { uint8_t ok[4] = {0, 0, 0, 0}; conn->write(ok, 4); }
      state = WAIT_CLIENTINIT;
      return true;
    }
    case WAIT_CLIENTINIT: {
      if (in.size() < 1) return false;
      in.erase(in.begin());
      sendInitial();
      state = NORMAL;
      return true;
    }
    case NORMAL: break;
  }
  if (in.empty()) return false;
  size_t need;
  switch (in[0]) {
    case 0: need = 20; break;
    case 2: if (in.size() < 4) return false; need = 4 + 4 * ((in[2] << 8) | in[3]); break;
    case 3: need = 10; break;
    case 4: need = 8; break;
    case 5: need = 6; break;
    case 6: {
      if (in.size() < 8) return false;
      uint32_t len = ((uint32_t)in[4] << 24) | (in[5] << 16) | (in[6] << 8) | in[7];
      if (len > 65536) { drop(); return false; }
      need = 8 + len;
      break;
    }
    default: drop(); return false;  // unknown message: can't resync
  }
  if (in.size() < need) return false;
  const uint8_t* m = in.data();
  switch (m[0]) {
    case 0: setPixelFormat(m + 4); break;
    case 2: {
      int n = (m[2] << 8) | m[3];
      hextile = false;
      for (int i = 0; i < n; i++) {
        int32_t e = (int32_t)(((uint32_t)m[4 + i * 4] << 24) | (m[5 + i * 4] << 16) | (m[6 + i * 4] << 8) | m[7 + i * 4]);
        if (e == 5) { hextile = true; break; }
        if (e == 0) break;
      }
      break;
    }
    case 3:
      updateRequested = true;
      if (!m[1]) fullRequested = true;
      break;
    case 4: {
      uint32_t key = ((uint32_t)m[4] << 24) | (m[5] << 16) | (m[6] << 8) | m[7];
      if (onKey) onKey(key, m[1] != 0);
      break;
    }
    case 5: {
      int x = (m[2] << 8) | m[3], y = (m[4] << 8) | m[5];
      // any button = the Mac's one button (right-click works too)
      if (onPointer) onPointer(x, y, (m[1] & 7) != 0);
      lastButtons = m[1];
      break;
    }
    case 6:
      if (onPaste) onPaste(std::string((const char*)m + 8, need - 8));
      break;
  }
  in.erase(in.begin(), in.begin() + need);
  return true;
}

void VncServer::sendInitial() {
  out.clear();
  put16(MAC_W);
  put16(MAC_H);
  // 32bpp, depth 24, little endian, true colour, 255/255/255, shifts 16/8/0
  uint8_t pf[16] = {32, 24, 0, 1, 0, 255, 0, 255, 0, 255, 16, 8, 0, 0, 0, 0};
  out.insert(out.end(), pf, pf + 16);
  put32(name.size());
  out.insert(out.end(), name.begin(), name.end());
  flush(true);
}

void VncServer::setPixelFormat(const uint8_t* pf) {
  int bits = pf[0];
  bpp = bits == 8 ? 1 : bits == 16 ? 2 : 4;
  bigEndian = pf[2] != 0;
  trueColour = pf[3] != 0;
  if (trueColour) {
    uint16_t rmax = (pf[4] << 8) | pf[5], gmax = (pf[6] << 8) | pf[7], bmax = (pf[8] << 8) | pf[9];
    int rs = pf[10], gs = pf[11], bs = pf[12];
    pal[0] = ((uint32_t)rmax << rs) | ((uint32_t)gmax << gs) | ((uint32_t)bmax << bs);
    pal[1] = 0;
  } else {
    pal[0] = 0; pal[1] = 1;
    sendColourMap();
  }
  for (int i = 0; i < TCOLS * TROWS; i++) tileDirty[i] = true;
  fullRequested = true;
}

void VncServer::sendColourMap() {
  put8(1); put8(0); put16(0); put16(2);
  put16(0xFFFF); put16(0xFFFF); put16(0xFFFF);   // 0 = white
  put16(0); put16(0); put16(0);                  // 1 = black
  flush(true);
}

// ---------------------------------------------------------------- encoding
void VncServer::putPixel(int black) {
  uint32_t v = pal[black ? 1 : 0];
  if (bpp == 1) out.push_back((uint8_t)v);
  else if (bpp == 2) {
    if (bigEndian) { out.push_back(v >> 8); out.push_back(v); }
    else { out.push_back(v); out.push_back(v >> 8); }
  } else {
    if (bigEndian) { out.push_back(v >> 24); out.push_back(v >> 16); out.push_back(v >> 8); out.push_back(v); }
    else { out.push_back(v); out.push_back(v >> 8); out.push_back(v >> 16); out.push_back(v >> 24); }
  }
}

// Reads one 16x16 tile (rows past the bottom of the screen are skipped). Returns its hash.
uint32_t VncServer::loadTile(const uint8_t* f, int tx, int ty, bool keep) {
  uint32_t h = 2166136261u;
  int rows = MAC_H - ty * 16;
  if (rows > 16) rows = 16;
  for (int r = 0; r < rows; r++) {
    uint16_t v = 0x5555;   // grey when there's no Mac running
    if (f) {
      const uint8_t* p = f + (ty * 16 + r) * (MAC_W / 8) + tx * 2;
      v = (p[0] << 8) | p[1];
    } else if (r & 1) v = 0xAAAA;
    if (keep) tile[r] = v;
    h = (h ^ (v & 0xFF)) * 16777619u;
    h = (h ^ (v >> 8)) * 16777619u;
  }
  if (keep) tileH = rows;
  return h;
}

void VncServer::encodeTileRaw() {
  for (int y = 0; y < tileH; y++)
    for (int x = 0; x < 16; x++) putPixel(tile[y] & (0x8000 >> x));
}

void VncServer::encodeTileHextile() {
  int black = 0;
  for (int y = 0; y < tileH; y++) black += __builtin_popcount(tile[y]);
  int total = 16 * tileH;
  if (black == 0 || black == total) {  // solid tile
    put8(0x02);
    putPixel(black != 0);
    return;
  }
  // background = the more common colour; foreground runs as subrects, merged downwards
  int bg = black * 2 > total ? 1 : 0;
  struct R { uint8_t x, y, w, h; bool open; };
  R rects[128];
  int nr = 0;
  bool overflow = false;
  for (int y = 0; y < tileH && !overflow; y++) {
    uint16_t fgBits = bg ? (uint16_t)~tile[y] : tile[y];
    bool used[128] = {false};
    int x = 0;
    while (x < 16) {
      if (!(fgBits & (0x8000 >> x))) { x++; continue; }
      int s = x;
      while (x < 16 && (fgBits & (0x8000 >> x))) x++;
      int w = x - s, found = -1;
      for (int i = 0; i < nr; i++)
        if (rects[i].open && rects[i].x == s && rects[i].w == w && rects[i].y + rects[i].h == y) { found = i; break; }
      if (found >= 0) { rects[found].h++; used[found] = true; }
      else {
        if (nr >= 127) { overflow = true; break; }
        rects[nr] = {(uint8_t)s, (uint8_t)y, (uint8_t)w, 1, true};
        used[nr] = true;
        nr++;
      }
    }
    for (int i = 0; i < nr; i++) if (!used[i]) rects[i].open = false;
  }
  if (!overflow && 2 + 2 * bpp + nr * 2 < 16 * tileH * bpp) {
    put8(0x02 | 0x04 | 0x08);   // background, foreground, any subrects
    putPixel(bg);
    putPixel(!bg);
    put8(nr);
    for (int i = 0; i < nr; i++) {
      put8((rects[i].x << 4) | rects[i].y);
      put8(((rects[i].w - 1) << 4) | (rects[i].h - 1));
    }
    return;
  }
  put8(0x01);
  encodeTileRaw();
}

void VncServer::maybeSendUpdate() {
  if (!updateRequested) return;
  uint32_t fc = frame ? frame() : 0;
  if (!fullRequested && fc == lastFrame) return;
  lastFrame = fc;
  const uint8_t* f = fb ? fb() : nullptr;

  int nDirty = 0;
  for (int ty = 0; ty < TROWS; ty++)
    for (int tx = 0; tx < TCOLS; tx++) {
      int i = ty * TCOLS + tx;
      uint32_t h = loadTile(f, tx, ty, false);
      if (fullRequested || h != tileHash[i]) tileDirty[i] = true;
      if (tileDirty[i]) nDirty++;
    }
  if (!nDirty) return;  // keep the request pending until something changes

  struct Run { int tx, ty, n; };
  std::vector<Run> runs;
  for (int ty = 0; ty < TROWS; ty++) {
    int tx = 0;
    while (tx < TCOLS) {
      if (!tileDirty[ty * TCOLS + tx]) { tx++; continue; }
      int s = tx;
      while (tx < TCOLS && tileDirty[ty * TCOLS + tx]) tx++;
      runs.push_back({s, ty, tx - s});
    }
  }
  out.clear();
  put8(0); put8(0);
  put16(hextile ? runs.size() : nDirty);
  for (auto& r : runs) {
    int h = MAC_H - r.ty * 16;
    if (h > 16) h = 16;
    if (hextile) {
      put16(r.tx * 16); put16(r.ty * 16); put16(r.n * 16); put16(h);
      put32(5);
      for (int t = 0; t < r.n; t++) {
        int tx = r.tx + t, i = r.ty * TCOLS + tx;
        tileHash[i] = loadTile(f, tx, r.ty, true);
        tileDirty[i] = false;
        encodeTileHextile();
        flush(false);
        if (!conn) return;
      }
    } else {
      for (int t = 0; t < r.n; t++) {
        int tx = r.tx + t, i = r.ty * TCOLS + tx;
        tileHash[i] = loadTile(f, tx, r.ty, true);
        tileDirty[i] = false;
        put16(tx * 16); put16(r.ty * 16); put16(16); put16(h);
        put32(0);
        encodeTileRaw();
        flush(false);
        if (!conn) return;
      }
    }
  }
  flush(true);
  updateRequested = false;
  fullRequested = false;
}

void VncServer::flush(bool force) {
  if (!conn) { out.clear(); return; }
  if (out.empty() || (!force && out.size() < 4096)) return;
  bytesSent += out.size();
  if (!conn->write(out.data(), out.size())) { out.clear(); drop(); return; }
  out.clear();
}
