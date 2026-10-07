# Original file inventory — GOG "Slipstream 5000"

Source folder (read-only): `~/Downloads/slip5000/` (160 files, ≈ 190 MB incl. GOG scaffolding). Nothing in it was modified or executed.
Confidence tags: **CONFIRMED** (checked against bytes/strings), **INFERRED** (strong evidence), **SPECULATIVE**.

## 1. Headline findings

| Question | Answer |
|---|---|
| Version / platform | DOS retail game (1995, Gremlin Interactive / The Software Refinery) repackaged by GOG (product id 1207658953, build 58887664246642649) running inside **DOSBox 0.74-2**. English GOG build; the game itself supports English/Français/Deutsch (`INSTALL.INI`). CONFIRMED |
| Main executable | `SLIPSTRM.EXE` (744,703 B), launched as `slipstrm 5000` via `SLIP.BAT` after `logo.exe` and `playgdv intro.gdv`. CONFIRMED |
| Executable format | 32-bit **LE** bound to **DOS/4GW Professional** (Rational Systems, "DOS/4G 1987-1993", "1990-1994"). **Not** packed. CONFIRMED |
| Compiler | **Watcom C/C++32 10.x** runtime banner in the entry code ("1988-1994"). CONFIRMED |
| Sound | **HMI Sound Operating System** (Human Machine Interfaces) drivers `HMI*.386/.DRV`; HMP music, AdLib/OPL2, Gravis UltraSound, SB16 detection IDs in `INSTALL.INI`. No Miles Sound System. CONFIRMED |
| Graphics | VGA mode 13h / VESA (details in `executable-analysis.md`). No Win32/DirectDraw. CONFIRMED |
| Asset storage | Almost everything is inside **`SLIP*.RES` archives** (custom format, name table XOR-obfuscated). A few stand-alone files: GDV videos, HMI drivers, INI/CFG. CONFIRMED |
| Not-the-game | `DOSBOX/`, `set_config/`, `__redist/`, `__support/`, `app/`, `commonappdata/`, `controls/`, `tmp/`, `goggame-*`, `multi.bat`, `*.map` are GOG/DOSBox wrapper files. CONFIRMED |

## 2. Game files (root)

