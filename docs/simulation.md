# Simulation numerics (Phase 4)

Status tags: **CONFIRMED** (verified against data/arith), **INFERRED** (from reading engine code), **UNKNOWN**.

## Representation — CONFIRMED / INFERRED
| Quantity | Representation | Evidence |
|---|---|---|
| Angles | 16-bit, `0x10000` = 360°, `0x4000` = 90° | `MATHS.BIN` tables fit sin/asin/atan to 1 LSB (CONFIRMED) |
| Trig, matrix and normal components | signed **2.14** (`0x4000` = 1.0); products via `IMUL` + `SHRD/SAR 14` | `SlotMoveStep`, `ShapeDraw` point transform, MATHS.BIN |
| World coordinates | signed 32-bit integers (units: lattice cell = 2^20) | start positions, TRD positions, `sar 20` grid index |
| Track-record vertices | s16 stored `>> 6` | engine compares bbox `<< 6`; renders correctly (CONFIRMED) |
| Time | PIT-driven ms counter, 16.16 (`0x3E80000/rate`, rate ≤ 70 Hz) | `FrameClockInstall/Tick` |
| Frame delta | `ms` (1..1000), `secs` as **2.14** (`ms·16.384`), `fps = 1000/ms` | `FrameDeltaDerive` (INFERRED, read instruction by instruction) |
| Ship speed | 32.16 fixed in ship data (+0x0C dword, +0x10 word); slot speed dword with `speed*dt >> 14` | `RaceSlotMove`, `SlotMoveStep` |
| Floating point | optional x87 only for `sqrt` (`FILD/FSQRT/FISTP`); otherwise integer bit-loop; FPU control word = round-to-zero, 64-bit | `ISqrt`, `FpuInit` |
| Randomness | 3-word lagged-additive generator, seeded from BIOS tick; seed saved/restored for replays | `RandomNext`, `DoGame3D` |

## Timestep — INFERRED
Variable timestep: each loop iteration measures `delta` (clamped ≤ 1000 ms, ≥ one tick) and passes it (ms and 2.14-seconds) to every slot update. Speeds are per second. The clock quantum is ≤ 70 Hz, so the effective frame rate cap and `delta` granularity (~14 ms steps) are part of original behaviour. Replays store seed + per-frame control block; they only reproduce exactly if `delta` is reproduced too (UNKNOWN whether deltas are recorded).

## Ship dynamics (what is known)
`RaceSlotMove` (VA 0x51B0A), STRONGLY INFERRED:
```
throttle:   a = f0 - (f0 - f1) * (speed / f3)         (speed/f3 via IDIV into 16.16)
no throttle: a = -f2
a *= factor  (0x4000 = 1.0; +0x2000 while the +0x3E timer runs, -0x1000 while the +0x40 timer runs)
speed += a * dt(2.14 s);  speed <= f3 * factor
drag: v -= v * 4 * dt (per axis, 2.14 chains); lateral/vertical response uses f4/f5
```
Ten parameter blocks (7 × s32: f0..f6) live at exe VA 0x502F8 (file offset 0x8DB4C); example, ship 1: `71500, 35750, 128700, 289575, 19660 (1.2), 16384 (1.0), 244000`. The reimplementation reads them from the user's `SLIPSTRM.EXE` at run time (`src/game/ship_params.cpp`) and uses only the thrust relation.

## What the current reimplementation does (NOT original)
`src/game/ship_sim.cpp`: thrust law above; steering rate, braking, hover height (14 000 u) and "edge = wall" are **placeholders**. No pitch/roll, no collision response, no damage, no AI, no weapons.

## Unknown / next
* Steering model, banking, hover force, track-surface interaction (`0x51EC4`, `0x13474/0x134E6`).
* Meaning of f4–f6; boost/handicap factor sources; bonus effects.
* Collision cubes/faces and response; AI controller (`RaceAIControl`, 0x5135E).

## Validation plan (REVERSE-ENGINEERING VALIDATION)
Capture from the original running under DOSBox: 0→top-speed time per ship, top speeds, turn radius at several speeds, braking distance, wall bounce, lap times of an idle AI. Compare with `slipstream_tests`-style harnesses driving `stepShip` headlessly; keep original numeric formats (2.14, 32.16, integer ms) once the model is understood.
