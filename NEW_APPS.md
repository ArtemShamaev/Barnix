# New Barnix Applications

This document describes the new applications added to the Barnix API and available in the Complete installation profile.

## Overview

Five new applications have been developed for the Barnix operating system using its native API:

### 1. Theme Changer (`theme.elf`)
**Usage:** `theme [theme_name]`

Changes the terminal color theme. Available themes:
- `default` - Standard VGA colors
- `dark` - Dark background with light text
- `light` - Light background with dark text
- `solarized` - Subtle colors with good contrast
- `monokai` - Dark background with vibrant colors
- `dracula` - Popular dark theme with purple accent

**Example:**
```
theme dark
```

**Features:**
- Applies color scheme to console
- Applies immediately and saves preference to `/home/USER/useretc/theme.cfg`
- Integrated with system language settings

### 2. Snake Game (`snake.elf`)
**Usage:** `snake` (root-installed `/usr/bin/snake`)

A classic Snake game with keyboard controls.

**Controls:**
- Arrow keys to move
- P to pause/resume
- Q to quit

**Features:**
- Score tracking
- Border collision detection
- Food generation and growth
- Game over screen

### 3. ASCII Art Generator (`asciiart.elf`)
**Usage:** `asciiart TEXT [--banner]`

Creates ASCII art from text.

**Options:**
- Standard mode: Creates a simple box around the text
- `--banner` mode: Creates large decorative text

**Example:**
```
asciiart --banner Hello
```

### 4. Excuse Generator (`excuse.elf`)
**Usage:** `./excuse.elf`

Generates random excuses.

**Features:**
- Random excuse selection
- Language-aware (English/Russian based on `/etc/sys-lang.cfg`)
- Fun collection of classic excuses

**Example Output:**
```
Your excuse for today:
The dog ate my homework
```

### 5. Fortune Teller (`fortune.elf`)
**Usage:** `./fortune.elf`

Generates random fortune predictions.

**Features:**
- Random fortune selection
- Language-aware (English/Russian based on `/etc/sys-lang.cfg`)
- Motivational and fun fortunes

**Example Output:**
```
Your fortune for today:
The stars are aligned in your favor today
```

## Technical Details

### Integration with Barnix API

All applications use the Barnix App API v5:

- **Console Output:** `barnix->puts()`, `barnix->print()`, `barnix->println()`
- **Input:** `barnix->getch()`
- **File Operations:** `barnix->read()`, `barnix->write()`
- **Screen Control:** `barnix->clear()`, `barnix->goto_xy()`
- **Language Support:** автоматическое определение через `/etc/sys-lang.cfg`

### Installation

These applications are included in the **Complete** profile of the Barnix Setup:

1. Boot from `barnix-efi-setup.iso` (UEFI) or `barnix-legacy-setup.iso` (Legacy BIOS)
2. Choose the **Complete** profile during installation
3. Install to your disk
4. After installation, the applications will be available in `/`

### Running Applications

After installation, run the applications directly:

```
./theme.elf dark
./snake.elf
./asciiart.elf --banner "Hello World"
./excuse.elf
./fortune.elf
```

Or copy them to a convenient location:
```
cp /theme.elf /usr/bin/theme.elf
cp /snake.elf /usr/bin/snake.elf
```

Then run directly:
```
theme.elf dark
snake.elf
```

## Development Notes

### Language Adaptation

All applications respect the system language setting from `/etc/sys-lang.cfg`:

- English mode: `LANG=en`
- Russian mode: `LANG=ru`

Applications automatically detect the current language and display appropriate messages.

### API Compatibility

All applications are compiled with:
- GCC 32-bit freestanding mode
- Barnix ABI v5
- Static linking with `barnixiolib/stdio.c`
- No dynamic libraries or external dependencies

### File Locations

- Source files: `apps/theme.c`, `apps/snake.c`, `apps/asciiart.c`, `apps/excuse.c`, `apps/fortune.c`
- Compiled: `apps/theme.elf`, `apps/snake.elf`, `apps/asciiart.elf`, `apps/excuse.elf`, `apps/fortune.elf`
- Makefile integration: Added to `APPS` list in main `makefile`

## Setup Configuration

The applications are included in the **Complete** profile (`profiles.json`):

```json
{
  "id": "complete",
  "title": "Complete",
  "description": "Standard + example ELF applications",
  "extends": "standard",
  "commands": [],
  "examples": ["hello.elf", "fileio.elf", "theme.elf", "asciiart.elf", "excuse.elf"]
}
```

Note: Due to filesystem inode limits, `fortune.elf` is not included in the default profile but can be manually installed.

## Usage Examples

