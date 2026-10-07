# Executable analysis — `SLIPSTRM.EXE` (Slipstream 5000, DOS)

Phase 2 deliverable. Static analysis only (nothing was executed). Companion documents:

* [`function-map.md`](function-map.md) — every named function / global with evidence and confidence.
* [`research-log.md`](research-log.md) — chronological log, dead ends, tool notes, open questions.
* `tools/re/` — the research tooling (reproducible pipeline + `validate_assets.py`, 30 checks that re-prove the format claims below).

> **Reading guide.** Every statement carries one of three confidence levels.
> **Verified** = checked mechanically against the real files/bytes (and re-runnable). **High** = established by the game's own embedded strings or by exact structural/behavioural match. **Medium / Low** = inference; marked inline. Unknowns are listed explicitly in §16. Addresses are *virtual addresses* (VA) in the original image (§2.3).

---

## 1. Summary

1. `SLIPSTRM.EXE` is a **32-bit LE executable bound to the DOS/4GW Professional extender**, built with **Watcom C/C++32** (build stamp `24/04/95@14:36:40/CD`). It contains ~383 KB of code/data in one object, a small object of 16-bit real-mode interrupt stubs, and a 41 KB data/stack object. **High**.
2. The game is built on a small proprietary engine by *The Software Refinery* made of **hand-written assembly modules** (resource manager `Res*`, entity system `Slots`, `Shape*`, `Track*`, `Draw3D*`, `Video*`, `Input*`, `Timer*`) plus Watcom-C game code. Error strings embed the original function names, which gave ~150 high-confidence names. **High**.
3. **All game assets live in `SLIP*.RES` archives.** The container format is fully decoded and verified on all five files: 28-byte directory records, names XOR-obfuscated with the 16-byte key `SOFTWAREREFINERY`, directory located by the last dword of the file. **Verified**.
4. Asset resolution order at runtime is: *primary archive* (EXE-adjacent `.RES`) → *secondary archive* (`SLIPCD.RES` via the configured source path) → *loose file with the same name on disk*. **High**.
5. Decoded/validated formats: `.PAL`, `.MAT`, `.SPR`, `.SHP`, `.ART`, `.TRK`, `.TRC`, `.TRD`, `.CAM`, `MATHS.BIN` (sin/asin/atan tables). Container-level or partial: `.FNT .SMP .HMP .BNK .ST0/1/2 .ANN .ZON`. **Verified** for the structural claims, with named unknown fields (§7).
6. The **track loader** is `TrackLoad` (VA 0x344DB): given `NAME.TRK` it also loads `NAME.TRD` and `NAME.TRC` (it overwrites the last character with `D` / `C`), validates version `0x2B`, links shapes by name, builds a spatial grid and lap-distance data. The full per-race load order is `RaceLoadTrack` (VA 0x594B3), §8.
7. The **game state is an entity array of 0xAE-byte "slots"** with callbacks; ships are slots whose server callback `RaceSlotControl` (VA 0x5047C) is message-driven (0x101 create, 0x104 update, 0x105 draw …). Math is **2.14 fixed point with 16-bit angles (0x10000 = 360°)** and tables in `MATHS.BIN`. **High**.
8. The **race loop is `DoGame3D`** (VA 0x586F2, loop at 0x58B17–0x591B2): frame clock → input/pause → record/replay → update (`RaceUpdate`, `SlotsUpdateAll`, `CollideStep`, …) → draw (`SlotsDrawAll`, HUD). Gameplay is **deterministic** (seeded PRNG + recorded control stream → replays). **High / Medium**.
9. A **protection trampoline** (obfuscated jump-maze at VA 0x5BB6E) sits between `main` and the real game start; a code-wheel string (`CODEWHEEL CODES`) is present. Not required to understand or port the data/engine, but it must not be mistaken for game logic. **Medium**.
10. Hidden developer features exist (`slipstrm.par` parser, `DEBUG.LOG`, collision-cube display). **Medium**.

---

## 2. Binary identification

### 2.1 Container (verified)

| Item | Value |
|---|---|
| File | `SLIPSTRM.EXE`, 744,703 bytes |
| Outer format | MZ stub → **DOS/4GW Professional** kernel (strings "DOS/4GW Professional Protected Mode Run-time", "Copyright (c) Rational Systems, Inc. 1990-1994") |
| Application | second MZ at file offset `0x26654`, `e_lfanew = 0x2A50` → **LE header at `0x290A4`** |
| Page size / pages | 4096 / 105 (last page 0x4AB bytes) |
| Data pages | file offset `0x4D854` (= `0x26654 + 0x27200`); object 1 starts there |
| Entry point | EIP = object 1 + `0x53978` → **VA `0x63978`** (`_cstart_`) |
| Stack | ESP = object 3 + `0xA6A0` |
| Fixups | 17,665 records, **all** 32-bit internal offsets: obj1→obj1 15,340; obj1→obj3 2,289; obj1→obj2 3; obj3→obj3 23; obj3→obj1 10 |

Object table:

| Obj | Preferred base | Size | Flags | Role |
|---|---|---|---|---|
| 1 | `0x10000` | `0x5D4C4` (382,148) | `0x2045` R/X, 32-bit | Code **and engine globals/tables/strings interleaved** (e.g. `[0x54000]`, `[0x26628]`), Watcom CRT at 0x5D8B2+, HMI SOS runtime at 0x64000+ |
| 2 | `0x70000` | `0x486` | `0x45` | **16-bit real-mode stubs** copied into DOS memory by `TimerInstall` (VA 0x70440, 0x46 bytes: IRQ0 handler) and `InputInstall` (VA 0x702C0, 0x171 bytes: INT 15h/4Fh keyboard hook). **High** |
| 3 | `0x80000` | `0xA6A0` (40,608) | `0x2043` R/W, 32-bit | Initialised data: message strings, `config.ini` keys, `slipstrm.par` keywords, HMI error strings, path buffer `0x807C0` ("SLIPCD.RES"); stack at the top |

File offset of a code VA is `0x4D854 + (VA − 0x10000)` (pages are contiguous; the file stores **un-relocated** pages, so absolute pointers inside instructions/tables only exist after applying the 17,665 fixups — the tooling does this).

### 2.2 Toolchain / runtime signatures (high)

* `WATCOM C/C++32 Run-Time system. (c) Copyright by WATCOM International Corp. 1988-1994` at VA `0x63979` immediately after the entry jump → Watcom C/C++32 10.x startup; `printf` ≈ VA 0x5D8EB, `strcmp` ≈ 0x5D8B2, `sscanf` ≈ 0x5D9D2.
* **Register calling convention** for Watcom-C code (args in EAX, EDX, EBX, ECX; result in EAX; e.g. `main(argc=EAX, argv=EDX)`); variadic CRT calls use the stack.
* **Engine modules follow an assembly convention**: CF = error, ESI = resource handle / name pointer, EDI = slot or record pointer, all registers preserved with `pushal/popal`.
* **HMI Sound Operating System** (Human Machine Interfaces): credit string "Sound Operating System by Human Machine Interface", drivers `hmidrv.386`, `hmimdrv.386`, `hmidet.386`, `.HMP` music, `.BNK` AdLib banks, error-string table at VA 0x80482+.
* DPMI services (INT 31h) are used heavily: DOS memory alloc (0100h), real-mode vectors (0200h/0201h/0204h/0205h), real-mode call (0300h), linear memory (0500h–0502h).

### 2.3 Address convention used in all docs

`VA` = object-1 base `0x10000` + object offset; data VAs `0x80000+`. Strings in object 1 are therefore at VA = `0x10000 + <offset reported by `strings`>`.

### 2.4 Sibling executables (not analysed)

`LOGO.EXE`, `PLAYGDV.EXE` (GDV video player used for `INTRO.GDV`, `LOGO_S.GDV`), `INSTALL.EXE`, `LOADPATS.EXE`. `SLIP.BAT` runs `logo.exe`, `playgdv intro.gdv`, then **`slipstrm 5000`**. `DOSBOX/*.map` files are DOSBox keymapper files (no symbols).

---

## 3. Conventions used by the code

