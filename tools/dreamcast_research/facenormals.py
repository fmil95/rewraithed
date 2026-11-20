"""Compare each face's winding (cross product of its vertices) with its
stored normal (gNormalList[face.normal])."""
import collections
import re
import struct
import sys

sys.path.insert(0, __file__.rsplit('\\', 1)[0])
from objinfo import load  # noqa: E402

asm = open(sys.argv[1]).read()
start = asm.index('glabel gNormalList')
end = asm.index('glabel', start + 10)
shorts = [int(v, 16) for v in re.findall(r'\.short 0x([0-9A-Fa-f]+)', asm[start:end])]
shorts = [v - 0x10000 if v & 0x8000 else v for v in shorts]
normals = [tuple(shorts[i:i + 3]) for i in range(0, len(shorts) - 3, 4)]

for path in sys.argv[2:]:
    data, _ = load(path)
    u32 = lambda o: struct.unpack_from('<I', data, o)[0]
    mo = u32(u32(0xC))
    nv, vl, nn, nl, nf, fl, ns, sl = struct.unpack_from('<IIIIIIII', data, mo)
    verts = [struct.unpack_from('<hhh', data, vl + i * 8) for i in range(nv)]
    segv = {}
    for s in range(ns):
        ft, lt, fv, lv = struct.unpack_from('<hhhh', data, sl + s * 24 + 4)
        for v in range(fv, lv + 1):
            segv[v] = s
    sign = collections.Counter()
    neg = collections.Counter()
    for i in range(nf):
        a, b, c, n, flags = struct.unpack_from('<HHHBB', data, fl + i * 12)
        A, B, C = verts[a], verts[b], verts[c]
        u = [B[k] - A[k] for k in range(3)]
        w = [C[k] - A[k] for k in range(3)]
        cr = (u[1] * w[2] - u[2] * w[1], u[2] * w[0] - u[0] * w[2], u[0] * w[1] - u[1] * w[0])
        nv_ = normals[n] if n < len(normals) else (0, 0, 0)
        d = sum(cr[k] * nv_[k] for k in range(3))
        s = 'pos' if d > 0 else ('neg' if d < 0 else 'zero')
        sign[s] += 1
        if s == 'neg':
            neg[segv.get(a, -1)] += 1
    print(path, dict(sign), 'normals', len(normals))
    print('  negative by segment:', sorted(neg.items()))
