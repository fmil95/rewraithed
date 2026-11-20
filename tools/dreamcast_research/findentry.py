import struct
import sys

d = open(sys.argv[1], 'rb').read()
n = struct.unpack_from('<I', d, 0)[0]
E = [struct.unpack_from('<IIII', d, 4 + i * 16) for i in range(n)]


def owner(pos):
    for i, (h, s, o, k) in enumerate(E):
        if o <= pos < o + s:
            return i, h, s, o
    return None


for word in sys.argv[2:]:
    w = word.encode()
    pos = d.find(w)
    seen = set()
    while pos >= 0:
        ow = owner(pos)
        if ow and ow[0] not in seen:
            seen.add(ow[0])
            i, h, s, o = ow
            print('%-10s entry %4d hash %08x size %7d off %08x (+%x) len%d ext%d' % (word, i, h, s, o, pos - o, h >> 27, h & 7))
        pos = d.find(w, pos + 1)
