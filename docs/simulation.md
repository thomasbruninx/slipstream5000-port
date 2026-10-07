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
covered (it is the midpoint of C and the open position, as decoded). Ship-vs-door: see "Door collision" below.

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

### AI ships (STRONGLY INFERRED from `RaceAIControl` 0x5135E / 0x51688 / 0x3B6D0 / 0x515D2 / 0x515F2 / 0x3544F; checked by running whole races headless)
**Path.** A chain of **nodes** in the TRD (list offset at header +8, count, 0x32 bytes each: +0 next, +2 previous,
+4 alternative route, +6 merge marker, +8 straightness (0x8000 = no turn), +0xC position, +0x18 width); every piece entry
points to its node (`+0x1E`). `InitRefuel` (0x3D568) gives a node the *pit* flag (`+0x28 = 0xFFFF`) when its alternative
route reaches the node of the piece that holds a `REFUEL 3` polygon before a merge node (only tracks with such a piece
have a pit route: Hawaii, Norway, Cave, Can, Amazon).
**Aim** (per frame, `ship_ai.cpp`): target node = node of the piece the ship is in, or the next one closer than 0x800
(length estimate `0x21F87` = max + (mid+min)/4); threshold `0x5F50 + speed*0x1E8/0x2CB/4 + 2.5*width`; the node window
advances when closer; the aim point lies on the segment to the previous node at `dist - threshold`; the direction is
rotated into the ship's bank-free frame and `steer = clamp(right*0x4000, +-0x800)*8`, `pitch` likewise (raw axes).
**Target speed** = `min(minSpeed[track] + speedRange[track]*(0x4000 - curvature)>>14, 214500)` (tables 0x5027A/0x502A2 read
from the exe; curvature = sum of `0x8000 - straightness` over the next 0x5F500 units, only when the node is within `f6`
= 244000 of the ship); AI ships are capped at 0x345E4 = 214500 (`record +0xD`); throttle when target >= speed.
**Neighbours** (`TrackSlotGetNeighbours`): for every other ship, same target node -> `ahead` if (its distance to the node +
extent) <= (mine - my extent), `behind` if >= (mine + my extent), else `alongside`; different node -> ahead/behind by the
sign of the dot product with my heading; nearest of each class by the length estimate.
**Avoidance** (after 3 s of start delay, `slot +0x4A = 0xBB8`): ahead ship within 0x17D40 and mine faster (`0x515D2`) and room
(`0x515F2`: lateral position (right, up) of the other in its node frame; `rest = width - (E_o - lat)` with extents + 0x1310;
needs `E_me <= rest`) -> lateral offset `(width + E_o - lat)/2` pointing away from it, applied to the aim point in the node
frame (right `(f.z,0,-f.x)`, up `f x right`), clamped to `width - extent - 0x988`; otherwise offsets are cleared (unless
someone is alongside) and the target speed is limited to the ahead ship's speed (+0x138D beyond 0x9880, -0x1BEE closer
than 0x5F50) when it is within 0xBEA0.
**Branches** (`TrackSlotCheckBranch`): at a split node (previous node has no alternative): pit node -> damaged ships (either
counter > 50) take the alternative route; ordinary split -> AI ships only, never rank 1 or the last two, never while
someone is alongside, with probability 0xA00/0x10000 (0x6000 when the ship behind is further than 0x77240).
**Doors:** a ship on a door's piece forces it to state 0 (opening) at speed 0x53CA (`0x35564`); the speed resets to 0x37DC
at the next flip.
**Per-ship speed factor** (`RaceSlotMove 0x51BF1..`): AI controlled ships multiply thrust and speed cap by
`tier[difficulty][track][tier]/0x4000` (tables 0x5409C, 4 sets x 10 tracks x 4 tiers, values 0.52..1.31); the tier comes
from the start position (`{0,0,1,1,1,2,2,3,3,3}` for positions 1..10, 0x586DB); the difficulty set ([0x47234], menu) is
UNKNOWN: the port uses the second set (`AiTables::difficulty`). Timers: boost `+0x3E` adds 0.5, slow `+0x40` subtracts 0.25.
**Trailing boost** (human ships only, `0x51C70`): last place and the ship ahead more than 0x595B0 away -> boost for 6 s
(`applyTrailingBoost`; the lap-dependent extra condition via `[0x5440E]`/`slot +0x1C` is not ported). The position table
`0x50252` (mode `[0x54404]`) and `[0x5040C]` (mode `[0x5441C]`) are mode specific and not ported.
Headless races: nine AI ships on all ten tracks run the line at 210-230k u/s with a few contacts; stuck situations found on
the way (a seam where the move fails the piece check) are handled by the 15-step bisection of `0x38C97`.

