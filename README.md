# Heltec-Mac

**A 1984 Macintosh running on a Meshtastic radio board, played in your web browser, with sound.**

Heltec-Mac turns a **Heltec WiFi LoRa 32 V3** (the little ESP32-S3 board many people use for
Meshtastic) into a working Macintosh 128K. The board emulates the Mac's 68000 processor and its
hardware, keeps a library of floppy disks in its own flash, and serves the Mac's screen and
sound to any browser on your network. There's no app to install: open the board's address on a
PC, a Raspberry Pi or a phone, and you're at the Finder.

![The Amazing maze game running in the browser on a Heltec V3](docs/heltec-mac.png)

*The 1985 maze game Amazing, running on the board and viewed in Chrome. Every Mac pixel is drawn
as a crisp 2×2 block. Battery level and the board's controls sit right on the page.*

## New: Heltec-Mac SE for the Heltec V4

The **Heltec WiFi LoRa 32 V4** has 2 MB of PSRAM and 16 MB of flash, enough for a real
**1 MB Macintosh SE**. It now has its own home:
**[Heltec-Mac-SE](https://github.com/hardwaremack-prog/Heltec-Mac-SE)**. Flash [`flasher/Heltec-Mac-V4-Flasher.html`](flasher/Heltec-Mac-V4-Flasher.html),
upload the **Mac SE FDHD** (B306E171) or **Mac SE** (B2E362A8) ROM, and it boots System 4 to 6
from **800K and 1.44 MB HFS** disks.

![A Mac SE with 1 MB running System 4.2 and Finder 6.0 on a Heltec V4](docs/heltec-mac-se.png)

*Heltec-Mac SE on a V4: System 4.2, Finder 6.0, 1,024K of memory.*

What's different on the V4:

- **A real Mac SE.** The emulator now has the SE's **ADB** keyboard and mouse (the Apple
  Desktop Bus, driven through the VIA like on the real board), the SE's ROM overlay, and real
  VIA timers, which System 4 and later need for the Time Manager.
- **1 MB of RAM** in the V4's PSRAM, leaving the ESP32's own memory to Wi‑Fi.
- **About 13 MB of disks:** nine 1.44 MB disks or sixteen 800K disks.
- **The Plus and 128K ROMs work too**, with up to 1 MB on the V4.
- Everything else is the same: the browser page with sound, the clock, print to PDF (choose the
  ImageWriter in the Chooser first, as on a real SE), battery, sleep, backup and clone.
- Its hotspot is **Heltec-MacSE-Setup** (password **heltecmac**) and it answers to
  **heltec-macse.local**, so a V3 and a V4 can share a network.

![Printed from TeachText on the Mac SE](docs/se-printout.png)

## Highlights

- **Plays in any modern browser (HTML5).** The screen streams over a WebSocket into a canvas,
  sending only the parts of the screen that changed. Mouse, keyboard, paste, full screen and
  an on-screen keyboard for phones all work. Pixels stay sharp at any display scaling.
- **Real Mac sound in the browser.** The board captures the Mac's own sound hardware (22 kHz,
  8-bit) and streams it to the page, which buffers it against Wi‑Fi hiccups. Beeps, alerts and
  game sounds come through. It's on by default and starts at your first click. When you add a
  ROM or switch disks, the board waits for your browser to reconnect before switching the Mac
  on, so you hear its startup sound.
- **Print to PDF.** Choose **File > Print** in MacWrite, MacPaint or any other app, and the
  page hands you a PDF. The board emulates the Mac's printer port, and your browser acts as an
  Apple ImageWriter, in Draft, Standard or High quality.
- **A clock that knows the time.** The Mac's clock chip is emulated and set from your
  computer's time when you open the page. With no browser open, the board keeps time from the
  internet in your time zone. Control Panel settings like volume and mouse speed are saved too.