| Topic | Convention | Evidence |
|---|---|---|
| Angles | 16-bit, **0x10000 = 360°**, 0x4000 = 90° | `Sin/Cos/ASin/Atan` + MATHS.BIN (verified numerically) |
| Trig / matrix values | signed **2.14** (0x4000 = 1.0); products via `IMUL` then `SHRD x,14` / `SAR 14` | `SlotMoveStep`, `ShapeDraw` point transform |
| Time | ms counter 16.16; frame delta in ms **and** as seconds in 2.14 (`GetFrameDelta` → EAX ms, EBX secs-2.14, ECX fps) | §12.2 |
| Speed | 32.16 fixed in ship data (+0x0C dword, +0x10 word); slot speed (+0x2C) is a plain dword used with `speed*dt>>14` | `RaceSlotMove`, `SlotMoveStep` |
| Last-error | global `[0x209D8]` set by file/Res/slot code (2 not found, 3 read, 4 create, 5 write, 6 no memory, 7 no slots, 8 res table full); `ErrorExit` (0x4A86C) maps it to a user message | §4 |
| Fatal errors | `ESI = message; JMP 0x20CBD` (`FatalError`): prints `$`-terminated via INT 21h/09 and exits with 4C00 | 150+ call sites; all embed the calling function's name |
| Exit hooks | `RegisterExit(EDI = routine)` (VA 0x20C54, table of limited size); subsystems register their uninstall routines | `SlotsInstall`, `ShapesInstall`, `TrackInstall`, … |
| File names | 8.3, upper-cased, space-padded to 12 characters for archive lookup (`NAME    .EXT`) | `ResFileLoadByName` |
| Wildcards | `*` and `?` in resource names (`RACER*.SHP`, `GAMEF*.SPR`, `CAR?_FR?.SPR`) expand through `ResFindIDs` | strings + `ResFindIDs` |

---

## 4. Startup and shutdown — `main` (VA 0x10010)

| # | Step | Function (VA) | Notes / evidence |
|---|---|---|---|
| 0 | CRT startup | `_cstart_` 0x63978 → `main` call at 0x6AE69 | |
| 1 | `argc==2 && argv[1]=="5000"` sets `[0x54000]=1` | `main` | `SLIP.BAT` passes `5000`; **no other reader found** by static xref (flag may be consumed by the obfuscated gateway, §14) |
| 2 | DOS version + "running under Windows?" (INT 2Fh AX=1600h) | 0x20A87 / 0x20BFC | prints *"Slipstream will not run under Microsoft Windows(TM)…"* |
| 3 | Read `CONFIG.INI` | `ReadConfigIni` 0x101A4 | keys seen: `SoundCard SoundPort SoundIRQ SoundDMA MusicCard MusicPort SourcePath`; failure → "The file CONFIG.INI is missing. Please run INSTALL." |
| 4 | Sound/HMI driver init | `SoundInit` 0x10AA4 | error → "Couldn't initialise sound driver: %s." |
| 5 | Resource system | `ResInstallWrapper` 0x417B4 → `ResInstall` 0x244D8 with (4000 handles, 15000, 0) | |
| 6 | Open primary archive | `FileOpenResEXE` 0x1F00C → `ResFileOpen` 0x1F01A; `0x1F4D3` registers it | exe path from DOS env block, `.EXE`→`.RES` |
| 7 | Open secondary archive | `FileOpenRes` 0x1F190 with the path buffer at 0x807C0 (`"SLIPCD.RES"` prefixed by `SourcePath`), skipped when the drive letter is `A:`/`B:` | `0x1F4E4` registers it |
| 8 | Timer, video, input | `TimerInstall` 0x2878A; `VideoInstall(mode 0)` 0x4181C; `InputInstall(flags)` 0x1FC14 | flags = `ignoremouse` |
| 9 | Frame clock, text/event init | `FrameClockInstall` 0x12D80; 0x29D66 (unverified role) | |
| 10 | Maths tables | `MathsInstall` 0x211CE | loads `MATHS.BIN`; failure → *"CD or file is missing. Please correct problem and try again."* |
| 11 | Sound timer + config | 0x10CDC, `0x4989C` (`SLIPSTRM.CFG`) | |
| 12 | Fonts | `FontsInstall` 0x55750 (`SMALL.FNT`, `SHADE.FNT`, `SMALLEST.FNT`) | |
| 13 | **Game** | `ProtectionTrampoline` 0x5BB6E → `GameMain` 0x557B6 | §10, §14 |
| 14 | Shutdown | `ShutdownAll` 0x20B68 (runs registered exit routines) → return 0 | |

Failure handling: every by-name load is followed by `jb 0x4A86C` → `ErrorExit` (reads `[0x209D8]`, prints "ERROR: One of the files required to run Slipstream is missing or damaged. Reinstalling from your original disks may correct the problem." for codes 2–5, "ran out of memory" for 6, "internal error" otherwise). **High**.

---

## 5. Subsystem map (approximate address ranges)

Boundaries are inferred from string anchors and call-graph clustering (no symbols), ±a few hundred bytes.

| VA range | Subsystem | Key entry points (see function-map) | Confidence |
|---|---|---|---|
| 0x10000–0x11500 | startup, config, HMI glue | `main`, `ReadConfigIni`, `SoundInit`, `HmiPlaySong` | high |
| 0x11500–0x12D7F | **Artic** (articulated vehicle parts, `.ART`) | `ArticInstall`, `ArtSlotCreate`, `ArticSlotInit`, `ArticSlotDraw` | high |
| 0x12D80–0x13000 | **Frame clock** | `FrameClockInstall/Update`, `GetFrameDelta` | high |
| 0x13000–0x1695C | **Collide** (cubes/faces, ship/track/slot collision) | `CollideStep` 0x137A2, `CollideSlot*` | high/med |
| 0x1695D–0x18033 | **Com** (serial, modem, IPX) | `ComInstall`, `ComConnectSer`, `IpxInstall` | high/med |
| 0x18034–0x1E2F2 | **Draw3D** (materials, points, polygon front-end, clip, edge) | `Draw3DInstall`, `Draw3DSetMaterials` | high/low |
| 0x1EB3C–0x1F600 | DOS file layer + archive access | `Dos*`, `ResFileOpen`, `ResFileLoadByName`, `ResFileFindEntry` | high |
| 0x1FC14–0x20A85 | **Input** | `InputInstall`, `KeyHit`, `JoystickCalibrate` | high |
| 0x20A86–0x21100 | DOS glue, `RegisterExit`, key recorder | | high |
| 0x21100–0x24100 | **Maths** | `MathsInstall`, `Sin/Cos/ASin/Atan2`, `ISqrt` | high |
| 0x24100–0x25A00 | PRNG + **Res manager** | `ResInstall/ResOpen/ResClose/ResFind`, `Random` | high |
| 0x25A00–0x26750 | **Shapes** (`.SHP`) | `ShapesInstall`, `ShapeLoad`, `ShapeDraw` | high |
| 0x26750–0x28680 | **Slots** (entity system) | `SlotsInstall`, `SlotsUpdateAll`, `SlotCreate` | high |
| 0x28680–0x29B00 | string tables (`.ST*`) + **Timer** | `StrTabGet`, `TimerInstall`, `TimerAddCallback` | high |
| 0x29B00–0x2C000 | event + text engine | `EventStart`, `Text*` | high |
| 0x2C000–0x33100 | **Video** (mode set, palette, blitters, rasterisers) | `VideoInstall`, 0x2DE6D textured scanline | med/low |
| 0x33100–0x33F00 | LBM writer, VESA, text-map tables | `WriteIffLbm`, `TextMapTabInstall` | med |
| 0x33F00–0x3F200 | **Track** | `TrackInstall`, `TrackLoad`, `TrackSlot*` | high |
| 0x41800–0x4A000 | front-end screens (garage, config, link) | `ConfigMenu`, `MainMenuDraw` | med |
| 0x4F2C0–0x53000 | race effects: explosions, bonuses, damage | `RaceBang*`, `RaceSlotDamage` | high |
| 0x5047C–0x52035 | **Race slot control + physics** | `RaceSlotControl`, `RaceSlotMove` | high/med |
| 0x53000–0x55750 | save/load, results | `DoSaveGame` | med |
| 0x55750–0x56600 | game main, mode dispatch | `GameMain`, `FontsInstall` | high/med |
| 0x586F2–0x59900 | **DoGame3D** | `DoGame3D`, `RaceLoadTrack` | high |
| 0x5BB6E–0x5BFF4 | protection trampoline, replay | | med |
| 0x5C0F9–0x5D8B1 | weapons | | low |
| 0x5D8B2–0x63977 | Watcom C runtime | | high |
| 0x64000–0x6D4C4 | HMI SOS driver glue + DPMI helpers | | med |

---

## 6. Archive and resource pipeline

### 6.1 `.RES` file format (verified: 5 archives, 2447/2448/1491/2/2447 entries)

