import struct

d = open('BIGFILE.DAT', 'rb').read()
n = struct.unpack_from('<I', d, 0)[0]
E = {struct.unpack_from('<I', d, 4 + i * 16)[0] for i in range(n)}
EXT = ["pcm", "crm", "tim", "pmp", "pnd", "pmf", "pnf"]


def h(s, case, sub, signed=True):
    ext = 0
    strl = len(s) - 1
    pos = s.find('.')
    if pos >= 0:
        e = s[pos + 1:].lower()
        if e in EXT:
            ext = EXT.index(e)
            strl -= 4
    total = xor = length = 0
    while strl >= 0:
        c = s[strl]
        strl -= 1
        if c == '\\':
            continue
        c = ord(c.upper() if case == 'u' else c.lower() if case == 'l' else c) - sub
        c &= 0xFF
        if signed and c >= 0x80:
            c -= 0x100
        total += c
        xor ^= c * length
        length += 1
    return ((length << 27) | ((total << 15) & 0xFFFFFFFF) | ((xor << 3) & 0xFFFFFFFF) | ext) & 0xFFFFFFFF


names = [
    '\\kain2\\object\\raziel\\raziel.pcm', '\\kain2\\object\\kain\\kain.pcm',
    '\\kain2\\area\\under\\bin\\under1.pcm', '\\kain2\\area\\city\\bin\\city9.pcm',
    '\\kain2\\game\\psx\\warning.tim', '\\kain2\\kain2.arg', '\\kain2\\locals.pc',
    '\\kain2\\game\\psx\\locals.pc', '\\kain2\\object\\raziel\\raziel.crm',
]
for case in 'ulx':
    for sub in range(0, 0x80):
        hits = [nm for nm in names if h(nm, case, sub) in E]
        if hits:
            print(case, hex(sub), hits)

print('--- without kain2')
for case in 'ulx':
    for sub in range(0, 0x80):
        for nm in ['\object\raziel\raziel.pcm', 'object\raziel\raziel.pcm']:
            v = h(nm, case, sub)
            if v in E:
                print(case, hex(sub), nm, hex(v))
print(hex(h('\object\raziel\raziel.pcm', 'u', 0x1a)))
