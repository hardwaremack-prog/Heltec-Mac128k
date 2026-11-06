# Builds a one-page flasher: the template + esptool-js (Apache-2.0, Espressif) + the firmware.
#   npm install esptool-js@0.5.7
#   python3 make_flasher.py node_modules/esptool-js/bundle.js ../firmware/Heltec-V3-Mac128k.bin -> Heltec-V3-Mac128k-Flasher.html
#   python3 make_flasher.py node_modules/esptool-js/bundle.js ../firmware/Heltec-V4-Mac-SE.bin --v4 -> Heltec-V4-Mac-SE-Flasher.html
import sys, base64, re, hashlib
bundle, fw = sys.argv[1], sys.argv[2]
v4 = '--v4' in sys.argv[3:]
BOARDS = {
  False: dict(NAME='Heltec V3 Mac 128K', BOARD='Heltec WiFi LoRa 32 V3', SHORT='Heltec V3', AP='Heltec-V3-Mac-Setup', FLASHMB='8',
              PORTHELP='Click <b>Connect</b> and pick the board. On Windows it shows as <b>CP210x</b> or <b>Silicon Labs</b> (COM port); on a Mac, <b>usbserial</b>.',
              DRIVERHELP='Windows needs the <b>CP210x USB to UART</b> driver from Silicon Labs (silabs.com, search "CP210x VCP drivers"). After installing, unplug and replug the board.',
              WRONGBOARD='', BACKUP='heltec-v3-mac-backup', OUT='Heltec-V3-Mac128k-Flasher.html'),
  True:  dict(NAME='Heltec V4 Mac SE', BOARD='Heltec WiFi LoRa 32 V4', SHORT='Heltec V4', AP='Heltec-V4-MacSE-Setup', FLASHMB='16',
              PORTHELP='Click <b>Connect</b> and pick the board. The V4 uses the ESP32-S3&apos;s own USB: on Windows it shows as <b>USB JTAG/serial debug unit</b> or <b>USB Serial Device</b> (COM port); on a Mac, <b>usbmodem</b>.',
              DRIVERHELP='The V4 needs no extra driver: it uses the ESP32-S3&apos;s own USB. If it doesn&apos;t show up, put it in download mode (hold <kbd>PRG</kbd>, tap <kbd>RST</kbd>, let go) and click Connect again.',
              WRONGBOARD='Is this a V3? Use Heltec-V3-Mac128k-Flasher.html for the V3.', BACKUP='heltec-v4-macse-backup', OUT='Heltec-V4-Mac-SE-Flasher.html'),
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
