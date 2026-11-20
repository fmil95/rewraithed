"""Faces wound against most of their neighbours (likely flipped)."""
import collections
import struct
import sys

sys.path.insert(0, __file__.rsplit('\\', 1)[0])
from objinfo import load  # noqa: E402

for path in sys.argv[1:]:
    data, _ = load(path)
    u32 = lambda o: struct.unpack_from('<I', data, o)[0]
    mo = u32(u32(0xC))
    nv, vl, nn, nl, nf, fl, ns, sl = struct.unpack_from('<IIIIIIII', data, mo)
    verts = [struct.unpack_from('<hhh', data, vl + i * 8) for i in range(nv)]
    faces = [struct.unpack_from('<HHHBB', data, fl + i * 12) for i in range(nf)]
    segv = {}
    for s in range(ns):
        ft, lt, fv, lv = struct.unpack_from('<hhhh', data, sl + s * 24 + 4)
        for v in range(fv, lv + 1):
            segv[v] = s
    # undirected edge -> list of (face, direction)
    e = collections.defaultdict(list)
    for i, (a, b, c, n, f) in enumerate(faces):
        for x, y in ((a, b), (b, c), (c, a)):
            e[(min(x, y), max(x, y))].append((i, x < y))
    flipped = []
    for i, (a, b, c, n, f) in enumerate(faces):
        same = opp = 0
        for x, y in ((a, b), (b, c), (c, a)):
            mine = x < y
            for j, d in e[(min(x, y), max(x, y))]:
                if j == i:
                    continue
                if d == mine:
                    same += 1
                else:
                    opp += 1
        if same > opp:
            flipped.append(i)
    by = collections.Counter(segv.get(faces[i][0], -1) for i in flipped)
    print(path, 'faces against their neighbours:', len(flipped), sorted(by.items()))
    if path.startswith('dc_raziel'):
        print(' ', flipped)
