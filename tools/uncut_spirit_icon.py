"""Put the Spirit Glyph's icon in the glyph menu's icon sheet, for -uncut.

The July disc's glyph icon sheet (\\kain2\\object\\glphicon\\glphicon.crm)
holds eight 64x31 cells, each with its own palette; the eighth, a swirl, is
never drawn. The Spirit Glyph, slot 8 of the -uncut glyph menu, is drawn
from it. The April 14, 1999 prototype's sheet still has the Spirit Glyph's
own icon (its fifth cell); this copies that cell and its palette over the
swirl and writes the sheet to the mods folder, where it replaces the disc's
copy (port/src/mod_files.c). Nothing else in the sheet changes.

usage: uncut_spirit_icon.py <July disc image> <April disc image> <mods folder>
"""
import os
import struct
import sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'editor'))
from leveldump.disc import BigFile  # noqa: E402

SHEET = '\\kain2\\object\\glphicon\\glphicon.crm'
HEADER = 36           # file header plus the image block's header
ROW = 32              # 16 halfwords: 64 4-bit pixels, or one 16-colour palette
CELL_ROWS = 31
JULY_SWIRL = 7
APRIL_SPIRIT = 4


def read_sheet(image):
    data = BigFile(image, mods='').read_file(SHEET)
    w, h = struct.unpack_from('<HxxH', data, HEADER - 8)
    if (w, h) != (16, 256) or len(data) != HEADER + w * h * 2:
        sys.exit('%s: unexpected glyph icon sheet (%dx%d, %d bytes)' % (image, w, h, len(data)))
    return bytearray(data)


def cell(n):
    start = HEADER + n * CELL_ROWS * ROW
    return slice(start, start + CELL_ROWS * ROW)


def palette(n):
    start = HEADER + (255 - n) * ROW
    return slice(start, start + ROW)


def main():
    if len(sys.argv) != 4:
        sys.exit(__doc__)

    july = read_sheet(sys.argv[1])
    april = read_sheet(sys.argv[2])

    july[cell(JULY_SWIRL)] = april[cell(APRIL_SPIRIT)]
    july[palette(JULY_SWIRL)] = april[palette(APRIL_SPIRIT)]

    out = os.path.join(sys.argv[3], *SHEET.strip('\\').split('\\'))
    os.makedirs(os.path.dirname(out), exist_ok=True)
    with open(out, 'wb') as f:
        f.write(july)
    print('wrote', out)


if __name__ == '__main__':
    main()
