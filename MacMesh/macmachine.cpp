#include "macmachine.h"
#include "mackeys.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

extern "C" {
#include "src/core/umac.h"
#include "src/core/rom.h"
}

MacMachine mac;

static const uint32_t META_MAGIC = 0x4853'4D4D;  // "MMSH"
static const uint32_t SECTOR = 4096;
static const int64_t QUANTUM_US = 5000;          // Mac time run per umac_loop()

// ------------------------------------------------------------------ ROM identification
RomInfo identifyRom(const uint8_t* rom, size_t len) {
  RomInfo r = {0, false, "Unknown ROM", false};
  if (len < 8) return r;
  r.checksum = ((uint32_t)rom[0] << 24) | (rom[1] << 16) | (rom[2] << 8) | rom[3];
  uint32_t sum = 0;
  for (size_t i = 4; i + 1 < len; i += 2) sum += (rom[i] << 8) | rom[i + 1];
  r.checksumOk = sum == r.checksum;
  struct { uint32_t ck; const char* name; } known[] = {
      {0x28BA61CE, "Mac 128K/512K ROM"},
      {0x28BA4E50, "Mac 512K ROM"},
      {0x4D1EEEE1, "Mac Plus ROM v1 (Lonely Hearts)"},
      {0x4D1EEAE1, "Mac Plus ROM v2 (Lonely Heifers)"},
      {0x4D1F8172, "Mac Plus ROM v3 (Loud Harmonicas)"},
      {0xB2E362A8, "Mac SE ROM"},
      {0xB306E171, "Mac SE FDHD ROM"},
      {0xA49F9914, "Mac Classic ROM"},
  };
  for (auto& k : known)
    if (k.ck == r.checksum) r.name = k.name;
  r.supported = r.checksum == PLUS_V3_CHECKSUM && r.checksumOk && len == MAC_ROM_SIZE;
  return r;
}

// ------------------------------------------------------------------ disk image formats
static uint32_t be32(const uint8_t* p) { return ((uint32_t)p[0] << 24) | (p[1] << 16) | (p[2] << 8) | p[3]; }
static uint16_t be16(const uint8_t* p) { return (p[0] << 8) | p[1]; }

static bool isDC42(const uint8_t* h, size_t n) {
  // DiskCopy 4.2: name length byte (< 64), data size @0x40, magic 0x0100 @0x52, data @0x54
  return n >= 0x54 && h[0] < 64 && be16(h + 0x52) == 0x0100 && be32(h + 0x40) > 0 && (be32(h + 0x40) & 511) == 0;
}

DiskFormat detectDiskFormat(const uint8_t* h, size_t n, uint32_t& start, uint32_t& len) {
  start = 0; len = 0;
  if (isDC42(h, n)) { start = 0x54; len = be32(h + 0x40); return DISK_DC42; }
  // MacBinary wrapper (128-byte header) around a DiskCopy image
  if (n >= 128 + 0x54 && h[0] == 0 && h[1] > 0 && h[1] < 64 && h[74] == 0 && isDC42(h + 128, n - 128)) {
    start = 128 + 0x54; len = be32(h + 128 + 0x40); return DISK_MACBINARY_DC42;
  }
  // Raw image: boot blocks ('LK') or an MFS/HFS volume header at 1 KB
  if (n >= 2 && h[0] == 'L' && h[1] == 'K') return DISK_RAW;
  if (n >= 1026 && (be16(h + 1024) == 0xD2D7 || be16(h + 1024) == 0x4244)) return DISK_RAW;
  return DISK_UNKNOWN;
}

const char* diskFormatName(DiskFormat f) {
  switch (f) {
    case DISK_RAW: return "raw disk image";
    case DISK_DC42: return "DiskCopy 4.2 image";
    case DISK_MACBINARY_DC42: return "MacBinary DiskCopy image";
    default: return "unknown";
  }
}

void inspectDisk(FlashArea* disk, uint32_t size, bool& bootable, std::string& fs, std::string& volName) {
  bootable = false; fs.clear(); volName.clear();
  if (!disk || size < 2048) return;
  uint8_t h[2]; uint8_t mdb[64];
  disk->read(0, h, 2);
  bootable = h[0] == 'L' && h[1] == 'K';
  disk->read(1024, mdb, sizeof mdb);
  uint16_t sig = be16(mdb);
  if (sig == 0xD2D7) {          // MFS: name at 0x24
    fs = "MFS";
    int l = mdb[0x24] < 27 ? mdb[0x24] : 27;
    volName.assign((const char*)mdb + 0x25, l);
  } else if (sig == 0x4244) {   // HFS: name at 0x24
    fs = "HFS";
    int l = mdb[0x24] < 27 ? mdb[0x24] : 27;
    volName.assign((const char*)mdb + 0x25, l);
  }
  for (auto& c : volName) if ((uint8_t)c < 0x20 || (uint8_t)c > 0x7E) c = '?';
}

