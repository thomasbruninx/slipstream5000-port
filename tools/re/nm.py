#!/usr/bin/env python3
"""usage: nm.py add ADDR NAME CONF 'evidence'   (CONF=high|medium|low)
          nm.py g ADDR NAME 'note'   -> global/data label"""
import json,sys,os
HERE=os.path.dirname(os.path.abspath(__file__))
P=os.path.join(HERE,'data','names.json'); G=os.path.join(HERE,'data','gnames.json')
def load(p):
    try: return json.load(open(p))
    except Exception: return {}
if __name__=='__main__':
    if sys.argv[1]=='add':
        n=load(P); n["%x"%int(sys.argv[2],16)]=dict(name=sys.argv[3],conf=sys.argv[4],evidence=sys.argv[5] if len(sys.argv)>5 else ''); json.dump(n,open(P,'w'),indent=1,sort_keys=True)
    elif sys.argv[1]=='g':
        g=load(G); g["%x"%int(sys.argv[2],16)]=dict(name=sys.argv[3],note=sys.argv[4] if len(sys.argv)>4 else ''); json.dump(g,open(G,'w'),indent=1,sort_keys=True)
