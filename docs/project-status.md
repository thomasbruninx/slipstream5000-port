# Project status (updated 2026-10-07)

## WORKING
* **Native macOS app (arm64, SDL3)**: `build/bin/slipstream`, bundle `dist/Slipstream.app` via `scripts/package_macos.sh`. Software renderer, ~120+ fps at 960×540.
* Reads the user's original installation (`SLIPSTRM.RES`/`SLIPCD.RES`, loose-file fallback); **no original assets are in the source tree or the app**.
* **Track viewer** (all 10 tracks): original geometry, textures, palette, scenery shapes, ships on the start grid. Free camera (WASD/QE, mouse look, Shift fast, `[` `]` change track, Esc quits). Gamepad: sticks move/look.
* **Shape viewer** (F2): all 280 `.SHP` files, orbit camera. **Sprite viewer** (F3): all 886 `.SPR`.
* **Track rendering follows the original's structure**: portal visibility walk, per-record class masks, BSP back-to-front painter order with per-group draw-order trees, billboards, size cull, back-face culling, polygon flags (portal/hidden). See `docs/research-log.md` (latest sessions) and `docs/formats/trk.md`. F5/F6/F7 toggle the parts for comparison.
* **Ship shadows** (the original's "translucent" decal path): ships project onto up-facing track polygons along the light direction; `SLIP_NOSHADOW=1` disables. Doors (sliding DOORS.SPR panels in Dummy 0x21 portals) are decoded but not implemented.
* **Drive demo** (Space or `--drive`): any of the 10 ships hovers on the track surface with placeholder handling (original thrust law, everything else invented).
* Tools: `slipstream_inspect`, `tools/re/` (disassembly workbench, `validate_assets.py` 30 checks), CTest `slipstream_tests` (skipped without game data).

## PARTIALLY UNDERSTOOD
* Track formats: placement (TRD pieces) and geometry (TRC) are right; TRK cells, TRD auxiliary lists and a few unplaced TRC records are not understood.
* Materials (numeric flags), transparency (index 0 assumed transparent; `Dummy` material = invisible portal polygons (INFERRED); indices 248–255 use placeholder colours), lighting (simple directional light on shade ramps).
* Ship assembly from `.ART` (parts/offsets) and ship display scale (×2 default, chosen visually against the road's start boxes; INFERRED); model front = +z (from ART reference points); track handedness (mirror) UNVERIFIED.
* Physics: thrust law known; the rest of `RaceSlotMove`, collision, AI, weapons only surveyed.
* Timing model (variable timestep, ≤70 Hz clock) INFERRED, not measured.

## UNKNOWN
* `.ANN`, `.ZON`, `.FNT` layouts; `SLIPSTRM.CFG`/`.SAV` formats; championship/save data.
* Palette entries 248–255; `.SPR` header words; SHP sort tree.
* Menus, race rules, laps/checkpoints data (TRK cells?), weapons, pickups, AI.

## NEXT OBJECTIVES
1. Validate scale/handedness with the original under DOSBox (camera FOV, ship size vs road).
2. Decode TRK cells (checkpoints/lap distance) and use them for lap counting and a racing line.
3. Port `RaceSlotMove` faithfully (2.14 / 32.16 integer maths) + collision against track polygons; build a headless validation harness.
4. ~~Audio~~ DONE: `.SMP` effects + `.HMP` music via SDL3 + FluidSynth (docs/audio.md); more triggers to wire.
5. Menus/championship (`.ST*`, `.FNT`, `.ZON`, save format), then weapons/AI.

## Milestones (docs/ task list)
A window/input/loop ✅ · B display original asset ✅ · C free-camera track viewer ✅ · D vehicles on track ✅ (static grid) · E basic movement ✅ (placeholder) · F original physics ⏳ · G collision ⏳ · H full race ⏳


## Physics / collision / doors (this phase)
* DONE (oracle-checked): ship speed, steering/bank/yaw, pitch, drag, speed->velocity (`ship_sim.cpp`, `tests/physics_tests.cpp`).
* DONE (geometry and response decoded and ported; damage/explosion not): ship-vs-track polygon sweep (`ship_collide.cpp`), controls E/Q pitch, F8 = hover assist.
* DONE (decoded): sliding doors including the collision cube, blocked-door reopen, contact opening at 0x6FB8 (`doors.cpp`).
* DONE (oracle-verified): ship-vs-ship contact (TOI), frame re-simulation, damage model, wreck on a second hit.
* DONE (decoded, behaviour-checked): AI ships (`ship_ai.cpp`), F9 toggle.
* DONE (decoded, behaviour-checked): AI avoidance, doors, pit/random branches, tier speed factors, trailing boost, wreck/debris handler with recovery.
* DONE: lap line / finish / ranks / race end / start bonus / difficulty from the CFG (docs/simulation.md "Race rules"). OPEN: exact node distance at route splits, special modes ([0x54414], intro demo tables).


## Sound (this phase)
* DONE: SDL3 audio stream + software mixer + FluidSynth MIDI with `--soundfont` (default bundled GeneralUser GS); `.SMP` = 8-bit unsigned mono 11025 Hz; `.HMP` -> SMF converter (120 ticks/s); Fx id table and positional rule from the original; engine loop, wall/contact/wreck effects, race music; `.app` bundles the soundfont and FluidSynth + dylibs.
* DONE: HMI loop points, race countdown with announcer + ENGSTART, water hit, ambient loops (refuel / crowd), win/lose music at the finish, laps/finish HUD.
* DONE: weapon / pickup / hit sounds, pilot and announcer voice cues (see below). OPEN: menu sounds, the TV-camera jet-by, intro/menu music flow, start speed-bonus table 0x50252.


## Weapons, pickups, voices (this phase; details in docs/weapons.md)
* DONE (decoded from the original, unit-tested with synthetic ships): the 12-weapon table, energy pools / ammo / cooldown, cycling, lock-on cone, beams (Blaster, Disrupter), homing missiles (Frag, Super Frag, Seeker, Super Seeker, Ambler, Hyper Neuro, Bomber, Scrambler), Mini Mines, Smoker (cosmetic), victim damage and status effects (reversed controls, half speed cap, jammed throttle, hypersensitive steering, booster), boosters, pit repair, pickups of all six types with the per-track placement tables, AI weapon use with the original loadouts.
* DONE: pilot / announcer voice cues with the original busy / no-repeat rules; `F` fire, `X` next weapon; HUD line, lock marker, sprites for pickups and explosions.
* OPEN / SPECULATIVE: particle effects (sprites used instead), beam colours, owner-vs-projectile contact rule, shop / loadout selection (default = the cheat loadout), contact and passing voice lines, difficulty-dependent blaster damage.
