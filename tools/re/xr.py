import sys,pickle,re,collections,os
sys.path.insert(0,os.path.dirname(os.path.abspath(__file__)))
from an import *
D=pickle.load(open(os.path.join(os.path.dirname(os.path.abspath(__file__)),'work','db.pkl'),'rb'))
im=img()
def is_str(a):
    s=im.cstr(a,120)
    return s if s and len(s)>=3 and all(32<=c<127 or c in(10,13,9) for c in s) else None
fs=collections.defaultdict(list)
for t,fr in D['data_refs'].items():
    s=is_str(t)
    if s:
        for f,adr in fr.items(): fs[f].append((t,s.decode()))
if __name__=='__main__':
    for f in sorted(fs):
        print("%x: "%f+" | ".join("%s"%x[1][:50] for x in fs[f][:6]))
