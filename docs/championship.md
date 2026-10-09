# Championship mode (main loop mode 0x13, `0x559D8`..`0x55DE9`)

Everything below is read from the executable unless marked INFERRED / SPECULATIVE. Port: `src/game/championship.*` (rules), `frontend_champ.cpp`
(calendar loop, positions, saved games), `frontend_reporters.cpp` (reporter scenes), `viewer_intro.cpp` (TV fly-through), `original_formats/ann.*`
(scripts), tests `tests/champ_tests.cpp`, `tests/ann_tests.cpp`.

## Rules
* Calendar `0x54E10`: **Arizona, Chicago, Amazon, London, Norway, Egypt, France, Hawaii, Tokyo, New York** (track numbers 6,1,7,8,4,9,5,2,3,10); the number of
  races is `{6, 8, 10}[difficulty]` (`0x543E4`) - the first 6 / 8 / 10 entries. The same order is the track list of single races and the unlock order.
* Roster: the human plus nine AI ships, 750 money each (`0x58503`, `0x5860D`), 0 points. The human starts with turbo item 0 (Delphine Injection).
* After a race (`0x561CF`) every finisher gets points by place `10, 6, 4, 3, 2, 1, 0...` (`0x556E8`) and prize money `650, 450, 350, 200, 100, 50, 0...`
  (`0x556FC`); the championship position is then given out 1..10 to the unranked driver with the most points, **ties going to the later roster entry**
  (`0x56217`: only smaller values are skipped). Roster order: the human first (INFERRED from 0x5854F being called first).
* The next race's starting grid is the reversed result: record `+0x24 = 11 - place` (`0x55DC8`), start slot = `+0x24 - 1` (`RaceInitRacer 0x593CC`). Race 1 uses
  the port's single race grid (ship number = slot); the original's first grid is INFERRED to be the same as in a single race.
* Loop per race: `573B7(track, 0)` reporters -> `PlayTrackIntro` (fly-through) -> music -> `573B7(track, 1)` reporters -> garage (money and parts carry over; SPECULATIVE:
  weapons are not refilled for free - the port keeps what the driver bought, the original's roster fields are not written back during a race) -> race -> results
  `0x5A820` -> records `0x422EC` -> points `0x561CF` -> **Championship Positions** `0x5623E` (WIN.HMP; buttons Save Game / Continue; Esc opens the save dialog) or after the
  last race **Final Positions** `0x56FF0`. Saving returns 0 (`0x536BD`) and the championship goes on with the next race; Cancel returns to the positions.
* Quitting a race ends the championship. Replay of the results screen is not offered in the championship (the port has no replay).

## Screens (layouts CONFIRMED, see docs/frontend.md for the common frame / button routine `0x56137` / `0x5609F`)
* Positions: `RACERES.SPR` + dark copy `RACERESD.SPR`, title `CHAMPPOS.ST0 TITL` in the box (59,10)-(258,26), buttons (40,175)-(127,191) "Save Game" and
  (190,175)-(277,191) "Continue", rows from y 40 every 13 px: place `%d.` at x 20, name x 40, points at x 250; AI rows `RESULTSA.FNT`, the human `RESULTSB.FNT`.
* Final positions: `FINALPOS.SPR` (the trophy) + `FINPOSD.SPR`, title box (92,11)-(226,27), Ok (109,175)-(209,191), fonts `RESULTSC` (AI) / `RESULTSD` (human).
* Saved games (`0x53536` save / `0x53318` load): `RES_GAME.SPR` with six doors numbered 1..6 (zones `RESGAMEZ.ZON`: ids 1..6 doors, 129..134 their lamps, 7 Cancel),
  the lamp `RES_GL<n>` lights while the pointer is on a door, the chosen door opens with `RES_G<frame 0..7><n>` (8 frames), the top panel shows the title
  (`SAVED.ST0` CHSE "Select A Saved Game" / CHS1 "Select Slot To Save To" / ENTR "Enter Name For Game") and the slot's name or "[Unused Slot]", the lower panel is Cancel.
  Text `SMALL.FNT` colour 0xF0. The save file format of the original (`SLIPSTRM.SAV`) is not decoded; the port writes `slot<N>.sav` (name line + text) next to its records file.
  Loading shows the positions of the saved game first (`0x55D98`); Continue starts the next race.

## Reporters (`0x573B7`) and scripts (`.ANN`)
* Scene: `STARS.SPR`, the turning globe at (91,109) with the flag on the track (positions table `0x54FDC`: Chicago -90/43, Hawaii -160/25, Tokyo 152/44, Norway 14/66, France 5/54,
  Arizona -105/39, Amazon -59/1, London 0/59, Egypt 22/33, New York -76/43 degrees, `+0x600/65536` of a turn added to the longitude), the title plate `CH_TRACK.SPR` with the track name
  (STARFONT), the reporter's face at (182,25), the subtitle in `SMALL.FNT` in the box x 184..308 / y 127..196 (dark text, then gold text one pixel to the right). No music.
* Script per track and phase: `<X>INT.ANN` before the fly-through, `<X>IN1.ANN` after it (table `0x57930`, the sixth character `T` -> `1`); the header's dword at +8 selects the face
  (0 = male Lyall Mint, else female Crystal Eyes). Commands (u16 opcode): `0` wait until the ship is in the named piece (10 bytes, fly-through only), `1` wait N ms (4),
  `2` voice line (4 byte tag + word; sample = the tag with its first character replaced by the language letter `E`, subtitle = string table entry of that tag; 8),
  `3` end - waits for the last voice (2), `4` face program (word length + program), `5` subtitle only (6), `6` wait until the lap distance is <= N (fly-through only, 6).
  `GARAGE.ANN` and `DEMOEND.ANN` use samples that this edition does not contain.
* Face programs (`0x41D6C..0x41F53`, only played for English): ops `8` wait until the program clock reaches N ms, `6` set a layer's frame (layer, frame), `5` end (the engine also knows
  jump / call / spawn / add / random wait, none of which occurs in the shipped scripts). Layers (table `0x41A80`, `0x41AC2` female): mouth 15 frames at (26,45) / (31,51),
  eyes 20 at (31,40) / (32,45), lids 4 at (31,40) / (32,45), brows 6 at (28,25) / (31,30); sprites `MOUTH_A..`, `EYES_A..`, `LIDS_A..`, `BROWS_A..` (`F` prefix for the woman) over `FACE.SPR` / `FFACE.SPR`.
