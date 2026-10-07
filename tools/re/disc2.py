import sys,pickle,collections,re
sys.path.insert(0,'.')
from an import *
im=img()
CODE_LO=0x10000; CODE_HI=0x10000+len(im.segs[0][1])
def incode(a): return CODE_LO<=a<CODE_HI
insn,calls,seeds=pickle.load(open('work/disc.pkl','rb'))
calls=collections.defaultdict(set,calls)
jt_pat=re.compile(r'\[(?:e[a-z]{2}\*4 \+ )?(0x[0-9a-f]+)\]')
BAD={'in','out','insb','insw','insd','outsb','outsw','outsd','hlt','arpl','bound','into','lds','les','aaa','aas','daa','das','aam','aad','sahf','lahf','salc','xlatb','cli','sti','popal_','enter','leave_','retf','iret','iretd','lcall','ljmp','clts','wait'}
def tentative(start):
    """try descent; return (dict of new insns, new seeds, callrefs) or None if looks like garbage"""
    new={}; stack=[start]; ns=set(); cr=[]; lastm=None
    cnt=0
    while stack:
        a=stack.pop(); lastm=None
        while True:
            if a in insn or a in new: break
            if not incode(a): return None
            x=im.dis1(a)
            if not x: return None
            _,sz,m,o,by=x
            if by==b'\x00\x00': return None
            if m in BAD: return None
            new[a]=x; cnt+=1
            if m=='int' and o=='0x21' and lastm and '0x4c' in lastm: break
            lastm=o if m=='mov' else None
            if cnt>4000: return None
            nxt=a+sz
            if m=='call':
                if re.fullmatch(r'0x[0-9a-f]+',o):
                    t=int(o,16)
                    if not incode(t): return None
                    ns.add(t); cr.append((t,a))
                a=nxt; continue
            if m=='jmp':
                if re.fullmatch(r'0x[0-9a-f]+',o):
                    t=int(o,16)
                    if not incode(t): return None
                    stack.append(t)
                else:
                    mj=jt_pat.search(o)
                    if mj and '*4' in o:
                        tb=int(mj.group(1),16); k=0
                        while tb+4*k in im.relocs and incode(im.relocs[tb+4*k]):
                            stack.append(im.relocs[tb+4*k]); k+=1
                break
            if m.startswith('j'):
                if re.fullmatch(r'0x[0-9a-f]+',o):
                    t=int(o,16)
                    if not incode(t): return None
                    stack.append(t)
                a=nxt; continue
            if m=='ret': break
            a=nxt
    return new,ns,cr
def accept(res):
    new,ns,cr=res
    insn.update(new)
    for t,a in cr: calls[t].add(a)
    return ns
def gaps():
    cov=bytearray(CODE_HI-CODE_LO)
    for a,x in insn.items():
        for k in range(x[1]): cov[a-CODE_LO+k]=1
    g=[];i=0
    while i<len(cov):
        if not cov[i]:
            j=i
            while j<len(cov) and not cov[j]: j+=1
            g.append((CODE_LO+i,j-i)); i=j
        else: i+=1
    return g,cov
PADS=[b'\x90',b'\x8d\x40\x00',b'\x8d\x49\x00',b'\x8d\x52\x00',b'\x8d\x5b\x00',b'\x8d\x76\x00',b'\x8d\x64\x24\x00',b'\x89\xc0',b'\x00']
def skip_pad(a,end):
    while a<end:
        for p in PADS[:-1]:
            if im.rd(a,len(p))==p: a+=len(p); break
        else: break
    return a
for rnd in range(30):
    prog=0
    g,cov=gaps()
    # candidates: reloc targets in gaps + gap starts (after padding) + any 53/51.. prologue signature inside gaps
    cands=set()
    for ga,gl in g:
        cands.add(skip_pad(ga,ga+gl))
        # skip zero bytes (buffer ends) then padding
        z=ga
        while z<ga+gl and im.u8(z)==0: z+=1
        if z!=ga: cands.add(skip_pad(z,ga+gl))
        # also first nonzero positions within 24 bytes after zeros
        for dz in range(0,6): 
            if z+dz<ga+gl: cands.add(z+dz)
    for t in set(im.relocs.values()):
        if incode(t) and t not in insn: cands.add(t)
    for c in sorted(cands):
        if c in insn: continue
        r=tentative(c)
        if r:
            ns=accept(r); prog+=1
    # also, function starts discovered via calls from accepted code
    for t in list(calls):
        if t not in insn and incode(t):
            r=tentative(t)
            if r: accept(r); prog+=1
    # scan gaps for prologue signature
    g,cov=gaps()
    for ga,gl in g:
        data=im.rd(ga,gl)
        for m in re.finditer(rb'(?:\x53\x51\x52\x56\x57\x55|\x53\x51\x52\x56\x55|\x53\x51\x52\x55|\x53\x51\x56\x57\x55|\x53\x52\x56\x57\x55|\x53\x56\x57\x55|\x53\x51\x52\x56\x57|\x53\x51\x52\x56|\x53\x51\x56\x57|\x53\x51\x52|\x53\x56\x57|\x51\x52\x56\x57\x55)',data):
            a=ga+m.start()
            if a in insn: continue
            r=tentative(a)
            if r: accept(r); prog+=1
    # scan gaps for E8 call rel32 whose target is in a gap (validated via tentative decode)
    g,cov=gaps()
    tcount=collections.Counter()
    for ga,gl in g:
        data=im.rd(ga,gl)
        for k in range(0,gl-4):
            if data[k]==0xe8:
                rel=struct.unpack_from('<i',data,k+1)[0]
                t=ga+k+5+rel
                if incode(t) and t not in insn and not cov[t-CODE_LO]: tcount[t]+=1
    for t,c in tcount.items():
        if t in insn: continue
        r=tentative(t)
        if r: accept(r); prog+=1
    g,cov=gaps()
    print("round",rnd,"accepted",prog,"covered",sum(cov),"gaps",len(g))
    if not prog: break
pickle.dump((insn,dict(calls),seeds),open('work/disc2.pkl','wb'))
big=[x for x in g if x[1]>=48]
print("remaining big gaps",len(big),sum(x[1] for x in g))