- **A disk library on the board.** About 13 floppy disks fit in the board's 5.6 MB of disk
  storage. Switch disks with one click, download a disk with your changes, or remove one and
  get its space back.
- **Drop in almost any old Mac file.** Disk images (`.img`, `.dsk`, DiskCopy 4.2 `.image`) and
  archives (`.zip`, `.7z`, `.sit` including StuffIt 5, `.hqx`, `.bin`, `.gz`), even nested
  inside each other. Your browser unpacks them, so the board stays fast. Loose programs, like
  Missile Command or Bricks, are copied straight onto the disk you're using, and programs on
  800K disks that this Mac can't read are pulled off and copied over too. Add the same program
  again and it replaces the old copy.
- **Your choice of ROM.** Works with the original Mac 128K/512K ROM (recommended) or the
  Mac Plus ROM. ROMs aren't included; bring your own.
- **Board controls on the page.** Battery level, how long the board has been up and why it last
  started, plus **Restart Mac**, **Restart board** and **Sleep board** buttons. Sleep turns
  everything off until you press PRG.
- **Works anywhere.** On your home Wi‑Fi, or straight from the board's own hotspot when there's
  no network around.
- **One-page flasher.** Flash it from Chrome or Edge with a single HTML file. No tools to install.
  It also backs up a whole board to one file, and restores or clones it onto another board.
- **Extras.** VNC on port 5900, a status display on the board's OLED (or a tiny live view of the
  Mac screen), and a safe mode that keeps the page reachable if something goes wrong.

| The clock follows your computer | Games, with sound |
|---|---|
| ![Control Panel showing the current date and time](docs/clock.png) | ![Missile Command](docs/missile-command.png) |
| **Programs copied onto a System disk** | **Bricks, added from a StuffIt 5 archive** |
| ![The Finder showing Bricks and Amazing on the System Disk](docs/finder.png) | ![Bricks running](docs/bricks.png) |

![The disk library on the board's web page](docs/disk-library.png)

*The disk library: five disks stored on the board, one click to start from any of them, or
from no disk at all. Here Amazing.zip (an 800K disk) was just opened, and its program is
ready to copy onto the disk in use.*

## What you need

- A **Heltec WiFi LoRa 32 V3** (ESP32-S3, 8 MB flash) for Heltec-Mac, or a **Heltec WiFi LoRa 32
  V4** (2 MB PSRAM, 16 MB flash) for Heltec-Mac SE, and a USB-C cable that carries data
- **Chrome or Edge** on a computer, for flashing
- Your own **Mac ROM** and **disk images**. Apple's software isn't included.

> Flashing Heltec-Mac replaces Meshtastic on the board. To go back, reflash Meshtastic from
> flasher.meshtastic.org.

## Quick start

### 1. Flash the board

Download [`flasher/Heltec-Mac-Flasher.html`](flasher/Heltec-Mac-Flasher.html) (V3) or
[`flasher/Heltec-Mac-V4-Flasher.html`](flasher/Heltec-Mac-V4-Flasher.html) (V4) and open it in
Chrome or Edge. The firmware is built into the page. Plug in the board, click **Connect**, pick
it from the list, and click **Flash**. The page checks the write and restarts the board. If
anything goes wrong, the page walks you through the usual fixes (USB driver, cable, download
mode), and **Watch the board start** shows the board's own startup log.

![The Heltec-Mac Flasher, with the backup and clone section](docs/flasher.png)

Prefer the command line? `esptool --chip esp32s3 write-flash 0x0 firmware/Heltec-Mac.bin` (V3) or
`firmware/Heltec-Mac-V4.bin` (V4). The V4 uses the ESP32-S3's own USB, so it needs no driver.

Updating later keeps your ROM, disks and Wi‑Fi settings.

**Back up or clone.** After **Connect**, **Back up this board** saves the whole 8 MB flash
(firmware, ROM, every disk and the Wi‑Fi settings) as one `.bin` file, checked against the
board. When it's done, the flasher offers to disconnect and restart the board for you.
**Write backup to this board** puts a backup back, or onto a second Heltec V3 to make a clone.
The file holds your Wi‑Fi password and your ROM, so keep it private.

### 2. Put it on your Wi‑Fi

The first time, the board makes its own hotspot: **Heltec-Mac-Setup**, password **heltecmac**.
Join it, open **http://192.168.4.1**, pick your network under **Wi‑Fi**, and click
**Save & restart**. The OLED then shows the board's address on your network. It also answers
to **heltec-mac.local** on most computers.

No Wi‑Fi around? You can also use the Mac straight from the hotspot: join
**Heltec-Mac-Setup** and type **http://192.168.4.1** into your browser (the `.local` name
only works on your home network). Your phone or laptop may say the hotspot has no internet;
The board answers your device's "am I online?" check, so it stays on the hotspot instead of
switching to another network. When it starts the hotspot, the board picks the quietest
channel (1, 6 or 11) so it doesn't fight your router.