### Theme changer
```
# List available themes
theme

# Change to dark theme
theme dark

# Change to solarized theme (for better coding)
theme solarized
```

### Snake game
```
# Start game
./snake.elf

# Game controls:
# - Arrow keys: Move snake
# - P: Pause/resume
# - Q: Quit game
```

### ASCII art
```
# Create simple ASCII art box
asciiart.elf "Hello"

# Create large banner-style art
asciiart.elf --banner "Barnix"
```

### Excuse generator
```
# Get a random excuse
excuse.elf
```

### Fortune teller
```
# Get your daily fortune
fortune.elf
```

## Future Enhancements

Potential improvements for future versions:

1. **Theme system expansion:**
   - Add more color themes
   - Support for custom theme files
   - VGA palette modification

2. **Snake game improvements:**
   - High score persistence
   - Different difficulty levels
   - Multiplayer mode

3. **ASCII art:**
   - More fonts and styles
   - Support for UTF-8 characters
   - Color ASCII art

4. **Application framework:**
   - Better error handling
   - Configuration files
   - Help system

## License

These applications are part of the Barnix operating system and follow the same licensing terms.


## Pong, Paint and mouse input

`pong` plays against the computer; `pong 2` selects two players. W/S and arrow
keys control the paddles; P pauses and Q/Esc exits. Snake and Pong now advance
on a timer while polling keyboard input.

`paint [FILE.bmp]` is a mouse-operated 78×36 pixel-art editor with a 16-color
palette, brush, eraser, fill, undo and BMP save/load. The default output is
`/home/USER/picture.bmp`. Paint is included in the Complete profile. PS/2 or USB HID boot mouse (direct UHCI port)
input works in both the installer and editor on VGA and UEFI GOP.

Current account/configuration behavior is documented in
[PERMISSIONS_README.md](PERMISSIONS_README.md); legacy native binaries must be
rebuilt for ABI 7 and installed by root in `/usr/bin` for ordinary users to run them.

## Barnocraft

`barnocraft` is a voxel sandbox ported from the NASM x86-64 / X11 / OpenGL
prototype (`barnocraft.asm`). It is installed in `/usr/bin` on the LiveCD and in the
**Complete** Setup profile.

The 80×80×32 world is generated exactly as in the original: value-noise
terrain, two-noise tunnel caves, coal and iron veins, trees, sky-column
lighting, gravity and jumping with AABB collisions, block breaking/placing
within 5 blocks, and the 36-slot inventory with 64-item stacks.

Barnix programs are i386 code without FPU or OpenGL, so rendering is a
fixed-point voxel raycaster with mipmapped textures (distant faces do not
shimmer), face shading, sky-column light, fog and translucent glass. It runs
on the BDK Barnix Graphics library (`BDK/include/barnix_gfx.h`, ABI 10):

- the canvas has the display's size (800×600 on UEFI GOP and on Legacy BIOS
  through Bochs/QEMU/VirtualBox VBE; VGA 320×200×256 with ordered dithering
  elsewhere); the text console is restored on exit;
- `F` switches detail: high (one ray per pixel), medium (half), low (quarter);
  it is lowered automatically below 20 / 12 FPS;
- the frame is rendered in bands of 8 rows on every processor core
  (`bgx_parallel`, ABI 11); `F3` shows the FPS and the number of cores;
- sounds are recorded clips, as in the original: every block has a sound set
  (grass, gravel for dirt, stone, wood) used quietly for footsteps and loudly
  for placing; digging stone sounds like a pickaxe, wood like an axe, glass
  shatters; landing thuds. A random clip of the set plays each time. Short
  clicks on the PC speaker;
- the same game runs in a Happy Gouse window (`apps/barnocraft_game.c` is the
  shared core);
- key events carry releases, so held keys stop exactly when released, and
  mouse look uses unclamped relative motion.

**Controls:** WASD move, Space jump, mouse or arrows look, LMB/`X` break,
RMB/`C` place, `1`–`9` or wheel select, `E` inventory (LMB/Space
take/put/swap, RMB/`X` half/one, arrows move the selection), Esc pause, `Q` in
pause quits, `F3` debug line.

Textures come from `apps/barnocraft_textures.png` (64×64 atlas, see
`make_textures.py`). `scripts/barnocraft_assets.py` turns it into
`apps/barnocraft_assets.h` (texels with mipmaps, sine table) during `make`.

## Happy Gouse

`gouse` is a windowed environment in the spirit of Windows 1.0, with its own
logo (a happy goose, `apps/gouse/logo.png`). It is in every Setup profile and
on the LiveCD. It needs a graphics display (UEFI GOP, Bochs/QEMU/VirtualBox VBE
on Legacy BIOS) and about 16 MiB of RAM above the program arena.