```
offset 0                      entry data, packed back-to-back (no padding, no alignment)
...
diroff = u32 at (filesize-4)  start of the directory
diroff + 0   u32  header      bit31 = "names obfuscated", bits0..30 = entry count
diroff + 4   entry[count]     28 bytes each:
                 u32 flags    always 2 in all archives ("external file in archive")
                 char name[16]  8.3 name padded: "NAME    .EXT" + 4 NULs;  if hdr bit31:
                                name[i] ^= "SOFTWAREREFINERY"[i]   (key at VA 0x1F180)
                 u32 offset   absolute file offset of data
                 u32 size
last 4 bytes of file          u32 = diroff   (directory ends at filesize-4)
```

Constraints enforced by the loader (`ResFileOpen`, VA 0x1F01A): count ≤ 5000 (0x1388), 3 archives open at most (table at 0x1EB44, 12-byte slots). The directory is **not sorted**; lookup is a linear scan comparing 12 characters (6 words) and `flags == 2` (`ResFileLoadByName` 0x1F1E7, `ResFileFindEntry` 0x1ECF8 → 0x1F406). Entries are contiguous: `offset[i+1] == offset[i] + size[i]` and the last entry ends exactly at `diroff`. No duplicate names.

Archive variants in the GOG package (all verified by `validate_assets.py`):

| File | Entries | Content |
|---|---|---|
| `SLIPSTRM.RES` = `SLIPMAX.RES` (byte-identical) | 2447 | everything except `MATHS.BIN` |
| `SLIPCD.RES` | 2448 | the above **plus `MATHS.BIN`** (40,972 bytes) |
| `SLIPMED.RES` | 1491 | as SLIPSTRM but **956 of 980 `.SMP` samples removed** |
| `SLIPMIN.RES` | 2 | `SMALL.FNT`, `SMALLEST.FNT` only |

Contents of `SLIPSTRM.RES` by extension: SMP 980 (44.6 MB), SPR 886 (12.1 MB), SHP 280 (0.59 MB), TRC 10, TRD 10, TRK 10, MAT 23, PAL 10, CAM 10, ART 11, ANN 32, FNT 18, HMP 7, BNK 2, ZON 2, ST0/ST1/ST2 52 each.

### 6.2 Resolution order (high)