// ------------------------------------------------------------------ disk callbacks for umac
static int cbRead(void* ctx, uint8_t* data, unsigned int off, unsigned int len) {
  return ((MacMachine*)ctx)->diskRead(data, off, len);
}
static int cbWrite(void* ctx, uint8_t* data, unsigned int off, unsigned int len) {
  return ((MacMachine*)ctx)->diskWrite(data, off, len);
}

bool MacMachine::loadSector(int32_t idx) {
  if (idx == secIdx) return true;
  flushDisk();
  if (!diskArea->read(idx * SECTOR, secBuf, SECTOR)) { secIdx = -1; return false; }
  secIdx = idx;
  return true;
}

void MacMachine::flushDisk() {
  if (!secDirty || secIdx < 0) return;
  diskArea->erase(secIdx * SECTOR, SECTOR);
  diskArea->write(secIdx * SECTOR, secBuf, SECTOR);
  secDirty = false;
}

int MacMachine::diskRead(uint8_t* data, uint32_t off, uint32_t len) {
  if (off + len > m.diskSize) return -1;
  while (len) {
    int32_t idx = off / SECTOR;
    uint32_t in = off % SECTOR, n = SECTOR - in;
    if (n > len) n = len;
    if (idx == secIdx) memcpy(data, secBuf + in, n);
    else if (!diskArea->read(off, data, n)) return -1;
    data += n; off += n; len -= n;
  }
  return 0;
}

int MacMachine::diskWrite(const uint8_t* data, uint32_t off, uint32_t len) {
  if (m.diskRO || off + len > m.diskSize) return -1;
  while (len) {
    int32_t idx = off / SECTOR;
    uint32_t in = off % SECTOR, n = SECTOR - in;
    if (n > len) n = len;
    if (!loadSector(idx)) return -1;
    if (memcmp(secBuf + in, data, n)) { memcpy(secBuf + in, data, n); secDirty = true; }
    data += n; off += n; len -= n;
  }
  lastDiskWrite = curNow;
  return 0;
}

// ------------------------------------------------------------------ setup
bool MacMachine::begin(FlashArea* r, FlashArea* d) {
  romArea = r; diskArea = d;
  memset(&m, 0, sizeof m);
  romArea->read(ROM_AREA_META, &m, sizeof m);
  if (m.magic != META_MAGIC || m.version != 1) {
    memset(&m, 0, sizeof m);
    m.magic = META_MAGIC; m.version = 1;
    saveMeta();
  }
  if (m.diskSize > diskArea->size()) m.diskSize = 0;
  m.diskName[sizeof m.diskName - 1] = 0;
  return true;
}

void MacMachine::lastBootCrashed() {
  m.crashCount++;
  saveMeta();
  if (m.ramSetting || !m.lastRamKB) return;
  static const uint32_t sizes[] = {256, 128};
  for (uint32_t kb : sizes)
    if (kb < m.lastRamKB) { m.autoCapKB = kb; saveMeta(); return; }
}

void MacMachine::saveMeta() {
  romArea->erase(ROM_AREA_META, SECTOR);
  romArea->write(ROM_AREA_META, &m, sizeof m);
}

// Patches the ROM for this RAM size, using the (not yet started) Mac RAM as the 128 KB work
// buffer, so no extra memory is needed.
bool MacMachine::patchRom(uint32_t ramBytes, uint8_t* buf) {
  bool ok = romArea->read(ROM_AREA_ORIG, buf, MAC_ROM_SIZE);
  umac_ram_size = ramBytes;
  ok = ok && rom_patch(buf) == 0;
  ok = ok && romArea->erase(ROM_AREA_PATCHED, MAC_ROM_SIZE) && romArea->write(ROM_AREA_PATCHED, buf, MAC_ROM_SIZE);
  if (ok) { m.patchedRam = ramBytes; saveMeta(); }
  return ok;
}

