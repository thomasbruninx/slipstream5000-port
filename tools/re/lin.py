import sys; sys.path.insert(0,'.')
from show import *
lo=int(sys.argv[1],16); hi=int(sys.argv[2],16)
prev_end=None
for x in [x for x in sorted(insn) if lo<=x<hi]:
    i=insn[x]
    if x in funcs: print("\n; --- %s @%x callers %s"%(fname(x),x,sorted(hex(c) for c in callers.get(x,[]))[:8]))
    elif prev_end is not None and x!=prev_end: print("   ...")
    prev_end=x+i[1]
    o,note=annot(i)
    print("%06x  %-12s %-6s %-36s %s"%(x,i[4].hex()[:12],i[2],o,("; "+", ".join(note)) if note else ""))
