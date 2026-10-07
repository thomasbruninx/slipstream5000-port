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

## Emulator oracle (tools/re/emu.py, phys_emu.py, phys_run*.py)

An x86-32 integer interpreter runs the original `RaceSlotMove` (0x51B0A) against a synthetic slot. MATHS.BIN is loaded
and the table pointers are set from its header exactly as `MathsInstall` (0x211CE) does (header words +0 sin, +2 asin,
+4 atan). The ship record's class word must be 1..9 (index into the parameter table at 0x502CC). The C++ port is
checked against the emulator in `tests/physics_tests.cpp` (no game data needed).

### Ship dynamics (CONFIRMED against the oracle, implemented in `src/game/ship_sim.cpp`)
Per frame (`dt` = 2.14 seconds `dtq = floor(dt*16384)`), controls `CX` steer, `DX` pitch (both +-0x4000, keyboard ramps
them at 4.0/s: `sub_59A05`), `EAX` bit 0 = accelerate:
1. **Speed** (slot data +0xC, units/s): thrust law of the 7-dword parameter block (`f0 - (f0-f1)*v/vtop` with throttle,
   `-f2` without), capped at `vtop`.
2. **Orientation**, rows of the 3x3 slot matrix = right, up, forward (world):
   * `roll = Atan2Matrix(snapshot)` = atan2(-m[1], m[4]); target bank = steer/2 (45 deg at full steer);
     `bank delta = (target - roll)*dtq >> 12` (about 4/s exponential approach);
   * `yaw delta = hi16(dtq*steer) + hi16(dtq*(-m[1]))` (the bank feeds back into the turn rate), scaled by block[5];
   * `pitch delta = hi16(dtq*pitch)` scaled by block[4] (1.2); blocked beyond |forward.y| > 0.8125;
   * applied through `sub_2708F` = pitch (rows up/forward), roll (right/up), yaw (x/z of every row), then `sub_23003`
     re-orthonormalises.
3. **Drag** on the slide velocity (slot data +0..+8): `v -= v*4*dt`.
4. **Speed -> velocity** (`RaceSlotHover` 0x51EC4, name is historical: it does *not* hold height): world velocity =
   forward * speed * (0x2C00 + 0x1000 + 0x200 - clamp(m[7])/8 - |m[1]|/32)/0x4000 + slide velocity; the length goes to
   slot +0x2C and the normalised direction to the heading (+0x5A..), which `SlotMoveStep` integrates.
There is **no gravity and no height control** anywhere in the ship callback: the matrix only changes through the
controls, the pitch is manual (arrow keys up/down in the original: `PauseMenu` 0x59904 maps them to DX). Results of the
oracle: full right steer, full throttle, 61 frames at dt = 262/16384 s give speed 65756 and forward vector
(9326, 0, 13471)/16384; the port reproduces the matrix within 1 % and the final velocity length (62268) within 0.5 %.

### Track collision (STRONGLY INFERRED structure, geometry CONFIRMED; response PLACEHOLDER)
* `RaceSlotMove` ends with `134BC` (save position + matrix), the move, then the collision query `0x13474` and, when
  it reports a hit, `134E6` (restore). `CollideStep` (0x137A2, once per frame from `DoGame3D`) runs sub-steps of
  a slot-vs-slot solver (cube vs cube, `0x14620`) and calls the track callbacks registered by `TrackInstall`
  (`CollideSetTrackCallbacks`): `0x38C97` (sweep, below), `0x38E84` (post-step bisection), `TrackSlotCheckStatic`
  (0x34B97 -> 0x391CC), `0x35642` (segment test, records the hit material in `[0x339A2]`), `TrackSlotsCheckLOS`.
* A ship's track collider (`TrackSlotAdd` 0x348D0) is the box of its ART model (`ArticSlotGetExtent`/`0x11A88`,
  x made symmetric: `[-maxX, minY, minZ]..[maxX, maxY, maxZ]`), eight corners rotated by the slot matrix.
* Membership (`0x391CC`, per-corner piece lookup `0x36E57` -> `0x37E7C/0x383F8`): every corner must lie in a piece;
  the slot version of the cell test (`0x37FCC`) is the camera test (bbox + inner side of every list-A polygon without
  flag 0x40, `0x38524`) with a slack of **0x200** instead of 0x100. (This matters: Chicago has a non-portal
  "Trench Ent" quad about 1000 units inside the next piece; with 0x100 a thin slab belongs to no piece.)
