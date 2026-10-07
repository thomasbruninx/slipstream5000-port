"""Harness: run the original RaceSlotMove/RaceSlotHover on a synthetic ship slot (research tool)."""
import sys, struct, pickle
sys.path.insert(0, '.')
from emu import CPU, EmuError, M32

F = pickle.load(open('/tmp/slip/F.pkl', 'rb'))
SLOTS = 0x00500000
SLOT_SZ = 0xAE
REC = 0x00540000      # ship (roster) record

def make_cpu():
    cpu = CPU()
    m = cpu.mem
    maths = F['MATHS.BIN']
    MB = 0x00400000
    m.write(MB, maths)
    hdr = lambda o: maths[o] | (maths[o + 1] << 8)
    m.w32(0x21198, MB + hdr(0))   # g_SinTable   (MathsInstall 0x211ce: header words +0 sin, +2 asin, +4 atan)
    m.w32(0x2119c, MB + hdr(2))   # g_AsinTable
    m.w32(0x21194, MB + hdr(4))   # g_AtanTable
    m.w32(0x26628, SLOTS)         # g_SlotsBase
    m.w16(0x26632, 4)             # g_SlotCount
    return cpu

def init_slot(cpu, idx=0, pos=(0, 0, 0), cls=1):
    m = cpu.mem
    base = SLOTS + idx * SLOT_SZ
    for i in range(SLOT_SZ): m.w8(base + i, 0)
    m.w16(base, 1)                      # in use
    for k, v in enumerate(pos): m.w32(base + 0x14 + 4 * k, v & M32)
    ident = [0x4000, 0, 0, 0, 0x4000, 0, 0, 0, 0x4000]
    for k, v in enumerate(ident): m.w16(base + 0x48 + 2 * k, v)
    m.w16(base + 0x5a, 0); m.w16(base + 0x5c, 0); m.w16(base + 0x5e, 0x4000)   # heading +z
    # ship data at +0x60
    d = base + 0x60
    m.w32(d + 0x20, REC)
    for off in (0x16, 0x18, 0x1a): m.w16(d + off, 0x4000)
    # record
    m.w16(REC, cls)
    m.w16(REC + 2, 2)
    return idx * SLOT_SZ  # slot id used as 'si'

if __name__ == '__main__':
    cpu = make_cpu()
    si = init_slot(cpu)
    cpu.mem.w32(0x12d6c, 16); cpu.mem.w32(0x12d70, 16 * 16384 // 1000); cpu.mem.w32(0x12d68, 60)
    try:
        cpu.call(0x51b0a, {'esi': si, 'eax': 1, 'ecx': 0, 'edx': 0})
        print('ok')
    except EmuError as e:
        print('ERR', e)
