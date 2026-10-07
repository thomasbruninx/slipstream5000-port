# Slipstream 5000 — clean reimplementation (work in progress)

A modern C++20/SDL3 reimplementation of the 1995 DOS game *Slipstream 5000*, built by reverse-engineering the original files from a legally owned copy. **The repository contains no original game assets or code**; the program reads your own installation.

Current state: a native macOS data viewer / drive demo (see `docs/project-status.md`). It is **not** yet a playable reproduction of the game.

## Build and run (macOS, Apple Silicon)
```
brew install cmake ninja sdl3
./scripts/package_macos.sh          # builds dist/Slipstream.app  (or: cmake -S . -B build -G Ninja && cmake --build build)
build/bin/slipstream --data /path/to/Slipstream5000   # folder containing SLIPSTRM.RES (or SLIPCD.RES)
```
The folder is remembered for later launches (`~/Library/Application Support/Slipstream/data_dir.txt`); it is also searched at `$SLIPSTREAM_DATA`, `.`, and `~/Downloads/slip5000`.

Controls — track viewer: `WASD` move, `Q/E` down/up, mouse look, `Shift` fast, `[` `]` previous/next track, `1`–`0` choose ship, `Space` drive/fly, `Tab` toggle backface culling, `F4`/`Cmd+C` copy the debug overlay, `F5` portal/class visibility, `F6` original painter order (vs z-buffer), `F7` show all scenery, `F8` hover assist (legacy) vs original-style flight, `F9` AI ships on/off, `M` music, `N` effects, `F1/F2/F3` track/shape/sprite viewer, `Esc` quit. Drive: `W/S` throttle/brake, `A/D` steer, `E/Q` pitch, `V` cycles three views: cockpit (default), close chase camera locked to the ship's bank/pitch/yaw (`--ship-view`), far smoothed chase (`--chase`) (gamepad Y), `F` fire, `X` next weapon, `Esc` pause menu (Esc outside a race quits), `H` HUD on/off (blaster / weapon A / weapon B / booster) (gamepad: left stick + triggers, `A`/south button toggles driving, bumpers change item, Start quits).

Combat (docs/weapons.md): the 12 original weapons, boosters, bonus objects and AI shooting are in. `--weapons seeker:9,scrambler:9,booster:2` sets the player's loadout
(default: the original's cheat loadout, `none` = blaster only), `--no-pickups`, `--no-ai-weapons`, `--no-voices`, `--difficulty 0..2` (default: the setting in your `SLIPSTRM.CFG`).

Headless: `slipstream --track 3 --screenshot out.ppm`, `--bench 120`, `--models RACER0`, `--sprites MAINMENU`, `--drive --sim 4 --screenshot ...`.

## Layout
`src/original_formats` parsers · `src/game` runtime scene + (placeholder) ship sim · `src/renderer` software renderer · `src/platform` SDL3 app · `tools/re` reverse-engineering workbench (research only) · `tools/inspect` · `tests` · `docs` (findings; start with `project-status.md`).


## Sound
Effects use the original `.SMP` samples; the MIDI music (`.HMP`) is played with FluidSynth and the SoundFont
`resources/GeneralUser-GS.sf2` (bundled in the `.app`). Options: `--soundfont FILE`, `--music NAME`, `--no-music`, `--no-sfx`,
`--no-audio`, `--volume/--music-volume/--sfx-volume`. See `docs/audio.md`. Building needs `brew install fluid-synth`
(without it the game builds with effects only).
