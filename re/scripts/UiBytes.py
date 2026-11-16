# Print the first N bytes at VA(s) of a PE32 file: UiBytes.py <exe> <n> <va_hex>...
import sys, struct
d = open(sys.argv[1], 'rb').read()
n = int(sys.argv[2])
pe = struct.unpack_from('<I', d, 0x3c)[0]
ns = struct.unpack_from('<H', d, pe + 6)[0]; osz = struct.unpack_from('<H', d, pe + 20)[0]
base = struct.unpack_from('<I', d, pe + 52)[0]
secs = [struct.unpack_from('<III', d, pe + 24 + osz + i * 40 + 12) for i in range(ns)]
secs = [(va, struct.unpack_from('<I', d, pe + 24 + osz + i * 40 + 16)[0], rp) for i, (va, _, rp) in enumerate(secs)]
def off(v):
    for va, sz, rp in secs:
        if base + va <= v < base + va + sz: return v - base - va + rp
for a in sys.argv[3:]:
    v = int(a, 16)
    print('%08x: %s' % (v, d[off(v):off(v) + n].hex(' ')))
