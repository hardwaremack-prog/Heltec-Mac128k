// MacMachine: wraps the umac Mac Plus core with flash storage for the ROM and disk.
// Portable: the ESP32 build uses flash partitions, the PC test build uses plain files.
#pragma once
#include "heltec_config.h"
#include <stdint.h>
#include <stddef.h>
#include <string>

// A region of flash (or a file standing in for one). Writes need erased (0xFF) space.
struct FlashArea {
  virtual ~FlashArea() {}
  virtual size_t size() const = 0;
  virtual bool read(uint32_t off, void* buf, size_t n) = 0;
  virtual bool write(uint32_t off, const void* buf, size_t n) = 0;
  virtual bool erase(uint32_t off, size_t n) = 0;          // 4 KB aligned
  virtual const uint8_t* map(uint32_t off, size_t n) = 0;  // read-only view
  virtual void unmap() = 0;
};

#ifdef HELTEC_V4
// Heltec V4 (PSRAM, 16 MB flash): room for the 256 KB Mac SE ROMs too
static const uint32_t MAC_ROM_SIZE = 0x40000;          // 256 KB window (smaller ROMs are mirrored)
#else
static const uint32_t MAC_ROM_SIZE = 0x20000;          // 128 KB
#endif
static const uint32_t ROM_AREA_ORIG = 0;               // the ROM file as uploaded
static const uint32_t ROM_AREA_PATCHED = MAC_ROM_SIZE; // patched copy the emulator runs from
static const uint32_t ROM_AREA_META = 2 * MAC_ROM_SIZE;   // 4 KB of settings
static const uint32_t ROM_AREA_SIZE = 2 * MAC_ROM_SIZE + 0x1000;
static const uint32_t PLUS_V3_CHECKSUM = 0x4D1F8172;
static const uint32_t MAC128K_CHECKSUM = 0x28BA61CE;   // original 64 KB Mac 128K/512K ROM
static const uint32_t SE_CHECKSUM = 0xB2E362A8;        // Mac SE (256 KB)
static const uint32_t SEFDHD_CHECKSUM = 0xB306E171;    // Mac SE FDHD (256 KB)
inline bool isSeRom(uint32_t ck) { return ck == SE_CHECKSUM || ck == SEFDHD_CHECKSUM; }

struct MacMeta {
  uint32_t magic;        // 'MMSH'
  uint32_t version;
  uint32_t romOk;        // a valid Mac Plus v3 ROM is in the "orig" area
  uint32_t romChecksum;
  uint32_t patchedRam;   // RAM size (bytes) the patched copy was made for; 0 = needs patching
  uint32_t diskSize;     // bytes of disk image in the disk area; 0 = no disk
  uint32_t diskRO;       // write-protect
  uint32_t ramSetting;   // 0 = auto, else KB
  uint32_t turbo;        // run flat out instead of Mac Plus speed
  char diskName[64];
  uint32_t autoCapKB;    // after a crash in Auto mode: don't go above this
  uint32_t lastRamKB;    // RAM size used by the last boot
  uint32_t crashCount;   // boots in a row that ended in a crash (safe mode at 3)
  // clock chip: parameter RAM (Control Panel settings) and the last known time
  uint8_t pram[20];
  int32_t tzMin;         // your computer's offset from UTC, minutes (from the browser)
  uint32_t lastTime;     // Mac time when last set/saved
  uint32_t clockFlags;   // bit 0: pram valid, bit 1: tzMin known
  uint32_t reserved[5];
  // disk library (several disks kept in the disk area; one is in use)
  uint32_t libMagic;     // 'LIB1' once set up
  int32_t active;        // library index of the disk in use, -1 = none
  struct LibDisk { char name[48]; uint32_t off, size; } lib[16];
};
static const int LIB_MAX = 16;

struct RomInfo { uint32_t checksum; bool checksumOk; const char* name; bool supported; };
RomInfo identifyRom(const uint8_t* rom, size_t len);

enum DiskFormat { DISK_RAW, DISK_DC42, DISK_MACBINARY_DC42, DISK_UNKNOWN };
// Looks at the first bytes of an upload; returns where the disk data starts and how long it is (0 = to the end)
DiskFormat detectDiskFormat(const uint8_t* head, size_t n, uint32_t& dataStart, uint32_t& dataLen);
const char* diskFormatName(DiskFormat f);
// What's on the disk: "MFS", "HFS" or ""; bootable = has boot blocks ('LK')
void inspectDisk(FlashArea* disk, uint32_t base, uint32_t size, bool& bootable, std::string& fs, std::string& volName);

class MacMachine {
 public:
  bool begin(FlashArea* romArea, FlashArea* diskArea);
  const MacMeta& meta() const { return m; }
  void saveMeta();
  void setRamSetting(uint32_t kb) { m.ramSetting = kb; saveMeta(); }
  void setDiskRO(bool ro) { m.diskRO = ro; saveMeta(); }
  void setTurbo(bool t) { m.turbo = t; saveMeta(); }

