"""Minimal SH-4 disassembler for reading 1ST_READ.BIN (loaded at 0x8C010000).

usage: sh4dis.py <bin> <start file offset> <count> [--find-refs-to <addr>]
"""
import struct
import sys

BASE = 0x8C010000


def s8(v):
    return v - 0x100 if v & 0x80 else v


def s12(v):
    return v - 0x1000 if v & 0x800 else v


def dis(d, off):
    op = struct.unpack_from('<H', d, off)[0]
    pc = BASE + off
    n = (op >> 8) & 0xF
    m = (op >> 4) & 0xF
    i8 = op & 0xFF
    hi = op >> 12
    lo4 = op & 0xF
    lo8 = op & 0xFF
    R = lambda x: 'r%d' % x

    if hi == 0xE:
        return 'mov #%d,%s' % (s8(i8), R(n))
    if hi == 0x9:
        a = pc + 4 + i8 * 2
        v = struct.unpack_from('<h', d, a - BASE)[0] if 0 <= a - BASE < len(d) else None
        return 'mov.w @(0x%x),%s  ; =0x%x' % (a, R(n), (v or 0) & 0xFFFF)
    if hi == 0xD:
        a = (pc & ~3) + 4 + i8 * 4
        v = struct.unpack_from('<I', d, a - BASE)[0] if 0 <= a - BASE < len(d) else None
        extra = ''
        if v is not None and BASE <= v < BASE + len(d):
            s = d[v - BASE:v - BASE + 32].split(b'\0')[0]
            if s and all(32 <= c < 127 for c in s) and len(s) >= 3:
                extra = ' "%s"' % s.decode()
        return 'mov.l @(0x%x),%s  ; =0x%x%s' % (a, R(n), v or 0, extra)
    if hi == 0x7:
        return 'add #%d,%s' % (s8(i8), R(n))
    if hi == 0xA:
        return 'bra 0x%x' % (pc + 4 + s12(op & 0xFFF) * 2)
    if hi == 0xB:
        return 'bsr 0x%x' % (pc + 4 + s12(op & 0xFFF) * 2)
    if hi == 0x8:
        sub = n
        disp = s8(i8)
        if sub == 0x9:
            return 'bt 0x%x' % (pc + 4 + disp * 2)
        if sub == 0xB:
            return 'bf 0x%x' % (pc + 4 + disp * 2)
        if sub == 0xD:
            return 'bt/s 0x%x' % (pc + 4 + disp * 2)
        if sub == 0xF:
            return 'bf/s 0x%x' % (pc + 4 + disp * 2)
        if sub == 0x8:
            return 'cmp/eq #%d,r0' % disp
        if sub == 0x0:
            return 'mov.b r0,@(%d,%s)' % (lo4, R(m))
        if sub == 0x1:
            return 'mov.w r0,@(%d,%s)' % (lo4 * 2, R(m))
        if sub == 0x4:
            return 'mov.b @(%d,%s),r0' % (lo4, R(m))
        if sub == 0x5:
            return 'mov.w @(%d,%s),r0' % (lo4 * 2, R(m))
    if hi == 0x1:
        return 'mov.l %s,@(%d,%s)' % (R(m), lo4 * 4, R(n))
    if hi == 0x5:
        return 'mov.l @(%d,%s),%s' % (lo4 * 4, R(m), R(n))
    if hi == 0xC:
        sub = n
        names = {0x0: 'mov.b r0,@(%d,gbr)', 0x1: 'mov.w r0,@(%d,gbr)', 0x2: 'mov.l r0,@(%d,gbr)',
                 0x4: 'mov.b @(%d,gbr),r0', 0x5: 'mov.w @(%d,gbr),r0', 0x6: 'mov.l @(%d,gbr),r0'}
        if sub == 0x7:
            return 'mova @(0x%x),r0' % ((pc & ~3) + 4 + i8 * 4)
        if sub == 0x8:
            return 'tst #%d,r0' % i8
        if sub == 0x9:
            return 'and #%d,r0' % i8
        if sub == 0xA:
            return 'xor #%d,r0' % i8
        if sub == 0xB:
            return 'or #%d,r0' % i8
        if sub == 0x3:
            return 'trapa #%d' % i8
        if sub in names:
            scale = {0: 1, 1: 2, 2: 4, 4: 1, 5: 2, 6: 4}[sub]
            return names[sub] % (i8 * scale)
    if hi == 0x6:
        t = {0: 'mov.b @%s,%s', 1: 'mov.w @%s,%s', 2: 'mov.l @%s,%s', 3: 'mov %s,%s',
             4: 'mov.b @%s+,%s', 5: 'mov.w @%s+,%s', 6: 'mov.l @%s+,%s', 7: 'not %s,%s',
             8: 'swap.b %s,%s', 9: 'swap.w %s,%s', 0xA: 'negc %s,%s', 0xB: 'neg %s,%s',
             0xC: 'extu.b %s,%s', 0xD: 'extu.w %s,%s', 0xE: 'exts.b %s,%s', 0xF: 'exts.w %s,%s'}
        return t[lo4] % (R(m), R(n))
    if hi == 0x2:
        t = {0: 'mov.b %s,@%s', 1: 'mov.w %s,@%s', 2: 'mov.l %s,@%s', 4: 'mov.b %s,@-%s',
             5: 'mov.w %s,@-%s', 6: 'mov.l %s,@-%s', 7: 'div0s %s,%s', 8: 'tst %s,%s', 9: 'and %s,%s',
             0xA: 'xor %s,%s', 0xB: 'or %s,%s', 0xC: 'cmp/str %s,%s', 0xD: 'xtrct %s,%s',
             0xE: 'mulu.w %s,%s', 0xF: 'muls.w %s,%s'}
        if lo4 in t:
            return t[lo4] % (R(m), R(n))
    if hi == 0x3:
        t = {0: 'cmp/eq', 2: 'cmp/hs', 3: 'cmp/ge', 4: 'div1', 5: 'dmulu.l', 6: 'cmp/hi', 7: 'cmp/gt',
             8: 'sub', 0xA: 'subc', 0xB: 'subv', 0xC: 'add', 0xD: 'dmuls.l', 0xE: 'addc', 0xF: 'addv'}
        if lo4 in t:
            return '%s %s,%s' % (t[lo4], R(m), R(n))
    if hi == 0x0:
        if op == 0x000B:
            return 'rts'
        if op == 0x0009:
            return 'nop'
        if lo4 == 0x3 and m == 0:
            return 'bsrf %s' % R(n)
        if lo4 == 0x3 and m == 2:
            return 'braf %s' % R(n)
        t = {4: 'mov.b %s,@(r0,%s)', 5: 'mov.w %s,@(r0,%s)', 6: 'mov.l %s,@(r0,%s)',
             0xC: 'mov.b @(r0,%s),%s', 0xD: 'mov.w @(r0,%s),%s', 0xE: 'mov.l @(r0,%s),%s'}
        if lo4 in (4, 5, 6):
            return t[lo4] % (R(m), R(n))
        if lo4 in (0xC, 0xD, 0xE):
            return t[lo4] % (R(m), R(n))
        if lo4 == 7:
            return 'mul.l %s,%s' % (R(m), R(n))
        if lo8 == 0x29:
            return 'movt %s' % R(n)
        if lo8 == 0x2A:
            return 'sts pr,%s' % R(n)
        if lo8 == 0x1A:
            return 'sts macl,%s' % R(n)
        if lo8 == 0x0A:
            return 'sts mach,%s' % R(n)
    if hi == 0x4:
        t = {0x0B: 'jsr @%s', 0x2B: 'jmp @%s', 0x22: 'sts.l pr,@-%s', 0x26: 'lds.l @%s+,pr',
             0x00: 'shll %s', 0x01: 'shlr %s', 0x08: 'shll2 %s', 0x09: 'shlr2 %s', 0x18: 'shll8 %s',
             0x19: 'shlr8 %s', 0x28: 'shll16 %s', 0x29: 'shlr16 %s', 0x10: 'dt %s', 0x11: 'cmp/pz %s',
             0x15: 'cmp/pl %s', 0x20: 'shal %s', 0x21: 'shar %s', 0x24: 'rotcl %s', 0x25: 'rotcr %s',
             0x2A: 'lds %s,pr', 0x1E: 'ldc %s,gbr', 0x04: 'rotl %s', 0x05: 'rotr %s'}
        if lo8 in t:
            return t[lo8] % R(n)
        if lo4 == 0xC:
            return 'shad %s,%s' % (R(m), R(n))
        if lo4 == 0xD:
            return 'shld %s,%s' % (R(m), R(n))
    if hi == 0xF:
        return 'fpu %04x' % op
    return '.word %04x' % op


def main():
    d = open(sys.argv[1], 'rb').read()
    start = int(sys.argv[2], 0)
    count = int(sys.argv[3], 0)
    for k in range(count):
        off = start + k * 2
        print('%08x  %06x  %04x  %s' % (BASE + off, off, struct.unpack_from('<H', d, off)[0], dis(d, off)))


if __name__ == '__main__':
    main()
