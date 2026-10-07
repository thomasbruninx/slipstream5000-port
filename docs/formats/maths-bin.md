# `MATHS.BIN` — CONFIRMED (SLIPCD.RES only)
Header: three u16 offsets 6, 0x4008, 0x800A. Table 0: sine quarter wave, 8193 × u16 (0..0x4000 = 2.14). Table 1: arcsine, 8193 × u16, angle 0..0x4000 (0x4000 = 90°). Table 2: arctangent of i/4096, 4097 × u16, angles in 0x10000-per-turn units (0..0x2000). Max error vs libm = 1 LSB. Angles are 16-bit, 0x10000 = 360°.