  // Boot: pick a RAM size that fits (freeBytes/largestBlock from the heap), patch the ROM if
  // needed, allocate RAM and start the Mac. Returns false (with why) if it can't start.
  bool start(size_t freeBytes, size_t largestBlock, size_t reserveBytes, std::string& why);
  // Call at boot if the last run crashed (e.g. ran out of memory): Auto mode then uses a smaller Mac
  void lastBootCrashed();
  void bootWasClean() { if (m.crashCount) { m.crashCount = 0; saveMeta(); } }
  // Clock: set from the browser (your computer's local time) or from the internet (UTC + saved offset).
  // Thread-safe: applied on the emulator thread at the next step.
  void setClock(uint32_t macSeconds, int tzMin, bool fromBrowser);
  bool timezoneKnown() const { return m.clockFlags & 2; }
  int timezoneMin() const { return m.tzMin; }
  volatile uint32_t pendingTime = 0;
  volatile bool clockSet = false;
  void setRamSettingClear(uint32_t kb) { m.ramSetting = kb; m.autoCapKB = 0; m.crashCount = 0; saveMeta(); }
#ifdef MACMESH_HOST
  // PC test build only: run an unpatched test ROM image with the given RAM size
  bool startTest(const uint8_t* testRom, uint32_t ramBytes);
#endif
  bool running() const { return ram != nullptr; }
  uint32_t ramBytes() const { return ramSize; }

  // Run ~5 ms of Mac time. Call in a loop from one thread only; it does its own timing
  // (vsync, 1 Hz, keyboard/mouse feeding). Returns microseconds to sleep (0 = keep going).
  int64_t step(int64_t nowUs);
  void reset();
  bool crashed() const;

  // Framebuffer: 512x342, 1 bit per pixel, 64 bytes per row, MSB = leftmost, 1 = black
  const uint8_t* framebuffer() const;
  volatile uint32_t frameCount = 0;
  volatile uint32_t loops = 0;   // ~5 ms Mac-time slices run (for the speed readout)

  // Sound: one entry per Mac video frame. byte 0 = volume (0-7) or 0xFF = silent, then 370 samples
  static const int AUDIO_FRAMES = 12, AUDIO_BYTES = 371;
  volatile bool audioWanted = false;              // a browser has sound turned on
  bool popAudio(uint8_t out[AUDIO_BYTES]);        // network thread
  int audioQueued() const { return (aHead - aTail + AUDIO_FRAMES) % AUDIO_FRAMES; }
  // Printer port: the Mac's ImageWriter output, read by the network thread and sent to the
  // browser (which turns it into a PDF). When this 1 KB buffer is full the Mac simply waits.
  int printerRead(uint8_t* out, int max);

  // Input (thread-safe: may be called from the network thread)
  void mouse(int x, int y, bool button);
  void flushDisk();

  // Disk access for the core
  int diskRead(uint8_t* data, uint32_t off, uint32_t len);
  int diskWrite(const uint8_t* data, uint32_t off, uint32_t len);

  // Uploads (the emulator must be stopped: call before start() or after pausing + stop())
  void beginRomUpload();                              // staging = the patched-copy area
  bool romUploadChunk(const uint8_t* d, size_t n);    // false = too big
  bool finishRomUpload(std::string& msg);             // validates, then keeps it as the ROM
  // replaceIndex >= 0 overwrites that library disk (same or smaller size); activate = make it the disk in use
  void beginDiskUpload(const std::string& name, int replaceIndex = -1, bool activate = true);
  bool diskUploadChunk(const uint8_t* d, size_t n);   // false = error (msg in uploadMsg)
  bool finishDiskUpload(std::string& msg);
  void ejectDisk();                                   // removes the disk in use from the library
  bool removeDisk(int i);
  bool useDisk(int i);                                // takes effect when the Mac starts
  uint32_t libraryFree() const;                       // free bytes (kept in one piece by compacting)
  void compactLibrary();                              // slides disks down over the gaps
  int lastUploadIndex = -1;
  std::string uploadMsg;
  void stop();                                        // release RAM + ROM mapping (before uploads)

  FlashArea* romArea = nullptr;
  FlashArea* diskArea = nullptr;

 private:
  MacMeta m;
  uint8_t* ram = nullptr;
  uint32_t ramSize = 0;
  const uint8_t* rom = nullptr;
  int64_t lastVsync = 0, last1Hz = 0, macTime = 0, lastDiskWrite = 0, curNow = 0;
  bool timeInit = false;
  volatile int mx = -1, my = -1;
  volatile bool mb = false;
  volatile uint32_t mouseSeq = 0;
  uint32_t mouseDone = 0;
  // one-sector write-back cache for the disk
  uint8_t* secBuf = nullptr;
  int32_t secIdx = -1;
  bool secDirty = false;
  bool loadSector(int32_t idx);
  int64_t lastPramCheck = 0, lastTimeSave = 0;
  uint32_t diskBase = 0;                              // where the disk in use starts in the disk area
  void freeGap(uint32_t& start, uint32_t& len, int ignore = -1) const;
  int upReplace = -1; bool upActivate = true; uint32_t upBase = 0, upLimit = 0;
  uint8_t audio[AUDIO_FRAMES][AUDIO_BYTES];
  volatile int aHead = 0, aTail = 0;
  void captureAudio();
  // upload state
  uint32_t upPos = 0, upErased = 0, upStart = 0, upLen = 0, upSkipped = 0;
  bool upHeaderDone = false, upFailed = false;
  DiskFormat upFormat = DISK_UNKNOWN;
  std::string upName;
  uint8_t upHead[256];
  uint32_t upHeadN = 0;
  bool diskWriteUpload(const uint8_t* d, size_t n);
  bool patchRom(uint32_t ramBytes, uint8_t* buf);
};

extern MacMachine mac;
