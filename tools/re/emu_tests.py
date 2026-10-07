import sys,math
sys.path.insert(0,'.')
from emu import CPU
cpu=CPU()
# ISqrt: eax -> eax?
for v in (0,1,1000000,123456789,0xFFFFFFFF):
    cpu.r=[0]*8; cpu.r[4]=0x00F00000
    try:
        cpu.call(0x233bd,{'eax':v})
        print('ISqrt',hex(v),'->',cpu.r[0], int(math.isqrt(v)))
    except Exception as e: print('ISqrt err',e)
