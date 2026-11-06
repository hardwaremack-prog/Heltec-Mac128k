# Builds a one-page flasher: the template + esptool-js (Apache-2.0, Espressif) + the firmware.
#   npm install esptool-js@0.5.7
#   python3 make_flasher.py node_modules/esptool-js/bundle.js ../firmware/Heltec-Mac.bin          -> Heltec-Mac-Flasher.html (V3)
#   python3 make_flasher.py node_modules/esptool-js/bundle.js ../firmware/Heltec-Mac-V4.bin --v4  -> Heltec-Mac-V4-Flasher.html
import sys, base64, re, hashlib
bundle, fw = sys.argv[1], sys.argv[2]
v4 = '--v4' in sys.argv[3:]
BOARDS = {
  False: dict(NAME='Heltec-Mac', BOARD='Heltec WiFi LoRa 32 V3', SHORT='Heltec V3', AP='Heltec-Mac-Setup', FLASHMB='8',
              PORTHELP='Click <b>Connect</b> and pick the board. On Windows it shows as <b>CP210x</b> or <b>Silicon Labs</b> (COM port); on a Mac, <b>usbserial</b>.',
              DRIVERHELP='Windows needs the <b>CP210x USB to UART</b> driver from Silicon Labs (silabs.com, search "CP210x VCP drivers"). After installing, unplug and replug the board.',
              WRONGBOARD='', OUT='Heltec-Mac-Flasher.html'),
  True:  dict(NAME='Heltec-Mac SE', BOARD='Heltec WiFi LoRa 32 V4', SHORT='Heltec V4', AP='Heltec-MacSE-Setup', FLASHMB='16',
              PORTHELP='Click <b>Connect</b> and pick the board. The V4 uses the ESP32-S3&apos;s own USB: on Windows it shows as <b>USB JTAG/serial debug unit</b> or <b>USB Serial Device</b> (COM port); on a Mac, <b>usbmodem</b>.',
              DRIVERHELP='The V4 needs no extra driver: it uses the ESP32-S3&apos;s own USB. If it doesn&apos;t show up, put it in download mode (hold <kbd>PRG</kbd>, tap <kbd>RST</kbd>, let go) and click Connect again.',
              WRONGBOARD='Is this a V3? Use Heltec-Mac-Flasher.html for the V3.', OUT='Heltec-Mac-V4-Flasher.html'),
}[v4]
js = open(bundle, encoding='utf-8').read()
m = re.search(r'export\{([^}]*)\};?\s*$', js)
pairs = [p.strip().split(' as ') for p in m.group(1).split(',')]
js = js[:m.start()] + 'window.esptool={' + ','.join(f'{b}:{a}' for a, b in pairs) + '};'
js = '/* esptool-js 0.5.7, Copyright Espressif Systems, Apache License 2.0 */\n' + js.replace('</script', '<\\/script')
data = open(fw, 'rb').read()
info = f"({len(data)//1024} KB, md5 {hashlib.md5(data).hexdigest()[:8]})"
t = open('template.html', encoding='utf-8').read()
for k, v in BOARDS.items():
    if k != 'OUT': t = t.replace('%%' + k + '%%', v)
t = t.replace('%%ESPTOOL%%', js).replace('%%FWB64%%', base64.b64encode(data).decode()).replace('%%FWINFO%%', info)
open(BOARDS['OUT'], 'w', encoding='utf-8').write(t)
print(BOARDS['OUT'], len(t), 'bytes', info)
