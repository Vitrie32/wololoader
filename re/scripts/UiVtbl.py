# Print the RTTI class name and first slots of the vftable at a VA.
import sys, struct
data = open(sys.argv[1], 'rb').read()
pe = struct.unpack_from('<I', data, 0x3c)[0]
n = struct.unpack_from('<H', data, pe + 6)[0]; osz = struct.unpack_from('<H', data, pe + 20)[0]
base = struct.unpack_from('<I', data, pe + 52)[0]
secs = [(struct.unpack_from('<I', data, pe+24+osz+i*40+12)[0], struct.unpack_from('<I', data, pe+24+osz+i*40+16)[0], struct.unpack_from('<I', data, pe+24+osz+i*40+20)[0]) for i in range(n)]
def off(v):
    for va, sz, rp in secs:
        if base+va <= v < base+va+sz: return v-base-va+rp
u = lambda v: struct.unpack_from('<I', data, off(v))[0]
vt = int(sys.argv[2], 16)
col = u(vt - 4); td = u(col + 12)
name = data[off(td + 8):off(td + 8) + 80].split(b'\0')[0]
print(hex(vt), name.decode())
for k in range(int(sys.argv[3]) if len(sys.argv) > 3 else 14):
    print('  [%2d +0x%02x] %08x' % (k, k*4, u(vt + k*4)))
