// Checks the Mac keyboard wire bytes produced for VNC/browser key events (stubs out the emulator).
// Build: g++ -std=c++17 -I../Heltec-Mac/src/core -o tests/keys_test tests/keys_test.cpp ../Heltec-Mac/mackeys.cpp   (run from host/)
#include <cstdio>
#include <vector>
#include <cstdint>
#include "../../Heltec-Mac/mackeys.h"
static std::vector<uint8_t> sent;
extern "C" int umac_kbd_ready(void) { return 1; }
extern "C" void umac_kbd_event(uint8_t sc, int down) { sent.push_back(down ? sc : sc | 0x80); }
static std::vector<uint8_t> run(std::vector<std::pair<uint32_t, bool>> ev, const char* text = nullptr) {
  MacKeys* k = new MacKeys();
  sent.clear();
  for (auto& e : ev) k->push(e.first, e.second);
  if (text) k->typeText(text);
  for (int i = 0; i < 400; i++) k->service();
  delete k;
  return sent;
}
static int fails = 0;
static void check(const char* name, std::vector<uint8_t> got, std::vector<uint8_t> want) {
  bool ok = got == want;
  printf("%-34s %s  [", name, ok ? "ok  " : "FAIL");
  for (auto b : got) printf(" %02X", b);
  printf(" ]\n");
  if (!ok) fails++;
}
int main() {
  // 'a' = keycode 0x00 -> down 0x01, up 0x81
  check("a down/up", run({{'a', 1}, {'a', 0}}), {0x01, 0x81});
  // Shift + 2 (VNC sends '@' while shift held) -> shift 0x38: 0x71, key 2 (0x13): 0x27
  check("Shift+2 (@)", run({{0xffe1, 1}, {'@', 1}, {'@', 0}, {0xffe1, 0}}), {0x71, 0x27, 0xA7, 0xF1});
  // auto-repeat downs are ignored (the Mac repeats by itself)
  check("repeat ignored", run({{'x', 1}, {'x', 1}, {'x', 1}, {'x', 0}}), {0x0F, 0x8F});
  // Ctrl = Command (0x37 -> 0x6F), then Q (0x0C -> 0x19)
  check("Ctrl+Q = Command-Q", run({{0xffe3, 1}, {'q', 1}, {'q', 0}, {0xffe3, 0}}), {0x6F, 0x19, 0x99, 0xEF});
  // Left arrow: keypad code 0x46 -> 0x79 prefix, then ((0x46-0x40)<<1)|1 = 0x0D (M0110A protocol)
  check("Left arrow (keypad prefix)", run({{0xff51, 1}, {0xff51, 0}}), {0x79, 0x0D, 0x79, 0x8D});
  check("Up/Right/Down codes", run({{0xff52, 1}, {0xff53, 1}, {0xff54, 1}}), {0x79, 0x1B, 0x79, 0x05, 0x79, 0x11});
  // Caps Lock locks: first press = down, second press = up
  check("Caps Lock toggles", run({{0xffe5, 1}, {0xffe5, 0}, {0xffe5, 1}, {0xffe5, 0}}), {0x73, 0xF3});
  // pasted "Hi!" -> Shift H, i, Shift 1
  check("paste Hi!", run({}, "Hi!"), {0x71, 0x09, 0x89, 0xF1, 0x45, 0xC5, 0x71, 0x25, 0xA5, 0xF1});
  // Return in pasted text
  check("paste newline", run({}, "\n"), {0x49, 0xC9});
  printf(fails ? "%d FAILED\n" : "all keyboard tests passed\n", fails);
  return fails != 0;
}