bool MacMachine::start(size_t freeBytes, size_t largest, size_t reserve, std::string& why) {
  if (!m.romOk) { why = "No Mac Plus ROM yet - upload one on the web page."; return false; }

  // Pick the RAM size: the user's choice if it fits, else the biggest that leaves room for Wi-Fi.
  // Only 128 KB and 256 KB work: the Mac Plus ROM needs a power-of-two memory size
  // (160/192/208 KB hang on a grey screen).
  // (Bigger sizes only ever fit in the PC and browser builds.)
  static const uint32_t sizes[] = {4096, 2048, 1024, 512, 256, 128};
  if (m.ramSetting & (m.ramSetting - 1)) m.ramSetting = 0;   // not a power of two
  uint32_t pick = 0;
  auto fits = [&](uint32_t kb, size_t keep) { return kb * 1024 <= largest && freeBytes >= kb * 1024 + keep; };
  if (m.ramSetting && fits(m.ramSetting, reserve / 2)) pick = m.ramSetting;
  for (uint32_t kb : sizes)
    if (!pick && fits(kb, reserve) && (!m.autoCapKB || kb <= m.autoCapKB)) pick = kb;
  if (!pick && fits(128, reserve / 2)) pick = 128;
  if (!pick) { why = "Not enough free memory for even a 128K Mac."; return false; }
  uint32_t bytes = pick * 1024;

  ram = (uint8_t*)malloc(bytes);
  if (!ram) { why = "RAM allocation failed."; return false; }
  if (m.patchedRam != bytes && !patchRom(bytes, ram)) {
    free(ram); ram = nullptr;
    why = "Couldn't prepare the ROM (bad ROM?) - upload it again.";
    return false;
  }
  umac_ram_size = bytes;
  rom = romArea->map(ROM_AREA_PATCHED, MAC_ROM_SIZE);
  if (!rom) { free(ram); ram = nullptr; why = "Couldn't map the ROM."; return false; }
  memset(ram, 0, bytes);
  ramSize = bytes;
  if (m.lastRamKB != pick) { m.lastRamKB = pick; saveMeta(); }
  if (!secBuf) secBuf = (uint8_t*)malloc(SECTOR);

  disc_descr_t discs[DISC_NUM_DRIVES];
  memset(discs, 0, sizeof discs);
  discs[0].base = nullptr;              // nullptr = use the read/write callbacks
  discs[0].size = m.diskSize;
  discs[0].read_only = m.diskRO ? 1 : 0;
  discs[0].op_ctx = this;
  discs[0].op_read = cbRead;
  discs[0].op_write = cbWrite;
  umac_init(ram, (void*)rom, discs);
  timeInit = false;
  return true;
}

#ifdef MACMESH_HOST
bool MacMachine::startTest(const uint8_t* testRom, uint32_t bytes) {
  umac_ram_size = bytes;
  rom = testRom;
  ram = (uint8_t*)calloc(1, bytes);
  ramSize = bytes;
  if (!secBuf) secBuf = (uint8_t*)malloc(SECTOR);
  disc_descr_t discs[DISC_NUM_DRIVES];
  memset(discs, 0, sizeof discs);
  umac_init(ram, (void*)rom, discs);
  timeInit = false;
  return true;
}
#endif

static void reinsertDisk(MacMachine* mm, uint32_t size, bool ro) {
  disc_descr_t discs[DISC_NUM_DRIVES];
  memset(discs, 0, sizeof discs);
  discs[0].size = size;
  discs[0].read_only = ro ? 1 : 0;
  discs[0].op_ctx = mm;
  discs[0].op_read = cbRead;
  discs[0].op_write = cbWrite;
  disc_init(discs);
}

void MacMachine::reset() {
  flushDisk();
  if (ram) { reinsertDisk(this, m.diskSize, m.diskRO); umac_reset(); }
  macKeys.releaseAll();
}

bool MacMachine::crashed() const { return ram && umac_crashed(); }

const uint8_t* MacMachine::framebuffer() const {
  return ram ? ram + umac_get_fb_offset() : nullptr;
}

void MacMachine::captureAudio() {
  int n = (aHead + 1) % AUDIO_FRAMES;
  if (n == aTail) return;   // viewer isn't keeping up: drop this frame
  uint8_t* f = audio[aHead];
  int vol = 0;
  bool on = umac_sound_frame(f + 1, &vol);
  bool flat = true;
  for (int i = 2; i <= 370 && flat; i++) flat = f[i] == f[1];
  f[0] = (!on || vol == 0 || flat) ? 0xFF : (uint8_t)vol;
  aHead = n;
}

