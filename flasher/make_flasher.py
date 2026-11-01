# Builds Heltec-Mac-Flasher.html: the template + esptool-js (Apache-2.0, Espressif) + the firmware.
#   npm install esptool-js@0.5.7 && python3 make_flasher.py node_modules/esptool-js/bundle.js ../firmware/Heltec-Mac.bin
import sys, base64, re, hashlib
bundle, fw = sys.argv[1], sys.argv[2]
js = open(bundle, encoding='utf-8').read()
m = re.search(r'export\{([^}]*)\};?\s*$', js)
pairs = [p.strip().split(' as ') for p in m.group(1).split(',')]
js = js[:m.start()] + 'window.esptool={' + ','.join(f'{b}:{a}' for a, b in pairs) + '};'
js = '/* esptool-js 0.5.7, Copyright Espressif Systems, Apache License 2.0 */\n' + js.replace('</script', '<\\/script')
data = open(fw, 'rb').read()
info = f"({len(data)//1024} KB, md5 {hashlib.md5(data).hexdigest()[:8]})"
t = open('template.html', encoding='utf-8').read()
t = t.replace('%%ESPTOOL%%', js).replace('%%FWB64%%', base64.b64encode(data).decode()).replace('%%FWINFO%%', info)
open('Heltec-Mac-Flasher.html', 'w', encoding='utf-8').write(t)
print('Heltec-Mac-Flasher.html', len(t), 'bytes', info)