* Esc / Enter / click end a scene.

## TV fly-through (`PlayTrackIntro 0x57A79`, camera F4 `0x45196`)
* One ship (RACER0) flies the lap alone on the autopilot at the intro speed factor `1 - table 0x5040C` (Norway 0.5, France 0.5, Arizona 0.25, Amazon 0.375, others 0), a random race song
  plays, the track's `<TRACK>.ANN` (table `0x580D8`: CHICAGO, HAWAII, TOKYO, NORWAY, CAVE, COLORADO, AMAZON, LONDON, EGYPT, NEWYORK) is the commentary: waits, voice lines and
  "wait until the ship is N units from the line" commands; subtitles from `<X>PREV.ST0`, `SHADE.FNT`, centred in x 8..311 / y 170..197 with the black window frame (8 px top, 7 bottom, 4 sides).
* TV camera: the nearest of the track's 60 `.CAM` positions (12 byte records, x = -1 unused) looks at the ship; zoom = 1 + 3 * clamp((distance - 0x2620) / 0x477C0), and a fast ship
  (speed >= 0x2BA3E) that passes within 0x17D40 of a newly chosen camera plays JETPASS1.SMP (`0x4B848`). The original also tests the camera's line of sight (`0x36669` / `0x140B2`): not ported
  (nearest camera only, INFERRED to match in most places).

## Configuration screens (`0x472FB` and pages) and difficulty
See docs/frontend.md. Difficulty (`DIFF.ST0`): Level Easy / Normal / Hard (CFG word `0x492EA`; AI tier tables, shop prices, championship length, blaster damage) and Damage Off / On
(`0x492EE`: **off = the human takes no damage**, `RaceSlotDamage 0x52035`). The original disables the Difficulty button while a race is paused (`0x47421`): the port's pause menu shows the level read-only.

## Unlock progression (single races)
CFG word `0x493E6` = number of tracks open (1 in a fresh install): only the first N tracks of the calendar order can be chosen in single races and practice; finishing a single race in the first
four on the newest open track opens the next (`0x5A85B`; not in the championship, not in practice, which has no results screen). The port stores the number in `config.txt` (`progress`) and
offers `--unlock-all`.

## Not done
Records: the original keeps the best three laps of every track with a name entry (`0x422EC` / `0x42389`); the port keeps its own top five without names. The original's replay (`RunRaceReplay`),
the two-player championship, the line-of-sight test of the TV camera, the language switch (the `.ST1` / `.ST2` French and German tables and samples exist), key remapping in the Controls page
(it lists the port's fixed keys) and the copy-protection code screen (`CWL`, no callers) are not ported.
