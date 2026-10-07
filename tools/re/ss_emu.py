"""Oracle for slot-vs-slot collision: runs the original 0x14620 (box vs box sweep) on two synthetic collide slots."""
import sys, math, random
sys.path.insert(0, '.')
from phys_emu import *
COL = 0x560000
def s16(v): return v - 65536 if v & 0x8000 else v
def u16(v): return v & 0xffff

def mat_yaw_pitch(yaw, pitch=0.0):
    # rows right, up, forward (world), as the port's matrix; heading = forward
    cy, sy, cp, sp = math.cos(yaw), math.sin(yaw), math.cos(pitch), math.sin(pitch)
    f = (sy * cp, sp, cy * cp)
    r = (cy, 0, -sy)
    u = (f[1] * r[2] - f[2] * r[1], f[2] * r[0] - f[0] * r[2], f[0] * r[1] - f[1] * r[0])
    return [r, u, f]

def setup(cpu, idx, pos, M, speed, box, extent):
    m = cpu.mem
    base = SLOTS + idx * SLOT_SZ
    for i in range(SLOT_SZ): m.w8(base + i, 0)
    m.w16(base, 1)
    for k in range(3): m.w32(base + 0x14 + 4 * k, int(pos[k]) & M32)
    flat = [int(round(M[r][c] * 16384)) for r in range(3) for c in range(3)]
    for k, v in enumerate(flat): m.w16(base + 0x48 + 2 * k, u16(v))
    for k in range(3): m.w16(base + 0x5a + 2 * k, u16(int(round(M[2][k] * 16384))))
    m.w32(base + 0x2c, int(speed))
    rec = COL + idx * 0x58
    for i in range(0x58): m.w8(rec + i, 0)
    m.w16(base + 0xc, rec - COL)
    m.w16(rec + 0x40, idx * SLOT_SZ)
    m.w16(rec + 0x44, 2)
    for k in range(6): m.w32(rec + 0xc + 4 * k, int(box[k]) & M32)
    m.w32(rec + 0x24, int(extent))
    return rec

def box_extent(box):
    xs = [box[0], box[3]]; ys = [box[1], box[4]]; zs = [box[2], box[5]]
    best = 0
    for x in xs:
        for y in ys:
            for z in zs: best = max(best, math.sqrt(x * x + y * y + z * z))
    return best

def run(posA, yawA, spdA, posB, yawB, spdB, box, dt=0.016):
    cpu = make_cpu(); m = cpu.mem
    m.w32(0x12d6c, 16); m.w32(0x12d70, int(dt * 16384)); m.w32(0x12d68, 60)
    m.w32(0x12f80, COL)
    POOL = 0x570000   # face-point pool (0x133BE): 0x20 nodes of 0x1c bytes in a circular list (+0x14 next, +0x18 prev)
    m.w32(0x12fd0, POOL); m.w32(0x12fd4, POOL)
    for k in range(0x20):
        n = POOL + 0x1c * k; nx = POOL + 0x1c * ((k + 1) % 0x20)
        m.w32(n + 0x14, nx); m.w32(nx + 0x18, n)
    ext = box_extent(box)
    A = setup(cpu, 0, posA, mat_yaw_pitch(yawA), spdA, box, ext)
    B = setup(cpu, 1, posB, mat_yaw_pitch(yawB), spdB, box, ext)
    m.w32(A, int(spdA * dt)); m.w32(B, int(spdB * dt))
    m.w32(0x130c8, A); m.w32(0x130f8, B)
    cpu.call(0x14620, {})
    hit = bool(cpu.cf)
    return hit, m.r32(0x13220), m.r32(0x12ff0)

if __name__ == '__main__':
    box = (-3596, -1055, -5361, 3596, 1055, 5361)
    print(run((0, 0, 0), 0, 100000, (0, 0, 20000), 0, 0, box))
    print(run((0, 0, 0), 0, 100000, (0, 0, 40000), 0, 0, box))
