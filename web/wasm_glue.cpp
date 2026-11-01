// Heltec-Mac browser build: the same emulator, run as WebAssembly in a web page.
// The ROM and disk image live in memory buffers the page fills from files the user picks.
#include <stdlib.h>
#include <string.h>
#include <string>
#include "../Heltec-Mac/macmachine.h"
#include "../Heltec-Mac/mackeys.h"

#define EXPORT(n) __attribute__((export_name(#n)))

struct MemArea : FlashArea {
  uint8_t* d = nullptr; size_t n = 0;
  void init(size_t sz) { free(d); d = (uint8_t*)malloc(sz); n = sz; memset(d, 0xFF, sz); }
  size_t size() const override { return n; }
  bool read(uint32_t o, void* b, size_t k) override { if (o + k > n) return false; memcpy(b, d + o, k); return true; }
  bool write(uint32_t o, const void* b, size_t k) override { if (o + k > n) return false; memcpy(d + o, b, k); return true; }
  bool erase(uint32_t o, size_t k) override { if (o + k > n) return false; memset(d + o, 0xFF, k); return true; }
  const uint8_t* map(uint32_t o, size_t) override { return d + o; }
  void unmap() override {}
};

static MemArea romA, diskA;
static std::string msg;
static uint8_t audioBuf[MacMachine::AUDIO_BYTES];

extern "C" {
EXPORT(mm_buffer) uint8_t* mm_buffer(uint32_t size) { return (uint8_t*)malloc(size); }
EXPORT(mm_free) void mm_free(void* p) { free(p); }

// Set up empty storage (disk area sized for the biggest image the user might load)
EXPORT(mm_init) void mm_init(uint32_t diskMax) {
  romA.init(ROM_AREA_SIZE);
  diskA.init(diskMax);
  mac.begin(&romA, &diskA);
}
// Returns 1 if the ROM is a usable Mac Plus v3 ROM; mm_message() says why not
EXPORT(mm_load_rom) int mm_load_rom(const uint8_t* data, uint32_t len) {
  mac.beginRomUpload();
  mac.romUploadChunk(data, len);
  return mac.finishRomUpload(msg) ? 1 : 0;
}
EXPORT(mm_load_disk) int mm_load_disk(const uint8_t* data, uint32_t len) {
  mac.beginDiskUpload("disk");
  for (uint32_t o = 0; o < len; o += 4096) mac.diskUploadChunk(data + o, len - o < 4096 ? len - o : 4096);
  return mac.finishDiskUpload(msg) ? 1 : 0;
}
EXPORT(mm_message) const char* mm_message() { return msg.c_str(); }
EXPORT(mm_start) int mm_start(uint32_t ramKB) {
  mac.setRamSetting(ramKB);
  return mac.start(16u << 20, 16u << 20, 0, msg) ? 1 : 0;
}
EXPORT(mm_ram) uint32_t mm_ram() { return mac.ramBytes(); }
EXPORT(mm_step) int mm_step(double nowUs) { return (int)mac.step((int64_t)nowUs); }
EXPORT(mm_frame) uint32_t mm_frame() { return mac.frameCount; }
EXPORT(mm_fb) const uint8_t* mm_fb() { return mac.framebuffer(); }
EXPORT(mm_crashed) int mm_crashed() { return mac.crashed() ? 1 : 0; }
EXPORT(mm_reset) void mm_reset() { mac.reset(); }
EXPORT(mm_key) void mm_key(uint32_t sym, int down) { macKeys.push(sym, down != 0); }
EXPORT(mm_type) void mm_type(const char* s) { macKeys.typeText(s); }
EXPORT(mm_mouse) void mm_mouse(int x, int y, int b) { mac.mouse(x, y, b != 0); }
EXPORT(mm_audio_on) void mm_audio_on(int on) { mac.audioWanted = on != 0; }
EXPORT(mm_audio) const uint8_t* mm_audio() { return mac.popAudio(audioBuf) ? audioBuf : nullptr; }
EXPORT(mm_disk_size) uint32_t mm_disk_size() { mac.flushDisk(); return mac.meta().diskSize; }
EXPORT(mm_disk_data) const uint8_t* mm_disk_data() { return diskA.d; }
EXPORT(mm_set_turbo) void mm_set_turbo(int t) { mac.setTurbo(t != 0); }
}
