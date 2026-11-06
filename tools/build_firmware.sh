#!/bin/sh
# Builds both firmwares with arduino-cli and writes ready-to-flash images (bootloader +
# partition table + app, for writing at 0x0):
#   firmware/Heltec-V3-Mac128k.bin  Heltec WiFi LoRa 32 V3 (Mac 128K / Plus)
#   firmware/Heltec-V4-Mac-SE.bin   Heltec WiFi LoRa 32 V4 (Mac SE, uses the PSRAM)
# Usage: tools/build_firmware.sh [v3|v4]     (default: both)
#   ARDUINO_CLI=/path/to/arduino-cli  ARDUINO_CONFIG=/path/to/arduino-cli.yaml  to override
set -e
here=$(cd "$(dirname "$0")/.." && pwd)
CLI=${ARDUINO_CLI:-arduino-cli}
CFG=${ARDUINO_CONFIG:+--config-file $ARDUINO_CONFIG}
EXTRA=${ARDUINO_EXTRA:-}
mkdir -p "$here/firmware"

image() {   # merged.bin -> image trimmed to the end of the app
  python3 - "$1" "$2" "$3" <<'PY'
import sys
merged, app, out = sys.argv[1:]
m = open(merged, 'rb').read(); a = open(app, 'rb').read()
n = (0x10000 + len(a) + 4095) & ~4095
assert m[0] == 0xE9 and m[0x8000:0x8002] == b'\xaa\x50' and m[0x10000:0x10000 + len(a)] == a
open(out, 'wb').write(m[:n]); print(out, n, 'bytes')
PY
}

build_v3() {
  out=$(mktemp -d)
  $CLI $CFG compile --fqbn esp32:esp32:heltec_wifi_lora_32_V3 $EXTRA --output-dir "$out" "$here/Heltec-Mac"
  image "$out/Heltec-Mac.ino.merged.bin" "$out/Heltec-Mac.ino.bin" "$here/firmware/Heltec-V3-Mac128k.bin"
}

build_v4() {
  # same sketch, with the V4's 16 MB partition table
  tmp=$(mktemp -d); cp -r "$here/Heltec-Mac" "$tmp/Heltec-Mac"; rm -rf "$tmp/Heltec-Mac/build"
  cp "$here/Heltec-Mac/partitions-v4.csv" "$tmp/Heltec-Mac/partitions.csv"
  out=$(mktemp -d)
  $CLI $CFG compile --fqbn "esp32:esp32:heltec_wifi_lora_32_V4:PSRAM=enabled,FlashSize=16M,CDCOnBoot=cdc,USBMode=hwcdc" $EXTRA \
      --build-property upload.maximum_size=2097152 \
      --output-dir "$out" "$tmp/Heltec-Mac"
  image "$out/Heltec-Mac.ino.merged.bin" "$out/Heltec-Mac.ino.bin" "$here/firmware/Heltec-V4-Mac-SE.bin"
  rm -rf "$tmp"
}

case "${1:-both}" in
  v3) build_v3 ;;
  v4) build_v4 ;;
  *) build_v3; build_v4 ;;
esac
