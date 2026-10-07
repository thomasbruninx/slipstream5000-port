import sys,pickle,collections,re
sys.path.insert(0,'.')
from an import *
im=img()
CODE_LO=0x10000; CODE_HI=0x10000+len(im.segs[0][1])
def incode(a): return CODE_LO<=a<CODE_HI
insn={}          # addr -> instr
funcs={}         # start -> dict(blocks)
calls=collections.defaultdict(set) # callee -> callers (addr)
work=[]
seeds=set()
entry=0x10000+0x53978
def add_seed(a,why):
    if incode(a) and a not in seeds:
        seeds.add(a); work.append(a)
add_seed(entry,'entry')
# reloc targets in obj1 that look like function starts
PRO=(0x53,0x51,0x52,0x56,0x57,0x55,0x83,0x81,0x8b,0xb8,0x31,0x33,0x6a,0x68,0xa1,0x50,0x89,0x80,0xc7,0xe8,0xff,0x90)
reloc_targets=collections.Counter(im.relocs.values())
# data-ish pointers also (jump tables) are handled when seen; seed plausible function pointers after discovery pass
jt_pat=re.compile(r'\[(?:e[a-z]{2}\*4 \+ )?(0x[0-9a-f]+)\]')
def descend(start):
    stack=[start]
    lastm=None
    while stack:
        a=stack.pop()
        lastm=None
        while True:
            if not incode(a): break
            if a in insn: break
            x=im.dis1(a)
            if not x: break
            _,sz,m,o,by=x
            if by==b'\x00\x00': break
            insn[a]=x
            nxt=a+sz
            if m=='int' and o=='0x21' and lastm and ('0x4c' in lastm): break
            lastm=o if m in('mov',) or True else None
            lastm=x[3] if x[2]=='mov' else None
            if m=='call':
                mo=re.fullmatch(r'0x[0-9a-f]+',o)
                if mo:
                    t=int(o,16); calls[t].add(a); add_seed(t,'call')
                a=nxt; continue
            if m=='jmp':
                mo=re.fullmatch(r'0x[0-9a-f]+',o)
                if mo:
                    t=int(o,16)
                    if incode(t): stack.append(t)
                else:
                    mj=jt_pat.search(o)
                    if mj and '*4' in o:
                        tb=int(mj.group(1),16); k=0
                        while True:
                            ta=tb+4*k
                            if ta in im.relocs and incode(im.relocs[ta]):
                                stack.append(im.relocs[ta]); k+=1
                            else: break
                break
            if m.startswith('j') :
                mo=re.fullmatch(r'0x[0-9a-f]+',o)
                if mo:
                    t=int(o,16)
                    if incode(t): stack.append(t)
                a=nxt; continue
            if m in('ret','retf','iret','hlt','iretd'): break
            a=nxt
while work:
    s=work.pop()
    descend(s)
print("insns",len(insn),"seeds",len(seeds))
# coverage
cov=bytearray(CODE_HI-CODE_LO)
for a,x in insn.items():
    for k in range(x[1]): cov[a-CODE_LO+k]=1
print("code bytes covered",sum(cov),"of",len(cov))
pickle.dump((insn,dict(calls),seeds),open('work/disc.pkl','wb'))
# gaps
gaps=[];i=0
while i<len(cov):
    if not cov[i]:
        j=i
        while j<len(cov) and not cov[j]: j+=1
        gaps.append((CODE_LO+i,j-i)); i=j
    else: i+=1
print("gaps",len(gaps),"bytes",sum(g[1] for g in gaps))
big=[g for g in gaps if g[1]>=32]
print("big gaps",len(big))
for g in big[:80]: print(hex(g[0]),g[1])