### Debris routine (wrecks), CONFIRMED by reading `0x3E8F2` / `0x3E9F5`
A second wall hit within 0x190 ms (see above) calls `0x3E8F2(speed*0.75/2, 1000 ms, 0xE000)`: slot speed = max(that, 0x1174C);
timer = 1 s + min(5 s, distance to the next node / 0x77240 s); random spin signs; flight direction = towards the next node
(the heading keeps steering towards it at 2/s); the original handler is saved and `0x3E9F5` installed. Per frame (flying):
tumble yaw 0xE000 units/s (0.875 turn/s) and pitch twice that, each rotation reverted if it collides; when the timer ends
the wreck is "landed" and moves at 0xE000 u/s towards the target with its orientation settling onto the direction. It
recovers (orientation re-aligned to the track direction by `0x2260A`, handler restored, ship speed = 0.75 * speed at the
crash, slide 0) when it leaves the piece it crashed in, touches a wall again (0x107 while landed) or another ship (0x106). Wall
hits while flying: speed/2 (>= 0x1174C), bounce 22.5 deg off the surface (`0x3BD82` with angle 0x1000), grazing hits (|h.n| <= 0x100) are ignored.
Port: `ShipState::wreck*`, `stepShipDynamics`, `shipHitResponse`, `updateWrecks`. Simplifications: the per-rotation collision
revert is replaced by the normal track sweep, and a 12 s safety timeout recovers wrecks that never leave their piece.

### Pit lane / refuel (CONFIRMED by 0x35E53, 0x3544F, 0x3BEA2, 0x3D568, 0x50E97, 0x39AE6; checked headless with `SLIP_DAMAGE=60 SLIP_PIT_LOG=1`)
* There is a pit on **every track** (the manual: "blue and white lighting flashes above or below the entrances of tunnels"). The refuel zone is one TRD piece, the one whose record has a polygon with the
  material "REFUEL 3" (`InitRefuel` 0x3D568 -> `[0x3D55C]`); a ship is in the pit when its slot piece is that piece (`TrackSlotCheckRefuel` 0x35E53). It always lies on an alternative route (`TrackNode::pit`
  marks the split that leads to it). **Bug fixed in this phase:** polygon materials are table *ids*, not indices; the port looked the name up by index and therefore found no refuel piece on Chicago, Tokyo,
  Amazon, London and New York (and a wrong one elsewhere). Now all ten tracks have one (Chicago piece 4, Hawaii 4, Tokyo 61, Norway 29, Cave 7, Can 9, Amazon 47, London 54, Egypt 31, New York 154).
* Inside, every frame (0x50E97..0x50EF1) both damage counters fall by 25 points/s and the booster fuel refills at 1/s; the player also hears the PITSLP loop.
* **AI decision** (0x3544F + 0x51390): the state is evaluated one node *before* the split: 1 = the previous node has an alternative, 2 = the next node (by the branch flag) is an ordinary split, 3 = it is a pit
  entrance. State 3: the flag becomes "either damage counter > 50". State 2: AI ships only, rank rules as before.
* **Following the branch**: `TrackSlotGetTarget` (0x3BEA2) uses the slot's branch flag: when it is set and the previous node of the ship's piece node has an alternative, the target node is that alternative
  (0x3BEE5). Before the fix the flag had no effect, so no AI ship ever took a branch. With 60 % damage on every ship all ten take the pit route on all ten tracks; a pass at full speed repairs about 17 points.
* **Piece lighting** (0x39427): while a piece is drawn the diffuse level (0x3333) and the ambient (0x0CCC) of the lighting law are multiplied by the piece light (TRD entry `+0x20`, 0..0x4000; many tunnel
  pieces are 0x800..0x1000, a few are 0). Textured polygons are not lit in the original, so only flat polygons change. `SoftwareRenderer::pieceLight`.
* **Pit flicker** (0x39AE6..0x39AF8, `[0x3D560] = -1` set by InitRefuel): for the refuel piece the light is replaced by a fresh random 14-bit value every time it is drawn (generator 0x3667B:
  `x = (x + 1) >> 1`, xor 0xB400 when a bit fell out, seed 0x5A4A). The port draws it once per frame (`SLIP_NOPITFLICKER=1` turns it off).

### Door collision (door slot server 0x3BF86, TrackInitDoors 0x3C813; `doors.cpp`, tested in `tests/physics_tests.cpp`)
* **Collider** (INFERRED mapping): the door slot gets a box = the panel rectangle (half extents |v0-v1|/2 and |v0-v3|/2, from the shape bounds that 0x26003 returns) with a thickness of +-0x7A0 along the panel normal (`ecx = -0x7A0`, `edi = 0x7A0` at 0x3C92D, `0x135CA`); collider class 2.
* **Contact, message 0x106** (0x3BFDB): if the other slot is a ship (slot flag +0xA0 & 4) the door is set to state 0 (opening) with speed 0x6FB8 (28600 u/s, twice the normal speed). The ship's own 0x106 handler runs the generic contact branch (flag tests for bonus / weapon fail): it is pushed away along the relative velocity by `max(1.5*|rel|, 0x37DC)`, loses 37.5 % speed when it was the rammer and takes (0, 4.0) damage - the same as a ship-ship contact with a standing ship. The port feeds the doors to the contact solver as static boxes that carry the panel velocity.
* **Door update, message 0x104** (0x3C00E): when the door's cube overlaps a slot at the start of the frame it opens (state 0, speed 0x37DC). Each move (0x3C17B): the panel is placed at the new position and the overlap test (0x13474) runs; a *closing* door that now overlaps a slot goes back to where it was and reopens (state 0, speed 0x37DC); an opening door pushes on (the contact message handles the ship). End stops flip the state as before.
* Missiles that reach a door are destroyed (their 0x106 handler); beams ignore it (not decoded further).

