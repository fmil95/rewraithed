"""Faces whose winding disagrees with their neighbours, by segment."""
import collections
import struct
import sys

sys.path.insert(0, __file__.rsplit('\\', 1)[0])
from objinfo import load  # noqa: E402

data, _ = load(sys.argv[1])
u32 = lambda o: struct.unpack_from('<I', data, o)[0]
mo = u32(u32(0xC))
nv, vl, nn, nl, nf, fl, ns, sl = struct.unpack_from('<IIIIIIII', data, mo)
faces = []
for i in range(nf):
    v0, v1, v2, normal, flags = struct.unpack_from('<HHHBB', data, fl + i * 12)
    faces.append((v0, v1, v2, flags))

# segment of each vertex
segv = {}
for s in range(ns):
    first_tri, last_tri, first_v, last_v = struct.unpack_from('<hhhh', data, sl + s * 24 + 4)
    for v in range(first_v, last_v + 1):
        segv[v] = s

edges = collections.defaultdict(list)
for i, (a, b, c, f) in enumerate(faces):
    for e in ((a, b), (b, c), (c, a)):
        edges[e].append(i)

bad = collections.Counter()
badfaces = set()
for (a, b), owners in edges.items():
    if (a, b) in edges and len(owners) and (b, a) not in edges:
        pass
    # the same directed edge used by two faces: one of them is flipped
    if len(owners) > 1:
        for i in owners:
            badfaces.add(i)
for i in badfaces:
    bad[segv.get(faces[i][0], -1)] += 1
print('faces sharing a directed edge with another face:', len(badfaces))
print('by segment:', sorted(bad.items()))
print('flags of those:', collections.Counter(faces[i][3] for i in badfaces))