* Sweep (`0x39054` -> `0x3CAEA`): for each corner (4 when heading is within 0x3FF0 of the last one, else 8) the
  polygons of the corner's piece and of the neighbours reached through the three portal links are tested (flags & 0x41
  == 0): the movement must run against the normal (cos >= 0x10/0x4000), the corner must be in front of the plane, the
  plane is reached 0x1E8 (488) units early (margin) and the hit point must lie inside the polygon (`0x36CEC`). The
  nearest hit shortens the step (`[0x33E04]`); the polygon normal and material are passed to `0x1394E`, which records
  the collision (type 3) in the collide slot for the response.
* The port (`src/game/ship_collide.cpp`) implements exactly this geometry.
* **Response (CONFIRMED by reading the code, bounce direction checked against the emulator).** `0x1394E` stores a
  type-3 record in the collide slot; `CollideStep` -> `0x144F0` -> `0x145B6` sends the ship's slot message **0x107**
  (normal, material, hit position). The handler (`RaceSlotControl` 0x50A64..0x50B77): effects/sound (impact above
  0x22E98 u/s is "hard"; `WATE*` material = splash), **speed *= 0.75** (`mul 0xC0000000`), then
  `slide += U * speed` with `U = cos(0x3000)*unit(heading - n(heading.n)) + sin(0x3000)*n` (sub `0x3BD82`: 67.5 deg
  off the surface; heading parallel to n gives U = n), `RaceSlotHover` recomputes the velocity and
  `RaceSlotDamage(0x20000, 0x10000)` is called. `CollideStep` then carries on with the remaining time (up to 0x20
  sub-steps) using the new velocity. The position is NOT slid along the plane. Ported in `shipHitResponse` /
  `stepShip` (tests: bounce vector vs the emulator). Not ported: damage, effects, and the branch taken when a hit
  arrives while ship data `+0x12` (set to 0x190 by the first hit, counts down in ms) is non-zero: it zeroes the
  ship's timers/velocity, spawns an explosion (`0x3E8F2`) and installs the dead-ship handler `0x51293` (so
  repeated hits within 400 ms look fatal; the port limits hit messages to one per 1/60 s and never explodes).
  F8 still switches to the old hover assist.

### Doors (STRONGLY INFERRED, see research-log): `src/game/doors.cpp`
Door polygons = list-A polygons with file flag 0x20 (Dummy portal quads). `FindDoors` 0x3C324 derives a record per door
(slide direction s = -snap(v0 - v3), centre C = (v0+v2)/2, b = |v0-v3|/2, open = C + s(2b - b/16), closed = (C+open)/2);
the door slot server 0x3BF86 runs a constant 0x37DC u/s state machine between closed (+0x30) and open (+0x24) with
state flips at both ends (no pause). The panel is the Dummy quad's rectangle textured with the track's `DOORS` material
(105x46 texture, 197x154 in Egypt) drawn with the piece. The closed position leaves half of the Dummy quad's span
covered (it is the midpoint of C and the open position, as decoded). Ship-vs-door (collision cube +-0x7A0) is not ported.

### Ship-vs-ship (STRONGLY INFERRED structure, response CONFIRMED by reading; narrow phase substituted)
* `CollideStep` -> `0x15A46` loops over slot pairs whose collider has flag 2 and calls `0x14620`: broad phase =
  centre distance <= extent(A)+extent(B)+travelled(A)+travelled(B) (`[slot+0x24]` extents and `[slot+0]` distances),
  then a cube-vs-cube contact generator (`0x1497B` = `0x16454, 0x14B6C, 0x14DC0, 0x15008, 0x1525C, 0x156F8, 0x154A4`,
  not decoded). `0x149A1` keeps the earliest pair (fraction `[0x12FAA]`), `0x14A16` writes the records: **normal =
  unit(vA - vB) for B and its negation for A (not geometric)**, `+0x3C` = |vA - vB|, contact point `+0x2C`.
  `0x1656B` decides who is the rammer (`+0x38 = -1`): with one slot treated as standing, the one whose own motion
  still produces the collision; if neither alone does, both. `0x1453C` sends message **0x106** to each ship.
* Ship handler (`RaceSlotControl` 0x50832..0x50A57; bonuses are the other branch of 0x106, tested through `0x26FB1`
  bit 0): rammer: speed *= 0.625; `slide += normal * max(1.5*|rel|, 0x37DC)`; RaceSlotHover; `RaceSlotDamage(0x40000
  (+0x20000 for the rammer), ...)`. Weapon/bonus effects and sounds are skipped.