### Race rules (CONFIRMED by reading RaceUpdate 0x5A4EC, the game loop 0x59003.. and RaceSlotMove 0x51C10..0x51CDB; `ship_ai.cpp`)
* **Lap line.** TRD header `+4` / `+6` are the TRD offsets of two consecutive pieces A and B (`Track::lapPieceA/B`; on all ten tracks A holds path node 1 and B node 0).
  Every frame the slot's current piece is compared with the previous one ([record +0x28]). Moving **B -> A** is a forward crossing: the first one only sets
  the lap counter (record `+0x20`) to 1 (the grid lies before the line), later ones store the lap time (`+0xE` -> last / best lap `+0x16`) and add 1;
  the ship is finished when `laps - 1 >= total laps` (`[0x5440E]`), record `+0xD = 1`, finishing rank = number of finished ships. Moving **A -> B** sets `+0x22`:
  the next B -> A crossing is swallowed (driving back over the line). The port tracks the piece like the slot cell does (only linked pieces).
* **Clocks.** Race time `+0xE` and the lap timer run only after the countdown (`[0x54408] == 0`) and stop at the finish.
* **Ranks** (0x5A6C6..0x5A74F): finished ships keep their finishing rank; the others are sorted by `laps - (+0x22)` descending, then by `+0x1C` ascending, which is
  `3BD0D`: the distance still to go along the node chain (node `+0x24`, computed at load by 0x3BB14) plus the distance to the ship's piece node.
* **Race end** (0x5A74F..0x5A793, 0x59092): five seconds after the second AI ship finished (or when no AI ship is left racing) `[0x5440C]` expires and the race
  ends; unfinished ships keep their rank and get a projected finish clock (0x5A461: `clock + distance_to_go / route length * 90000 ms + (laps total - laps) * 90000 ms`, lap estimate table 0x5A4C4, all 90000), shown as "projected". WIN.HMP / LOSE.HMP and the result line play then.
* **Finished ships are autopiloted** (0x51111: record `+0xD` set -> RaceAIControl), the player's too.
* **Start phase**: during the first 15 s after GO (`[0x54404] = 0x3A98`) every ship's speed factor gains `table 0x50252[rank]` = 0.75, 0.6875, 0.625, 0.5, 0.375,
  0.25, 0.125, 0.0625, 0.03125, 0.0156 for ranks 1..10 (it multiplies thrust and the cap like the other factors). The tables 0x5040C / `[0x5441C]` belong to the
  intro demo only (set in PlayTrackIntro 0x57A79, cleared at 0x57FDC), `[0x54414]` / `[0x50254]` to a special mode: not ported.
* **Difficulty** `[0x49F04]` = `[0x492EA]` = the word at offset 157 of `SLIPSTRM.CFG` (the file is loaded at 0x4924D); 1 in the shipped config and in the executable default. It indexes the
  AI tier tables (0x5409C), the shop prices and doubles the blaster damage again at 2. The port reads the CFG (`--difficulty N` overrides).
  This confirms the earlier assumption of the second table.
* **Trailing boost** (0x51C70) corrected: it is not for the last place; it needs the human to be **exactly one place behind the best AI ship** (`[0x50438] + 1 == rank`,
  `[0x50438]` = best AI rank, 0x50446), the ship ahead more than 0x595B0 away, and not the final lap (the original's test of slot data `+0x1C` against 0x1DC90 is nearly always true).
* **Voice cues** wired from this: lap line -> position announcement, the human taking the place of the best AI ship (0x50BB4: pass line, table 0x50C03 or 0x50C2B at random),
  contact line (0x509A6, only when the high word of slide x is 2, which the original's code does by accident), finish line, result line, "under fire" / taunt cues of the weapon module.

* **Retired flag** (record `+0xC`): the only writer in the race code is the init that clears it (0x50568); nothing sets it in a single player race, so it is not ported. The node distance `+0x24` (0x3BB14) takes the shorter of the main and the alternative route at splits; the port uses the main route length (affects rank ties near the pit route only).

### Weapons, pickups and status effects
See `docs/weapons.md` (full tables and confidence labels). Ship-side effects live in `ShipState` / `stepShipDynamics`: reversed steering and pitch
(`+0x26`), speed cap / 2 (`+0x28`), throttle forced on (`+0x2A`), steering x16 (`+0x2C`, `+0x40`), booster gain (`+0x2E` / booster switch).

### Not done
* Pitch coupling to the ground.
* Mode-specific start tables (0x50252 / 0x5040C), the shop and the difficulty switch.