**At home, put the board on your home Wi‑Fi instead** (step 2 above). The hotspot is for
using the Mac where there's no Wi‑Fi.
If the board can't find your saved network when it starts (say you took it somewhere else), it
starts the hotspot instead. Press RST once you're back in range to join your network again.

### 3. Add a ROM

Under **Mac ROM**, upload your ROM file. The page checks it and tells you what it is.

| ROM | Size | Checksum | Notes |
|---|---|---|---|
| **Mac 128K / 512K** | 64 KB | `28BA61CE` | **Recommended.** Makes the board a real Mac 128K, with room for MacPaint and MacWrite. Reads 400K disks. |
| Mac Plus (version 3) | 128 KB | `4D1F8172` | Also reads 800K disks, but uses more of the Mac's small memory, so some 1984 apps won't fit. |

### 4. Add disks and start the Mac

Under **Disks**, choose a disk image or an archive and click **Add**. If a file holds several
disks, tick the ones you want and they're all added in one go. Click **Use** next to a disk to
start the Mac from it.

## Using it

Open `http://<board address>` (or `http://heltec-mac.local`) and click the Mac screen.

- **Mouse:** click and drag on the screen. Any mouse button is the Mac's one button.
- **Keyboard:** **Ctrl** or the **Windows key** is the Mac's **Command** key, and **Alt** is
  **Option**. Browsers keep a few shortcuts for themselves (Ctrl+W, T, N), so the **⌘ Command**
  button holds Command for your next key or click.
- **Paste** text from your computer straight into the Mac, or use **Type into the Mac** for
  longer text.
- **Phones and tablets:** tap **Keyboard** for the on-screen keyboard.
- **Sound** is on by default and starts at your first click. The **Sound** button turns it off,
  and your choice is remembered.
- **Pixels:** **Auto** keeps the screen razor sharp when there's room, **Fill** uses all the
  space. **Full screen** gives you just the Mac.
- **No disk** takes the disk out and restarts the Mac with an empty drive.
- **Restart Mac** works like the Mac's reset switch. **Restart board** restarts the whole
  Heltec, like pressing RST; on the hotspot it comes straight back as the hotspot, but your
  device may need a moment (or a tap in its Wi‑Fi list) to rejoin it. **Sleep board** puts it
  to sleep (see below). Each one saves the disk first.

One browser at a time can view the Mac. Opening it somewhere else takes over.

### Disks and programs

| You add | What happens |
|---|---|
| `.img`, `.dsk` (raw disk image) | Stored as is |
| `.image` (DiskCopy 4.2) | Header removed, stored as a disk |
| `.zip`, `.7z`, `.gz` | Unpacked in your browser; every disk inside is offered |
| `.sit` (StuffIt 1.5 to 5, including "Arsenic" compression) | Unpacked in your browser; checksums verified |
| `.hqx` (BinHex), `.bin` (MacBinary) | Decoded in your browser |
| A loose program inside any of these | **Add to the disk in use** copies it onto that disk |
| An 800K HFS disk with no System on it (a game or app disk) | Its programs are pulled out and offered for **Add to the disk in use** |

