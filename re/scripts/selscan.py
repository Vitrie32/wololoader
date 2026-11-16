import re,sys,bisect
S=sys.argv[1]; F=sys.argv[2]
funcs=[]
for l in open(F):
    p=l.split('\t')
    try: funcs.append((int(p[0],16),p[1],int(p[2])))
    except: pass
funcs.sort(); starts=[f[0] for f in funcs]
def fn(a):
    i=bisect.bisect_right(starts,a)-1
    return funcs[i] if i>=0 else None
rx=re.compile(r'^\s*([0-9a-f]+):\s*(.*)$')
disp=re.compile(r'[+]0x([0-9a-f]+)\]')
per={}
for l in open(S+'/text.asm'):
    m=rx.match(l)
    if not m: continue
    a=int(m.group(1),16); ins=m.group(2)
    for d in disp.findall(ins):
        v=int(d,16)
        if 0x15c<=v<=0x254:
            f=fn(a)
            if f: per.setdefault(f[0],[]).append((a,v,ins.strip()))
cnt=0
for fa,refs in sorted(per.items()):
    ds={v for _,v,_ in refs}
    if 0x254 in ds and (ds & set(range(0x15c,0x251))):
        cnt+=1
        f=fn(fa)
        print('== %08x %s (size %d)'%(fa,f[1],f[2]))
        for a,v,i in refs:
            if v==0x254 or 0x15c<=v<=0x250: print('   %08x %s'%(a,i))
print('functions:',cnt)
