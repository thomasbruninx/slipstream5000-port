import sys
from phys_emu import *
def s32(v): return v-(1<<32) if v&0x80000000 else v
def run(btn, steer, pitch, frames=90):
    cpu = make_cpu(); si = init_slot(cpu)
    m = cpu.mem
    m.w32(0x12d6c,16); m.w32(0x12d70,16*16384//1000); m.w32(0x12d68,60)
    base = SLOTS+si; d = base+0x60
    out=[]
    for f in range(frames):
        cpu.call(0x51b0a, {'esi':si,'eax':btn,'ecx':steer,'edx':pitch})
        cpu.call(0x26a97, {'esi':si}) if False else None
        if f%15==0:
            out.append((f, s32(m.r32(d+0xC)), m.r16(d+0x10), [s32(m.r32(d+4*k)) for k in range(3)],
                        [s32(m.r32(base+0x14+4*k)) for k in range(3)], [m.r16(base+0x5a+2*k) for k in range(3)]))
    return out
for btn in (1,2,4):
    print('btn',btn)
    for r in run(btn,0,0): print(r)
