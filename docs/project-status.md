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
* Sample rate of `.SMP`; `.ANN`, `.ZON`, `.FNT` layouts; `SLIPSTRM.CFG`/`.SAV` formats; championship/save data.
* Palette entries 248–255; `.SPR` header words; SHP sort tree.
* Menus, race rules, laps/checkpoints data (TRK cells?), weapons, pickups, AI.

## NEXT OBJECTIVES
1. Validate scale/handedness with the original under DOSBox (camera FOV, ship size vs road).
2. Decode TRK cells (checkpoints/lap distance) and use them for lap counting and a racing line.
3. Port `RaceSlotMove` faithfully (2.14 / 32.16 integer maths) + collision against track polygons; build a headless validation harness.
4. Audio: `.SMP`, `.HMP`→MIDI/FM synth via SDL3 audio.
5. Menus/championship (`.ST*`, `.FNT`, `.ZON`, save format), then weapons/AI.

## Milestones (docs/ task list)
A window/input/loop ✅ · B display original asset ✅ · C free-camera track viewer ✅ · D vehicles on track ✅ (static grid) · E basic movement ✅ (placeholder) · F original physics ⏳ · G collision ⏳ · H full race ⏳


## Physics / collision / doors (this phase)
* DONE (oracle-checked): ship speed, steering/bank/yaw, pitch, drag, speed->velocity (`ship_sim.cpp`, `tests/physics_tests.cpp`).
* DONE (geometry and response decoded and ported; damage/explosion not): ship-vs-track polygon sweep (`ship_collide.cpp`), controls E/Q pitch, F8 = hover assist.
* DONE (decoded, visual only): sliding doors (`doors.cpp`).
* OPEN: damage + explosion branch, ship-vs-ship (0x14620), ship-vs-door cube, damage/boost timers, AI.
