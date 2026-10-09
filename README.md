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

Controls — **race** (the original's defaults, remappable in Configuration > Controls): cursor keys steer / pitch (up = nose down), `Space` accelerate, `Alt` fire, `Ctrl` select weapon, `F1`–`F5` cockpit / chase / rear / TV / free camera, `Esc` pause (after finishing: results), `Ctrl+Q` quit. Debug and viewer shortcuts are on `Ctrl+Shift+key` (HUD `H`, track map `M`, ship `1`–`0`, ...) and the viewer mode (`--viewer`, no race) keeps its plain keys (`WASD` fly, `[` `]` track, `F1/F2/F3` track / shape / sprite viewer, `Tab` culling, `Space` drive). Full tables in `docs/controls.md`.

Combat (docs/weapons.md): the 12 original weapons, boosters, bonus objects and AI shooting are in. `--weapons seeker:9,scrambler:9,booster:2` sets the player's loadout
(default: the original's cheat loadout, `none` = blaster only), `--no-pickups`, `--no-ai-weapons`, `--no-voices`, `--difficulty 0..2` (default: the setting in your `SLIPSTRM.CFG`).

Front end (docs/frontend.md): a plain launch plays the original logo and intro movie and shows the main menu (One Player: Practice / Single Race, track choice, the parking lot to pick your craft, pilot information, the garage shop for weapons / turbo, race results, best laps); `--skip-intro` skips the movies, `--viewer` starts the old track viewer.

Multiplayer (docs/multiplayer.md): up to 10 players, peer to peer over TCP in the same subnet. `F10` opens the menu (host, browse LAN games, join by address) or start with
`slipstream --host --name Alice` / `slipstream --join 192.168.1.20 --name Bob`. Everybody needs the same game files; empty seats are AI ships flown by the host.
`tools/net_race_test.sh 10` runs a ten-process headless race as a smoke test.

Headless: `slipstream --track 3 --screenshot out.ppm`, `--bench 120`, `--models RACER0`, `--sprites MAINMENU`, `--drive --sim 4 --screenshot ...`.

## Layout
`src/original_formats` parsers · `src/game` runtime scene + (placeholder) ship sim · `src/renderer` software renderer · `src/platform` SDL3 app · `tools/re` reverse-engineering workbench (research only) · `tools/inspect` · `tests` · `docs` (findings; start with `project-status.md`).


## Sound
Effects use the original `.SMP` samples; the MIDI music (`.HMP`) is played with FluidSynth and the SoundFont
`resources/GeneralUser-GS.sf2` (bundled in the `.app`). Options: `--soundfont FILE`, `--music NAME`, `--no-music`, `--no-sfx`,
`--no-audio`, `--volume/--music-volume/--sfx-volume`. See `docs/audio.md`. Building needs `brew install fluid-synth`
(without it the game builds with effects only).
