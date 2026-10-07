from phys_emu import *
def s32(v): return v-(1<<32) if v&0x80000000 else v
def s16(v): return v-65536 if v&0x8000 else v
def run(btn, steer, pitch, frames, step):
    cpu = make_cpu(); si = init_slot(cpu); m = cpu.mem
    m.w32(0x12d6c,16); m.w32(0x12d70,16*16384//1000); m.w32(0x12d68,60)
    base=SLOTS+si; d=base+0x60
    for f in range(frames):
        cpu.call(0x51b0a, {'esi':si,'eax':btn,'ecx':steer,'edx':pitch})
        if f%step==0:
            print(f, 'spd',s32(m.r32(d+0xC)),'head',[s16(m.r16(base+0x5a+2*k)) for k in range(3)],
                  'mat',[s16(m.r16(base+0x48+2*k)) for k in range(9)],
                  'vel',[s32(m.r32(d+4*k)) for k in range(3)])
print('full right'); run(1, 0x4000, 0, 61, 10)
print('half'); run(1, 0x2000, 0, 61, 10)
