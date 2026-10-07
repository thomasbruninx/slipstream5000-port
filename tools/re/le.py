import struct,sys
class LE:
    def __init__(s,path):
        d=open(path,'rb').read(); s.d=d
        # DOS4G/W bound exes: MZ stub then BW then LE; find "LE" header via e_lfanew chain
        import re
        for m in re.finditer(rb'LE\x00\x00',d):
            s.hdr=m.start()
            # find MZ whose e_lfanew points here
            ok=False
            for mm in re.finditer(rb'MZ',d):
                p=mm.start()
                if p+0x40<len(d) and p+struct.unpack_from('<I',d,p+0x3c)[0]==s.hdr: s.base=p; ok=True; break
            if ok: break
        h=s.hdr; u=lambda o:struct.unpack_from('<I',d,h+o)[0]
        s.pagesize=u(0x28); s.numpages=u(0x14); s.eip_obj=u(0x18); s.eip=u(0x1c)
        s.objtab=u(0x40); s.numobj=u(0x44); s.objpagemap=u(0x48); s.fixpage=u(0x68); s.fixrec=u(0x6c)
        s.datapages=u(0x80); s.lastpagebytes=u(0x2c); s.impmod=u(0x70); s.impproc=u(0x74)
        s.nummods=u(0x78)
        s.objs=[]
        for i in range(s.numobj):
            vs,base,flags,pmi,np,_=struct.unpack_from('<6I',d,h+s.objtab+i*24)
            s.objs.append(dict(vsize=vs,base=base,flags=flags,pmi=pmi,np=np))
    def pagefile(s,pg): # 1-based page -> file offset
        return s.base+s.datapages+(pg-1)*s.pagesize
    def object_bytes(s,i):
        o=s.objs[i]; out=bytearray()
        for k in range(o['np']):
            pg=o['pmi']+k
            pe=struct.unpack_from('<I',s.d,s.hdr+s.objpagemap+(pg-1)*4)[0]  # LE: 4-byte entries (hi 3 bytes page num)
            # LE page map entry: 3-byte page number (big-endian hi) + 1 byte flags
            b=s.d[s.hdr+s.objpagemap+(pg-1)*4:s.hdr+s.objpagemap+(pg-1)*4+4]
            pnum=(b[0]<<16)|(b[1]<<8)|b[2]
            ln=s.pagesize
            if pnum==s.numpages: ln=s.lastpagebytes
            off=s.pagefile(pnum)
            out+=s.d[off:off+ln].ljust(s.pagesize,b'\0')
        return bytes(out[:max(o['vsize'],0)])
if __name__=='__main__':
    l=LE(sys.argv[1])
    print("hdr@%x base@%x pagesize %d pages %d eip obj%d:%x datapages@%x"%(l.hdr,l.base,l.pagesize,l.numpages,l.eip_obj,l.eip,l.datapages))
    for i,o in enumerate(l.objs): print(i+1,{k:hex(v) for k,v in o.items()})

def fixups(l):
    """return list of (src_obj, src_off, type, tgt_obj, tgt_off) in object-relative terms"""
    d=l.d; h=l.hdr
    pt=[struct.unpack_from('<I',d,h+l.fixpage+4*i)[0] for i in range(l.numpages+1)]
    recbase=h+l.fixrec
    out=[]
    # map page -> (obj, page index in obj)
    pg2obj={}
    for oi,o in enumerate(l.objs):
        for k in range(o['np']): pg2obj[o['pmi']+k]=(oi,k)
    for pg in range(1,l.numpages+1):
        p=recbase+pt[pg-1]; end=recbase+pt[pg]
        oi,k=pg2obj[pg]
        while p<end:
            st=d[p]; fl=d[p+1]; p+=2
            stype=st&0xf
            if st&0x20: # source list
                cnt=d[p]; p+=1; srcs=None
            else:
                srcoff=struct.unpack_from('<h',d,p)[0]; p+=2; srcs=[srcoff]
            tt=fl&3
            if tt==0: # internal
                if fl&0x40: tobj=struct.unpack_from('<H',d,p)[0]; p+=2
                else: tobj=d[p]; p+=1
                if stype==2: toff=None # 16-bit selector
                elif fl&0x10: toff=struct.unpack_from('<I',d,p)[0]; p+=4
                else: toff=struct.unpack_from('<H',d,p)[0]; p+=2
                if stype==2: toff=0
            else:
                raise Exception("unhandled fixup target type %d at %x"%(tt,p))
            if srcs is None:
                srcs=[]
                for _ in range(cnt): srcs.append(struct.unpack_from('<h',d,p)[0]); p+=2
            for s in srcs:
                out.append((oi,k*l.pagesize+s,stype,tobj-1,toff))
    return out