* **Contact generator checked against the original (oracle `tools/re/ss_emu.py`, CONFIRMED equivalent for
  separated boxes).** The emulator runs `0x14620` on two synthetic collide slots (face-point pool `0x133BE`
  initialised by hand). Result: for boxes that do not overlap at the start the carry flag/`[0x13220]` equal the plain
  time of impact of the two oriented boxes (ART box, rotation fixed during the frame): 193 of 193 random separated
  configurations agree on hit/no-hit and on the contact distance within 6 units (integer rounding), e.g. two
  5361-half-length boxes 11500 apart report 778 = 11500 - 10722. Boxes that already overlap are reported as no contact in
  ~87 % of the cases (14 of 107 random overlapping starts still produce a hit through feature crossings); the port
  treats "overlapping at the start" as no contact. The seven routines of `0x1497B` were therefore not transcribed; they
  implement the same sweep with face/edge tests on 16.16 integers. (The comparison also caught a scaling bug in the
  port's first SAT test.)
* Port: `resolveShipPairs`/`shipPairResponse` (`ship_sim.cpp`): the earliest pair contact of the remaining time is
  found (SAT sweep, 256 samples + bisection), every ship is advanced to it, the 0x106 response is applied, then all
  ships continue with the remaining time and new velocities (up to 4 passes, like the CollideStep loop). Other grid
  ships are simulated so the player can shove them.

### Damage (CONFIRMED from `RaceSlotDamage` 0x52035 / RaceSlotHover / RaceSlotMove; checked with the oracle)
Two accumulators in the ship record, 16.16 (the port uses 0..100): **A (`+0x2A`) lowers the speed**: the hover factor
term `0x1000 - 0x28*A` shrinks (at 50: 0.9686 -> 0.8275); **B (`+0x2E`) degrades steering**: both rotation gains lose
`0x51*B>>16`. `RaceSlotDamage(a, b)`: ignored while the 3 s immunity (`slot data +0x24 = 0xBB8`) runs; any non-zero damage
starts it; values are clamped at 0; if a sum exceeds 100 only effects fire and nothing is stored. Sources: wall hit
(2.0, 1.0), ship contact (0, 4.0; the rammer 2.0, 6.0). Port: `shipDamage`, unit-tested (oracle: damage 50/50 -> speed 54246,
matrix as the emulator). The wall-hit "second hit within 0x190 ms" branch is implemented: the ship is wrecked (tumbling
debris, no controls; the original hands it to the debris mover `0x3E9F5`, which follows the track centre line - not
ported) and half its speed is kept.

### AI ships (STRONGLY INFERRED from `RaceAIControl` 0x5135E / 0x51688; behaviour checked by running whole races headless)
The path is a chain of **nodes** in the TRD (list offset at header +8, count, 0x32 bytes each: +0 next, +2 previous,
+4 alternative (refuel) route, +8 straightness (0x8000 = no turn), +0xC position, +0x18 width); every piece entry points
to its node (`+0x1E`). Per frame (port: `ship_ai.cpp`): the target node is the node of the piece the ship is in, or the
next one when closer than 0x800 (estimated length `0x21F87` = max + (mid+min)/4); the look-ahead threshold is
`0x5F50 + speed*0x1E8/0x2CB/4 + 2.5*width`; when the node is closer the node window advances; the aim point lies on the
segment to the previous node at `(dist - threshold)` from the node (`0x517E7`); the direction is rotated into the
ship's *bank-free* frame (matrix with the roll terms zeroed, re-orthonormalised) and `steer = clamp(right*0x4000,
+-0x800)*8`, `pitch = clamp(up*0x4000, +-0x800)*8` (raw axes, no keyboard ramp); throttle when the target speed >= speed.
Target speed = `min` of: `minSpeed[track] + speedRange[track]*(0x4000 - curvature)>>14` (tables at 0x5027A/0x502A2, read
from the user's exe; curvature = sum of `0x8000 - straightness` over the next 0x5F500 units; only when the node is within
the ship's `f6` = 244000) and 214500 (AI cap `0x345E4`). Damaged ships (A or B > 50) take the alternative route.
Headless races: 9 AI ships complete laps on all ten tracks at 214-230k u/s with few contacts. Not ported: lateral
avoidance of other slots (record `+0x42/+0x46`, neighbours `0x3B6D0`), doors (`TrackSlotFindDoor`), random branch choice,
rubber-band caps, pit logic.

### Not done
* Ship-vs-ship collision (`0x14620`, OBB solver), damage and the explosion branch of the wall-hit handler.
* Pitch coupling to the ground, damage (`RaceSlotDamage`), boost/slow timers (+0x3E/+0x40), AI (`RaceAIControl`).
