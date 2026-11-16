# List the ret instructions (with imm16 = stdcall/thiscall arg bytes) of functions in an objdump -d -M intel listing.
# usage: UiRet.py <full.asm> <va_hex>...   (scans from VA until the int3 padding that follows the first ret)
import sys, re
lines = open(sys.argv[1], errors='replace').read().split('\n')
idx = {}
for i, l in enumerate(lines):
    m = re.match(r'\s*([0-9a-f]+):\t', l)
    if m: idx[int(m.group(1), 16)] = i
for a in sys.argv[2:]:
    v = int(a, 16); i = idx.get(v)
    if i is None: print('%08x: not found' % v); continue
    rets = []; seen = False
    for l in lines[i:i + 6000]:
        if '\tret' in l:
            rets.append(l.split('\t')[0].strip() + ' ' + l.split('\t')[2].strip()); seen = True
        elif seen and '\tint3' in l: break
    print('%08x: %s' % (v, '; '.join(rets) or 'no ret'))
