# Slipstream 5000 — clean reimplementation

A modern C++20 / SDL3 reimplementation of the 1995 DOS racing game *Slipstream 5000* (The Software Refinery / Gremlin Interactive), built by reverse-engineering the
data files and the executable of a legally owned copy. It runs natively on **macOS (Apple Silicon)**, **Linux x64** and **Windows x64**.

> **This repository contains no original game assets and no original code.** Models, textures, sounds, music, movies, menus and track data are read at run time from
> *your own* copy of the game. You need an official copy to play (see below). This is an unofficial fan project, not affiliated with or endorsed by the rights holders.

## Foreword

Slipstream 5000 was one of my favorite racing games growing up, and I have fond memories of playing it on our old family computer. This project is a labor of love to bring that experience to modern platforms while preserving the original game's feel and mechanics.
*- Thomas 'absurdhealer' Bruninx*

## You need the original game
The port reads the files of the original game (`SLIPSTRM.RES` / `SLIPCD.RES`, plus the movies and samples next to them). Buy an official copy:

* **GOG.com** (the DOS version this port is developed against): <https://www.gog.com/en/games?query=slipstream%205000>
* **Steam**: <https://store.steampowered.com/search/?term=slipstream+5000> (availability and edition may differ; the port has only been tested with the GOG files)
* An original **CD-ROM** release works as well if you copy / mount its files (`SLIPCD.RES`); this is untested.

Install the game once (on any OS: the files are the same) and point the port at the folder that contains `SLIPSTRM.RES`:
`slipstream --data /path/to/Slipstream5000` (remembered afterwards), or set `SLIPSTREAM_DATA`. Folders such as `./`, `~/Downloads/slip5000`, `~/Games/slip5000` and `~/GOG Games/Slipstream 5000` are searched automatically.
The original files are never modified.

