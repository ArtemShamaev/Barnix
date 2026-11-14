#!/usr/bin/env python3
"""Rasterize the fonts of the Gouse Web browser into one file.

    python3 scripts/web_font.py OUTPUT.bwf

Liberation Sans (regular, bold), Serif and Mono (both regular) are drawn
once at BASE pixels with antialiasing (Pillow); the browser scales the
glyphs to every CSS font size and makes bold and italic of the others
itself. The file is zlib data (inflated by bnet_inflate) of:

    "BWF1" u16 faces
    per face: char name[12], u16 base, s16 ascent, s16 descent, u16 glyphs,
              u32 bitmap bytes, then glyphs x {u32 codepoint, u16 advance
              (1/16 px at base), s8 left, s8 top (from the baseline, up),
              u8 width, u8 height, u16 pad, u32 offset}, then the bitmaps:
              4-bit alpha, two pixels a byte (high nibble first), rows
              padded to whole bytes.
Little-endian throughout.
"""
import os
import pathlib
import struct
import sys
import zlib

from PIL import Image, ImageDraw, ImageFont

BASE = 28
# Liberation fonts: Arch/Fedora path first, then Debian/Ubuntu; LIBERATION_FONTS overrides.
FONTS = next((d for d in (os.environ.get('LIBERATION_FONTS', ''), '/usr/share/fonts/liberation/',
                          '/usr/share/fonts/truetype/liberation/')
              if d and os.path.isfile(os.path.join(d, 'LiberationSans-Regular.ttf'))),
             '/usr/share/fonts/liberation/')
if not FONTS.endswith('/'):
    FONTS += '/'
BASIC = [(0x20, 0x7e), (0xa0, 0x17f), (0x400, 0x45f), (0x490, 0x491), (0x2010, 0x2027), (0x2030, 0x205e), (0x20a0, 0x20cf),
         (0x2116, 0x2116), (0x2122, 0x2122), (0xfffd, 0xfffd)]
# Plain sans also covers IPA, Greek, Latin Extended-B and common symbols;
# the other faces fall back to it for those (web_font.c).
EXTENDED = BASIC + [(0x180, 0x24f), (0x250, 0x2ff), (0x370, 0x3ff), (0x2100, 0x218f), (0x2190, 0x21ff), (0x2200, 0x22ff),
                    (0x2300, 0x23ff), (0x2500, 0x259f), (0x25a0, 0x25ff), (0x2600, 0x26ff), (0x2700, 0x27bf)]
FACES = [('sans', 'LiberationSans-Regular.ttf', EXTENDED), ('sans-bold', 'LiberationSans-Bold.ttf', BASIC),
         ('serif', 'LiberationSerif-Regular.ttf', BASIC), ('mono', 'LiberationMono-Regular.ttf', BASIC)]
RANGES = BASIC


def face(name, filename, ranges=None):
    font = ImageFont.truetype(FONTS + filename, BASE, layout_engine=ImageFont.Layout.BASIC)
    ascent, descent = font.getmetrics()
    glyphs, bitmaps = [], bytearray()
    cmap = set()
    try:
        from fontTools.ttLib import TTFont   # optional: skip characters the font lacks
        cmap = set(TTFont(FONTS + filename).getBestCmap())
    except ImportError:
        cmap = None
    def picture(ch):
        image = Image.new('L', (BASE * 2, BASE * 2), 0)
        ImageDraw.Draw(image).text((BASE // 2, BASE * 3 // 2), ch, font=font, fill=255, anchor='ls')
        return image.tobytes()
    missing = picture('\U0010fffd')   # what Pillow draws for characters the font lacks
    codes = sorted({code for first, last in (ranges or RANGES) for code in range(first, last + 1)})
    for code in codes:
        if True:
            if cmap is not None and code not in cmap and code != 0x20:
                continue
            try:
                if cmap is None and code != 0x20 and picture(chr(code)) == missing:
                    continue
            except (RuntimeError, OSError):
                continue
            ch = chr(code)
            advance = font.getlength(ch)
            box = font.getbbox(ch, anchor='ls')   # relative to the baseline origin
            left, top, right, bottom = box
            width, height = max(0, right - left), max(0, bottom - top)
            offset = len(bitmaps)
            if width and height:
                image = Image.new('L', (width, height), 0)
                ImageDraw.Draw(image).text((-left, -top), ch, font=font, fill=255, anchor='ls')
                pixels = image.tobytes()
                for y in range(height):
                    row = pixels[y * width:(y + 1) * width]
                    for x in range(0, width, 2):
                        a = (row[x] + 8) // 17
                        b = (row[x + 1] + 8) // 17 if x + 1 < width else 0
                        bitmaps.append(a << 4 | b)
            if width > 255 or height > 255 or not -128 <= left < 128 or not -128 <= -top < 128:
                raise ValueError(f'glyph {code:x} too large')
            glyphs.append(struct.pack('<IHbbBBxxI', code, round(advance * 16), left, -top, width, height, offset))
    header = struct.pack('<12sHhhHI', name.encode(), BASE, ascent, descent, len(glyphs), len(bitmaps))
    return header + b''.join(glyphs) + bytes(bitmaps)


def main():
    data = b'BWF1' + struct.pack('<H', len(FACES)) + b''.join(face(n, f, r) for n, f, r in FACES)
    packed = zlib.compress(data, 9)
    pathlib.Path(sys.argv[1]).write_bytes(packed)
    print(f'{sys.argv[1]}: {len(FACES)} faces, {len(data)} bytes, {len(packed)} compressed')


if __name__ == '__main__':
    main()