bool MacMachine::popAudio(uint8_t out[AUDIO_BYTES]) {
  if (aTail == aHead) return false;
  memcpy(out, audio[aTail], AUDIO_BYTES);
  aTail = (aTail + 1) % AUDIO_FRAMES;
  return true;
}

void MacMachine::mouse(int x, int y, bool button) {
  mx = x; my = y; mb = button;
  mouseSeq++;
}

int64_t MacMachine::step(int64_t now) {
  if (!ram) return 10000;
  curNow = now;
  if (!timeInit) { lastVsync = last1Hz = macTime = now; timeInit = true; }
  if (umac_crashed()) return 20000;

  // Throttle to Mac Plus speed: each umac_loop() is 5 ms of Mac time
  if (!m.turbo) {
    if (macTime > now + QUANTUM_US) return macTime - now - QUANTUM_US / 2;
    if (macTime < now - 100000) macTime = now - 100000;   // too far behind: don't try to catch up
  }
  umac_loop();
  loops++;
  macTime += QUANTUM_US;

  if (now - lastVsync >= 16667) {
    lastVsync += 16667;
    if (now - lastVsync > 50000) lastVsync = now;
    umac_vsync_event();
    frameCount++;
    uint32_t seq = mouseSeq;
    if (seq != mouseDone) { mouseDone = seq; umac_mouse_abs(mx, my, mb); }
    macKeys.service();
    if (audioWanted) captureAudio();
    // write the disk sector back once the Mac has been quiet on it for a second
    if (secDirty && now - lastDiskWrite > 1000000) flushDisk();
  }
  if (now - last1Hz >= 1000000) { last1Hz += 1000000; if (now - last1Hz > 2000000) last1Hz = now; umac_1hz_event(); }
  return 0;
}

// ------------------------------------------------------------------ uploads
void MacMachine::stop() {
  flushDisk();
  if (ram) { free(ram); ram = nullptr; ramSize = 0; }
  if (rom) { romArea->unmap(); rom = nullptr; }
}

void MacMachine::beginRomUpload() {
  stop();
  romArea->erase(ROM_AREA_PATCHED, MAC_ROM_SIZE);
  m.patchedRam = 0;   // the patched copy is being overwritten
  saveMeta();
  upPos = 0; upFailed = false;
}

// Keeps the first 128 KB; anything after it is counted but ignored (some ROM dumps carry extra
// bytes on the end). finishRomUpload() checks that the first 128 KB is a real ROM.
bool MacMachine::romUploadChunk(const uint8_t* d, size_t n) {
  if (upPos < MAC_ROM_SIZE) {
    size_t k = MAC_ROM_SIZE - upPos < n ? MAC_ROM_SIZE - upPos : n;
    romArea->write(ROM_AREA_PATCHED + upPos, d, k);
  }
  upPos += n;
  return true;
}

bool MacMachine::finishRomUpload(std::string& msg) {
  if (upPos < MAC_ROM_SIZE || upPos > 2 * MAC_ROM_SIZE) {
    char b[160];
    snprintf(b, sizeof b, "That file is %u bytes; a Mac Plus ROM is 131072 bytes (128 KB).", (unsigned)upPos);
    msg = b;
    return false;
  }
  // checksum + identify, reading back from flash in pieces
  uint8_t head[8];
  romArea->read(ROM_AREA_PATCHED, head, 8);
  uint32_t ck = ((uint32_t)head[0] << 24) | (head[1] << 16) | (head[2] << 8) | head[3];
  uint32_t sum = 0;
  uint8_t buf[1024];
  for (uint32_t off = 0; off < MAC_ROM_SIZE; off += sizeof buf) {
    romArea->read(ROM_AREA_PATCHED + off, buf, sizeof buf);
    for (uint32_t i = (off ? 0 : 4); i < sizeof buf; i += 2) sum += (buf[i] << 8) | buf[i + 1];
  }
  uint8_t fake[8]; memcpy(fake, head, 8);
  RomInfo info = identifyRom(fake, 8);   // just for the name
  char b[200];
  if (sum != ck) {
    snprintf(b, sizeof b, "Checksum doesn't match (header %08X, contents %08X) - the file looks damaged or isn't a Mac ROM.", (unsigned)ck, (unsigned)sum);
    msg = b; return false;
  }
  if (ck != PLUS_V3_CHECKSUM) {
    if (strcmp(info.name, "Unknown ROM") == 0)
      snprintf(b, sizeof b, "That isn't a ROM Mac Mesh knows (checksum %08X). It needs the Mac Plus v3 ROM, checksum 4D1F8172.", (unsigned)ck);
    else
      snprintf(b, sizeof b, "That's a %s (%08X). Mac Mesh needs the Mac Plus v3 ROM, checksum 4D1F8172.", info.name, (unsigned)ck);
    msg = b; return false;
  }
  // good: copy staging -> orig
  romArea->erase(ROM_AREA_ORIG, MAC_ROM_SIZE);
  for (uint32_t off = 0; off < MAC_ROM_SIZE; off += sizeof buf) {
    romArea->read(ROM_AREA_PATCHED + off, buf, sizeof buf);
    romArea->write(ROM_AREA_ORIG + off, buf, sizeof buf);
  }
  m.romOk = 1; m.romChecksum = ck; m.patchedRam = 0; m.crashCount = 0;
  saveMeta();
  msg = std::string("Mac Plus v3 ROM saved (") + info.name + ").";
  if (upPos > MAC_ROM_SIZE) {
    snprintf(b, sizeof b, " The file had %u extra bytes after the ROM; they were ignored.", (unsigned)(upPos - MAC_ROM_SIZE));
    msg += b;
  }
  return true;
}

