"""Print the Object and Model headers of an object file (.drm / .pcm)."""
import struct
import sys


def load(path):
    d = open(path, 'rb').read()
    n = struct.unpack_from('<I', d, 0)[0]
    table = ((n + 512) // 512) * 512 * 4
    return d[table:], [struct.unpack_from('<I', d, 4 + i * 4)[0] for i in range(n)]


def main(path):
    data, relocs = load(path)
    u32 = lambda o: struct.unpack_from('<I', data, o)[0]
    s16 = lambda o: struct.unpack_from('<h', data, o)[0]
    print('==', path, 'data', len(data), 'relocs', len(relocs))
    print(' oflags %08x id %d numModels %d numAnims %d modelList %x animList %x' % (
        u32(0), s16(4), s16(8), s16(0xA), u32(0xC), u32(0x10)))
    name = u32(0x24)
    print(' name', data[name:name + 8], 'oflags2 %08x' % u32(0x2C), 'vram %d,%d %dx%d' % struct.unpack_from('<hhhh', data, 0x44))
    for m in range(s16(8)):
        mo = u32(u32(0xC) + m * 4)
        f = struct.unpack_from('<IIIIIIIIIhhIIII', data, mo)
        nv, vl, nn, nl, nf, fl, ns, sl, at, maxRad, pad, maxRadSq, ms, st, et = f
        ntex = (et - st) // 16 if et > st else 0
        print(' model %d @%x: verts %d normals %d faces %d segs %d textures %d (start %x end %x)' % (
            m, mo, nv, nn, nf, ns, ntex, st, et))
        if m == 0:
            print('  first vertex', struct.unpack_from('<hhhH', data, vl))
            print('  first face', data[fl:fl + 12].hex(' '))
            if ntex:
                print('  first texture', data[st:st + 16].hex(' '))
            print('  gap faces->? next struct sizes: vertex stride guess', (nl - vl) / nv if nv else 0,
                  'face stride guess', (sl - fl) / nf if (nf and sl > fl) else None)


if __name__ == '__main__':
    for p in sys.argv[1:]:
        main(p)
