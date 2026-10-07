"""Linear-sweep disassembly of a VA range from the exe: python3 dis_range.py 0x515d2 0x51688"""
import sys
sys.path.insert(0,'.')
import an
im=an.img()
lo=int(sys.argv[1],16); hi=int(sys.argv[2],16)
a=lo
while a<hi:
    x=im.dis1(a)
    if not x: print('%06x ??'%a); a+=1; continue
    _,size,mn,ops,b=x
    print('%06x  %-14s %s %s'%(a,bytes(b).hex()[:14],mn,ops))
    a+=size