## Build and run
| Platform | How |
|---|---|
| macOS (arm64) | `brew install cmake ninja sdl3 fluid-synth` then `./scripts/package_macos.sh` → `dist/Slipstream.app` |
| Linux x64 | `./scripts/build_linux.sh` → `dist/slipstream-linux-x86_64.tar.gz` (or `./scripts/build_linux_docker.sh` from any machine with Docker) |
| Windows x64, cross compiled from macOS / Linux | `./scripts/build_windows_cross.sh` → `dist/slipstream-windows-x64.zip` (MinGW-w64; fetches MSYS2's FluidSynth / SDL3 packages, so it has MIDI music) |
| Windows x64, native | MSYS2 MINGW64 shell: `./scripts/build_windows_msys2.sh` |

Dependencies, package names, options and the status of each port are in **`docs/building.md`**. Build status honestly: macOS is played every session; the Linux build compiles and passes the
unit tests in a container but has not been run with a window and sound device; the Windows build compiles and links but has **never been run** (no Windows machine here), so
expect rough edges, especially in the network code and the controller code. Bug reports are welcome.

Run it with `slipstream` (a plain launch plays the original logo and intro movie and shows the main menu), `--skip-intro` skips the movies, `--viewer` opens the old track viewer.
Per-user files (options, key bindings, records, saved championships) live in `~/Library/Application Support/Slipstream` (macOS), `%APPDATA%\Slipstream` (Windows) or `~/.config/slipstream` (Linux).

## Controls (details: `docs/controls.md`)
Race defaults are the original's, remappable in *Configuration > Controls*: cursor keys steer / pitch (up = nose down), `Space` accelerate, `Alt` fire, `Ctrl` select weapon, `F1`–`F5` cockpit / chase / rear / TV / free camera,
`Esc` pause (after finishing: results), `Ctrl+Q` quit. Debug and viewer shortcuts moved to `Ctrl+Shift+key`.
**Controllers** (USB / Bluetooth, through SDL3's gamepad API) work together with keyboard and mouse at any time, you can switch on the fly: left stick steers, triggers accelerate / brake, X fire, B select weapon, Y camera, Start pause,
all remappable in *Configuration > Controls > Controller*.

## Features implemented
**Game data and rendering**
* Parsers for the original formats (resource archives, tracks, shapes, sprites, fonts, string tables, samples, MIDI, movies, animation scripts, config / save files); software renderer that follows the original's structure (portal visibility, BSP painter order, shade ramps, billboards, shadows, translucent decals).
* All 10 tracks, all 10 craft with their reference points, doors, pit lane, crowds, ambient effects.

**Racing**
* Ship physics ported from the original (thrust, drag, steering, banking, pitch, ship-to-ship contact, track-polygon collision, damage model, wrecks); lap counting, ranks, finish rules, three difficulty levels.
* AI opponents (route following, avoidance, pit and branch choices, door handling, speed tiers, boost).
* Particle effects of the original (missile smoke trails and flames, smoke screens, explosion smoke, fireballs, debris of hit and destroyed craft, wall sparks and water droplets), two-colour beams, all voice cues of the executable.
* All 12 weapons (blaster, disrupter, frag / super frag, seeker / super seeker, ambler, scrambler, hyper neuro, bomber, mini mines, smoker), boosters, six bonus types, status effects, lock-on, pilot and announcer voice lines with the original rules.
* Cameras: cockpit with the original console, chase, rear, TV camera, free camera; rear monitor; track map; talking pilot portraits; race HUD of the original; pause menu.
* Sound: original samples and positional rules, MIDI music through FluidSynth with a bundled SoundFont, ambient loops, announcer.

**Front end and championship**
* Logo, intro and credits movies, main menu, track selection with the rotating globe, parking lot with pilot cards and voiced biographies, garage / workshop (weapons, turbo, systems, prices), race results, best laps with pilot faces.
* **Championship mode**: calendar of tracks, points and prize money, standings, saved games (6 slots), reporters with animated faces and voices before each race, TV fly-through, unlockable tracks, reward craft, economy, difficulty and configuration screens.
* Configuration screens of the original (sound, detail, difficulty, controls and key remapping).

**Multiplayer**
* Up to 10 players, peer to peer over TCP in the same subnet (host / browse LAN games / join by address, empty seats flown by AI on the host); see `docs/multiplayer.md`. `tools/net_race_test.sh 10` is a headless smoke test.

## Features added or changed compared with the original
Things that are not (or not exactly) in the original, mostly on request:
* Native macOS / Linux / Windows builds, **gamepad support** with remapping and on-the-fly switching with keyboard / mouse, **key remapping** (the original's default keys are the defaults), Ctrl+Shift debug keys.
* **Multiplayer** over a network (the original had a two-player mode only); menu entries *Singleplayer* / *Multiplayer* (Host Game, Search LAN, Direct IP).
* **Top-3 best laps with name entry** per track, race **replay** (also in the championship), results screen on Esc / Enter after the finish while the ship keeps flying on its own.
* **Drones** ("little airbuses") that can be shot for bonus pickups, with lock-on and a bonus from any kill.
* **Tactical AI weapons**: opponents start calm, grow more aggressive with race time and when they are hit, keep their heavy weapons for revenge and good shots (original behaviour: `SLIP_CLASSIC_AI=1`).
* Start speed bonus off by default, random first championship grid, weapons are not refilled for free between championship races.
* **Reset Player** in the pause menu: puts your craft back on the centre line of the track (horizontally and vertically), heading along the track and at rest, for when you are stuck.
* Rear monitor with the console (the original's second monitor) and the weapons monitor (the missile's view).
* Joystick calibration pages replaced by SDL3 controller mapping (optional extra mappings in `gamecontrollerdb.txt`).

## Planned features
* Hardware acceleration for the renderer. Shader support and GPU optimizations are planned. (Perhaps even ray tracing in the future)
* Improved AI behavior and additional difficulty settings. Offering more control over opponent behavior and challenge levels.
* Internet play (NAT traversal, matchmaking, host migration, authentication); the network code is LAN only. Dedicated matchmaking services could be added in the future.
* An app icon / installer for Windows and Linux, Linux and Windows builds that have been tested on real machines.
* Everything is documented per area in `docs/` (start with `docs/project-status.md`; each topic file has a "Not done" section).

## Skipped features
* Other languages than English (the original's other-language string tables and samples exist, but are not used in this reimplementation)
* Two-player championship and split-screen layout (the original had this, but it is not implemented in this reimplementation)
* Joystick calibration (replaced with SDL3 controller mapping).
* Direct modem connection (the original's peer-to-peer feature, not relevant in this reimplementation).

## Layout
`src/original_formats` parsers · `src/game` game logic (physics, AI, weapons, championship, front end) · `src/renderer` software renderer · `src/audio` mixer + music · `src/net` network code ·
`src/platform` SDL3 application · `tools/re` reverse-engineering workbench (research only, reads your files, never modifies them) · `tools/inspect` · `tests` · `scripts` build scripts · `docs` findings and notes.

Useful options: `--data DIR`, `--track N`, `--difficulty 0..2`, `--weapons seeker:9,scrambler:9,booster:2`, `--no-ai-weapons`, `--no-pickups`, `--no-voices`, `--soundfont FILE`, `--no-music`, `--no-sfx`, `--no-audio`,
`--host` / `--join ADDRESS` / `--name NAME`, headless `--screenshot out.ppm`, `--bench 120`, `--models RACER0`, `--sprites MAINMENU`. `slipstream --help` lists everything.

## Legal notice and takedown policy
*Slipstream 5000* and all of its original assets, code, names, music, graphics, movies and trademarks belong to their respective owners (the developer The Software Refinery and the publisher Gremlin Interactive, or their successors).
This project is an independent, non-commercial reimplementation made for preservation, interoperability and educational purposes. It does not distribute any original game content; the SoundFont in `resources/` is a separate freely licensed work
(GeneralUser GS, see its own licence). Using this software requires an official copy of the game.

**Notice and takedown.** If you are a rights holder (or their authorised representative) and believe that anything in this repository infringes your rights, please contact the maintainer by opening an issue titled
"Takedown request" at <https://github.com/thomasbruninx/slipstream5000-port/issues> (or through the maintainer's GitHub profile if you prefer not to post publicly), with: (1) your name and contact details,
(2) identification of the work, (3) the exact files or locations in this repository you believe infringe, and (4) a statement that you are the rights holder or authorised to act for them. The material will be
reviewed promptly and removed or changed if the claim is valid; the maintainer will cooperate in good faith.
