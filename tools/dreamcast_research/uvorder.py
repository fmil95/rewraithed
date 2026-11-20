"""Which pairing of a record's three UVs with a face's three vertices makes
shared vertices agree on their UV."""
import collections
import itertools
import struct
import sys

sys.path.insert(0, __file__.rsplit('\\', 1)[0])
from objinfo import load  # noqa: E402

data, _ = load(sys.argv[1])
u32 = lambda o: struct.unpack_from('<I', data, o)[0]
mo = u32(u32(0xC))
nv, vl, nn, nl, nf, fl = struct.unpack_from('<IIIIII', data, mo)
st, et = struct.unpack_from('<II', data, mo + 0x30)
faces = []
for i in range(nf):
    v = struct.unpack_from('<HHH', data, fl + i * 12)
    color = u32(fl + i * 12 + 8)
    if st <= color < et:
        uv = struct.unpack_from('<6H', data, color)
        tid = struct.unpack_from('<H', data, color + 12)[0] & 0x1FFF
        faces.append((v, [(uv[0], uv[1]), (uv[2], uv[3]), (uv[4], uv[5])], tid))

for perm in itertools.permutations(range(3)):
    seen = collections.defaultdict(collections.Counter)
    for v, uvs, tid in faces:
        for k in range(3):
            seen[v[k]][(tid, uvs[perm[k]])] += 1
    agree = sum(c.most_common(1)[0][1] for c in seen.values())
    total = sum(sum(c.values()) for c in seen.values())
    print(perm, '%.3f' % (agree / total))