- **No disk** (at the end of the list) starts the Mac with an empty drive. It waits at a
  floppy icon, as a real Mac does with no disk in.
- **Use**, **No disk**, **Write-protect** and removing the disk in use restart only the Mac,
  not the board, so Wi‑Fi and the hotspot stay up and your browser stays connected. Adding
  disks and ROMs still restarts the board.
- **Download** saves a disk, with everything you've changed, back to your computer.
- **Remove** deletes a disk and gives its space back. The disks after it slide down in flash,
  which can take a few seconds.
- **Write-protect** stops the Mac from changing the disk in use.

**What runs well:** the original 400K disks from 1984 to 1986. That means System 1.x to 3.x,
MacPaint, MacWrite, the Guided Tour and early games. Each original app disk carries its own
System, so it starts straight into the app. Disks for System 4 and later (800K HFS) need a Mac
with 512 KB or more, which doesn't fit on this board. Many small 800K game disks are just one
program, though, and those usually run fine once copied onto a 400K System disk.

### Printing to PDF

![MacWrite's Sample Memo printed to a PDF](docs/print-pdf.png)

Print the way you would on a real Mac: **File > Print**, pick the quality, click **OK**. The
Mac thinks an Apple ImageWriter is plugged into its printer port. The board passes what the
Mac sends to your browser, and when the Mac finishes, a box under the screen offers
**Open PDF** and **Download**. The file is named with the date and time, like
`Mac printout 2026-11-04 01.28.04.pdf`.

- **High** and **Standard** quality print the page as a picture, just as the ImageWriter did
  (High is sharper and takes longer).
- **Draft** prints real text in Courier, so you can search and copy it in the PDF.
- Pages are US Letter. Each print job becomes one PDF, with every page in it.
- Keep the page open while printing. If no browser is connected, the Mac waits; open the page
  and printing carries on.
- The PDFs live in your browser until you close the page, so download the ones you want to keep.

### Status, battery and sleep

<img src="docs/status.png" alt="The Status panel" width="372" align="right">

The **Status** panel shows whether the Mac is running and at what speed, who's watching, the
battery, and how long the board has been up along with why it last started (power on, RST
button, restart, woke from sleep, crash, watchdog or brownout). If the board ever acts up,
that line is the first clue.

With a LiPo plugged into the board's battery connector, the OLED's title bar shows its voltage
and charge (for example `3.9V 72%`), and the web page shows it in the header and Status panel.
`USB` means it's on USB power with the battery full or charging; `NO BAT` means nothing is
connected. The reading is a rough guide: it comes from the board's voltage divider, like
Meshtastic's.

**Sleep board** saves the disk, switches off the screen, Wi-Fi and LED,
and puts the Heltec into deep sleep. Press **PRG** (or RST) on the board to wake it. The page
waits and reconnects by itself, and the Mac starts once you're watching, so you hear the chime.
<br clear="right">

### Clock

Each time the page opens, your browser sends your computer's local time and the Mac's clock is
set to match. The menu bar, Alarm Clock, Control Panel and file dates all use it. With no
browser open, the board sets the time from the internet using the time zone your browser last
reported, so the clock is right even after a power cut.

### Over VNC

Any VNC viewer (TigerVNC, RealVNC Viewer, TightVNC, Remmina) can connect to
`<board address>:5900` with no password. The keys are the same as in the browser. VNC has no
sound; use the browser for that.

### The board itself

- **OLED:** the board's address and the Mac's status. Tap **PRG** to switch to a tiny live view
  of the Mac screen.
- **Hold PRG for 2 seconds** to restart the Mac.
- **White LED:** a short, dim blink every few seconds while someone is viewing.
- **Safe mode:** if the board crashes three starts in a row, it comes up without the Mac so the
  page still works and shows what happened.

## How it works

The ESP32-S3 has two processor cores and 512 KB of RAM:

- **Core 1** runs the Mac: the Musashi 68000 emulator plus the Mac's hardware (video, sound,
  keyboard, mouse, clock chip, floppy drive), based on the umac emulator. It runs at the speed
  of a real 8 MHz Mac.
