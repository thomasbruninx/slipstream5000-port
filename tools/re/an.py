import ctypes,struct,re,sys,os,collections,pickle
sys.path.insert(0,os.path.dirname(__file__))
from le import LE,fixups
EXE=os.environ.get('SLIP_EXE','/Users/thomasbruninx/Downloads/slip5000/SLIPSTRM.EXE')
CS=ctypes.CDLL(os.environ.get('CAPSTONE_LIB','/opt/homebrew/Cellar/capstone/5.0.9/lib/libcapstone.dylib'))
class Insn(ctypes.Structure):
    _fields_=[('id',ctypes.c_uint),('address',ctypes.c_uint64),('size',ctypes.c_uint16),('bytes',ctypes.c_uint8*24),
              ('mnemonic',ctypes.c_char*32),('op_str',ctypes.c_char*160),('detail',ctypes.c_void_p)]
CS.cs_open.argtypes=[ctypes.c_int,ctypes.c_int,ctypes.POINTER(ctypes.c_size_t)]
CS.cs_malloc.restype=ctypes.POINTER(Insn); CS.cs_malloc.argtypes=[ctypes.c_size_t]
CS.cs_disasm_iter.argtypes=[ctypes.c_size_t,ctypes.POINTER(ctypes.POINTER(ctypes.c_uint8)),ctypes.POINTER(ctypes.c_size_t),ctypes.POINTER(ctypes.c_uint64),ctypes.POINTER(Insn)]
CS.cs_disasm_iter.restype=ctypes.c_bool
h=ctypes.c_size_t(); CS.cs_open(3,1<<2,ctypes.byref(h))  # X86, MODE_32
_ins=CS.cs_malloc(h)

class Img:
    def __init__(s):
        s.le=LE(EXE); s.segs=[]
        for i,o in enumerate(s.le.objs):
            s.segs.append([o['base'],bytearray(s.le.object_bytes(i))])
        s.fix=fixups(s.le)
        s.relocs={}   # address -> target address
        for so,off,st,to,toff in s.fix:
            if st!=7: continue
            tgt=s.le.objs[to]['base']+toff
            s.relocs[s.le.objs[so]['base']+off]=tgt
            s.segs[so][1][off:off+4]=struct.pack('<I',tgt)
        s.bases=[(b,len(d)) for b,d in s.segs]
    def seg(s,a):
        for b,d in s.segs:
            if b<=a<b+len(d): return b,d
        return None
    def rd(s,a,n):
        sg=s.seg(a)
        if not sg: return None
        b,d=sg; return bytes(d[a-b:a-b+n])
    def u32(s,a):
        x=s.rd(a,4); return struct.unpack('<I',x)[0] if x and len(x)==4 else None
    def u16(s,a):
        x=s.rd(a,2); return struct.unpack('<H',x)[0] if x and len(x)==2 else None
    def u8(s,a):
        x=s.rd(a,1); return x[0] if x else None
    def cstr(s,a,maxn=200):
        sg=s.seg(a)
        if not sg: return None
        b,d=sg; i=a-b; j=i
        while j<len(d) and d[j]!=0 and j-i<maxn: j+=1
        return bytes(d[i:j])
    def dis1(s,a):
        sg=s.seg(a)
        if not sg: return None
        b,d=sg
        buf=(ctypes.c_uint8*(min(16,len(d)-(a-b)))).from_buffer_copy(bytes(d[a-b:a-b+16]))
        p=ctypes.cast(buf,ctypes.POINTER(ctypes.c_uint8)); sz=ctypes.c_size_t(len(buf)); ad=ctypes.c_uint64(a)
        if CS.cs_disasm_iter(h,ctypes.byref(p),ctypes.byref(sz),ctypes.byref(ad),_ins):
            i=_ins[0]; return (a,i.size,i.mnemonic.decode(),i.op_str.decode(),bytes(i.bytes[:i.size]))
        return None
    def dis(s,a,n):
        out=[]
        for _ in range(n):
            x=s.dis1(a)
            if not x: out.append((a,1,'db','?',b'')); a+=1; continue
            out.append(x); a+=x[1]
        return out
IMG=None
def img():
    global IMG
    if IMG is None: IMG=Img()
    return IMG
HEX=re.compile(r'0x[0-9a-f]+')
def fmt(i,names=None):
    a,sz,m,o,by=i
    return "%08x  %-16s %s %s"%(a,by.hex(),m,o)
