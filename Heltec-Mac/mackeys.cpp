#include "mackeys.h"
#include <string.h>

extern "C" {
#include "src/core/umac.h"
}
#include "src/core/adb.h"

MacKeys macKeys;

// Mac keycodes (Apple numbering, as used by the Mac Plus ROM)
enum : uint8_t {
  K_A = 0x00, K_S = 0x01, K_D = 0x02, K_F = 0x03, K_H = 0x04, K_G = 0x05, K_Z = 0x06, K_X = 0x07,
  K_C = 0x08, K_V = 0x09, K_B = 0x0B, K_Q = 0x0C, K_W = 0x0D, K_E = 0x0E, K_R = 0x0F, K_Y = 0x10,
  K_T = 0x11, K_1 = 0x12, K_2 = 0x13, K_3 = 0x14, K_4 = 0x15, K_6 = 0x16, K_5 = 0x17, K_EQUAL = 0x18,
  K_9 = 0x19, K_7 = 0x1A, K_MINUS = 0x1B, K_8 = 0x1C, K_0 = 0x1D, K_RBRACKET = 0x1E, K_O = 0x1F,
  K_U = 0x20, K_LBRACKET = 0x21, K_I = 0x22, K_P = 0x23, K_RETURN = 0x24, K_L = 0x25, K_J = 0x26,
  K_QUOTE = 0x27, K_K = 0x28, K_SEMI = 0x29, K_BACKSLASH = 0x2A, K_COMMA = 0x2B, K_SLASH = 0x2C,
  K_N = 0x2D, K_M = 0x2E, K_PERIOD = 0x2F, K_TAB = 0x30, K_SPACE = 0x31, K_GRAVE = 0x32,
  K_BACKSPACE = 0x33, K_ESC = 0x35, K_COMMAND = 0x37, K_SHIFT = 0x38, K_CAPS = 0x39, K_OPTION = 0x3A,
  // Mac Plus keypad (sent with the 0x79 prefix)
  KP_DOT = 0x41, KP_RIGHT = 0x42, KP_MUL = 0x43, KP_PLUS = 0x45, KP_LEFT = 0x46, KP_CLEAR = 0x47,
  KP_DOWN = 0x48, KP_DIV = 0x4B, KP_ENTER = 0x4C, KP_UP = 0x4D, KP_MINUS = 0x4E, KP_EQUAL = 0x51,
  KP_0 = 0x52, KP_1 = 0x53, KP_2 = 0x54, KP_3 = 0x55, KP_4 = 0x56, KP_5 = 0x57, KP_6 = 0x58,
  KP_7 = 0x59, KP_8 = 0x5B, KP_9 = 0x5C,
  K_NONE = 0xFF
};

