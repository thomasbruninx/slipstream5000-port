import sys,pickle,re,os,json,collections
HERE=os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0,HERE)
from an import *
im=img()
D=pickle.load(open(os.path.join(HERE,'work','db.pkl'),'rb'))
funcs=D['funcs']; call_edges=D['call_edges']; data_refs=D['data_refs']
insn,calls,seeds=pickle.load(open(os.path.join(HERE,'work','disc2.pkl'),'rb'))
NAMES_PATH=os.path.join(HERE,'data','names.json')
def load_names():
    try: return json.load(open(NAMES_PATH))
    except Exception: return {}
names=load_names()
GN_PATH=os.path.join(HERE,'data','gnames.json')
def load_g():
    try: return json.load(open(GN_PATH))
    except Exception: return {}
gnames=load_g()
callers=collections.defaultdict(set)
for f,cs in call_edges.items():
    for c in cs: callers[c].add(f)
def fname(a):
    n=names.get("%x"%a)
    return n['name'] if n else "sub_%x"%a
def gname(a):
    n=gnames.get("%x"%a)
    return n if isinstance(n,str) else (n['name'] if n else None)
def strat(a):
    s=im.cstr(a,70)
    if s and len(s)>=3 and all(32<=c<127 or c in(10,13,9) for c in s): return s.decode().replace('\n','\\n').replace('\r','\\r')
    return None
HEXR=re.compile(r'0x[0-9a-f]{4,}')
def annot(i):
    a,sz,m,o,by=i
    note=[]
    if m in('call','jmp') or m.startswith('j'):
        mo=re.fullmatch(r'0x[0-9a-f]+',o)
        if mo:
            t=int(o,16)
            if m=='call': note.append(fname(t))
            return o,note
    for k in range(1,sz-3):
        ra=a+k
        if ra in im.relocs:
            t=im.relocs[ra]; s=strat(t) if t>=0x10000 else None
            g=gname(t)
            if g: note.append(g)
            elif s: note.append('"%s"'%s)
            elif t in funcs: note.append('&'+fname(t))
            else: note.append('@%x'%t)
    return o,note
def show(a,maxn=3000):
    if a not in funcs: print("not a function start; showing linear"); body=[a]
    else: body=sorted(funcs[a])
    nm=fname(a)
    print("; ==== %s @%x  callers:%s"%(nm,a,sorted(hex(x) for x in callers.get(a,[]))[:12]))
    prev=None
    for x in body[:maxn]:
        i=insn[x]
        if prev is not None and x!=prev: print("   ...")
        prev=x+i[1]
        o,note=annot(i)
        print("%06x  %-14s %-6s %-34s %s"%(x,i[4].hex()[:14],i[2],o,("; "+", ".join(note)) if note else ""))
if __name__=='__main__':
    for arg in sys.argv[1:]:
        show(int(arg,16)); print()
