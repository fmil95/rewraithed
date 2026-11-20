"""Import Raziel's and Kain's models from the Dreamcast version.

Reads the Dreamcast disc (a GDI dump) and writes, into <out-dir>:

    raziel.pcm, kain.pcm    the two object files, as on the disc
    texNNNN.vq              every texture they use: a 256 x 256 VQ texture
                            (2048-byte codebook + 128 x 128 twiddled indices)

The game (port/src/dc_models.c) looks for them in a "dreamcast" folder next
to rewraithed.exe.

usage: python tools/dc_import.py <disc.gdi> <out-dir>

The disc's BIGFILE.DAT has the same layout as the PlayStation one (a count,
then hash, size, offset and checksum per file); the two objects are found by
their hashes and checked by the object name inside. TEXTURES.VQ has no
index: texture number n is the 0x4800-byte slot at n * 0x4800.
"""
import os
import struct
import sys

SECTOR = 2048
TEXTURE_SLOT = 0x4800

# bigfile hashes of the two objects on the Dreamcast disc, and the name
# each object file carries
OBJECTS = {
    'raziel': (0x90850620, b'raziel__'),
    'kain': (0x797909D0, b'kain____'),
}


class Gdi:
    """The data tracks of a GDI dump, read by logical sector."""

    def __init__(self, path):
        folder = os.path.dirname(os.path.abspath(path))
        self.tracks = []
        with open(path) as f:
            lines = f.read().split('\n')
        for line in lines[1:]:
            p = line.split()
            if len(p) < 5:
                continue
            lba, ttype, size, name = int(p[1]), int(p[2]), int(p[3]), p[4]
            if ttype == 4:
                f = os.path.join(folder, name)
                self.tracks.append((lba, os.path.getsize(f) // size, size, f))
        self.handles = {}

    def sector(self, lba):
        for start, count, size, f in self.tracks:
            if start <= lba < start + count:
                h = self.handles.get(f)
                if h is None:
                    h = self.handles[f] = open(f, 'rb')
                h.seek((lba - start) * size + (16 if size == 2352 else 0))
                return h.read(SECTOR)
        raise IOError('sector %d is in no data track' % lba)

    def read(self, lba, offset, length):
        out = bytearray()
        lba += offset // SECTOR
        skip = offset % SECTOR
        while len(out) < length + skip:
            out += self.sector(lba)
            lba += 1
        return bytes(out[skip:skip + length])


def find_file(gdi, name):
    """(lba, size) of a file in the root of the high-density area."""
    pvd = gdi.sector(45000 + 16)
    if pvd[1:6] != b'CD001':
        raise IOError('no ISO 9660 file system in the high-density area')
    root = pvd[156:156 + 34]
    lba, size = struct.unpack_from('<I', root, 2)[0], struct.unpack_from('<I', root, 10)[0]
    data = gdi.read(lba, 0, size)
    pos = 0
    while pos < len(data):
        n = data[pos]
        if n == 0:
            pos = (pos // SECTOR + 1) * SECTOR
            continue
        rec = data[pos:pos + n]
        fname = rec[33:33 + rec[32]].decode('latin-1').split(';')[0]
        if fname.upper() == name:
            return struct.unpack_from('<I', rec, 2)[0], struct.unpack_from('<I', rec, 10)[0]
        pos += n
    raise IOError('%s not on the disc' % name)


def texture_ids(pcm):
    """Texture numbers used by the faces of an object file's first model."""
    n = struct.unpack_from('<I', pcm, 0)[0]
    data = pcm[((n + 512) // 512) * 512 * 4:]
    u32 = lambda o: struct.unpack_from('<I', data, o)[0]
    model = u32(u32(0xC))
    num_faces, faces = u32(model + 0x10), u32(model + 0x14)
    start, end = u32(model + 0x30), u32(model + 0x34)
    ids = set()
    for i in range(num_faces):
        flags = data[faces + i * 12 + 7]
        tex = u32(faces + i * 12 + 8)
        if (flags & 2) and start <= tex < end:
            ids.add(struct.unpack_from('<H', data, tex + 12)[0] & 0x1FFF)
    return sorted(ids)


def main():
    if len(sys.argv) != 3:
        print(__doc__)
        sys.exit(1)
    gdi = Gdi(sys.argv[1])
    out = sys.argv[2]
    os.makedirs(out, exist_ok=True)

    big_lba, _ = find_file(gdi, 'BIGFILE.DAT')
    count = struct.unpack_from('<I', gdi.read(big_lba, 0, 4))[0]
    directory = gdi.read(big_lba, 4, count * 16)
    entries = {}
    for i in range(count):
        h, size, offset, _ = struct.unpack_from('<IIII', directory, i * 16)
        entries[h] = (size, offset)

    tex_lba, tex_size = find_file(gdi, 'TEXTURES.VQ')
    textures = set()

    for name, (h, tag) in OBJECTS.items():
        if h not in entries:
            raise IOError('%s not in BIGFILE.DAT (another release of the disc?)' % name)
        size, offset = entries[h]
        pcm = gdi.read(big_lba, offset, size)
        if tag not in pcm:
            raise IOError('%s: the file found is not that object' % name)
        with open(os.path.join(out, name + '.pcm'), 'wb') as f:
            f.write(pcm)
        ids = texture_ids(pcm)
        textures.update(ids)
        print('%s.pcm: %d bytes, textures %s' % (name, size, ids))

    for n in sorted(textures):
        if (n + 1) * TEXTURE_SLOT > tex_size:
            raise IOError('texture %d is past the end of TEXTURES.VQ' % n)
        with open(os.path.join(out, 'tex%04d.vq' % n), 'wb') as f:
            f.write(gdi.read(tex_lba, n * TEXTURE_SLOT, TEXTURE_SLOT))
    print('%d textures written to %s' % (len(textures), out))


if __name__ == '__main__':
    main()
