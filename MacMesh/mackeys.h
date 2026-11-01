// Turns VNC (X11 keysym) key events and pasted text into Mac Plus keyboard transitions.
// US layout, positional: the PC's Shift state is passed through, so Shift+2 gives '@' on the Mac.
//   Ctrl or Windows key = Command, Alt = Option, Caps Lock = Caps Lock (locks), Delete = Backspace,
//   arrows and the numeric keypad use the Mac Plus keypad codes.
#pragma once
#include <stdint.h>

class MacKeys {
 public:
  void push(uint32_t keysym, bool down);   // network thread
  void typeText(const char* utf8);          // network thread: queue text to "type" (paste)
  void releaseAll();                        // either thread: lift every held key
  void service();                           // emulator thread, once per vsync
  int pendingText() const;

 private:
  struct Ev { uint32_t sym; uint8_t down; };
  static const int QN = 256;
  Ev q[QN];
  volatile int qHead = 0, qTail = 0;
  volatile bool releaseReq = false;

  static const int TN = 2048;
  char text[TN];
  volatile int tHead = 0, tTail = 0;

  static const int WN = 64;                 // Mac wire bytes waiting for the keyboard
  uint8_t wire[WN];
  int wHead = 0, wTail = 0;

  uint8_t held[128];                        // Mac keycodes currently down
  bool capsLocked = false;

  void sendCode(uint8_t code, bool down);
  bool wireFree(int n) const;
  void handle(uint32_t sym, bool down);
  bool typeChar(uint32_t ch);
};

extern MacKeys macKeys;
// keysym -> Mac keycode (0xFF = none); 'shift' set if the character needs Shift on a US Mac keyboard
uint8_t macKeyForKeysym(uint32_t sym, bool* shift);
