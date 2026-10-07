# Sound system and mixer

Two parts, one SDL3 audio stream (float, stereo, 44.1 kHz):
* **Effects**: the original `.SMP` samples go through a software mixer (`src/audio/mixer.{hpp,cpp}`: 32 voices, gain, equal
  power pan (centre = unity), pitch, looping, linear interpolation; unit-tested without SDL).
* **Music**: the original `.HMP` files are converted to Standard MIDI Files (`hmpToSmf`) and played by **FluidSynth**
  (`src/audio/music_player.cpp`, `fluid_player` + `fluid_synth_write_float` — no FluidSynth audio driver; the synth is
  rendered into the same mix) with a SoundFont.
* `AudioSystem` (`src/audio/audio_system.cpp`) ties them to `SDL_OpenAudioDeviceStream` (callback renders 1024-frame blocks),
  owns volumes, the sample cache and the Fx-id table (`enum class Fx`).

## SoundFont
Default `resources/GeneralUser-GS.sf2` (GeneralUser GS). Lookup order: `--soundfont FILE` (an explicit path is never
silently replaced) -> `$SLIPSTREAM_SOUNDFONT` -> `Slipstream.app/Contents/Resources/` -> `<exe dir>/resources/` ->
the source tree's `resources/` (compile-time) -> `./resources/`. The `.app` bundles the SoundFont and FluidSynth with all of
its dylibs (`scripts/bundle_dylibs.py`), so the package is self-contained.

## Command line / keys
`--soundfont FILE`, `--music NAME[.HMP]` (INGAME2/3/4/6, INTRO, WIN, LOSE), `--no-music`, `--no-sfx`, `--no-audio`,
`--volume V`, `--music-volume V` (0.8), `--sfx-volume V`. Keys: `M` music on/off, `N` effects on/off. Headless modes
(`--screenshot`, `--bench`) never open a device. Without FluidSynth at build time (or without a soundfont) the game runs with
effects only and says so on stderr.
Offline rendering for checks: `slipstream_audio_dump --music INGAME2.HMP --seconds 20 --out x.wav` /
`--sfx CRASH.SMP` (no sound device needed; prints peak/RMS).

## What is wired (as in the original)
* **Music**: a random race song (INGAME2/3/4; like `PlayTrackIntro`/`DoGame3D`). HMI loop markers are honoured: the song plays
  from the start to its first loop end (controller 111) and then that loop (from the matching controller 109) forever - the
  game never issues branch commands, so later sections are unused (INGAME2: ticks 2..3595, INTRO: 6748..8545). WIN.HMP /
  LOSE.HMP play once when the player finishes the race (position <= 3 -> WIN, else LOSE, `0x5A8D0`). INTRO.HMP is available via
  `--music INTRO` (the port has no intro/menu flow).
* **Race start sequence** (`DoGame3D` 0x58AD7 / 0x59010): 5 s countdown, ships held (INFERRED): the per-track announcer sample
  at 5 (`EM01`, `EF12`, ... lists at 0x4B38F, language letter E), `ENGSTART` at 3, the second announcer sample at 1 and the
  engine voice starts, then GO. `--no-countdown` skips it.
* **Effects** (`Fx` ids, positional rule of `0x4B4DB`): wall hit SCRAPE1 / SCRAPE2 (slot speed threshold 0x22E98) or WATERHIT on
  `WATE*` surfaces, ship contact EXPLOSN, wreck CRASH, engine loop `LOW.SMP` with pitch `1 + v/262144` (the original opens two
  engine voices for the two human players, both with LOW; HIGH.SMP is never referenced by the code).
* **Ambient loops** (`0x4B658`, selected at 0x58CB6): `PITSLP` while the player is in the refuel piece, `CROWDLP` in pieces whose
  name starts with CROW or GRID; fade in/out about 1 s.
* Race flow for the sounds: laps (completed circuits of the node chain), `--laps N` (default 3), "FINAL LAP!!" and "FINISHED"
  on the HUD.
* **Weapons, pickups, voices** (this phase, `docs/weapons.md`): BLASTER (beam launch), MISSILE (missile / smoker launch), MINEDROP, LASERHIT,
  DISRUPTR, EXPLOSN (missile / mine hit and wall explosion), BOMBER, SCRAMBLE, HYPERNEU, AMBLER (hit sounds), BONUSCOL (pickups), ENGSTART (booster on)
  are played positionally like every other effect. Pilot / announcer lines come from the 85-entry cue list of the executable
  (`AudioSystem::playCue`): one voice at a time, a cue is dropped while the previous one still plays and when it equals one of the last four.
* **Not wired** because the port has nothing to trigger them: menu sounds (SELECT), the speech samples used by menus, and
  the **jet-by** (JETPASS1): in the original it belongs to the trackside TV camera (`0x45196` picks a camera spot, plays
  JETPASS1 at volume 0x4000 when a ship faster than 0x2BA3E passes within 0x17D40 of it).