- Desktop with icons, the **Gouse** menu (Ctrl+Esc), a taskbar with a clock.
- Overlapping windows: move, resize, minimize, maximize (double-click the
  title), close; Alt+Tab, Alt+F4, F10 menu bar, F11 full screen / maximize.
- **Cooperative multitasking**: every window gets a time slice each frame, so
  the clock ticks, Reversi thinks and Barnocraft keeps running in the
  background (at a third of the frame rate) while another window has focus.
- Built-in applications: Executive (files and programs), Notepad, Gouse Prompt
  (command window), Barnocraft (in a window; click to capture the mouse, Esc
  frees it, F11 full screen), Paint (BMP), Calculator, Clock, Calendar (notes in
  `~/calendar.txt`), Cardfile (`*.cards`), Reversi, Mines, Puzzle, Piano,
  Media Player, Control Panel (themes, desktop, mouse speed, clock, volume and
  system sounds; `~/.gouse.cfg`), Clipboard, About.
- **Piano**: two octaves under the mouse and the keyboard (`Z S X D C V G B H
  N J M` and `Q 2 W 3 E R 5 T 6 Y 7 U`), Piano/Organ/Chiptune/Flute/PC speaker,
  octave shift, "Ode to Joy", Record / Play take / Save WAV (22050 Hz mono, up
  to 14 s).
- **Media Player** plays WAV files (PCM 8/16 bit, mono/stereo, 4–96 kHz) with
  a position bar and loop; Executive opens `*.wav` files in it.
- Sounds: the fanfare at start, a goodbye tune at exit, the goose honk for
  errors (failed saves, unknown Prompt commands, Calculator errors), a ding
  for messages and a chime for questions; windows sound when they open,
  close, minimize and maximize; Mines (reveal, flag, explosion, victory),
  Reversi (discs) and Puzzle (tiles) have their own. Control Panel sets the
  volume; its "System sounds" switch leaves the game sounds on. Without a
  sound card the PC speaker plays short melodies.
- Barnix programs run full screen through Executive or `run <name>` in the
  prompt, as DOS programs did under Windows 1: Gouse saves its windows, the
  kernel runs the program, and Gouse comes back with the same windows.

Sources are in `apps/gouse/`; `scripts/gouse_assets.py` draws the logo and the
icons from vector shapes (`--png DIR` also writes `logo.png` and `icons.png`)
and stores their pixels deflated (unpacked with `bnet_inflate` at start-up).

### Happy Gouse 1.1: desktop shortcuts and Gouse Web

- **Shortcuts** are freedesktop.org Desktop Entry files in `~/Desktop`
  (`*.desktop`), shown on the desktop after the applications with an arrow:

      [Desktop Entry]
      Type=Application        (or Link, Directory)
      Name=Snake
      Name[ru]=Змейка
      Exec=/usr/bin/snake        (a Barnix program, or gouse:Notepad /home/root/a.txt)
      URL=https://example.com (Type=Link: opens in Gouse Web)
      Path=/usr/bin           (Type=Directory: opens in Executive)
      Icon=program            (an icon name: browser, notepad, executive, paint...)

  Double-click opens one. Right-click on the desktop: New shortcut (a program,
  folder, file or address; the type is worked out), New web link, Refresh;
  right-click on a shortcut: Open, Rename, Delete shortcut, Properties.
  Executive opens `.desktop` files and has File → Shortcut on desktop for the
  selected item; Gouse Web has View → Add to desktop. The first start makes
  `~/Desktop` with four examples.
