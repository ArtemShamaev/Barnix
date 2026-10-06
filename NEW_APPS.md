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
**Usage:** `snake` (root-installed `/bin/snake`)

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
cp /theme.elf /bin/theme.elf
cp /snake.elf /bin/snake.elf
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
rebuilt for ABI 7 and installed by root in `/bin` for ordinary users to run them.
