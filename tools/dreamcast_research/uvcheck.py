"""Draw the UV triangles of the faces using one texture over that texture."""
import struct
import sys

sys.path.insert(0, __file__.rsplit('\\', 1)[0])
from objinfo import load  # noqa: E402
from vqdecode import decode, write_png  # noqa: E402


def bf16(v):
    return struct.unpack('<f', struct.pack('<I', v << 16))[0]


obj, tid, out = sys.argv[1], int(sys.argv[2]), sys.argv[3]
data, _ = load(obj)
u32 = lambda o: struct.unpack_from('<I', data, o)[0]
mo = u32(u32(0xC))
nv, vl, nn, nl, nf, fl = struct.unpack_from('<IIIIII', data, mo)
st, et = struct.unpack_from('<II', data, mo + 0x30)
tex = open(__file__.rsplit('\\', 1)[0] + '\\TEXTURES.VQ', 'rb').read()
img = decode(tex[tid * 0x4800:(tid + 1) * 0x4800], '1555')


def plot(x, y):
    if 0 <= x < 256 and 0 <= y < 256:
        img[y][x * 4:x * 4 + 4] = b'\x00\xff\x00\xff'


def line(a, b):
    steps = int(max(abs(b[0] - a[0]), abs(b[1] - a[1]))) + 1
    for i in range(steps + 1):
        t = i / steps
        plot(int(a[0] + (b[0] - a[0]) * t), int(a[1] + (b[1] - a[1]) * t))


count = 0
umin = vmin = 9
umax = vmax = -9
for i in range(nf):
    color = u32(fl + i * 12 + 8)
    if not (st <= color < et):
        continue
    rec = data[color:color + 16]
    if struct.unpack_from('<H', rec, 12)[0] & 0x1FFF != tid:
        continue
    uv = [bf16(v) for v in struct.unpack_from('<6H', rec, 0)]
    pts = [(uv[0] * 256, uv[1] * 256), (uv[2] * 256, uv[3] * 256), (uv[4] * 256, uv[5] * 256)]
    umin = min(umin, uv[0], uv[2], uv[4]); umax = max(umax, uv[0], uv[2], uv[4])
    vmin = min(vmin, uv[1], uv[3], uv[5]); vmax = max(vmax, uv[1], uv[3], uv[5])
    line(pts[0], pts[1]); line(pts[1], pts[2]); line(pts[2], pts[0])
    count += 1
write_png(out, img)
print(count, 'faces; u', umin, umax, 'v', vmin, vmax)
