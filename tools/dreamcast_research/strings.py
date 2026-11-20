import re
import sys

d = open(sys.argv[1], 'rb').read()
words = [w.lower() for w in sys.argv[2:]]
for m in re.finditer(rb'[ -~]{4,}', d):
    s = m.group().decode()
    if any(w in s.lower() for w in words):
        print(hex(m.start()), s)