uint8_t macKeyForKeysym(uint32_t sym, bool* shift) {
  bool sh = false;
  uint8_t k = K_NONE;
  if (sym >= 'a' && sym <= 'z') sym -= 32;
  else if (sym >= 'A' && sym <= 'Z') sh = true;
  static const uint8_t letters[26] = {K_A, K_B, K_C, K_D, K_E, K_F, K_G, K_H, K_I, K_J, K_K, K_L, K_M,
                                      K_N, K_O, K_P, K_Q, K_R, K_S, K_T, K_U, K_V, K_W, K_X, K_Y, K_Z};
  if (sym >= 'A' && sym <= 'Z') k = letters[sym - 'A'];
  else {
    switch (sym) {
      case '1': k = K_1; break;  case '!': k = K_1; sh = true; break;
      case '2': k = K_2; break;  case '@': k = K_2; sh = true; break;
      case '3': k = K_3; break;  case '#': k = K_3; sh = true; break;
      case '4': k = K_4; break;  case '$': k = K_4; sh = true; break;
      case '5': k = K_5; break;  case '%': k = K_5; sh = true; break;
      case '6': k = K_6; break;  case '^': k = K_6; sh = true; break;
      case '7': k = K_7; break;  case '&': k = K_7; sh = true; break;
      case '8': k = K_8; break;  case '*': k = K_8; sh = true; break;
      case '9': k = K_9; break;  case '(': k = K_9; sh = true; break;
      case '0': k = K_0; break;  case ')': k = K_0; sh = true; break;
      case '-': k = K_MINUS; break;      case '_': k = K_MINUS; sh = true; break;
      case '=': k = K_EQUAL; break;      case '+': k = K_EQUAL; sh = true; break;
      case '[': k = K_LBRACKET; break;   case '{': k = K_LBRACKET; sh = true; break;
      case ']': k = K_RBRACKET; break;   case '}': k = K_RBRACKET; sh = true; break;
      case '\\': k = K_BACKSLASH; break; case '|': k = K_BACKSLASH; sh = true; break;
      case ';': k = K_SEMI; break;       case ':': k = K_SEMI; sh = true; break;
      case '\'': k = K_QUOTE; break;     case '"': k = K_QUOTE; sh = true; break;
      case '`': k = K_GRAVE; break;      case '~': k = K_GRAVE; sh = true; break;
      case ',': k = K_COMMA; break;      case '<': k = K_COMMA; sh = true; break;
      case '.': k = K_PERIOD; break;     case '>': k = K_PERIOD; sh = true; break;
      case '/': k = K_SLASH; break;      case '?': k = K_SLASH; sh = true; break;
      case ' ': k = K_SPACE; break;
      case 0xFF08: case 0xFFFF: k = K_BACKSPACE; break;   // BackSpace, Delete
      case 0xFF09: case 0xFE20: k = K_TAB; break;          // Tab, ISO_Left_Tab
      case 0xFF0D: k = K_RETURN; break;
      case 0xFF1B: k = K_ESC; break;
      case 0xFF51: case 0xFF96: k = KP_LEFT; break;
      case 0xFF52: case 0xFF97: k = KP_UP; break;
      case 0xFF53: case 0xFF98: k = KP_RIGHT; break;
      case 0xFF54: case 0xFF99: k = KP_DOWN; break;
      case 0xFF7F: case 0xFF0B: k = KP_CLEAR; break;       // Num_Lock / Clear
      case 0xFF8D: k = KP_ENTER; break;
      case 0xFFAA: k = KP_MUL; break;
      case 0xFFAB: k = KP_PLUS; break;
      case 0xFFAD: k = KP_MINUS; break;
      case 0xFFAE: case 0xFF9F: k = KP_DOT; break;
      case 0xFFAF: k = KP_DIV; break;
      case 0xFFBD: k = KP_EQUAL; break;
      case 0xFFB0: case 0xFF9E: k = KP_0; break;
      case 0xFFB1: case 0xFF9C: k = KP_1; break;
      case 0xFFB2: k = KP_2; break;
      case 0xFFB3: case 0xFF9B: k = KP_3; break;
      case 0xFFB4: k = KP_4; break;
      case 0xFFB5: case 0xFF9D: k = KP_5; break;
      case 0xFFB6: k = KP_6; break;
      case 0xFFB7: case 0xFF95: k = KP_7; break;
      case 0xFFB8: k = KP_8; break;
      case 0xFFB9: case 0xFF9A: k = KP_9; break;
      case 0xFFE1: case 0xFFE2: k = K_SHIFT; break;
      case 0xFFE3: case 0xFFE4: case 0xFFEB: case 0xFFEC: k = K_COMMAND; break;  // Ctrl, Super
      case 0xFFE7: case 0xFFE8: case 0xFFE9: case 0xFFEA: case 0xFE03: k = K_OPTION; break;  // Meta, Alt, AltGr
      case 0xFFE5: k = K_CAPS; break;
      case '\n': case '\r': k = K_RETURN; break;
      case '\t': k = K_TAB; break;
      default: break;
    }
  }
  if (shift) *shift = sh;
  return k;
}

// ---------------------------------------------------------------- network side
void MacKeys::push(uint32_t sym, bool down) {
  int n = (qHead + 1) % QN;
  if (n == qTail) return;  // full: drop
  q[qHead] = {sym, (uint8_t)down};
  qHead = n;
}