void MacMachine::beginDiskUpload(const std::string& name) {
  stop();
  secIdx = -1; secDirty = false;
  m.diskSize = 0;
  snprintf(m.diskName, sizeof m.diskName, "%s", name.c_str());
  saveMeta();
  upPos = upErased = upStart = upLen = upSkipped = upHeadN = 0;
  upHeaderDone = upFailed = false;
  upFormat = DISK_UNKNOWN;
  upName = name;
  uploadMsg.clear();
}

bool MacMachine::diskWriteUpload(const uint8_t* d, size_t n) {
  // skip the container header, stop at the data length
  while (n && upSkipped < upStart) { d++; n--; upSkipped++; }
  if (upLen && upPos + n > upLen) n = upLen - upPos;
  if (!n) return true;
  if (upPos + n > diskArea->size()) {
    char b[120];
    snprintf(b, sizeof b, "Disk image is too big: the board holds up to %u KB.", (unsigned)(diskArea->size() / 1024));
    uploadMsg = b; upFailed = true; return false;
  }
  while (upErased < upPos + n) { diskArea->erase(upErased, SECTOR); upErased += SECTOR; }
  diskArea->write(upPos, d, n);
  upPos += n;
  return true;
}

bool MacMachine::diskUploadChunk(const uint8_t* d, size_t n) {
  if (upFailed) return false;
  if (!upHeaderDone) {
    while (n && upHeadN < sizeof upHead) { upHead[upHeadN++] = *d++; n--; }
    if (upHeadN < sizeof upHead && n == 0) return true;   // need more to decide
    upFormat = detectDiskFormat(upHead, upHeadN, upStart, upLen);
    upHeaderDone = true;
    if (!diskWriteUpload(upHead, upHeadN)) return false;
  }
  return diskWriteUpload(d, n);
}

bool MacMachine::finishDiskUpload(std::string& msg) {
  if (!upHeaderDone && upHeadN) {   // tiny file
    upFormat = detectDiskFormat(upHead, upHeadN, upStart, upLen);
    upHeaderDone = true;
    diskWriteUpload(upHead, upHeadN);
  }
  if (upFailed) { msg = uploadMsg; return false; }
  if (upPos < 2048) { msg = "That file is too small to be a Mac disk image."; return false; }
  if (upLen && upPos < upLen) { msg = "The DiskCopy image is cut short."; return false; }
  uint32_t size = upPos & ~511u;
  m.diskSize = size;
  saveMeta();
  bool boot; std::string fs, vol;
  inspectDisk(diskArea, size, boot, fs, vol);
  char b[220];
  snprintf(b, sizeof b, "Saved %u KB %s%s%s%s%s.", (unsigned)(size / 1024), diskFormatName(upFormat),
           fs.size() ? (", " + fs).c_str() : "", vol.size() ? (" \"" + vol + "\"").c_str() : "",
           boot ? ", bootable" : ", not bootable (no boot blocks)",
           upFormat == DISK_UNKNOWN ? " - doesn't look like a Mac disk, but kept anyway" : "");
  msg = b;
  return true;
}

void MacMachine::ejectDisk() {
  stop();
  m.diskSize = 0;
  m.diskName[0] = 0;
  saveMeta();
}
