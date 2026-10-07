import sys,pickle,re,collections,json,os
sys.path.insert(0,os.path.dirname(os.path.abspath(__file__)))
from an import *
im=img()
insn,calls,seeds=pickle.load(open(os.path.join(os.path.dirname(os.path.abspath(__file__)),'work','disc2.pkl'),'rb'))
CODE_LO=0x10000; CODE_HI=CODE_LO+len(im.segs[0][1])
DATA_LO=0x80000; DATA_HI=DATA_LO+len(im.segs[2][1])
HEXR=re.compile(r'0x[0-9a-f]+')
jt_pat=re.compile(r'\[(?:e[a-z]{2}\*4 \+ )?(0x[0-9a-f]+)\]')
# function starts = call targets + entry + code reloc targets that are in insn & look like starts (after ret/jmp or padding)
fstarts=set(t for t in calls if t in insn)
fstarts.add(0x10000+0x53978)
# reloc targets that were seeded as function pointers: those preceded by a terminator
sorted_ins=sorted(insn)
prev_end={}
for a in sorted_ins:
    pass
def is_fstart_ptr(t):
    if t not in insn: return False
    # prior insn (non-padding) is ret/jmp/ or gap
    p=t-1
    for k in range(1,12):
        q=t-k
        if q in insn and q+insn[q][1]==t:
            return insn[q][2] in ('ret','jmp','nop') or insn[q][2]=='lea' 
    return True
for t in set(im.relocs.values()):
    if t in insn and is_fstart_ptr(t): fstarts.add(t)
# tail-jump targets shared by many functions
jt=collections.defaultdict(set)
_f0={}
for s0 in sorted(fstarts):
    pass
funcs={}   # start-> sorted list of addresses
owner={}
def flood(s):
    seen=set(); st=[s]
    while st:
        a=st.pop()
        while a in insn and a not in seen:
            if a!=s and a in fstarts: break
            seen.add(a); x=insn[a]; m=x[2]; o=x[3]; nxt=a+x[1]
            if m=='call': a=nxt; continue
            if m=='jmp':
                if HEXR.fullmatch(o): st.append(int(o,16))
                else:
                    mj=jt_pat.search(o)
                    if mj and '*4' in o:
                        tb=int(mj.group(1),16);k=0
                        while tb+4*k in im.relocs and im.relocs[tb+4*k] in insn: st.append(im.relocs[tb+4*k]);k+=1
                break
            if m.startswith('j'):
                if HEXR.fullmatch(o): st.append(int(o,16))
                a=nxt; continue
            if m=='ret': break
            a=nxt
    return seen
for s in sorted(fstarts): 
    funcs[s]=flood(s)
jc=collections.defaultdict(set)
for s,body in funcs.items():
    for a in body:
        x=insn[a]
        if x[2]=='jmp' and HEXR.fullmatch(x[3]):
            t=int(x[3],16)
            jc[t].add(s)
newf=[t for t,ss in jc.items() if len(ss)>=5 and t in insn and t not in fstarts]
print("tail-jump function starts:",[hex(t) for t in newf])
fstarts.update(newf)
funcs={}
for s in sorted(fstarts): funcs[s]=flood(s)
# orphan promotion: decoded code not owned by any function
owned=set()
for s,b in funcs.items(): owned|=b
orph=sorted(a for a in insn if a not in owned)
added=[]
prev_end=None
for a in orph:
    if a in owned: continue
    if prev_end is None or a!=prev_end or True:
        pass
    # start of a run if the previous decoded instruction does not fall into it
    pe=None
    for k in range(1,16):
        q=a-k
        if q in insn and q+insn[q][1]==a: pe=q;break
    if pe is None or pe in owned or insn[pe][2] in ('ret','jmp') :
        fstarts.add(a); body=flood(a); funcs[a]=body; owned|=body; added.append(a)
print("orphan functions added:",len(added))

# xrefs
call_edges=collections.defaultdict(set)   # caller func -> callee funcs
str_refs=collections.defaultdict(list)
data_refs=collections.defaultdict(lambda: collections.defaultdict(list)) # data addr -> func -> [(insn addr, 'r'/'w')]
int_calls=collections.defaultdict(list)
addr2func={}
for s,body in funcs.items():
    for a in body: addr2func.setdefault(a,s)
def classify(a):
    if CODE_LO<=a<CODE_HI: return 'code'
    if DATA_LO<=a<DATA_HI: return 'data'
    return None
for s,body in funcs.items():
    for a in sorted(body):
        x=insn[a]; m=x[2]; o=x[3]
        if m=='call':
            if HEXR.fullmatch(o):
                t=int(o,16); call_edges[s].add(t)
        if m=='int': int_calls[s].append((a,o))
        # refs via reloc'd imm32/disp32: instruction bytes contain a reloc'd address
        for k in range(1,x[1]-3):
            ra=a+k
            if ra in im.relocs:
                t=im.relocs[ra]
                data_refs[t][s].append(a)
print("functions",len(funcs),"call edges",sum(len(v) for v in call_edges.values()))
pickle.dump(dict(funcs=funcs,call_edges=dict(call_edges),data_refs={k:dict(v) for k,v in data_refs.items()},int_calls=dict(int_calls)),open(os.path.join(os.path.dirname(os.path.abspath(__file__)),'work','db.pkl'),'wb'))
