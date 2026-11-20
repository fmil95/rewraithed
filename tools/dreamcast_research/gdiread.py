"""Read the ISO 9660 file system of a Dreamcast GDI (high-density area).

usage: gdiread.py <gdi> list
       gdiread.py <gdi> extract <path-in-disc> <out-file>
       gdiread.py <gdi> extractall <out-dir> [substring]
"""
import os
import struct
import sys


class Gdi:
    def __init__(self, path):
        self.dir = os.path.dirname(path)
        lines = open(path).read().split('\n')
        self.tracks = []
        for line in lines[1:]:
            p = line.split()
            if len(p) < 5:
                continue
            num, lba, ttype, size, name = int(p[0]), int(p[1]), int(p[2]), int(p[3]), p[4]
            if ttype == 4:
                f = os.path.join(self.dir, name)
                count = os.path.getsize(f) // size
                self.tracks.append((lba, count, size, f))
        self.files = {}

    def sector(self, lba):
        for start, count, size, f in self.tracks:
            if start <= lba < start + count:
                h = self.files.get(f)
                if h is None:
                    h = self.files[f] = open(f, 'rb')
                h.seek((lba - start) * size + (16 if size == 2352 else 0))
                return h.read(2048)
        return bytes(2048)

    def read(self, lba, length):
        out = bytearray()
        while len(out) < length:
            out += self.sector(lba)
            lba += 1
        return bytes(out[:length])


def walk(g, lba, size, prefix, out):
    data = g.read(lba, size)
    pos = 0
    while pos < len(data):
        n = data[pos]
        if n == 0:
            pos = (pos // 2048 + 1) * 2048
            continue
        rec = data[pos:pos + n]
        elba = struct.unpack_from('<I', rec, 2)[0]
        esize = struct.unpack_from('<I', rec, 10)[0]
        flags = rec[25]
        nlen = rec[32]
        name = rec[33:33 + nlen].decode('latin-1')
        pos += n
        if name in ('\x00', '\x01'):
            continue
        name = name.split(';')[0]
        path = prefix + '/' + name
        if flags & 2:
            walk(g, elba, esize, path, out)
        else:
            out.append((path, elba, esize))


def listing(g):
    pvd = g.sector(45000 + 16)
    assert pvd[1:6] == b'CD001', pvd[:8]
    root = pvd[156:156 + 34]
    rlba = struct.unpack_from('<I', root, 2)[0]
    rsize = struct.unpack_from('<I', root, 10)[0]
    out = []
    walk(g, rlba, rsize, '', out)
    return out


def main():
    g = Gdi(sys.argv[1])
    cmd = sys.argv[2]
    files = listing(g)
    if cmd == 'list':
        for path, lba, size in files:
            print('%10d %s' % (size, path))
    elif cmd == 'extract':
        for path, lba, size in files:
            if path.lstrip('/').lower() == sys.argv[3].lstrip('/').lower():
                open(sys.argv[4], 'wb').write(g.read(lba, size))
                print('wrote', sys.argv[4], size)
                return
        print('not found')
    elif cmd == 'extractall':
        sub = sys.argv[4].lower() if len(sys.argv) > 4 else ''
        for path, lba, size in files:
            if sub in path.lower():
                dst = os.path.join(sys.argv[3], path.lstrip('/'))
                os.makedirs(os.path.dirname(dst), exist_ok=True)
                open(dst, 'wb').write(g.read(lba, size))
                print('wrote', dst, size)


main()
