// Minimal RFB (VNC) server for the Mac's 512x342 black-and-white screen.
// - RFB 3.3 / 3.7 / 3.8, security type "None"
// - Raw and Hextile encodings, any pixel format (8/16/32 bpp true colour or 8-bit colour map)
// - Only changed 16x16 tiles are sent
// - Pointer -> Mac mouse (absolute), keys -> Mac keyboard, clipboard paste -> typed on the Mac
// Transport is abstract so the same code runs on the ESP32 (WiFiClient) and on a PC (sockets).
#pragma once
#include <stdint.h>
#include <string>
#include <vector>
#include <functional>

static const int MAC_W = 512, MAC_H = 342;

struct VncConn {
  virtual ~VncConn() {}
  virtual int read(uint8_t* buf, int n) = 0;            // non-blocking; returns bytes read (0 = none)
  virtual bool write(const uint8_t* buf, int n) = 0;    // blocking; false = connection lost
  // Non-blocking: bytes taken now (0 = the connection is full, try again), -1 = lost.
  virtual int writeSome(const uint8_t* buf, int n) { return write(buf, n) ? n : -1; }
  virtual void idle() {}                                 // wait about 1 ms
  virtual bool connected() = 0;
  virtual void close() = 0;
};

class VncServer {
 public:
  // fb() returns the current framebuffer (or nullptr: show a blank grey screen);
  // frame() returns a counter that changes every Mac vsync.
  std::function<const uint8_t*()> fb;
  std::function<uint32_t()> frame;
  std::function<void(uint32_t keysym, bool down)> onKey;
  std::function<void(int x, int y, bool button)> onPointer;
  std::function<void(const std::string&)> onPaste;
  std::function<void()> onDisconnect;
  std::string name = "Mac Plus on Heltec V3";
  uint32_t bytesSent = 0;

  void attach(VncConn* conn);   // takes ownership; replaces any current client
  void poll();                  // call often
  bool active() const { return conn != nullptr; }

 private:
  enum State { WAIT_VERSION, WAIT_SECTYPE, WAIT_CLIENTINIT, NORMAL };
  static const int TCOLS = MAC_W / 16, TROWS = (MAC_H + 15) / 16;

  VncConn* conn = nullptr;
  State state = WAIT_VERSION;
  int minor = 3;
  std::vector<uint8_t> in, out;

  int bpp = 4;
  bool bigEndian = false, trueColour = true;
  uint32_t pal[2];          // [0] = white, [1] = black, in the client's pixel format
  bool hextile = false;

  bool updateRequested = false, fullRequested = false;
  uint32_t lastFrame = 0xFFFFFFFF;
  uint32_t tileHash[TCOLS * TROWS];
  bool tileDirty[TCOLS * TROWS];
  uint16_t tile[16];        // current tile, one 16-bit row each (MSB = left)
  int tileH = 16;
  uint8_t lastButtons = 0;

  void drop();
  void sendInitial();
  bool handleMessage();
  void setPixelFormat(const uint8_t* pf);
  void sendColourMap();
  void maybeSendUpdate();
  void putPixel(int black);
  uint32_t loadTile(const uint8_t* f, int tx, int ty, bool keep);
  void encodeTileRaw();
  void encodeTileHextile();
  void flush(bool force);
  void put8(uint8_t v) { out.push_back(v); }
  void put16(uint16_t v) { out.push_back(v >> 8); out.push_back(v & 0xFF); }
  void put32(uint32_t v) { put16(v >> 16); put16(v & 0xFFFF); }
};