void MacKeys::typeText(const char* s) {
  for (const uint8_t* p = (const uint8_t*)s; *p; p++) {
    uint32_t ch = *p;
    if (ch >= 0x80) {  // skip UTF-8 multibyte sequences (nothing to type them with)
      continue;
    }
    if (ch == '\r' && p[1] == '\n') continue;
    int n = (tHead + 1) % TN;
    if (n == tTail) return;
    text[tHead] = (char)ch;
    tHead = n;
  }
}

int MacKeys::pendingText() const { return (tHead - tTail + TN) % TN; }

void MacKeys::releaseAll() { releaseReq = true; }

// ---------------------------------------------------------------- emulator side
bool MacKeys::wireFree(int n) const {
  if (umac_is_se) return adb_room() >= n;   // Mac SE: keys go to the ADB keyboard
  return (WN - 1 - (wHead - wTail + WN) % WN) >= n;
}

// Mac SE (ADB): key codes are the same as the Plus keyboard's, except the arrow keys,
// which the Plus sent from its keypad
static uint8_t adbCode(uint8_t code) {
  switch (code) {
    case KP_LEFT: return 0x3B;
    case KP_RIGHT: return 0x3C;
    case KP_DOWN: return 0x3D;
    case KP_UP: return 0x3E;
  }
  return code;
}

// Keyboard wire format (M0110 protocol): main keys are (code << 1) | 1, bit 7 set on release.
// Keypad keys (codes 0x40-0x7F) are sent as 0x79 followed by ((code - 0x40) << 1) | 1.
void MacKeys::sendCode(uint8_t code, bool down) {
  if (code >= 128 || !wireFree(2)) return;
  if (down && held[code]) return;          // client auto-repeat: the Mac repeats by itself
  if (!down && !held[code]) return;
  held[code] = down;
  if (umac_is_se) { adb_key(adbCode(code), down); return; }
  if (code >= 0x40) {
    wire[wHead] = 0x79; wHead = (wHead + 1) % WN;
    wire[wHead] = (uint8_t)((((code - 0x40) << 1) | 1) | (down ? 0 : 0x80)); wHead = (wHead + 1) % WN;
  } else {
    wire[wHead] = (uint8_t)(((code << 1) | 1) | (down ? 0 : 0x80)); wHead = (wHead + 1) % WN;
  }
}

void MacKeys::handle(uint32_t sym, bool down) {
  uint8_t k = macKeyForKeysym(sym, nullptr);
  if (k == K_NONE) return;
  if (k == K_CAPS) {  // the Mac's Caps Lock is a locking key: toggle on each press
    if (!down) return;
    capsLocked = !capsLocked;
    held[K_CAPS] = !capsLocked;  // let sendCode see a change
    sendCode(K_CAPS, capsLocked);
    return;
  }
  sendCode(k, down);
}

bool MacKeys::typeChar(uint32_t ch) {
  bool sh;
  uint8_t k = macKeyForKeysym(ch, &sh);
  if (k == K_NONE) return true;   // nothing to type: skip it
  if (!wireFree(6)) return false;
  bool shiftWasDown = held[K_SHIFT];
  if (sh && !shiftWasDown) sendCode(K_SHIFT, true);
  if (!sh && shiftWasDown) sendCode(K_SHIFT, false);
  sendCode(k, true);
  sendCode(k, false);
  if (sh && !shiftWasDown) sendCode(K_SHIFT, false);
  if (!sh && shiftWasDown) sendCode(K_SHIFT, true);
  return true;
}

void MacKeys::service() {
  if (releaseReq) {
    releaseReq = false;
    qTail = qHead;
    for (int c = 0; c < 128; c++)
      if (held[c] && c != K_CAPS) sendCode(c, false);
  }
  while (qTail != qHead && wireFree(4)) {
    Ev e = q[qTail];
    qTail = (qTail + 1) % QN;
    handle(e.sym, e.down);
  }
  // pasted text: only when nothing else is waiting, so typing and pasting don't interleave
  if (wHead == wTail && tTail != tHead) {
    if (typeChar((uint8_t)text[tTail])) tTail = (tTail + 1) % TN;
  }
  if (!umac_is_se && wHead != wTail && umac_kbd_ready()) {
    umac_kbd_event(wire[wTail], 1);   // raw wire byte (down=1 leaves it unchanged)
    wTail = (wTail + 1) % WN;
  }
}