- **Gouse Web** (`app_browser.c`, engine in `web_*.c`, `web.h`): a browser
  for HTML5 and CSS3 with QuickJS JavaScript support.
  - JavaScript: modern syntax, modules, promises, `queueMicrotask`, timers,
    animation frames, a small DOM/event layer and Canvas 2D. Pending promise
    jobs continue on later browser ticks; animation callbacks can be canceled
    by timers or earlier callbacks in the same frame. `make test-web-js`
    checks these paths, interruption, navigation and the Three.js canvas demo.
    DOM and Canvas support is partial; WebGL contexts return `null`.
  - HTML: the HTML5 tokenizer and tree building where pages depend on it
    (character references, implied and omitted tags, raw text), UTF-8 plus
    windows-1251, KOI8-R and Latin-1 from `<meta charset>`, quirks mode.
  - CSS: style sheets (`<style>`, `<link>`, `@import`, `style=`), the user
    agent sheet, presentational attributes (`bgcolor`, `width`, `<font>`,
    `<center>`...), selectors (combinators, attributes, `:nth-child()`,
    `:not()/:is()/:where()`, `::before/::after` with `content`),
    specificity, `!important`, inheritance, custom properties and `var()`,
    `calc()`, `min()`, `max()`, `clamp()`, `px em rem % vw vh pt...`, colors
    (names, hex, `rgb()`, `hsl()`), `@media` width and type queries,
    `@supports`, `@layer`. `:hover` and other states never match.
  - Layout: block and inline flow with line breaking, `white-space`,
    `text-align` (also justify), floats and `clear`, inline-block, lists,
    tables (colspan, captions), Flexbox (wrap, grow/shrink, justify, align,
    order, gap), Grid (`grid-template-columns` with `fr`, `repeat()`,
    `auto-fill`, `minmax()`, spans), `position` relative/absolute/fixed,
    `overflow` clipping, `opacity`, `visibility`, `border-radius`, linear
    gradients, borders of every style.
  - Pictures: PNG (all types, interlaced, `data:` URLs); other formats show
    their `alt` text.
  - Forms: text, password, checkbox, radio, select, textarea and buttons;
    GET and POST submission.
  - Fonts: Liberation Sans, Sans Bold, Serif and Mono drawn once by
    `scripts/web_font.py` into `/usr/share/gouse/web.bwf` (124 KiB) and
    scaled to every size; italic and the missing bolds are synthesized.
    Without the file the system font stands in.
  - The browser: address bar (addresses, `file:` paths and folders, or words
    to search with DuckDuckGo Lite), back/forward, reload, home page
    (`about:home`), find on page (Ctrl+F, F3), zoom (Ctrl +/−), page source,
    save page, downloads of other files, links in the status bar.
  - Pages load over HTTP and HTTPS (BDK Barnix Net) and block the desktop
    while they do; the status bar shows the progress (`g_flush`).
  - The engine has no Gouse dependencies: `make test-web` runs it on the host
    (`tests/web_render.c`) with AddressSanitizer against the test pages in
    `tests/web/`, URL cases and mangled pages; `make test-gouse-web` drives
    the browser in QEMU.
  - Not supported yet: JPEG/GIF/SVG/WebP, web fonts, `:hover`,
    transforms and animations, cookies.
- The engine and the browser are built with `-Os`, so Happy Gouse (about
  585 KiB) stays below the 640 KiB file limit; the make rule checks it.

### Kernel and file system changes for it

- ABI 10 `memory()` hands a native program the free RAM above the GRUB modules
  (all of it, zeroed on request) for screen-sized buffers.
