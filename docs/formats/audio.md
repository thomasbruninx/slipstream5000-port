# Audio formats (SMP samples, HMP music, BNK)

## .SMP — sound effect samples (980 files)
Raw **8-bit unsigned mono PCM, no header** (first bytes cluster around 0x80). Played at **11025 Hz**: `SoundInit` (0x10AA4)
opens the HMI SOS digital driver with rate `0x2B11` = 11025 (CONFIRMED in code; the format is CONFIRMED from the data).
Names: `EM##`/`EF##` (speech, male/female — not used yet), `EA..`/`EC..`/`EPS#` (not yet identified), and the 24 named
effects loaded by the Fx engine (`0x4B298`, name list at 0x4AEBE): LOW, HIGH, JETPASS1, SCRAPE2, SCRAPE1, CRASH, BLASTER,
MISSILE, BONUSCOL, PITSLP, CROWDLP, MINEDROP, ????, WATERHIT, EXPLOSN, LASERHIT, DISRUPTR, ENGSTART, ????, BOMBER, SCRAMBLE,
HYPERNEU, AMBLER, WOOSH.

### Effect ids (CONFIRMED, table 0x4B7C4 -> slot -> name)
`FxPlay` (0x4B92A; EDX = id) queues up to 20 effects; ids: 1 CRASH, 2 SCRAPE2, 3 SCRAPE1, 4 BLASTER, 5 MISSILE, 6 BONUSCOL,
7 MINEDROP, 8 WATERHIT, 9 EXPLOSN, 10 LASERHIT, 11 DISRUPTR, 12 ENGSTART, 13 BOMBER, 14 SCRAMBLE, 15 HYPERNEU, 16 AMBLER.
The queue entry kinds are 0 (global), 1 (attached to a slot) and 2 (fixed position). Processing (`0x4B4DB`, at most 4 per
frame): the volume is `0x7FFF * (0x11DF0 - d) / 0x11DF0` with `d` the length estimate (0x21F87) between the listener and the
source; beyond 0x11DF0 (73200) units nothing is played; an effect belonging to the listener's own slot is played at full
volume; there is no panning. Engine sound: two voices (`FxAddEngine` 0x4B86D) looping `LOW.SMP`; the playback rate is
`0x10000 + speed/4` (16.16, i.e. `1 + speed/262144`).
Used ids found so far: wall hit (0x50A64) 3 (slot speed <= 0x22E98) or 2 (above), 8 for a `WATE*` surface; ship contact 9
(0x5095D); bonus pickups 6/0xB/0xC/0xD...; the 0x202 message path 0xA/0xB.

## .HMP — MIDI music (7 files: INGAME2/3/4/6, INTRO, WIN, LOSE)
Human Machine Interfaces "HMIMIDIP013195".
* +0x00 signature (14 bytes + padding), +0x20 length of the music data, +0x30 number of tracks, +0x34 0x180, +0x38 0x78,
  +0x3C length in seconds. The first track chunk is at **0x388**.
* Track chunk: `u32 track number, u32 length (including these 12 bytes), u32 channel hint`, then events until `FF 2F 00`.
* **Delta time**: little-endian base-128, bytes with bit 7 **clear** continue, the last byte has bit 7 **set** (Standard MIDI
  Files are the other way round). Events are ordinary MIDI channel events with an explicit status byte (no running status)
  and FF meta events.
* **Loop markers**: CC109 = loop id (value >= 128), CC110 = 255 (forever), CC111 = loop end with that id, CC113 = branch id, CC119 = beat. The game has no branch calls, so a song runs to its first loop end and then repeats that loop (`hmpToSegments`); WIN/LOSE have no markers.
* **Controllers 108..119** carry HMI loop / branch / beat data (e.g. `B0 6E FF`, `B0 71 BE`) and are dropped. Track 1 holds
  those markers; track 0 is only an end marker that sets the length.
* **Timing: 120 ticks per second** (CONFIRMED: longest track / 120 = header seconds for INGAME2 131, INGAME3 299, INGAME4 129,
  INGAME6 92, LOSE 10, WIN 17; INTRO's header says 302 s but has 419 s of events). Written as an SMF with division 120 and tempo
  1,000,000 us/quarter.
* Programs are General MIDI numbers (30 distortion guitar, 55 orchestra hit, 88, 36 slap bass ...), drums on channel 9.
* Which song: `PlayTrackIntro` (0x57A79) and the race start (`DoGame3D`, 0x589C1) pick `random(3)` of the table
  INGAME2/3/4/6 (the fourth only when a memory check fails). INTRO plays in the intro, WIN/LOSE after races.
Code: `src/original_formats/audio.{hpp,cpp}` (`hmpToSmf`), tests `tests/audio_tests.cpp`.

## .BNK — instrument banks (DRUM.BNK, MELODIC.BNK)
AdLib FM instrument banks (`ADLIB-` signature header) for the original's OPL driver. Not used: the music is rendered with a
General MIDI SoundFont instead (see docs/audio.md).

## Other sound facts (CONFIRMED by reading code)
* Announcer samples: `E` + `M`/`F` + number; per track (1..10) list 1 {EM01, EF12, EM09, EF08, EF10, EF06, EM03, EM05, EM07, EM11}
  at the start of the countdown, list 2 {EM02, EF13, EM10, EF09, EF11, EF07, EM04, EM06, EM08, EM12} one second before the
  start. The first letter is replaced by the language letter table `EFG` (0x49D75).
* Ambient loops: effect slot 0x4B280 selects PITSLP (1) / CROWDLP (2); volume ramps by `2*dt` (2.14) towards 0x7FFF.
* Race start timers: `[0x54408]` counts 5..0 in seconds; `[0x54404] = 0x3A98` (15 s) enables the position-based speed bonus table
  0x50252 (not ported).
