// Browser view of the Mac screen over a WebSocket (port 81).
// The web page (port 80) draws the 512x342 screen on a <canvas> and sends mouse, keys and paste.
//
// Server -> browser (binary frames):
//   'T' n:u16  then n x { tx:u8 ty:u8  16 rows x u16 (MSB = left pixel, 1 = black) }   changed 16x16 tiles
//   'S' n:u8  then n x { vol:u8 [370 x u8] }   n video frames of sound (~22.25 kHz unsigned 8-bit);
//                               vol 0xFF = a silent frame (no samples). Runs of silence aren't sent at all.
// Browser -> server (binary frames):
//   'A'                         ready for the next update (one update in flight at a time)
//   'M' x:u16 y:u16 b:u8        pointer (absolute, Mac pixels), b = button
//   'K' d:u8 keysym:u32         key down (d=1) / up (d=0), X11 keysym like VNC
//   'P' utf8...                 paste: type this text on the Mac
//   'F'                         resend the whole screen
//   'a' on:u8                   sound on/off
//   'p'                         keep-alive ping; the board answers with 'p'
//   'C' t:u32 tz:i16            the browser's local time as Mac time (seconds since 1904) and its UTC offset (minutes)
#pragma once
#include <stdint.h>
#include <string>
#include <vector>
#include <functional>
#include "macvnc.h"   // VncConn transport + MAC_W/MAC_H

class WebView {
 public:
  std::function<const uint8_t*()> fb;
  std::function<uint32_t()> frame;
  std::function<void(uint32_t keysym, bool down)> onKey;
  std::function<void(int x, int y, bool button)> onPointer;
  std::function<void(const std::string&)> onPaste;
  std::function<void()> onDisconnect;
  std::function<void(bool)> onAudio;                 // browser turned sound on/off
  std::function<bool(uint8_t*)> nextAudio;           // fetch one 371-byte sound frame
  std::function<int()> audioPending;                 // how many sound frames are waiting
  std::function<void(uint32_t macTime, int tzMin)> onClock;
  std::function<int(uint8_t*, int)> nextPrint;       // printer-port bytes for the browser
  uint32_t bytesSent = 0;
  bool audioOn = false;

  void attach(VncConn* conn);   // takes ownership; replaces any current viewer
  void poll();
  bool active() const { return conn != nullptr && open; }

 private:
  static const int TCOLS = MAC_W / 16, TROWS = (MAC_H + 15) / 16;
  VncConn* conn = nullptr;
  bool open = false;            // handshake done
  bool inSend = false, pongDue = false;   // reading input while a big send waits
  std::vector<uint8_t> in, out;
  bool ready = false, full = true;
  uint32_t lastFrame = 0xFFFFFFFF;
  uint32_t tileHash[TCOLS * TROWS];

  void drop();
  bool handshake();
  bool readFrame();
  void handleMessage(const uint8_t* p, size_t n);
  void sendUpdate();
  void sendFrame(uint8_t opcode, const uint8_t* p, size_t n);
  void sendBuf(uint8_t opcode, std::vector<uint8_t>& v);   // v starts with 4 spare bytes for the header
  void sendAudio();
  void sendPrint();
  void pumpInput();
  std::vector<uint8_t> aout;
};

// helpers (also used by tests)
void sha1(const uint8_t* data, size_t len, uint8_t out[20]);
std::string base64(const uint8_t* data, size_t len);