- Files may be 640 KiB (ext2 doubly indirect blocks); Gouse itself is ~585 KiB with its sounds and the browser.
- `make run` and `Setup/run.py` take their QEMU options from
  `scripts/qemu_host.py`: KVM when `/dev/kvm` is usable (TCG otherwise), up to
  8 cores, an SDL window that locks the mouse after a click (also on Wayland,
  where QEMU's GTK window cannot hold the pointer; Ctrl+Alt+G releases it), and
  Intel HD Audio plus the PC speaker through PipeWire/PulseAudio/SDL.
  `QEMU_HOST=` turns all of it off; `BARNIX_NO_KVM=1`, `BARNIX_NO_AUDIO=1` and
  `QEMU_DEFAULT_DISPLAY=1` turn off one part.

### Processor cores and sound (ABI 11)

- **SMP**: at boot the kernel wakes the other cores (INIT/SIPI broadcast,
  xAPIC or x2APIC). They take no part in the kernel; native programs spread
  pure computation over them with `parallel(job, argument, count)` and read the
  count with `cpus()`. Idle cores sleep in `hlt` and are woken by an NMI.
  `devices` prints the number of cores.
- **Sound**: Intel High Definition Audio (codec graph walk: line out,
  speaker and headphone pins, amplifiers, EAPD) and AC'97, 48 kHz stereo,
  with a 16-voice kernel mixer and per-clip volume, pan, loop and resampling;
  plus the PC speaker (`speaker(hz)`). Barnix polls instead of using
  interrupts, so the DMA buffer is refilled on every frame, input poll and
  sleep, 100 ms ahead. `devices` names the device.
- BDK `barnix_sound.h` (BSND): play/stop clips, speaker tones and beeps, a
  synthesizer (square, triangle, saw, sine, noise, organ, plucked string,
  pitch slides, attack/release), note frequencies and a WAV reader.
- Commands: `play [file.wav]` (a WAV file, or a fanfare without a file) and
  `beep [hz] [ms]`.
- The shell plays a chime after login and an error tone for unknown
  commands, syntax errors and failed logins (a beep on the PC speaker);
  `sounds off` / `sounds on` stores the choice in `~/useretc/sounds.cfg`.
  Snake (eating, crash) and Pong (bounces, points) have sounds too.
- Sound effects are CC0 clips from Kenney's packs (www.kenney.nl), listed in
  `scripts/import_sounds.py` and kept in `sounds/` (`sounds/LICENSE.txt`);
  music (fanfares, the start chime, the honk) is synthesized.
  `scripts/sound_assets.py` stores them in each program as 8-bit mu-law,
  which `bsnd_ulaw` decodes.
- Real chipsets may fetch DMA buffers without snooping the CPU caches (QEMU
  never does): the sound driver enables snooping where the chipset makes it
  optional (Intel DEVC, ATI/AMD, NVIDIA, TCSEL as Linux does) and writes back
  the cache lines of the command ring, buffer list and sample ring with
  `clflush`. `devices` names the codec and the number of outputs it routed.
- USB on real PCs: Intel 7/8/9-series chipsets start with every port wired
  to EHCI; once xHCI runs, Barnix switches them to it (as Linux does). EHCI,
  OHCI and UHCI now understand a hub on a root port (one level), which Intel
  EHCI always has (the rate-matching hub): devices behind it are reset and
  enumerated through the hub, full/low-speed ones on EHCI with split
  transactions, and attach/detach is checked twice a second. `devices` lists
  every controller and connected device with its VID:PID and what Barnix
  made of it (mouse, storage, hub, or the unsupported interface classes).
- ATA probing leaves CD/DVD (ATAPI) drives alone.
- 3D graphics cards are not used: their drivers (Mesa and the kernel DRM
  drivers) are millions of lines tied to Linux; using every CPU core is what
  speeds up the raycaster on real hardware.

## Network: TCP/IP, HTTPS and git (ABI 12)

- **Adapters** (polled, no interrupts): Intel PRO/1000 (8254x), 82574
  (e1000e) and the I217-I219 PCH family, Realtek RTL8139 and
  RTL8111/8168/8169/8101, AMD PCnet. Tested in QEMU with e1000, e1000e,
  rtl8139 and pcnet; the Realtek 8111/8168 and Intel PCH drivers follow the
  datasheets but QEMU cannot emulate those cards.
- **Kernel stack**: Ethernet, ARP (with replies), IPv4, ICMP echo (both
  ways), UDP, DHCP (address, mask, router, DNS), DNS with a small cache and
  1.1.1.1/8.8.8.8 as fallbacks, and TCP: connect, 64 KiB receive window,
  retransmission with exponential backoff, FIN and RST. The network comes up
  by itself when a program connects; `bnm internet` does it by hand.
- **ABI 12**: `net_open`, `net_send`, `net_recv`, `net_close`,
  `net_resolve`, `net_info`, `net_ping_host`, `random`. Connections a program
  leaves open are dropped when it exits.
- **BDK Barnix Net** (`barnix_net.h`): HTTP/1.1 (redirects, chunked bodies)
  over TLS 1.3 or TLS 1.2 (X25519/P-256 ECDHE, AES-128-GCM,
  ChaCha20-Poly1305). Certificates are checked against the Mozilla roots in
  `/etc/ssl/roots` (`scripts/make_roots.py` → `certs/roots.bin`): chain,
  signatures (RSA PKCS#1/PSS, ECDSA P-256/P-384), dates and host name.
  Revocation is not checked. Everything is written from scratch (SHA-1/2,
  HMAC/HKDF, AES-GCM, ChaCha20-Poly1305, X25519, RSA and ECDSA, inflate)
  and was checked against Python's `cryptography` and zlib.
- **Commands**:
  - `bnm` shows the adapter, the link and the address; `bnm internet`
    (cable, DHCP), `bnm connect WIFI [PASSWORD]` (asks for the password
    when it is left out), `bnm get-networks`, `bnm off` (disconnects the
    cable and Wi-Fi until the next `bnm internet`/`bnm connect`; programs
    then get `BARNIX_NET_OFF`), `bnm test-ping [HOST]`, `bnm resolve HOST`.
- **Wi-Fi** (`wifi.c`): scanning, open and WPA2-Personal networks (4-way
  and group key handshakes, CCMP), Ethernet framing for the TCP/IP stack.
  WPA3, WEP, TKIP and Enterprise networks are listed but refused. Real
  cards (Intel, Realtek, Atheros, Broadcom, MediaTek) are recognized but
  have no driver yet: they need vendor firmware. QEMU emulates no Wi-Fi
  hardware, so every `make run*` target and `Setup/run.py` add the Barnix
  virtual Wi-Fi card (an RTL8139 with MAC 02:57:49:46:49:xx carrying 802.11
  frames, `net_vwifi.c`) and start `scripts/virtual_wifi.py`, a WPA2 access
  point named `Quemu-Password-root` with the password `root`, bridged to its
  own QEMU user network (192.168.77.0/24) and the Internet. `QEMU_WIFI=`
  (or `BARNIX_NO_WIFI=1` for Setup/run.py) runs without it; the wired card
  must then not be `NET_MODEL=rtl8139`, as each driver runs one card. Tests:
  `make test-wifi` (host, against the access point) and `make test-wifi-qemu`.
- ABI: `net_connect_wifi(name, password)` joins a network and runs DHCP;
  `net_connect_wifi(0, 0)` is `bnm off`. `net_scan` writes one line per
  network (`*` marks the current one): name, tab, security, tab, signal in
  dBm. `net_info` reports state 1 on Wi-Fi.
  - `get URL [FILE]` downloads over HTTP or HTTPS (up to the 640 KiB file
    limit).
  - `git clone URL [DIR]` fetches the newest commit over the smart HTTP
    protocol (like `--depth 1`): refs, `git-upload-pack`, the pack with its
    deltas, SHA-1 checked objects, then the files. No history is stored.
    The live CD keeps files in a small RAM disk, so larger repositories need
    an installed system.

## brm: Barnix Repository Manager

`brm` installs programs from a repository with pacman's options:
`-S NAME...` install, `-Sy` refresh the list, `-Syu` upgrade, `-Ss [TEXT]`
search, `-Si`/`-Sl` information and list, `-Sc` forget the list,
`-R NAME...` remove, `-Q`, `-Qi`, `-Qs`, `-Qu`, `-Ql` query installed packages,
`-U FILE.elf` install a program file, `set-repo URL` / `repo` choose and show
the repository; `--noconfirm` and `--needed` as in pacman. Changes need root
(`sudo brm ...`); queries work for everyone.

- A repository is any HTTP(S) folder with `index2` (brm 2:
  `name|version|file|size|sha256|description|description.ru|category|flags|installed KiB|files`)
  and/or `index` (brm 1: the first seven fields) and the files they name.
  The default is `https://raw.githubusercontent.com/ArtemShamaev/BarnixRepo/main`,
  built by the separate BarnixRepo project (`./build.sh`, `./publish.sh`).
- A package is one program (`.elf`, installed as `/usr/bin/NAME`) or a bundle
  (`.bpz`: `BPZ1`, then blocks of `[raw size][packed size][raw deflate]` of at
  most 1 MiB that unpack to a BPK1 stream, Setup's format). Bundles unpack while
  they download, so GCC (59 MB, 180 MB on disk) needs little memory. Size and
  SHA-256 are checked (bundles at the end: a damaged one is taken back), and so
  is a program's ABI against the running kernel.
- Barnix itself is packages too (`scripts/system_packages.py`): every command,
  `gouse` (with its data and the manuals), `examples` and `gcc`. Their flag
  `base` lets them replace the copy the system came with, `essential` ones
  cannot be removed. Versions are `0.3.YYYYMMDD.HHMM` of the newest file, so
  `brm -Syu` updates the system. Images and Setup components list what they
  bring in `/etc/brm/base.d/PART` (and a bundle's files in `/etc/brm/files/NAME`).
- State in `/etc/brm` (readable by every user): `repo`, `sync` (the index),
  `local` (installed packages), `base.d/`, `files/`. Messages follow the
  Barnix language.

## BarnStore

The package manager with windows (Happy Gouse, on the desktop): categories,
search, details, Install / Update / Remove / Run, Refresh and Update all. It
reads brm's database and runs `brm --noconfirm` in its own window, where the
download progress shows. A user who is not root types root's password once
(remembered for 5 minutes). The IDE opens it with `gcc` selected when GCC is
missing.

## ABI 14: clipboard, programs in windows, sudo

- `clipboard_set`, `clipboard_get`, `clipboard_serial`: one clipboard for the
  shell (Ctrl+V pastes, Ctrl+C / Ctrl+X copy or cut the typed line), macro
  (Ctrl+C / Ctrl+X the line, Ctrl+V), Happy Gouse (every edit field, editor,
  the Prompt, Gouse Web's Edit menu) and BDK programs. Ctrl+letter works in
  the Russian layout too, and right Ctrl as well.
- `run_area(x, y, w, h)` before `run()`: the program's text console (80x25
  cells) or picture is drawn into that rectangle of the caller's last frame,
  so Gouse Prompt, the IDE and BarnStore run everything inside their windows,
  games and editors included; a program that keeps printing is shown after
  0.3 s. Linux programs read whole lines with echo (scanf works).
- `sudo(password)`: the next `run()` starts its program as root (the Prompt's
  `sudo`, BarnStore).
- `file_mode(path, mode)`: permission bits for root (brm's bundles).
- `disk_space(path, &free_kib, &free_files)`: brm checks room and inodes
  before it downloads (GCC does not fit the Live CD's 16 MiB RAM disk).
- While a program holds `memory()`, the kernel keeps its own blocks below it;
  when GCC is installed and RAM allows (about 1.5 GiB), the compiler's arenas
  are reserved first, so GCC runs from the IDE and the Prompt.
- GCC links statically by default (no `-static` needed), `/usr/include`,
  `/usr/lib`, `/usr/libexec`, `/usr/share` are readable by every user and
  `/tmp` is everyone's.

## Developer IDE and the manuals

The IDE colors C, C++ and Nova, numbers lines, keeps the indentation, undoes
(Ctrl+Z), finds (Ctrl+F, F3) and goes to a line (Ctrl+G). F5 builds with
`-Wall -O2`; the messages fill a panel where a click (or F4) jumps to the
line. F6 runs the program inside the IDE window, F1 opens the manual of the
language. The manuals (`docs/manual`, installed to `/usr/share/doc/barnix`)
cover C, C++, Nova, Java, HTML/CSS/JavaScript, the Barnix API, the shell and
brm; Gouse Web links them from its home page and opens `about:docs/c`.

## Happy Gouse in Russian

Gouse speaks Russian («Весёлый Гусь») unless English is chosen in the Control
Panel (saved as `lang=` in `~/.gouse.cfg`). Text stays English in the code and
is translated where it is drawn: `apps/gouse/gouse_ru.h` maps whole strings,
`g_text`/`bgx_text` look them up, `g_format` translates formats, dialogs their
title and text; month names have genitive forms (`g_month_of`). User data —
file names, typed text, Notepad, the Prompt's lines, Paint's text tool — is
drawn with the `_raw` functions and never translated.

## BDK changes

- Programs are linked with `--gc-sections`: unused library code (for example
  the TLS stack) is dropped, so a game is about 30 KiB instead of 110 KiB.
- `_start` accepts any kernel with the same or a newer ABI (the call table
  only grows), so repository programs keep working after kernel updates.

## Keyboard mouse and sturdier USB mice

- **Ctrl+Alt+M** turns the keyboard into a mouse, for machines whose mouse or
  touchpad Barnix cannot drive yet (two rising notes: on, falling: off). The
  arrows move the pointer (a tap nudges it two pixels, holding speeds up from
  90 to 600 logical pixels a second), Space or Enter is the left button,
  Shift+Space or Shift+Enter the right one, PageUp/PageDown the wheel. Those
  keys stop reaching programs while it is on. It works everywhere: the
  shell, Happy Gouse, games and BDK programs (pointer source 95).
- USB mice survive transfer errors: EHCI/OHCI/UHCI and xHCI retry, clear a
  halted endpoint (CLEAR_FEATURE, xHCI Reset Endpoint + Set TR Dequeue) and
  give up only after 64 errors in a row. EHCI and OHCI keep their periodic
  schedules running (two alternating qTDs; OHCI's dummy tail TD) instead of
  stopping them for every report, and any number of mice may share a root
  port behind a hub.
- `devices` shows for every mouse its protocol, the reports received and the
  errors with the last status, so a photo tells why a mouse does not move.

## Alpha Launcher

A Happy Gouse application for Minecraft Alpha a1.2.6 with a jar mod on BJVM.
The game folder (default `/mc`) holds `mc.jar`, an optional `mod.jar` and the
game's data in `minecraft/` (the game uses `user.home`, which the launcher sets
to the folder). Nickname, folder, Java heap (Auto/256/384/512 MiB) and the mod
switch are kept in `~/.alpha-launcher.cfg`. Play runs
`java -Duser.home=DIR -cp DIR/mod.jar:DIR/mc.jar net.minecraft.client.Minecraft NAME`
on the whole screen with all memory: Gouse closes meanwhile and comes back.
Images get `/mc` from the local FreesmLauncher instance (`scripts/install_minecraft.py`;
`BARNIX_MC_JAR`, `BARNIX_MOD_JAR`, `BARNIX_NO_MINECRAFT=1`), Setup copies it from
the LiveCD when the partition has room, and `/mc` is open to every user (worlds
are saved there). Such an image contains Mojang's game: keep it for your own
machines, do not publish it.
Since ABI 14 `launch()` takes arguments after the program (`"quoted"` ones keep
spaces).

## UEFI laptops (HONOR MagicBook, Alder Lake)

- `barnix.iso` (and `barnix-gcc.iso`) is hybrid: Legacy BIOS and UEFI, CD or
  USB stick. On UEFI it boots `kernel-efi.elf` with the GOP framebuffer.
- Intel laptops put the graphics aperture, and with it the GOP framebuffer,
  in a 64-bit BAR above 4 GiB (0x4000000000), out of a 32-bit kernel's reach.
  The EFI kernel moves that BAR to a free window below 4 GiB before it sets up
  the screen (`fbmove` on the kernel command line forces it, for QEMU tests).
- Intel's audio DSP (SOF, class 0401) is driven as HD Audio, its BAR moved
  below 4 GiB the same way.
- Not supported there yet: the I2C touchpad (use a USB mouse or Ctrl+Alt+M),
  Intel Wi-Fi (AX201) and so the network.

## Python 3.9

`sh tools/python/build.sh` builds CPython 3.9.25 (checksum checked) as a static
i386 Linux program linked at 0x80000000 (the large window, like GCC), with the
modules of `tools/python/Setup.local` built in and a trimmed, precompiled
standard library in `tools/python/root`. It becomes the brm package `python`
(`scripts/system_packages.py`), `scripts/install_python.py` puts it into an
image. The IDE runs `.py` files (F6, in its window; F5 checks the syntax).
For it the Linux layer got getdents64, getrandom, mkdir/rmdir, readlink,
rlimits, gettid, O_NOFOLLOW and a console that answers TCGETS/TCGETS2 and
TIOCGWINSZ (isatty, the REPL); fork snapshots of the large window are taken
at the first fork, so a lone program needs only its 256 MiB arena. Missing
system calls and terminal ioctls are written to QEMU's debug console.
Not yet: sockets, threads, zlib, sqlite3, tkinter.

## Barnix Code

Developer IDE became Barnix Code (`apps/gouse/app_code.c`), an editor in the
manner of VS Code: activity bar (Explorer, Search, Run), a folder tree, tabs,
Dark+ colors, a minimap, Quick Open (Ctrl+P), the Command Palette
(Ctrl+Shift+P), find/replace (Ctrl+F/H), search in files (Ctrl+Shift+F), and a
bottom panel with Problems (GCC and Python errors, F8), Output and a Terminal
in which programs run (run_area of the panel). F5 runs by language; brackets
and quotes close themselves, Ctrl+/ comments, Alt+arrows move lines, Ctrl+D
selects the next occurrence; undo and redo. Highlighting: C, C++, Python,
Nova, JavaScript, HTML, CSS, JSON, Markdown, Shell. The folder and open files
are kept in `~/.barnix-code.cfg`; unsaved files wait in memory (hot exit).
GTextEdit got its own palettes and bracket marks for it.

IntelliSense: a completion list under the cursor (Ctrl+Space, or by typing)
from `apps/gouse/code_symbols.inc` (Python with module members, C/C++ with
`std::`, JavaScript, HTML, CSS), the language's keywords and the file's words;
prefix and fuzzy matching, snippets with indentation, parameter hints over
calls. The Python debugger: F9 or a gutter click toggles a breakpoint; F5 runs
`/usr/share/gouse/debug.py` (a bdb front end on the ANSI console: source,
variables, watches, call stack, output; n s o c b p w q) over the editor, and
the breakpoints come back through `/tmp/.barnix-code-debug`. Ctrl+F5 runs
without it. The kernel console understands the ANSI/VT100 escapes it needs.

## Memory for Python, GCC and Java

Under UEFI with little RAM (OVMF at 512 MiB) PCI memory starts at 0x80000000,
the virtual window of large Linux programs, so the framebuffer replaced
Python's pages (exit 139). Boot now moves root-bus BARs out of that window
(`storage_pci_clear_large_window`) and refuses with a message if one stays.
The 256 MiB arena, fork snapshots and the exec buffer are given back when a
shell program exits, and the reservation Happy Gouse keeps for Python/GCC is
released when a full-screen program (BJVM with Minecraft) runs instead; the
program and exec buffers fit the program (16 MiB first, not 64). Before, one
`python3` in bssh left Minecraft with "Java heap space" at 512 MiB.
The HD Audio registers are now mapped for Linux programs too (key clicks
play inside their system calls), and an MMIO page table keeps the rest of
its 4 MiB mapped: after the BARs moved, the network card's table had hidden
the sound card at 0x41004000 and Python's REPL ended in "Fault in Linux ABI
kernel handler" (QEMU with -smp and intel-hda, as Setup/run.py starts it).
That panic now names the vector, EIP, error code and CR2. Gouse forgets held
keys after a nested program, so F5 after Ctrl+F5 debugs again.
