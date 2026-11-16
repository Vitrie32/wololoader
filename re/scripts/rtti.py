# Find MSVC RTTI vftables for class names in a PE32 file.
import sys, struct
data = open(sys.argv[1], 'rb').read()
pe = struct.unpack_from('<I', data, 0x3c)[0]
nsec = struct.unpack_from('<H', data, pe + 6)[0]
optsz = struct.unpack_from('<H', data, pe + 20)[0]
base = struct.unpack_from('<I', data, pe + 52)[0]
secs = []
for i in range(nsec):
    o = pe + 24 + optsz + i * 40
    va, rsz, rptr = struct.unpack_from('<I', data, o + 12)[0], struct.unpack_from('<I', data, o + 16)[0], struct.unpack_from('<I', data, o + 20)[0]
    secs.append((va, rsz, rptr))
def off2va(off):
    for va, rsz, rptr in secs:
        if rptr <= off < rptr + rsz: return base + va + off - rptr
def va2off(v):
    for va, rsz, rptr in secs:
        if base + va <= v < base + va + rsz: return v - base - va + rptr
def find_all(pat):
    i = data.find(pat)
    while i != -1:
        yield i; i = data.find(pat, i + 1)
for cls in sys.argv[2:]:
    name = ('.?AV%s@@' % cls).encode() + b'\0'
    for noff in find_all(name):
        td = off2va(noff - 8)
        for coff in find_all(struct.pack('<I', td)):
            col = coff - 12  # COL: sig, offset, cdOffset, pTypeDescriptor
            if struct.unpack_from('<I', data, col)[0] != 0: continue
            off = struct.unpack_from('<I', data, col + 4)[0]
            colva = off2va(col)
            for r in find_all(struct.pack('<I', colva)):
                vt = off2va(r + 4)
                slots = []
                for k in range(60):
                    p = struct.unpack_from('<I', data, r + 4 + k * 4)[0]
                    if not (0x401000 <= p < 0x9ac000): break
                    slots.append(p)
                print('%s vftable@%08x (this-offset %d), %d slots' % (cls, vt, off, len(slots)))
                for k, p in enumerate(slots): print('  [%2d +0x%03x] %08x' % (k, k * 4, p))