- **Printing:** the Mac's serial chip (SCC) sends whatever the Mac prints out of its printer
  port. The board passes those bytes to the browser, and `web/imagewriter.js` reads the
  ImageWriter commands, draws the pages at 144 dpi and writes the PDF.
- **Core 0** runs Wi‑Fi, the web page, the WebSocket stream and VNC. The screen is sent as
  16×16 blocks, and only the blocks that changed. Sound goes out in small bundles, and silence
  costs nothing.
- **Memory:** the Mac gets 128 KB of RAM, just like a 1984 Mac 128K. Wi‑Fi and the web page
  need most of the rest. The ROM stays in flash and is read directly from there.
- **Flash (8 MB):** the firmware, your ROM, and a 5.6 MB disk library. Disk writes are cached
  and saved to flash a second after the Mac goes quiet.
- **Your browser** does the heavy lifting for files: unpacking ZIP, 7-Zip, StuffIt and BinHex,
  and even writing programs onto Mac disks, before sending the finished disk to the board.

## Limitations

- On the V3 the Mac has 128 KB of RAM, because that's what the board can spare. System 1 to 3
  software runs; System 4 and later doesn't. For that, use a V4 (Heltec-Mac SE, 1 MB).
- On the V4, how fast the Mac runs depends on the PSRAM, which is slower than the ESP32's own
  memory; **Turbo** (under Machine) lets it run flat out. **Add to the disk in use** only
  copies programs onto 400K MFS disks for now, not HFS ones.
- ImageWriter 2.x (System 4 and later) waits a few seconds before printing, because it asks for
  an ImageWriter II's ID first and the virtual printer doesn't answer.
- One floppy drive. To switch disks, pick another one in the library, and the Mac restarts.
- No modem port, AppleTalk or SCSI. The printer port only talks to the built-in PDF printer.
- The emulation isn't cycle-exact, so a few timing-sensitive programs may run or sound slightly
  differently.
- Browser sound lags about 0.1 to 0.3 seconds behind the picture, to ride out Wi‑Fi hiccups.
- Wait a moment after the Mac writes to its disk before unplugging, or use **Shut Down** on the
  Mac.

## Troubleshooting

- **The board isn't in the flasher's list:** try another cable (many only charge) or another
  USB port. Windows needs the Silicon Labs **CP210x** driver.
- **Flashing stops at "Connecting":** hold **PRG**, tap **RST**, release **PRG**, and connect
  again. Choose **Slow** speed in the flasher.
- **Stuck on "JOINING WIFI..." or restarting:** flash the latest version. To see why, use
  **Watch the board start** in the flasher.
- **Your PC or phone keeps leaving the hotspot:** at home, put the board on your home Wi‑Fi.
  Otherwise, in your device's Wi‑Fi settings, forget or turn off auto-join for other nearby
  networks while you use the hotspot, and keep the board within a few metres.
- **The Mac freezes or the page drops off:** look at **Up** in the Status panel. "brownout"
  means the board's power dipped: try another USB port or cable, or charge the battery.
  "crash" or "watchdog" is a bug; the log from **Watch the board start** helps find it.
- **Remove** asks **Sure?** first: click it again within 4 seconds.
- **"There is not enough room in memory":** use the Mac 128K ROM instead of the Plus ROM.
- **No sound:** click the Mac screen once (browsers need a click before playing sound) and check
  that the **Sound** button says **on**.

## Building from source

