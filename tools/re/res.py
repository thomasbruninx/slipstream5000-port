import struct,sys
KEY=b'SOFTWAREREFINERY'
def parse(path):
    d=open(path,'rb').read()
    diroff=struct.unpack_from('<I',d,len(d)-4)[0]
    hdr=struct.unpack_from('<I',d,diroff)[0]
    n=hdr&0x7fffffff
    ents=[]
    p=diroff+4
    for i in range(n):
        flags=struct.unpack_from('<I',d,p)[0]
        nm=bytes(a^b for a,b in zip(d[p+4:p+20],KEY))
        off,size=struct.unpack_from('<II',d,p+20)
        ents.append((flags,nm,off,size)); p+=28
    return d,diroff,hdr,n,ents,p
if __name__=='__main__':
    d,diroff,hdr,n,ents,p=parse(sys.argv[1])
    print("size",len(d),"diroff",hex(diroff),"hdr",hex(hdr),"n",n,"parse end",p,"expect",len(d)-4)
    for e in ents[:int(sys.argv[2]) if len(sys.argv)>2 else 20]: print(e[0],e[1],hex(e[2]),e[3])
