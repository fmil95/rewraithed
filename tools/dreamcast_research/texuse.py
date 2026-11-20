"""Texture numbers used by the faces of a Dreamcast object, and their UV ranges."""
import collections
import struct
import sys

sys.path.insert(0, __file__.rsplit('\\', 1)[0])
from objinfo import load  # noqa: E402


def bf16(v):
    return struct.unpack('<f', struct.pack('<I', v << 16))[0]


for path in sys.argv[1:]:
    data, relocs = load(path)
    u32 = lambda o: struct.unpack_from('<I', data, o)[0]
    mo = u32(u32(0xC))
    nv, vl, nn, nl, nf, fl, ns, sl = struct.unpack_from('<IIIIIIII', data, mo)
    st, et = struct.unpack_from('<II', data, mo + 0x30)
    ids = collections.Counter()
    flags = collections.Counter()
    textured = 0
    for i in range(nf):
        f = fl + i * 12
        v0, v1, v2, normal, fflags, color = struct.unpack_from('<HHHBBI', data, f)
        flags[fflags] += 1
        if st <= color < et:
            textured += 1
            rec = data[color:color + 16]
            uv = struct.unpack_from('<6H', rec, 0)
            tid = struct.unpack_from('<H', rec, 12)[0]
            ids[tid] += 1
    print(path, 'faces', nf, 'textured', textured)
    print(' face flags', dict(flags))
    print(' texture ids', len(ids), sorted(ids.items())[:40])