Arduino IDE (or arduino-cli) with the **esp32** core 3.x. Open
[`Heltec-Mac/Heltec-Mac.ino`](Heltec-Mac/Heltec-Mac.ino). No extra libraries are needed.

- **V3:** board **Heltec WiFi LoRa 32(V3)**. The `partitions.csv` in the sketch folder sets up
  the app, the ROM area and the disk library.
- **V4:** board **Heltec WiFi LoRa 32(V4)** with **PSRAM: QSPI PSRAM**, **Flash Size: 16MB**
  and **USB CDC On Boot: Enabled**, and copy `partitions-v4.csv` over `partitions.csv`. The
  board setting turns on the V4 build (`heltec_config.h`).

`tools/build_firmware.sh` does both with arduino-cli and writes the images in `firmware/`.

| Folder | What's in it |
|---|---|
| `Heltec-Mac/` | The firmware: emulator glue, web page, WebSocket view, VNC, disk library |
| `Heltec-Mac/src/core/` | The emulator core (umac + Musashi) with Heltec-Mac's changes |
| `web/` | The browser-side unpackers (`macfiles.js`, `sit-arsenic.js`), the virtual ImageWriter (`imagewriter.js`) and a WebAssembly build |
| `flasher/` | The one-page flasher and the script that builds it from the firmware |
| `host/` | A PC build of the same emulator and web page, plus tests |
| `firmware/` | The ready-to-flash firmware images (V3 and V4) |

The PC build runs the same code on a desktop for testing:

```
cd host && ./build.sh
./machost --rom mac128k.rom --disk system.img   # web page at http://localhost:8080, VNC on 5900
V4=1 ./build.sh && ./machost-v4 --ram 1024 --rom macse.rom --disk system6.img   # the V4 / Mac SE build
./machost --testrom                             # built-in test pattern, no ROM needed
```

After changing `web/*.js`, run `python3 tools/embed_js.py` to rebuild the copy the board serves.
`PRINTFILE=print.bin ./machost ...` saves whatever the Mac prints to a file, for testing.
After changing the firmware, `flasher/make_flasher.py` rebuilds the flasher pages (`--v4` for the V4).

## Credits and licences

Heltec-Mac stands on the shoulders of some wonderful projects:

- **umac** by Matt Evans (MIT, `licenses/umac-LICENSE.txt`), the compact Mac emulator at the
  heart of this. Heltec-Mac adds runtime memory sizing, sound output, an absolute mouse, the
  clock chip with parameter RAM, a working printer port, Mac 128K ROM support, a disk library and
  more.
- The Mac SE's ADB keyboard and mouse (`src/core/adb.c`) follow **Mini vMac** by Paul C.
  Pratt (GNU GPL v2), whose ADB code and SE ROM notes made the SE possible.
- **Musashi** 68000 emulator by Karl Stenerud (MIT-style, `licenses/Musashi-LICENSE.txt`), with
  **SoftFloat** by John R. Hauser (`licenses/SoftFloat-README.txt`).
- The replacement floppy driver (`src/core/disc.c`, `sonydrv.h`) comes from **Basilisk II** by
  Christian Bauer, under the **GNU GPL v2**, so the firmware as a whole is distributed under the
  GPL v2.
- `web/sit-arsenic.js` is a JavaScript port of StuffIt "Arsenic" decompression from
  **XADMaster** (The Unarchiver) by MacPaw, under the **GNU LGPL 2.1**
  (`licenses/XADMaster-LGPL-2.1.txt`). StuffIt method 13 and the StuffIt 5 layout follow the
  format as described there; 7-Zip/LZMA follow the public-domain LZMA SDK documentation.
- **esptool-js** by Espressif (Apache 2.0, `licenses/esptool-js-LICENSE.txt`) powers the flasher.

Heltec-Mac's own code is yours to use. Apple ROMs and system software are **not** included and
aren't covered by any of this; use your own copies.