| File | Size | Type / signature | Purpose | Notes |
|---|---:|---|---|---|
| `SLIPSTRM.EXE` | 744,703 | MZ → DOS/4GW → LE (`file`: "LE for MS-DOS, DOS4GW embedded") | Main game | Strings: DOS/4GW Pro, Watcom runtime, HMI, ~1,800 game strings incl. original function names in error text. Not compressed. |
| `SLIPSTRM.RES` | 58,737,788 | custom archive | **Active data archive** (the exe opens its own name with `.RES`) | Byte-identical to `SLIPMAX.RES` (cmp). 2447 entries. CONFIRMED |
| `SLIPMAX.RES` | 58,737,788 | custom archive | Install source for "Maximum" install (`INSTALL.INI`: `copy slipmax.res slipstrm.res`) | = SLIPSTRM.RES |
| `SLIPMED.RES` | 14,531,362 | custom archive | Source for "Standard" install | 1491 entries: same as MAX minus 956 of 980 `.SMP` samples (read from CD at run time). |
| `SLIPMIN.RES` | 11,592 | custom archive | Source for "Minimum" install | 2 entries: `SMALL.FNT`, `SMALLEST.FNT`. |
| `SLIPCD.RES` | 58,778,788 | custom archive | The CD image archive, opened as the *secondary* archive | = MAX + `MATHS.BIN` (2448 entries). The game refuses to start without `MATHS.BIN` ("CD or file is missing"). |
| `CONFIG.INI` | 230 | text | Install result: language, `SourcePath=C:\`, `DestinationPath=C:\`, `Installation=Maximum`, `CDTransferRate`, sound card/port/IRQ/DMA, music card/port | Read by `ReadConfigIni` in the exe. |
| `SLIPSTRM.CFG` | 1,615 | binary, header "V24      Slipstream Config File / (-: No User Serviceable Parts Inside :-) / The Software Refinery Ltd" | Runtime options / key bindings / joystick calibration | Same size as `controls/*.CFG`; GOG's launcher copies `key.CFG` or `joy.CFG` over it. |
| `INSTALL.EXE` | 151,264 | MZ, **Borland C++ 1991** real-mode ("Install and Setup v%s … Written by Kevin Dudley") | Original installer / sound-card setup | Uses `INSTALL.INI`, `INSTGRAV.INI`. Not DOS4GW. |
| `INSTALL.INI` | 53,797 | text script (Latin-1) | Installer script: language list, min CPU 386, memory (503,808 B base, 3,072 KB ext), install tiers (Minimum 8 MB / Standard 23 MB / Maximum 68 MB), sound-card database with DIGIID/MIDIID | The three tiers copy `slipmin/med/max.res` → `slipstrm.res`. CONFIRMED |
| `INSTGRAV.INI` | 63 | text | Gravis patch-loading config (`O 256 8 22` … `M0 …`) | Consumed by `LOADPATS.EXE`; also looks like mixer options. |
| `LOADPATS.EXE` | 49,194 | MZ, Borland C++ 1991 | Loads Gravis UltraSound patches | |
| `LOGO.EXE` | 305,312 | LE + DOS/4GW | Publisher logo screen | Run first by `SLIP.BAT`. |
| `LOGO_S.GDV` | 741,736 | "GDV Format (C) Gremlin Interactive 1994/95" text header | Logo video | Gremlin Digital Video. |
| `PLAYGDV.EXE` | 335,936 | LE + DOS/4GW | GDV video player | |
| `INTRO.GDV` | 4,632,040 | GDV; magic bytes `94 19 11 29` = 0x29111994 (`file` wrongly says "OpenPGP key") | Intro movie | Header word 22050 at +0x0C (likely audio rate — INFERRED). Different header style from LOGO_S.GDV. |
| `HMIDRV.386`, `HMIMDRV.386` | 261,425 / 117,848 | HMI 386 driver blobs (`file`: data) | HMI digital (DMA) / MIDI (OPL2) drivers | Embedded `.com` names (`gus8m.com`, `fmmidi.com`), "Copyright 1992/1994 Human Machine Interfaces". |
| `HMIDRV.DRV`, `HMIMDRV.DRV`, `HMIDET.DRV` | 188,711 / 100,647 / 52,856 | HMI driver blobs (`file` mis-reports "Matlab") | Real-mode digital, MIDI and detection drivers (UltraSound MAX, Sound Master II, GUS detect…) | Names from strings. |
| `READ.ME` | 2,863 | text | Original readme: controls, camera keys F1–F5, Ctrl-Q quits | |
| `readme.txt` | 3,644 | text | GOG/ZOO Classics readme (same controls, Windows task-switching note) | Differs from READ.ME: fire = Alt, select weapon = Ctrl. |
| `Manual.PDF` | 2,066,011 | PDF 1.4, 42 pages | Original manual (incl. German manual section) | |
| `SLIP.BAT` | 55 | batch | `logo.exe` / `playgdv intro.gdv` / `slipstrm 5000` | The `5000` argument is checked by the exe. |

## 3. GOG / DOSBox wrapper files (separate from the game)

| Path | Size | Purpose |
|---|---:|---|
| `multi.bat` | 2,388 | LAN launcher: `ipxnet startserver/connect 127.0.0.1`, then the same game sequence. |
| `PS.map`, `XBOX.map` | 3,242 / 3,204 | **DOSBox keymapper files** (e.g. `key_esc "key 27"`), *not* symbol maps. |
| `controls/key.CFG`, `controls/joy.CFG` | 1,615 each | Preset `SLIPSTRM.CFG` variants (keyboard vs controller); differ from each other and from the shipped `SLIPSTRM.CFG`. |
| `__support/app/dosboxSlip5k.conf` | 11,207 | DOSBox config: `machine=vesa_oldvbe`, `memsize=16`, `cycles=auto`, `core=auto`, `sbtype=sb16`, `sbbase=220 irq=7 dma=1`, `mpu401=intelligent`, GUS off, IPX on, `scaler=normal2x`, `output=Overlay`, `aspect=true`. |
| `__support/app/dosboxSlip5k_single.conf` | 2,123 | Autoexec: mounts `..` as `C:`, overlay `cloud_saves`, launcher menu (game / DOS settings / LAN / input method), copies `controls\*.CFG` → `SLIPSTRM.CFG`. |
| `__support/app/dosboxSlip5k_settings.conf` | 169 | Settings-mode DOSBox config (`install.ini` check). |
| `goggame-1207658953.info` | 1,786 | JSON: play tasks (dosbox with the two confs), tools (Game Configurator, Settings), manual link. |
| `goggame-1207658953.script` | 1,838 | JSON installer actions (copy DOSBox config, registry values, `cloud_saves` folder). |
| `goggame-1207658953.hashdb` | 1,241 | zip-compressed GOG file-hash DB. |
| `goggame-*.ico`, `app/goggame-*.ico` | 125 KB / 160 KB | Icons. |
| `app/webcache.zip` | 317,535 | 9 GOG web-cache images. |
| `DOSBOX/` | 8.5 MB | DOSBox 0.74-2 (`DOSBox.exe`, `SDL.dll`, `SDL_net.dll`, `GOGDOSConfig.exe`, docs, source tarball, `zmbv` codec). |
| `set_config/` | 36 MB | "Game Configurator.exe" + `config_options.ini` (DOSBox option UI: resolution, fullscreen, IPX…). |
| `tmp/` | 4.4 MB, 102 files | GOG installer UI assets (PNG/JPG, EULAs, `botva2.dll`, `InnoCallback.dll`, fonts). |
| `__redist/ISI/scriptinterpreter.exe`, `commonappdata/GOG.com/supportInstaller/uninstall.dll` | 1.25 MB / 0.7 MB | GOG installer/uninstaller helpers. |

None of the wrapper files is needed by a native port; they only document how GOG runs the game (DOSBox, `C:` = game folder, `slipstrm 5000`, IPX LAN).

## 4. Contents of the `.RES` archives (CONFIRMED by parsing)

Format summary: file ends with `u32 directory_offset`; directory = `u32 header (bit31 = obfuscated names, low bits = count)` + `count × 28 B {u32 flags=2, char name[16] (8.3, space-padded, XOR "SOFTWAREREFINERY"), u32 offset, u32 size}`; data is contiguous, unsorted names, no compression. Parsed ending exactly at EOF−4 for all five archives.

| Ext | Count (SLIPSTRM) | Bytes | Likely purpose | Format status |
|---|---:|---:|---|---|
| `.SMP` | 980 | 44.6 MB | Sound effects & speech (raw PCM) | raw unsigned 8-bit mono (CONFIRMED header-less); rate unknown |
| `.SPR` | 886 | 12.1 MB | Sprites, UI, textures, sky | `u16 w, u16 h, 12 B header, 8-bit pixels` + optional palette block (CONFIRMED) |
| `.SHP` | 280 | 0.59 MB | 3D models: ships, scenery, weapons | version 12 polygon format (CONFIRMED structure) |
| `.TRC` | 10 | 0.42 MB | Track geometry/slot records | structure CONFIRMED, semantics partial |
| `.TRD` | 10 | 0.16 MB | Track object placement | structure partial |
| `.TRK` | 10 | 28 KB | Track cell topology, start positions | structure CONFIRMED, semantics partial |
| `.MAT` | 23 | 57 KB | Materials (name, palette ramp, texture pattern) | 46-byte records CONFIRMED |
| `.PAL` | 10 | 7.5 KB | Per-track palettes (6-bit VGA, 248 colours) | CONFIRMED |
| `.CAM` | 10 | 7.2 KB | TV-camera positions (60 × xyz int32) | CONFIRMED |
| `.ART` | 11 | 24 KB | Ship/part descriptors (node tree) | CONFIRMED |
| `.ANN` | 32 | 45 KB | Intro/announcer scripts | script bytecode, not decoded |
| `.FNT` | 18 | 241 KB | Bitmap fonts (`FONT` tag) | header partly understood |
| `.HMP` | 7 | 285 KB | Music: HMI MIDI ("HMIMIDIP013195") | standard HMI format |
| `.BNK` | 2 | 11 KB | AdLib instrument banks (`ADLIB-`) | standard HMI format |
| `.ST0/.ST1/.ST2` | 52 each | ≈ 23 KB each | String tables, English/French/German | `TAG4, u16 len, text` records |
| `.ZON` | 2 | 6.8 KB | Menu hot-zone tables | not decoded |
| `MATHS.BIN` (CD only) | 1 | 40,972 | sin / asin / atan lookup tables | CONFIRMED (numerically) |

Track names/files: CHICAGO, HAWAII, TOKYO, NORWAY, CAVE, CAN, AMAZON, LONDON, EGYPT, NEWYORK (UI names Chicago…New York with CAVE = "France", CAN = "Arizona"). Ten ships `RACER0–9.ART`.

## 5. Answers to the Phase-1 questions

1. **Executable format:** MZ + DOS/4GW + LE (protected-mode 32-bit). CONFIRMED.
2. **DOS real mode / protected mode / DOS4GW / Watcom:** protected mode, DOS4GW Professional, Watcom. `INSTALL.EXE`/`LOADPATS.EXE` are 16-bit Borland C++ real-mode programs. CONFIRMED.
3. **DOS extender:** Rational DOS/4GW Professional (embedded in `SLIPSTRM.EXE`, `LOGO.EXE`, `PLAYGDV.EXE`). CONFIRMED.
4. **Packed executables:** none seen (no UPX/PKLITE/LZEXE signatures; plain LE with readable strings). CONFIRMED for the main exe.
5. **Where things live:** tracks `.TRK/.TRC/.TRD/.MAT/.PAL/.CAM`; vehicles `.ART` + `RACER*.SHP`; textures/sprites `.SPR` (+`.MAT` patterns); palettes `.PAL` and trailing palettes in `.SPR`; audio `.SMP`; music `.HMP/.BNK`; UI `.SPR/.FNT/.ZON/.ST*`; config `CONFIG.INI`, `SLIPSTRM.CFG`; championship/game progress presumably `SLIPSTRM.SAV` (created at run time, not present); videos `.GDV`.
6. **Recognizable standard formats:** PDF, HMI MIDI (`.HMP`), HMI AdLib bank (`.BNK`), IFF/ILBM output (`GRAB0000.LBM`, from strings), raw PCM. GDV is a known Gremlin format (magic 0x29111994 in `INTRO.GDV`).
7. **Archives vs individual files:** archives (`.RES`); only videos, drivers and configs are loose.
8. **Lookup/index tables:** archive directory; `MATHS.BIN` (trig tables); track/art/camera/announcer name tables inside the exe; per-ship parameter table in the exe.

## 6. Unusual / interesting

* `SLIPSTRM.RES` is identical to `SLIPMAX.RES`; `SLIPCD.RES` is the same plus `MATHS.BIN`. The exe opens `<exename>.RES` from the environment block, then `SLIPCD.RES` as secondary, then loose files.
* Archive file names are XOR-obfuscated with the key `SOFTWAREREFINERY`.
* Error messages in the exe embed original function names (e.g. `TrackLoad - wrong data version`).
* An obfuscated jump-maze ("protection trampoline") sits between `main` and the game, plus a `CODEWHEEL` string; not needed for data/port work.
* Hidden developer options file `slipstrm.par`, `DEBUG.LOG`, and an `argv[1]=="5000"` launcher check.
* `file(1)` mis-identifies several files (`INTRO.GDV` as a PGP key, HMI `.DRV` as Matlab); signatures above come from inspecting bytes/strings.
* The CFG presets in `controls/` are the way GOG switches keyboard/controller defaults.

## 7. Cross-references

Details of the executable, function names and asset formats are in `executable-analysis.md`, `function-map.md` and `research-log.md` (work done after the inventory, ahead of the original plan).
