"""Decode texture slots of the Dreamcast TEXTURES.VQ to PNG.

Each texture number n is a fixed 0x4800-byte slot at n * 0x4800: a 256 x 256
VQ texture, a 2048-byte codebook (256 entries of 2x2 16-bit texels) and
128 x 128 twiddled indices.

usage: vqdecode.py <TEXTURES.VQ> <out-dir> <format> <id>...
       format: 1555, 565 or 4444
"""
import os
import struct
import sys
import zlib

SLOT = 0x4800
SIZE = 256


TRANSPOSE = bool(os.environ.get('VQ_TRANSPOSE'))


def twiddle(x, y):
    """Morton order index, x in the odd bits, y in the even (PowerVR)."""
    if TRANSPOSE:
        x, y = y, x
    v = 0
    for i in range(8):
        v |= ((y >> i) & 1) << (2 * i)
        v |= ((x >> i) & 1) << (2 * i + 1)
    return v


def to_rgba(c, fmt):
    if fmt == '565':
        r, g, b = (c >> 11) & 31, (c >> 5) & 63, c & 31
        return (r * 255 // 31, g * 255 // 63, b * 255 // 31, 255)
    if fmt == '4444':
        a, r, g, b = (c >> 12) & 15, (c >> 8) & 15, (c >> 4) & 15, c & 15
        return (r * 17, g * 17, b * 17, a * 17)
    a, r, g, b = (c >> 15) & 1, (c >> 10) & 31, (c >> 5) & 31, c & 31
    return (r * 255 // 31, g * 255 // 31, b * 255 // 31, 255 if a else 0)


def decode(slot, fmt):
    book = struct.unpack_from('<1024H', slot, 0)
    idx = slot[2048:2048 + 128 * 128]
    rows = [bytearray(SIZE * 4) for _ in range(SIZE)]
    for by in range(128):
        for bx in range(128):
            e = idx[twiddle(bx, by)]
            # 2x2 block texels, in twiddled order: (0,0) (0,1) (1,0) (1,1)
            for k, (dx, dy) in enumerate(((0, 0), (1, 0), (0, 1), (1, 1)) if TRANSPOSE else ((0, 0), (0, 1), (1, 0), (1, 1))):
                px = to_rgba(book[e * 4 + k], fmt)
                x, y = bx * 2 + dx, by * 2 + dy
                rows[y][x * 4:x * 4 + 4] = bytes(px)
    return rows


def write_png(path, rows):
    raw = b''.join(b'\0' + bytes(r) for r in rows)
    w, h = len(rows[0]) // 4, len(rows)

    def chunk(t, data):
        return struct.pack('>I', len(data)) + t + data + struct.pack('>I', zlib.crc32(t + data) & 0xFFFFFFFF)

    png = b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', w, h, 8, 6, 0, 0, 0))
    png += chunk(b'IDAT', zlib.compress(raw, 9)) + chunk(b'IEND', b'')
    open(path, 'wb').write(png)


def main():
    d = open(sys.argv[1], 'rb').read()
    out = sys.argv[2]
    fmt = sys.argv[3]
    os.makedirs(out, exist_ok=True)
    for a in sys.argv[4:]:
        n = int(a, 0) & 0x1FFF
        slot = d[n * SLOT:(n + 1) * SLOT]
        p = os.path.join(out, 'tex%04d_%s.png' % (n, fmt))
        write_png(p, decode(slot, fmt))
        print('wrote', p)


if __name__ == '__main__':
    main()
