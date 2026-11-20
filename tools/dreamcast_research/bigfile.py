"""Bigfile (PSX or Dreamcast) lookup by game path, with the game's hash.

usage: bigfile.py <BIGFILE.DAT> find <path>...
       bigfile.py <BIGFILE.DAT> extract <path> <out>
       bigfile.py <BIGFILE.DAT> stats
"""
import struct
import sys

EXT = ["pcm", "crm", "tim", "pmp", "pnd", "pmf", "pnf"] if "--dc" in sys.argv else ["drm", "crm", "tim", "smp", "snd", "smf", "snf"]
sys.argv = [a for a in sys.argv if a != "--dc"]


def hash_name(s):
    s32 = lambda v: ((v + 0x80000000) & 0xFFFFFFFF) - 0x80000000
    ext = 0
    strl = len(s) - 1
    pos = s.find('.')
    if pos >= 0:
        e = s[pos + 1:]
        for i, x in enumerate(EXT):
            if e.lower() == x:
                ext = i
                strl -= 4
                break
    total = 0
    xor = 0
    length = 0
    while strl >= 0:
        c = s[strl]
        strl -= 1
        if c == '\\':
            continue
        c = ord(c)
        if ord('a') <= c <= ord('z'):
            c &= ~0x20
        c = (c - 0x1A) & 0xFF
        if c >= 0x80:
            c -= 0x100      # char is signed
        total += c
        xor ^= c * length
        length += 1
    return ((length << 27) | ((total << 15) & 0xFFFFFFFF) | ((xor << 3) & 0xFFFFFFFF) | ext) & 0xFFFFFFFF


def entries(path):
    f = open(path, 'rb')
    count = struct.unpack('<I', f.read(4))[0]
    out = {}
    for _ in range(count):
        h, size, off, chk = struct.unpack('<IIII', f.read(16))
        out[h] = (size, off, chk)
    return out


def main():
    big = sys.argv[1]
    cmd = sys.argv[2]
    e = entries(big)
    if cmd == 'find':
        for p in sys.argv[3:]:
            h = hash_name(p)
            print('%08x %s -> %s' % (h, p, e.get(h)))
    elif cmd == 'extract':
        h = hash_name(sys.argv[3])
        size, off, _ = e[h]
        f = open(big, 'rb')
        f.seek(off)
        open(sys.argv[4], 'wb').write(f.read(size))
        print('wrote', sys.argv[4], size)
    elif cmd == 'stats':
        print(len(e), 'entries')


main()
