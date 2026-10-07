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

## What is wired
* Music: a random race song (INGAME2/3/4, like the original) when a track is shown; kept across track changes.
* Effects (`Fx` ids of the original, positional rule of `0x4B4DB`): wall hit light/hard (SCRAPE1/SCRAPE2 by the 0x22E98
  speed threshold), ship-to-ship contact (EXPLOSN), wreck (CRASH), the player's engine loop (LOW.SMP, pitch `1 + v/262144`).
  Events come from the simulation as counters in `ShipState` (`sfxWallLight`, `sfxWallHard`, `sfxContact`, `sfxWreck`) and are
  drained by `ViewerApp::drainSounds`.
* Not wired (nothing to trigger them yet): weapons, pickups, water, jet-by (JETPASS1), crowd/pit loops, speech (`EM/EF`),
  menu sounds (SELECT), the intro/win/lose jingles (the songs can be played with `--music`), the second engine voice (HIGH).