`LoadFile`/`ResFileLoadByName`/`ResFileFindEntry` all try, in order: **`[0x1EB3C]` primary archive** (exe-adjacent `.RES`, opened by `FileOpenResEXE`), **`[0x1EB40]` secondary archive** (`SLIPCD.RES`), then **a plain DOS file with the requested name** (`DosOpen`). `MATHS.BIN` exists only in `SLIPCD.RES`, so with the GOG layout (`SourcePath=C:\`) the secondary archive is mandatory for the full game. Loose-file fallback means **assets can be overridden or supplied without archives**.

### 6.3 Resource manager model (high)

* **Handle table** at `[0x2433C]`, 16-byte entries (handle = u16 index; `SI` in the asm convention): `+8` pointer to loaded block (0 if not resident), `+0xC` offset of the name in the name pool (`-1` = anonymous).
* **Block header** (0x20 bytes before the data returned by `ResOpen`): `+0x10/+0x14` size, `+0x18` handle back-reference, `+0x1C` flags (bit meanings inferred and unverified: `0x02` set by `ResOpen` paths and cleared by `ResModify`, `0x04` set when the block is returned to the free counter, `0x10` set for blocks allocated by name/anonymous allocation …), `+0x1E` word **refcount**. `ResOpen` returns `block+0x20` and increments the refcount; `ResClose` decrements and unlocks.
* **Name pool** (`ResNamesInstall`, 0x9C40 bytes): interned 12-char names; `ResFind(ESI=name) → SI handle`, adding a name if unknown.
* **Memory**: linear memory obtained through DPMI 0500h–0502h, purge-on-demand (`LoadFile` purges other resources when memory is short).
* **Type registry** (`ResAddType`, ≤ 0x20 types): a 4-char tag + callback; `ShapesInstall` registers `'shp '` (0x73687020) with `ShapeLoad` as the loader so that `.SHP` blocks are linked at load time.
* **Anonymous blocks**: `ResAlloc(EAX=size) → SI` for engine buffers (Draw3D, Slots, Video back buffers, track grid).

### 6.4 Loader entry point per asset type

| Asset | Loaded by (VA) | How located | Notes |
|---|---|---|---|
| `.PAL` | `RaceLoadTrack` → `VideoSetPalette` 0x32C5C | `g_TrackFileNames[i-1]` (0x59790, entries 0–9) | `{u16 first,u16 count,RGB(6-bit)}` |
| `.MAT` | `Draw3DSetMaterials` 0x18D0F | `g_TrackFileNames[10+i-1]`; `CARS.MAT` appended | 2 calls merge tables |
| `.SPR` | `ResFindAndOpen` / `ResFindIDs` + blitters in 0x2Cxxx | material sprite patterns (`chiclwa*.spr`), UI strings | wildcard `*` picks variants |
| `.SHP` | `ShapeLoad` 0x25CF1 (type `'shp '`), `ShapeDraw` 0x25D80 | by name from TRD/ART/code | version 12 |
| `.ART` | `ArtSlotCreate` 0x116C3 → `ArticSlotInit` 0x11808 | `g_ArtNames` (0x54D78), `DRONE.ART` | recursive node tree |
| `.TRK/.TRD/.TRC` | `TrackLoad` 0x344DB | `g_TrackFileNames[20+i-1]` | TRD/TRC by last-char swap |
| `.CAM` | TV camera code (table `g_CamNames` 0x430F8) | | 60 positions |
| `.SMP` | HMI digital driver glue near 0x10Fxx/0x4Bxxx (`FxAddEngine` 0x4B86D; sample-name tables at VA 0x4ADE1, 0x52320, and the pointer table 0x45846) | by name | raw 8-bit |
| `.HMP/.BNK` | `HmiPlaySong` 0x10F44 / `melodic.bnk`,`drum.bnk` (0x10D1E) | | |
| `.FNT` | `FontsInstall` 0x55750; font code in 0x2A5FD | | |
| `.ST0/1/2` | `StrTabGet` 0x28685 (+0x2865E/0x286F2) | by screen tag | language select |
| `.ANN` | `InitScript` 0x5816E, `CheckScriptMem` 0x58261, `FreeScript` 0x5837A | `g_IntroAnnNames`, `g_TrackAnnNames` | script bytecode |
| `.ZON` | menu code (`CH_TEAMZ.ZON`, `RESGAMEZ.ZON`) | | hot-zone tables |
| `MATHS.BIN` | `MathsInstall` 0x211CE | by name | 3 tables |

---

## 7. Asset formats

All "Verified" statements are checked by `tools/re/validate_assets.py` against every file of that type.

### 7.1 `.PAL` — palette (verified, 10 files)
`u16 first_index (=0), u16 count (=248), count × {r,g,b}` — components are **6-bit VGA** (max 63). Size = `4 + 3·count` = 748. Loaded by `VideoSetPalette` (VA 0x32C5C: reads `EBX=u16[0], ECX=u16[2]`, then `0x32C74`). Colours 248–255 are not covered by the file (probably reserved for UI/engine use). The 748-byte block also appears appended to 38 sprites (below).

### 7.2 `.MAT` — materials (verified, 23 files)
Header `u16 count, u16 version (=1)`, then `count` × **46-byte** records; file size = `4 + 46·count`.

| Off | Size | Meaning (from `Draw3DSetMaterials` 0x18D0F) |
|---|---|---|
| 0x00 | 16 | name, space padded (looked up case-normalised by `Draw3DGetMaterialNumber`) |
| 0x10 | u8 | palette range start (shade ramp) |
| 0x11 | u8 | palette range end (clamped to start+0x70) |
| 0x12 | u8 | → runtime +0x50 (unknown) |
| 0x13 | s8 | → runtime +0x30 (unknown) |
| 0x14 / 0x15 | s8 / s8 | → runtime +0x18 / +0x1A (unknown) |
| 0x16, 0x18, 0x1A, 0x1C | u16 ×4 | → runtime +0x1C/+0x20/+0x24/+0x28 (unknown) |
| 0x1E | u16 | shift/bit count → +0x2C; derived `ramp end = start + range − ((1<<shift)−1)` |
| 0x20 | u16 | not read by the loader |
| 0x22 | 12 | **sprite/texture file pattern**, e.g. `chiclwa*.spr`, `Chiclf*.spr` (`*` = variant) |

Runtime record is **0x54 bytes**; the table is preceded by a dword count and kept at `[0x188AC]`; `Draw3DSetMaterials` can be called twice (track MAT then `CARS.MAT`) and **appends**. Track MAT files start with a record named `"Dummy"`. Material number ↔ name resolution is used by SHP/TRC loaders (§7.4, §7.7).
*Unknown:* the exact meaning of the numeric fields (lighting/transparency/specular flags are likely).

### 7.3 `.SPR` — sprites/textures (verified, 886 files)
```
+0   u16 width
+2   u16 height
+4   u16  (0 in 146 files; varies: 4, 320, 1, 235, 182, 27 …)   unknown (origin/hot-spot?)
+6   u16  (0, 3, 10 …)                                          unknown
+8   u16  0xFFFF or 0                                           unknown (transparency/flags?)
+10  u16 0, +12 u16 0
+14  u16  = 16 + width*height (mod 65536)  = offset of the optional trailing block
+16  width*height bytes   8-bit palette indices, row-major
[trail] optional: {u16 first, u16 count, count × RGB(6-bit)}   (748 B = 248 colours: 38 files; 559 B = 185 colours: 20 files)
```
Size is exactly `16 + w·h` for 828 files; the others are exactly `+748` (full-screen 320×200 backgrounds) or `+559` (driver portraits). Textures are plain **indexed** bitmaps; the *material* selects the palette ramp. Widths are arbitrary (321, 552 …); they are probably tiled/strip wall and floor textures.
*Unknown:* semantics of header words +4/+6/+8.

### 7.4 `.SHP` — 3D shapes (verified, 280 files; version 12)
```
+0x00 u16 version = 12                  (ShapeLoad rejects anything else: "Wrong ShapeVersion")
+0x02 u16 vertex_shift n                (stored vertices are scaled by 2^n; 245 files use 0, others 1–6)
+0x04 u16 flags                         bit0 = "linked" (cleared at load, set by ShapeLink 0x26167 after materials are resolved)
+0x06 u16 0 / 0xFFFF                    unknown
+0x08 u32 total size == file size
+0x0C u32 offset of sort data (BSP-like tree) or 0   (when present it starts immediately after the points)
+0x10 u32 offset of vertex block
+0x14 u32 offset of polygon block        (always 0x52 = header size)
+0x18 u32 offset of material-name table  (always runs to EOF)
+0x1C s32 bounding radius
+0x20..0x37  six s32: xmin,xmax,ymin,ymax,zmin,zmax      (ShapeGetExtents 0x26003)
+0x38 u16 NOSORT flag (non-zero ⇒ no sort data; ShapeDraw 0x25D80 then uses polygon order)
+0x3A..0x51  unknown (24 bytes)
vertex block:   u16 count, count × {s16 x,y,z}              (6 bytes each; shift by n)
polygon block:  u16 count, then variable-size records
material table:u16 count, count × {char name[16], u16 local_id}   (18 bytes each)
```
Polygon record (verified: walking all polygons of all 280 files ends exactly at the vertex block):
```
u16 info        bits 0..13 = N (vertex count)
                bit 14 (0x4000) = per-vertex normals follow           (6·N bytes)
                bit 15 (0x8000) = per-vertex texture coordinates follow (4·N bytes: u16 u, u16 v;  0x4000 = 1.0)
s16 nx, ny, nz  face normal (2.14)
u16 material    local material id → mapped through the shape's material table to a global material number at link time (ShapeLink 0x26167 / 0x261D3)
u16 flags       bit 2 (4) = skip/hidden (checked by the draw prep at 0x260FC)
u16 index[N]
[ per-vertex data: in the order indices, (UV if bit15), (normals if bit14) — total stride 12 + 2N + 6N·bit14 + 4N·bit15 ]
```
Observed: 5,676 polygons with normals only, 839 plain, 23 with UVs only, 39 with both.
*Unknown:* the sort-tree node format (needed only for painter's-order sort; a modern depth-buffer port can ignore it), header bytes 0x3A–0x51, polygon flag bits other than 4, and the exact order of the optional per-vertex arrays (stride is verified, order is not).

### 7.5 `.ART` — articulated vehicle/part descriptor (verified, 11 files; version 18)
Header (0x58 bytes): `u32 version=18`, three `s32` min and three `s32` max extents of the main body (+4…+0x18, e.g. Racer0: x∈[−2999,3000], y∈[−880,880], z∈[−4472,4472]), `u32 root_node_offset` at **+0x20** (always 0x58), two 8×`u32` tables at +0x28 and +0x48 (copied into the slot at +0x7C/+0x9C; look like speed/performance step tables), remainder unknown.

Node (`ArticSlotInit` 0x11808; **size = 0x1D0 + 0x10·ref_count**, nodes tile the file exactly):

| Off | Size | Meaning |
|---|---|---|
| 0 | 4 | tag (stored byte-reversed: `niam` = `'main'`) — observed: `main`, `fan1..4`, `jet1..4`, `shld`/`shl1,2`, `drv1,2` |
| 4 | u32 | child node offset (0 = none) |
| 8 | u32 | next sibling offset (0 = none) |
| 0x0C | s32 | → part +0xE0 |
| 0x10/0x14/0x18 | s32×3 | part offset (x,y,z) relative to parent |
| 0x1C | 8×14 | **8 shape names** (12-char name + 2 bytes), `ResFind`ed to handles; `Racer0.Shp`, `Racer0a..e.Shp` = body/damage variants |
| 0x8C | 8×14 | **8 more shape names** (`Racer0s/ss/sd.Shp` = shadow/detail variants) |
| 0xFC | 4×26 | **4 debris entries**: 14-byte name (`R0frg00.SHP`) + 3×s32 (scale/velocity parameters) |
| 0x164 | 4×26 | 4 more debris entries (`R0frg50..53.SHP`) |
| 0x1CC | u32 | reference-point count (copied up to 5) |
| 0x1D0 | 16×n | **reference points**: 4-char byte-reversed tag + 3×s32 position (`lasl`,`lasr`,`weap`,`head`,`smok`,`fan1`,`shld`,`drv1` …) |

Missing shapes raise *"ArticSlotInit - one of the body shapes is missing"*. Reference points are looked up by tag at runtime (`0x11DC0/0x11DFE/0x11E06`; four-char constants such as `0x66616E31` = `fan1`, `0x6A657431` = `jet1`, `0x6D61696E` = `main`, `0x68656164` = `head`) and drive engine/jet effects, weapon mounts and smoke (`WeapFSmoker`, `RaceSlotDamage` fail with "Missing reference point"). `DRONE.ART` (552 bytes) is a single `main` node.

### 7.6 `.TRK` — track cells (verified, 10 files; version 0x2B)
```
+0x00 u16 size (== file size)
+0x02 u16 version = 0x2B           (TrackLoad: "wrong data version")
+0x04 u16 linked flag = 0xFFFF     (TrackLoad: "this data is unlinked" if 0)
+0x06..0x0B  unknown
+0x0C u16 offset of the cell table
+0x0E u16 offset of the corner-point pool (== end of the cell table)
+0x10.. unknown (0xFFFF, −274, −31, 0xC002 in Chicago)
+0x18  10 × {s32 x,y,z}  = start-grid positions of the 10 ships (12 bytes each; y constant per track)
...
cell table @+0x0C:  u16 count, then count × 24-byte records:
    u16 kind/flags          (0x16, 0x20, 0x1C, 0x4, 0x6, 0xFFFF …)
    u16 prev_cell_offset    (valid on "linked" records; circular doubly-linked list along the road)
    u16 next_cell_offset
    u16 trd_offset          (offset into the TRD for the cell's objects; 0 = none)
    u16 corner[8]           offsets into the corner pool: the 8 corners of the cell (shared between neighbours)
corner pool @+0x0E: u16 count, then count × 8-byte records {u16 x, u16 y, u16 z, u16 ?}  (runs exactly to EOF)
```
Verified on all files: `cell_table_end == header[+0x0E]`, the corner pool is `u16 count + count×8` bytes and ends at EOF, and every prev/next value of "linked"-kind records points at a cell record. About half of the records are "linked" kind (road cells); the others carry a TRD pointer (decor cells) with `kind = 0xFFFF`. The **spatial grid** builder `3A940` clears `[0x33F08]` (0xF00 bytes = 960 dwords = 12 × 4 × 20 cells) and, for every cell record whose `trd_offset != 0`, takes the cell's *first corner* (`corner[0]` → three u16 words), converts it with `0x3AA0E` to grid coordinates `gx = x/12`, `gy = y/12 − 13`, `gz = z/12 − 18` (the `shl 20 … sar 20` pair around it only sign-extends) and stores `TRD base + trd_offset` at `grid[(gx + 12·gy + 48·gz)·4]`. So corner words are small integers in units of 12 and decor cells are found through this grid. (**Verified by reading the code, not mechanically.**)
*Unknown:* meaning of `kind` values, the corner record's last 2 bytes, the u16/s16 coordinate scale (`/12`), the +0x06–0x0B and +0x10 words.

### 7.7 `.TRC` — track geometry/slot definitions (verified, 10 files)
```
+0  u16 size (== file size)
+2  u16 0, +4 u16 0
+6  u16 offset of the record list (always 0x0A)
+8  u16 offset of the material-name table (ends exactly at EOF)
list @ +0x0A:  u16 count (82–210), then records, each begins with its u16 size (records chain exactly to the material table)
record:
    +0  u16 size
    +2  u16 offset of an inline sub-record (e.g. 0x2F in Chicago rec 0)            unknown contents
    +4  u16 offset of polygon list A (0 = none)
    +6  u16 offset of polygon list B (0 = none)    — when both exist, B comes first and the lists tile the record's tail
    +8..+0x13  six s16: xmin,xmax,ymin,ymax,zmin,zmax (bounding box)     [Chicago rec0: ±1192, ±984, ±3354]
    +0x14 s16 radius (= √(Σ half-extent²), verified 3691 for rec0)
    +0x16 u16 type/flag
    +0x18 u16 name-present flag (0xFFFF), +0x1A: 8-char name + NUL ("GRID    ", "CROWD …")
material table: u16 count (≈32), count × {char name[16], u16 local id}
polygon list:   u16 count, then records (stride verified on all 1211 records):
    u16 info    bits 0..14 = N vertices;  bit 15 = per-vertex texture coordinates follow the indices (4·N bytes)
    s16 nx,ny,nz   face normal (2.14)
    u16 ?       at +0x08 (0 in the samples seen)
    u16 material at +0x0A   (bit 15 set by the linker = material not found)
    u16 index[N] from +0x0C   [+ N×{u16 u, u16 v} (0x4000 = 1.0) if bit 15]
    stride = 12 + 2N  or  12 + 6N when bit 15 is set
```
Record kinds seen: 865 records with list A only, 141 with list B only, 205 with both (430 have a B list). `38B31` (the TRC link step during `TrackLoad`) resolves each TRC material name to a global material number (`Draw3DGetMaterialNumber`) and rewrites polygon `+0xA`. The track-slot name is read by `TrackSlotGetName` (0x35372) from the TRC record referenced by a TRD piece entry (`+2` of the 0x22-byte entry, §7.8). `DoGame3D` classifies the track slot under the ship by comparing the record name to `'CROW'` and `'GRID'` (0x574F5243 / 0x44495247) → camera / lap logic. `InitRefuel` (0x3D568) scans TRC polygons for refuel pads (**exactly 12-vertex polygons**) matched by material.
**Update (second session):** the inline sub-record at `+2` is the record's **vertex block** (`u16 n; n×s16[3]`, stored `>> 6`; polygon indices point into it) and each record is placed by a TRD piece — see `docs/formats/trc.md` / `trd.md`. *Still unknown:* the type/flag word and the `+8` word.

### 7.8 `.TRD` — track objects (verified structure, 10 files)
```
+0  u16 size (== file size)
+2  u16 offset of group list (always 0x0A)
+4, +6 u16 offsets of two further lists;  +8 u16 end of the group chain (= offset of the 4th list)
group list @+0x0A: u16 count (16–92), then groups chained by their leading u16 size
group: +0 u16 size, +2 u16 (increasing number; role unknown), 
       +4 u16 offset of a "piece" list (0 in many groups):  u16 count, count × 0x22 bytes:
              {+2 u16 offset of a TRC record, +0x10 u16 runtime handle (cleared by 0x36E06), +0x12/+0x16/+0x1A world position x,y,z (s32)}
       +8 u16 offset of an array of shape instances:
              u16 count, count × 0x46 bytes: {char name[12] ("chibra.shp"), u16 handle (runtime, +0x0C), …, +0x1C dword, +0x20 dword (extents filled at link)}
```
(`36E06` walks groups → `+4` piece lists and zeroes `+0x10` of every 0x22-byte entry; `InitRefuel` reads positions at `+0x12/+0x16/+0x1A` and follows `+2` into the TRC.)
`TrackLinkShapes` (0x347D3) opens each named shape (`ResFindAndOpen`), stores the handle at `+0xC`, and stores extents (`ShapeGetExtents`) in `+0x1C/+0x20`; a missing shape raises *"TrackLoad - missing a shape:- <name>"*. Tracks reference between 0 and 46 shape instances (Chicago 46: `chibra.shp`, `chitwa.shp` …). `TrackSetLapDist` (0x3BAF8) and `FindDoors` (0x3C324) walk the linked cells.
*Unknown:* the exact record layout of the 0x22- and 0x46-byte entries beyond the fields above, and the three other lists.

### 7.9 `.CAM` — TV camera positions (verified, 10 files)
720 bytes = **60 × {s32 x, s32 y, s32 z}**. Table of names at VA 0x430F8; used by the "TV Camera" view (F4). Plausibly camera cut positions along the track (positions in the same world coordinates as the start grid). *Unknown:* look-at/zoom data (none stored here).

### 7.10 `MATHS.BIN` — maths tables (verified; CD archive only)
Header: three `u16` offsets `6`, `0x4008`, `0x800A`. Tables: **sine quarter-wave** (8193 × u16, 0…0x4000, i ↔ angle `i·π/2/8192`, max error 1 LSB vs `round(sin·16384)`), **arcsine** (8193 × u16, angle 0…0x4000 for x∈[0,1] with 0x4000 = 90°), **arctangent** (4097 × u16, `atan(i/4096)` in 0x10000-per-turn units, 0…0x2000). Pointers: `[0x21198]`, `[0x2119C]`, `[0x21194]`. A 64-bit FPU path (`FSQRT`) is used when a coprocessor is present (`FpuInit` sets round-to-zero, 64-bit precision).

### 7.11 Container-level knowledge only
| Type | What is known | Confidence |
|---|---|---|
| `.SMP` | raw **unsigned 8-bit mono PCM** (0x80 silence), no header; 980 files; rate not recovered (CONFIG.INI lists 22/44 kHz mixer options) | high (format), low (rate) |
| `.HMP` | HMI MIDI-packed: header `HMIMIDIP013195`, 7 files (`INTRO WIN LOSE INGAME2/3/4/6`) | high |
| `.BNK` | HMI AdLib instrument banks (`ADLIB-` header) `MELODIC.BNK`, `DRUM.BNK` | high |
| `.FNT` | tag `FONT`; per-glyph (offset,advance) table; 8-bit bitmaps stepping by `w·h` (SMALL: 7×8 = 56 bytes) | medium |
| `.ST0/ST1/ST2` | string tables: records `TAG4 + u16 len + text\0`; ST0 English, ST1 French, ST2 German (selected by language), one set per screen (`2PLAYER`, `CONTROLS`, …) | high |
| `.ANN` | script/announcer bytecode: 8-char id + commands (speech sample names like `EC03`, timing words); interpreted by `InitScript` | medium |
| `.ZON` | `u16 count` (0xC8) + zone offsets (menu hot-spot tables) | low |

### 7.12 Non-RES files
`CONFIG.INI` (text; keys above), `SLIPSTRM.CFG` ("V24 Slipstream Config File" header, 1,615 bytes; loader `ConfigFileLoad` 0x4989C, writer `ConfigFileSave` 0x499E4; holds options, key bindings, joystick calibration), `SLIPSTRM.SAV` (save slots, `DoSaveGame` 0x53536; format not analysed), `DEBUG.LOG`, `GRAB0000.LBM` (screenshots, `GrabScreenLBM` 0x55F50), `slipstrm.par` (§15).

---

## 8. Track loading pipeline (verified by reading `RaceLoadTrack`, VA 0x594B3)

Called as `RaceLoadTrack(EAX = 1-based track index)` from `DoGame3D` (and from `PlayTrackIntro`):

1. `SlotsInstall(100)` — 100 slots × 0xAE bytes.
2. `0x2A70C(0x32)` — text tree buffer.
3. `ArticInstall(20)` — 20 articulated parts (0xD8 bytes each + 0x160-byte records).
4. `0x12960`, **`Draw3DInstall(400, 0)`**, `0x18ACD(12)`, **`ShapesInstall`** (registers the `'shp '` loader).
5. **PAL**: `ResFindAndOpen(g_TrackFileNames[i-1])` → `VideoSetPalette` → close.
6. **MAT**: `g_TrackFileNames[10 + i-1]` → `Draw3DSetMaterials`; then **`CARS.MAT`** appended.
7. `CollideInstall(0x40)` (0x132F0).
8. **`TrackInstall`** (0x33F0E): `TrackInstallSlots` / `TrackInstallSlotDraw` / `TrackInstallMem`, registers slot server id 1, allocates the 0xF00-byte spatial grid, installs collision callbacks (`TrackSlotCheckStatic`, `TrackSlotsCheckLOS`, 0x35642, 0x38C97, 0x38E84).
9. **`TrackLoad(g_TrackFileNames[20 + i-1])`** (0x344DB):
   * copy name; `ResFind`+`ResOpen` **`.TRK`** → `[0x33D3C]`; header words: `+0x9E` (s16) → `[0x33D08]`, `+0xA2` selects a colour constant (`0x830E0` or `0xBEA00` → `[0x33CC6]`), `+2 == 0x2B`, `+4 != 0`;
   * last character → **`D`** (`.TRD` → `[0x33D40]`) and → **`C`** (`.TRC` → `[0x33D44]`);
   * `0x3A940` build spatial grid from the TRK cell table; `0x36E06` clear runtime handles in the TRD sub-lists; **`TrackLinkShapes`** (0x347D3); **`TrackSetLapDist`** (0x3BAF8); **`FindDoors`** (0x3C324); reset counters; **`0x38B31`** link TRC materials; **`InitRefuel`** (0x3D568).
10. `TrackReset` (0x34880), `0x1346C`, `0x19253`.
11. **Per-track scenery hook**: `call [0x595F8 + (i-1)*4]` — ten functions that install animated scenery/sky (clouds, hills, sun) for each track; their sprite wildcard patterns are at 0x43268–0x43AA8 (`cancld*`, `hawcld*`, `loncld*`, `eghill*`, `norhill*`, `Amacld*`, `NYcl**`, `NYSUNS`).
12. `TrackSetCameraRange` (0x34CAA).

Track index 1–10 = **CHICAGO, HAWAII, TOKYO, NORWAY, CAVE ("France" in the UI), CAN ("Arizona"), AMAZON, LONDON, EGYPT, NEWYORK** (arrays at 0x54E25 for UI names, 0x59790 for files, 0x430F8 for `.CAM`, 0x57930/0x580D8 for `.ANN`).
After loading, ships are placed on the grid from the TRK start positions (`RaceInitRacer` 0x593A6 → `ArtSlotCreate`, `SlotCreate`, `TrackSlotAdd`).

---

## 9. Vehicles, entities and physics

### 9.1 Slots (entity system) — `SlotsInstall` 0x26748 (high)
Array at `[0x26628]`, count `[0x26632]`, **slot size 0xAE**; doubly linked list through `[0x26634]`/`[0x26638]`.

| Off | Meaning |
|---|---|
| +0x00 u16 | in use |
| +0x02 u16 | flags (bit0 moved this frame; bit1 suspended) |
| +0x04 / +0x08 | next / previous (draw/update list) |
| +0x14,+0x18,+0x1C | **position x,y,z** (s32) |
| +0x2C | speed (s32); movement uses `(speed·dt>>14)·dir>>14` |
| +0x30 | callback (collision/extent? — unverified) |
| +0x34, +0x38 | **server callback** `+0x38` receives messages in AX |
| +0x3C | secondary callback (post-update); +0x40/+0x44 further hooks |
| +0x48…+0x59 | **3×3 orientation matrix** (9 × s16, 2.14; initialised to identity 0x4000) |
| +0x5A,+0x5C,+0x5E | **direction vector** (s16 ×3; = matrix third row) |
| +0x60…+0xAD | 0x4E bytes of server-private data (ship state, track-slot data, …) |

Messages: **0x101** created, **0x104** update (ECX = frame delta ms, EBX = delta secs-2.14), **0x105** draw; ships also use **0x106/0x107** (bonus/pickup, collision), **0x108** reset, **0x200/0x201** (set driver id / attach roster record), **0x202** destroyed/damage. `SlotsAddServer(AX=id, EDI=fn)` registers up to 10 servers. Frees during iteration are **deferred** (`[0x2663C]` depth, "SlotFree: Deferred list full").

### 9.2 Vehicle instantiation (high/medium)
`RaceInitRacer` (0x593A6) → `ArtSlotCreate` (0x116C3) loads `RACERn.ART` (names at 0x54D78) and calls `ArticSlotInit` to resolve every named shape; `SlotCreate` (0x26CC5) creates the slot with server `RaceSlotControl`; the **roster** (`g_Roster` 0x54444: `u16 count` + 0x4E-byte driver records; copy at `[0x592D0]`) stores per-ship race state (record +0x24 = position/lap, +0x0D = finished flag, +0x1A id …).

### 9.3 Physics outline (medium; verified reading of `RaceSlotMove` 0x51B0A)
* Inputs: control block (steer, pitch, button word) from `RaceReadControls` (player 1 `0x54420`, player 2 `0x5442C`) or `RaceAIControl` (0x5135E); button bit0 = **throttle**; stick values are `<<4` and clamped to ±0x4000.
* Per-ship parameter block: `[0x502CC + id·4]` (id 1–10, 28 bytes each at 0x502F8 + 0x1C·(id−1)). Observed layout (units: world units/s, 2.14 for factors):

| id | f0 thrust@0 | f1 thrust@top | f2 coast decel | f3 top speed | f4 | f5 | f6 |
|---|---|---|---|---|---|---|---|
| 1 | 71500 | 35750 | 128700 | 289575 | 1.2 | 1.0 | 244000 |
| 2 | 64350 | 39325 | 132275 | 293150 | 1.2 | 1.0 | 244000 |
| 3 | 76505 | 39325 | 128700 | 284570 | 1.2 | 1.0 | 244000 |
| 4 | 71500 | 35750 | 128700 | 286715 | 1.2 | 1.0 | 244000 |
| 5 | 71500 | 35750 | 128700 | 287430 | 1.2 | 1.0 | 244000 |
| 6 | 75075 | 35750 | 128700 | 285285 | 1.2 | 1.0 | 244000 |
| 7 | 78650 | 32175 | 130130 | 289575 | 1.2 | 1.0 | 244000 |
| 8 | 71500 | 35750 | 128700 | 293150 | 1.2 | 1.0 | 244000 |
| 9 | 85800 | 35750 | 125125 | 282425 | 1.2 | 1.0 | 244000 |
| 10 | 92950 | 42900 | 121550 | 288860 | 1.2 | 1.0 | 244000 |

  With throttle the code computes `a = f0 − (f0−f1)·(speed/f3)` (thrust falls linearly from f0 at rest to f1 at top speed, `speed/f3` obtained with an `IDIV` into 16.16); without throttle `a = −f2`. `a` is scaled by a bonus/handicap factor (`[0x51EB8]`, 0x4000 = 1.0; +0x2000 while the +0x3E timer runs, −0x1000 while the +0x40 timer runs), then `speed += a·dt` (dt = delta secs in 2.14) and the result is clamped to `f3 × factor`. `f4`/`f5` enter the lateral/vertical response (`0x51E14`); drag is applied as `vel −= vel·4·dt` per axis in 2.14; hover/bank offsets are in `RaceSlotHover` (0x51EC4); the move is committed through `0x13474/0x134E6` (collision). **Field roles are inferred from the arithmetic; verify before relying on them. `f6` (244000) is not used by this routine.**
* Timers in the ship's private data are counted down in ms every update (+0x12, +0x24…+0x30, +0x3E, +0x40, +0x4C): shield, boost, damage, ... Charges at +0x16/+0x18/+0x1A ramp toward 0x4000.
* **Bonuses** (`RaceSlotControl` msg 0x106): pickup types 0–5 (from `0x42BCB`): each either clears a timer (+0x2A or +0x2E), refills a charge (+0x32 = 0x4000), starts a 5-second timer (+0x26/+0x2E = 0x1388), or adds 0x32 to a racer-record counter (+4, probably cash). Unknown type → *"RaceSlotControl: Unknown bonus type."* Low–medium confidence on the semantics.
* **Weapons** (names in the UI strings): Blaster, Disrupter, Super Frag, Seeker, Super Seeker, Ambler, Scrambler, Hyper Neuro, Smoker, Bomber, Mini Mines; seven weapon shapes (`AIRMINE AMBLER BOMBER FRAG HYPER SCRAMBLE SEEKER .SHP`, table 0x5BF42); code in 0x5C0F9–0x5D8B1. Low confidence on per-weapon mapping.
* **Damage/explosions**: `RaceSlotDamage` (0x52035), `RaceBangInstall/RaceBangSlotControl` (sprites `Expl*`, `ExplF*`, `SmkBlk*`, `SmkGry*`, `Fire*`; materials `SPARK`, `SPLASH`, `FRAGBACK`).
* **Randomness**: `Random(AX)` (0x242C0) uses a lagged-additive 3-word generator seeded from the BIOS tick (0x46C); `DoGame3D` saves/restores the seed around recorded races.

### 9.4 Collision (medium)
`Collide*` module (0x13000–0x1695C): collision "cubes" (8 corner points) and face points per slot, `CollideSlotSetMainCube`, `CollideSlotReadMainCubePoints`, `GetFreeFacePoint`; per-frame `CollideStep` (0x137A2); track callbacks `TrackSlotCheckStatic` / `TrackSlotsCheckLOS` (line of sight). The `collidecubes` developer option displays the cubes.

---

## 10. Main loops

### 10.1 Front end — `GameMain` (VA 0x557B6)
Saves `ESP` in `[0x55DF6]` (the `PauseMenu` result 4 jumps to 0x55DEE, which restores ESP and returns from `GameMain`, i.e. quit to DOS), calls `IntroLogos` (0x565FE: `GREMLOGO.SPR`, version id), then loops at `0x557D1`: `MainMenu` (0x4CD6B) returns an index; `-1` ⇒ idle ⇒ **attract-mode demo** (random track `Random(9)+1`, random ship, `RunRace` with demo flags `[0x5440E]=[0x54410]=[0x54414]=1`). Valid indices dispatch through the 5-entry jump table at **0x5591B**:

| Index | Handler | Meaning (medium/low) |
|---|---|---|
| 0 | 0x5592F | restart: logos/intro |
| 1 | 0x559D8 | start game: switch on `g_GameMode` `[0x543F0]` ∈ {0x11,0x12,0x13}; track/car selection screens, link-up wait screens, then `RunRace` |
| 2 | 0x55943 | calls `0x4208E` (screen unidentified — low confidence: ship/garage view) |
| 3 | 0x5596E | load saved championship (`0x53318`) then mode 0x13 |
| 4 | 0x5594F | options (`ConfigMenu` 0x472FB) |

Mode 0x13 is the **championship** (race counter `[0x543FE]`, standings screens `CHAMPPOS`/`FINALPOS`, `WIN.HMP`/`LOSE.HMP`). `[0x543F4]` selects players (0 = one, 1 = two/link). Other mode meanings are unverified.

### 10.2 `DoGame3D` (VA 0x586F2) — high
Arguments: `AX` = track index, `ESI` = roster pointer, `EBX` = optional script pointer. **Setup:** record/playback start (`[0x543D9]`), roster copy, `RaceLoadTrack`, per-ship `ArtSlotCreate`+`SlotCreate`+`TrackSlotAdd`, camera and HUD sprites (`GAMEF*.SPR`), music (`INGAME2/3/4/6.HMP` table at 0x5801A). **Per-frame loop (0x58B17 → 0x591B2):**

1. `FrameClockUpdate` (0x12E7B); toggle page `[0x5BFC1]`
2. `RaceHandleKeys` (0x59869 → `PauseMenu` 0x59904); screenshot key → `GrabScreenLBM`
3. HUD/panels (`0x44654`, `0x4429D`, `0x5A34C`), track-state queries (`TrackSlotCheckRefuel`, `TrackSlotGetName`)
4. pause state machine `[0x592DA]` (0 running / 1 paused / 2 other)
5. controls: **record** (`ReplayRecordFrame`) or **playback** (`ReplayPlaybackFrame`)
6. scripted events/camera (`0x43EA3`, `0x44054`); countdown timers using `GetFrameDelta`
7. **update:** `RaceUpdate` 0x50446 → **`SlotsUpdateAll`** 0x26925 (→ `RaceSlotControl` per ship, msg 0x104) → **`CollideStep`** 0x137A2 → `RaceBangUpdate` 0x4F36C → track dynamics 0x34494/0x34B6A
8. **draw:** **`SlotsDrawAll`** 0x26967 (msg 0x105, painter's order via the slot list) → `RaceDrawHud` 0x5A4EC
9. `RaceCheckFinished` 0x44022 ≠ 0 ⇒ leave, else repeat.

**Teardown** (0x591D9…): free slots/handles, `RaceUnload` (0x59808: `TrackUnload`, articulated-part release, slot uninstall), sound stop, `ArtSlot` release, restore RNG. The function ends with the error string *"DoGame3D: Error."*

### 10.3 Replay determinism (medium)
`RunRace` sets `[0x543D9]=0` (record), `RunRaceReplay` sets 1 (play back). Recording stores the 6-byte control block per frame (`ReplayRecordFrame`) and the PRNG seed (`RandomGetSeed`); playback restores the seed and feeds the controls, returning CF at end of data. Hence **simulation depends only on (seed, controls, frame deltas)**; frame delta capping (≤1000 ms, ≥1 tick) is part of the model. A port that wants replays/network parity must reproduce `GetFrameDelta` semantics.

---

## 11. Rendering

### 11.1 Video (verified by reading `VideoInstall`)
`VideoInstall(EAX=mode)`: mode **0 = auto** (VESA if `VesaDetect` succeeds, else mode 0x55), **0x55 = VGA mode 13h, 320×200×8, double-buffered** (two 0xFA00-byte `ResAlloc` buffers `[0x2A924]/[0x2A928]`, row pointers `[0x2A930]` ×200), **0x56 = mode 13h single back buffer**, **0x57 = VESA** (`VesaSetMode`, pitch 0x140). Pitch `[0x2B294] = 0x140`; `TextMapTabInstall` (0x33316) builds the text-map (texture-mapping) tables at `[0x33310]`. 8-bit palette via VGA DAC ports 0x3C7/0x3C8/0x3C9 (6-bit components). The in-game "Detail" menu offers Very Low/Low/Medium/High.

### 11.2 3D pipeline (medium/low)
`ShapeDraw` (0x25D80) → set transform (`0x25D5C` stores EAX..EDI = position/matrix inputs) → `ShapeLink` if needed → **point transform loop** (`0x1DCF7` with callbacks `0x26295` (shift only) / `0x262D8` (3×3 matrix, 2.14, `SAR 14−n` then translate)) → polygon submit through `Draw3D` front-end (0x19DD8/0x19E0D/0x19F1F; polygon `info` bit 15 selects the textured path) → sort (BSP tree at SHP+0xC via `0x2739C`, or polygon order if NOSORT) → clip/edge (0x1B098/0x1B14B…) → **video rasterisers** in 0x2Cxxx–0x31xxx (flat, Gouraud, textured; the textured scanline core is the 2,288-instruction function at **0x2DE6D**, a classic unrolled affine/perspective span filler using IDIV slopes and the text-map table). Lighting: `InstallSpecularTables` (0x1E136) builds specular lookup tables; materials map to **palette shade ramps** (start/end/shift), so shading is done by indexing ramps rather than per-pixel RGB.
Track drawing is slot-based: each track slot's server draws its cell/polygons (`TrackInstallSlotDraw`, `SlotDrawAlloc`), plus `TrackDraw`-prefixed door slots (`DOORS` material).
Sprites (HUD, effects, sky): `ResFindAndOpen` + blitters at 0x2C7B7/0x2CC14/0x2CCD0/0x2CDBE; fades via `MakeFadePalette`; `SpriteGreyscale` for pause screens.

---

## 12. Input, timing, sound, networking

### 12.1 Input (high)
`InputInstall(flags)`: allocates a DOS-memory buffer, copies the **INT 15h/4Fh keyboard-intercept real-mode stub** (obj2 VA 0x702C0, 0x171 bytes) and chains it; maintains a **256-byte key table** `[0x1FC0C]`: **bit0 = newly pressed** (consumed by `KeyHit` 0x204F5 via `BTR`), **bit1 = held** (`KeyHeld` 0x20514). `KeyGetScancode` 0x2029B / `KeyGetAscii` 0x2035A (tables at 0x203F5/0x20475). If `[buf+0x100]` is set the BIOS (INT 16h) is used instead. **Mouse**: INT 33h (reset, motion counters AX=0Bh), cursor clamped to a rectangle; `ignoremouse` disables it. **Joystick**: game port, per-stick calibration (`JoystickCalibrate` steps 0 centre / 1 default / 2 extents / 3 finish; 6 words per stick saved in `SLIPSTRM.CFG`). A **timer callback** (24 Hz, fallback 18 Hz) polls devices (`InputPollIsr` 0x1FE93). Race controls per the original `READ.ME`: cursor left/right steer, cursor up/down pitch (inverted: up = nose down), Space accelerate, Enter (Alt in the GOG text) fire, S (Ctrl) select weapon, F1–F5 cameras (Cockpit, Chase, Rear, TV, Free; the free camera uses the numeric keypad), Ctrl-Q quit. Control block = `{steer s16, pitch s16, buttons u16}` per player.

### 12.2 Timing (high)
`TimerInstall` (0x2878A) measures the PIT, installs an IRQ0 handler (real-mode stub in obj2), and offers **5 callbacks** with `TimerAddCallback(AX = Hz, EBX = fn)` (PIT divisor `0x1234DC / Hz`). `FrameClockInstall` (0x12D80) registers a tick callback at **min(maxRate, 70) Hz** that adds `0x3E80000 / rate` (16.16 ms) to a 64-bit clock `[0x12E54:0x12E58]`. `FrameClockUpdate` (0x12E7B), once per loop iteration, computes `delta = clock − last` (clamped to ≤1000 ms and ≥ one tick), stores `[0x12D6C]` (ms), `[0x12D70]` = `delta·16.384` (seconds as 2.14) and `[0x12D68]` = `1000/delta` (fps). `GetFrameDelta` (0x12F03) returns them in **EAX, EBX, ECX**. So **the simulation is time-based, not frame-locked** (speeds are per second); the frame rate is capped by the clock quantum (≤70 Hz).

### 12.3 Sound (medium)
HMI SOS via `hmidrv.386`/`hmimdrv.386`; digital samples = `.SMP`, music = `.HMP` (+ `.BNK` for FM). `SoundInit` 0x10AA4 and `Hmi*` glue 0x10xxx; speech/FX tables (`EM*.SMP` male, `EF*.SMP` female, `EPS0–9.SMP`, `JETPASS1`, `SCRAPE1/2`, `CRASH`, `BLASTER`, `MISSILE` …) at VA 0x4ADE1–0x52ED0. Mixer options in `CONFIG.INI` (`O 256 8 22` …).

### 12.4 Networking (medium)
Serial/modem (`ComInstall`, `ComConnectSer`, `ComSend`, Hayes strings) and **IPX** (`IpxInstall`, socket UI `IPX Socket:`). Link-up screens exist for Serial/Modem/Network (`LINK_S/M/N.SPR`); the in-race link exchanges happen in 0x59ae3+ ("Waiting for other player to select ship…"). Not relevant to a single-player port.

---

## 13. Lookup tables, filename tables and global state

Filename/pointer tables (all arrays of 10 unless noted; addresses = VA):

| Table | VA | Contents |
|---|---|---|
| `g_TrackFileNames` | 0x59790 | 30 pointers: 10 `.PAL`, 10 `.MAT`, 10 `.TRK` |
| `g_TrackDisplayNames` | 0x54E25 | Chicago, Hawaii, Tokyo, Norway, France, Arizona, Amazon, London, Egypt, New York |
| `g_ArtNames` | 0x54D78 | `RACER0.ART … RACER9.ART` |
| `g_DriverNames` | 0x54E98 | Charles Edward-Royce, Rysho, Horst, Victoria Venice, The Shaman, Slayed, Cobra, Isis the Crisis, Kin and Gin Matsu, Ted 'Malibu' Beech |
| `g_CamNames` | 0x430F8 | `<TRACK>.CAM` |
| `g_IntroAnnNames` / `g_TrackAnnNames` | 0x57930 / 0x580D8 | `*INT.ANN` / `<TRACK>.ANN` |
| `g_InGameMusic` | 0x5801A | 4 × `INGAME*.HMP` |
| `g_WeaponShapeNames` | 0x5BF42 | 7 `.SHP` |
| `g_CarClassParamPtrs` | 0x502CC | `dword[11]`; entries 1–10 → 28-byte blocks at 0x502F8… |
| Speech/FX sample tables | 0x45846 (10 ptrs), 0x4B38F (packed ids), 0x52320 (28-byte entries) | `EM..`/`EF..`/`EPS..` .SMP |

Maths/lookup tables: **MATHS.BIN** (§7.10), key-scancode→ASCII tables (0x203F5, 0x20475), PIT constant `0x1234DC`, specular tables (generated at start by `InstallSpecularTables`), text-map tables (generated by `TextMapTabInstall`).

Key globals: see the *Global variables* section of `function-map.md` (slot array, resource tables, frame clock, track pointers, game mode flags `0x543F0..0x543FC`, control blocks `0x54420/0x5442C`, roster `0x54444`).

---

## 14. Protection / anti-tamper (medium)

* `ProtectionTrampoline` (0x5BB6E) is called from `main` instead of `GameMain`. Its body is an **obfuscated jump chain** (dozens of interleaved `jmp`s, bit-shift/xor arithmetic over `[0x5BBCB]`, a **self-modifying** `xor [0x5BC55]`), which ends with `jmp 0x557B6`. Disassemblers produce bogus "functions" there (0x5BBCB, 0x5BBF1 are not real).
* The string `CODEWHEEL CODES:AI` (VA 0x50DA0) and the `argv[1]=="5000"` launcher gate suggest a **code-wheel / launcher check**; no check is enforced in the data pipeline analysed here. **A port should not need any of this.**

---

## 15. Developer / hidden features (medium)

* **`slipstrm.par`** parser at VA 0x103E3 (no direct caller found; dead or reachable only in special builds): keywords `driversinrace` (2–10), `collidecubes`, `allowedtrack` (1–10), `notitles`, `testweap`, `soundfiles`, `ignoremouse`, `mainmenu` (0–6), `track` (0–10), `champstart` (0–10), `driver` (0–10), `framerate`, `noaimove`, `rollingdemo`, `speed`, plus car tuning lines (`car %i %i %i %i %i`, yaw/pitch scale, `<turn …`).
* `DEBUG.LOG` writer (`DebugLogString` 0x4A098), position/value dump strings, `Free Memory: %ld of %ld`, `Track: Drawn %d slots, %d cells, %d comps.` (an on-screen statistics overlay, 0x453B8).
* Ctrl-Q exits to DOS at any time (documented in the original README).

---

## 16. Confidence summary, unknowns and recommended next steps

### 16.1 What is solid enough to build on now
* RES container (read all assets) — **verified**.
* `.PAL`, `.MAT`, `.SPR`, `.SHP`, `.ART`, `.CAM`, `MATHS.BIN` — structure **verified** (see unknown fields).
* Track data **structure** (`TRK` cells, `TRC` records/polygon lists/materials, `TRD` groups/shapes) — verified to the level of record boundaries; coordinate semantics still partial.
* Engine concepts: slots, messages, frame-delta time base, 2.14/angle conventions, load order — **high**.

### 16.2 Known unknowns (ordered by importance for a port)
1. ~~TRC polygon vertex source and coordinate scale~~ (solved, see formats docs); TRK corner records (units of 12) and how `gx/gy/gz` relate to world coordinates; TRD 0x22/0x46-byte entry fields; the `kind` values. Needed to render tracks.
2. `.SHP` sort-tree format (optional for a depth-buffered renderer), header 0x3A–0x51, per-vertex array order, polygon flag bits.
3. Material numeric fields (transparency/lighting/specular flags) and sprite header words +4/+6/+8.
4. Physics field semantics (parameter blocks f4–f6, the full `RaceSlotMove` and `RaceAIControl` behaviour, collision response, weapon damage tables).
5. `.SMP` sample rate, `.FNT`/`.ZON`/`.ANN` exact layouts, `.ST*` language mapping for ST2.
6. Real-mode stubs in obj2 (only two of several identified), `SLIPSTRM.CFG`/`.SAV` formats.
7. ~22% of the code object (≈85 KB) is data or code not reached by static discovery (some of it hand-written asm reachable only through callbacks).

### 16.3 Recommended order for Phase 3 (asset readers)
1. **RES reader** + loose-file override (done conceptually; trivial).
2. **PAL + SPR + MAT** (small, verified) → can already render menus and textures.
3. **SHP** (verified polygon/vertex/material layout) → render ships and scenery objects.
4. **ART** (verified tree) → assemble vehicles with parts/reference points.
5. **TRC/TRK/TRD** — finish the semantic decoding using the consumers listed in §7 (start from `TrackSlotAdd` 0x348D0, `TrackInstallSlotDraw` 0x3CE7E, `InitRefuel`, 0x3A940); validate by rendering one track.
6. **CAM, MATHS.BIN** (regenerable but keep original tables for bit-exact angles), then sound (`.SMP` raw PCM, `.HMP`→MIDI, `.BNK`).
7. Physics: port `RaceSlotMove` and the class table after the track geometry exists, using the recorded-input replay model as a regression oracle.

### 16.4 Reproducing this analysis
```
tools/re/build.sh                      # disassembly database (needs libcapstone; see README)
python3 tools/re/validate_assets.py <game dir>
python3 tools/re/show.py 586f2         # annotated disassembly of a function
python3 tools/re/genmap.py > docs/function-map.md
```
