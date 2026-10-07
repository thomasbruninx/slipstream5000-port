import sys; sys.path.insert(0,'.')
from show import *
from xr import fs
def strs(a):
    return " | ".join(x[1].strip()[:40] for x in fs.get(a,[])[:3])
def tree(a,depth,maxd,seen,ind=0):
    print("  "*ind+"%x %s  %s"%(a,fname(a),strs(a)))
    if depth>=maxd: return
    for c in sorted(call_edges.get(a,[])):
        if c in seen: continue
        seen.add(c)
        if c in funcs: tree(c,depth+1,maxd,seen,ind+1)
if __name__=='__main__':
    tree(int(sys.argv[1],16),0,int(sys.argv[2]),set())
