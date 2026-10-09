# Research log — Phase 2 (executable analysis)

Date of work: 2026-10-07. Subject: `SLIPSTRM.EXE` and the `SLIP*.RES` archives from the GOG "Slipstream 5000" package (read-only input folder `slip5000/`). Output: `docs/executable-analysis.md`, `docs/function-map.md`, this file, and `tools/re/`.

Conventions: **[V]** verified mechanically against the files; **[H]** high-confidence inference (original strings / exact behaviour); **[M]/[L]** medium/low. "VA" = virtual address (object 1 base 0x10000).

---

## 0. Starting state and constraints

* The project folder `slipstream-macos/` was **empty** when Phase 2 started (no Phase-1 documents, no `docs/`). All Phase-2 documents were created from scratch; if Phase-1 notes exist elsewhere they were not available to me.
* No Ghidra/IDA/radare2 on the machine. Available: Apple clang, Python 3.14, `ndisasm`, `objdump`, Homebrew `capstone` 5.0.9 (library only; `pip` is broken in this sandbox: `truststore`/`mac_ver` ValueError).
* Rules followed: the game folder was only read; nothing was executed; nothing was downloaded; extracted assets were kept in the scratch area and **not** added to the project.
* The user's scope for this phase: identify architecture and the asset-loading pipeline; **do not** start the SDL port; **do not** mechanically translate the whole program; record evidence and confidence for every rename; stop and report.

---

## 1. Chronology

### 1.1 Inventory (min 0–5)
* Listed `slip5000/`: `SLIPSTRM.EXE` (744,703 B), `SLIPSTRM.RES` (58,737,788 B) = `SLIPMAX.RES` (cmp identical), `SLIPCD.RES` (+41,000 B), `SLIPMED.RES`, `SLIPMIN.RES`, `CONFIG.INI`, `SLIPSTRM.CFG`, `SLIP.BAT`, `READ.ME`, `Manual.PDF`, GOG/DOSBox scaffolding, `PS.map`/`XBOX.map`.
* `PS.map`/`XBOX.map` looked like symbol maps; they are **DOSBox keymapper files** (`key_esc "key 27"` …). Dead end, noted so nobody tries again.
* `file(1)`: *MS-DOS executable, LE for MS-DOS, DOS4GW DOS extender (embedded)*. `SLIP.BAT` = `logo.exe`, `playgdv intro.gdv`, `slipstrm 5000`.

### 1.2 Container parsing (min 5–25)
* Wrote an LE parser (`le.py`). First attempt failed: it assumed the first MZ/BW chain; the bound DOS/4GW stub contains several `MZ`/`BW` signatures. Fixed by scanning for `LE\0\0` and the MZ whose `e_lfanew` points at it (MZ at 0x26654, LE at 0x290A4).
* Data-page base `0x4D854` was validated by arithmetic (`0x4D854 + 104·4096 + 0x4AB = 0xB5CFF` = the file size exactly) and by locating the entry code (`eb 76 …` at file offset `0xA11CC`). [V]
* Parsed the fixup tables: 17,665 records, all 32-bit internal offsets. Applying them gives absolute addresses (indispensable for string/global cross-references in a flat image whose code embeds absolute pointers).
* Dumped the three objects. Obj 2 (0x486 bytes) later proved to be 16-bit real-mode stubs.

### 1.3 Tooling (min 25–60)
* `dis.c` (C + libcapstone) and then a ctypes wrapper in Python (`an.py`) for the interactive work.
* Function discovery in three layers (all in `tools/re/`):
  1. `disc.py` recursive descent from the entry point, call targets, and relocated function pointers; follows `jcc`, `jmp` and `jmp [reg*4+table]` using fixup-resolved tables.
  2. `disc2.py` validated *tentative* decoding of gap start addresses, E8-call targets inside gaps and prologue signatures (reject on undefined/privileged opcodes, double-zero bytes `00 00`, out-of-code branch targets).
  3. `db.py` per-function flood-fill, call graph, relocation-based data xrefs, tail-jump function starts (jump targets shared by ≥5 functions) and **orphan promotion** (decoded code that no direct call/pointer reaches, e.g. `slipstrm.par` parser).
* **Bugs found and fixed while building it** (kept here because they affect trust in the listings):
  * Fall-through after `INT 21h / AX=4C00` (program exit) decoded the following zero padding as `add [eax],al` and then garbage. Fix: stop on exit calls and on `00 00`.
  * First pass reported 53% coverage with fake functions inside zero-filled buffers (e.g. the "function" `0x25870` was a 0x14-byte zero buffer; the real `AddResName` starts at `0x25885`). After the fix, coverage settled at **77.9%** of the 382,148-byte code object (297,749 B) and **2,788 functions** (1,966 + 822 orphans).
  * The common error exit (`ESI = msg; JMP 0x20CBD`) was being absorbed into each caller; tail-jump promotion made `FatalError` a function.
* Remaining ≈84 KB of the code object is data (tables, strings, buffers) plus some unreached code. `tools/re/build.sh` reproduces the database in seconds.

### 1.4 Signatures and strings (min 60–90)
* `strings` on the bound image: "DOS/4GW Professional Protected Mode Run-time", "Rational Systems", "%s is not a WATCOM program" → DOS/4GW Pro; `WATCOM C/C++32 Run-Time system … 1988-1994` at VA 0x63979 right after the entry jump → Watcom C/C++32 10.x. [H]
* Object 1 contains ~1,800 strings. Crucial discovery: **the programmers' fatal-error messages include the name of the function that emits them** (`ResOpen - failed because LoadFile …`, `TrackLoad - wrong data version`, `ShapeLoad - Wrong ShapeVersion`, `DoGame3D: Error.` …). A script harvested these and mapped them to the function containing the reference (`func_strings.txt`) giving ≈150 high-confidence names. Names appearing only as part of such strings are tagged *original* in `function-map.md`.
* Asset name strings give the complete extension inventory (`.SPR .SHP .MAT .TRK .ART .PAL .CAM .ANN .SMP .FNT .HMP .ZON .CWL .BNK`) and wildcard patterns (`RACER*.SHP`, `GAMEF*.SPR`, `CAR?_FR?.SPR`), plus the hidden `slipstrm.par` keywords and `DEBUG.LOG`.
* Build stamp and credits strings dated the build (24/04/95) and identified The Software Refinery / Gremlin Interactive and the HMI sound OS.

### 1.5 The archive format (min 90–120) — **the key result**
* SLIPMIN.RES (11,592 bytes) starts with a `FONT` blob and ends with 8 odd-looking trailing bytes. Looking at the tail of every archive: a repeating pattern `…ware|…NERY` inside 28-byte records.
* Insight: `ware` (lower-case, spaces→`w`…) and `NERY` (zero bytes) are *fragments of the plaintext key* `SOFTWAREREFINERY` — the file name field is XOR-ed with it (space ⊕ 'W' = 'w'; NUL ⊕ 'N' = 'N'). This explained both the odd text and the key string found at VA 0x1F180 in the exe.
* Format derived on SLIPMIN (2 entries: `SMALL.FNT` 7874 B, `SMALLEST.FNT` 3654 B), then confirmed on the exe's code: `ResFileOpen` (0x1F01A) seeks to EOF−4 (`AX=4202h`, CX:DX=−4), reads the directory offset, reads the count (`and 0x7FFFFFFF`, limit 0x1388), multiplies by 0x1C, reads the entries and XORs the name field with the key when bit 31 was set. [H]+[V]
* `validate_assets.py` confirms on all five archives: parse ends exactly at EOF−4, entries are contiguous (no gaps/overlaps), flags always 2, no duplicate names. Archive relationships: `SLIPSTRM` = `SLIPMAX`; `SLIPCD` adds only `MATHS.BIN`; `SLIPMED` drops 956 samples.
* `ResFileLoadByName` (0x1F1E7) normalises names to 8.3 12-character form and linearly searches comparing 6 words; `LoadFile`/`ResFileFindEntry` try primary archive, secondary archive, then a loose file.

### 1.6 Resource manager and file layer (min 120–170)
* Mapped the DOS file layer (INT 21h wrappers `DosOpen/Create/Close/FileSize/ReadAt/WriteAt/ReadLine/Rename/Delete`) by `mov ah, XX` scanning every `int 21h` (the survey also located the Watcom CRT's own I/O at 0x6BD78+).
* Resource handle table / block header / name pool / types reconstructed from `ResOpen`, `ResClose`, `ResFind`, `ResAlloc`, `AddResName`, `LoadFile`, `ResAddType`. Conventions: CF = error, SI = handle, EAX = data pointer.
* `FileOpenResEXE` (0x1EF74) turned out to walk the DOS environment block for the program's own path and rewrite `.EXE`→`.RES`: the primary archive is *always the executable's sibling* (`SLIPSTRM.RES`).

### 1.7 Startup (`main`) (min 170–200)
* Entry → `main(argc,argv)` at VA 0x10010. Sequence documented in `executable-analysis.md` §4. Noted: `argv[1]=="5000"` flag at `[0x54000]` is written but has no other static reader.
* `MathsInstall` → `MATHS.BIN` failure produces the "CD or file is missing" message — this is why `SLIPCD.RES` must be reachable.

### 1.8 Engine core: slots, timer, input, maths (min 200–300)
* **Slots** (0xAE-byte entity records) reverse-engineered from `SlotsInstall`, `SlotAlloc`, `SlotCreate`, `SlotsUpdateAll/DrawAll`, `SlotSendMessage`, `SlotMoveStep`. Messages 0x101/0x104/0x105 are the same codes `RaceSlotControl` dispatches on. [H]
* **Timer / frame clock**: `TimerInstall` (PIT calibration, real-mode ISR copy from obj2) and `FrameClockInstall/Update`. The formulas for `GetFrameDelta`'s three outputs were derived instruction by instruction (`0x3E80000/rate`, `delta·16.384`, `1000/delta`). [H]
* **Input**: INT 15h/4Fh keyboard hook with a 256-byte table (bit0 new press, bit1 held), mouse INT 33h, joystick port polling and calibration state machine. [H]
* **Maths**: `MathsInstall` loads three tables from `MATHS.BIN`. Header offsets 6/0x4008/0x800A; tables verified numerically against `sin`, `asin`, `atan` (max error 1 LSB): table0 = quarter-wave sine 2.14, table1 = arcsine (0x4000 = 90°), table2 = arctangent (4097 entries, 0x10000 = 360°). The angle unit is therefore 0x10000 per turn. [V]
* PRNG found at 0x2428D (3-word lagged additive), seeded from the BIOS tick (0x46C); seed save/restore in `DoGame3D` tied to replay. [H]

### 1.9 Race pipeline (min 300–360)
* `RaceLoadTrack` (0x594B3) read in full → the exact load sequence (§8 of the analysis). `TrackLoad` (0x344DB) read in full: name-suffix swap `K→D→C` is how TRD/TRC are found. [H]
* `DoGame3D` (0x586F2): setup, per-frame loop order, teardown, and record/playback hooks. The shape of the loop is **high** confidence; individual HUD helpers are low.
* `GameMain` (0x557B6): the front-end loop and the 5-entry jump table at 0x5591B; attract-mode demo triggered when the menu returns −1.
* `ProtectionTrampoline` (0x5BB6E): reading the jump chain by hand shows `jmp 0x557B6` at its end; its arithmetic touches `[0x5BBCB]`, `[0x5BBCC]`, `[0x5BC55]`. Marked as obfuscation rather than game logic. [M]

### 1.10 Asset formats (min 360–520)
Approach for each format: (1) find the loader/consumer from strings and call graph, (2) read what the loader reads, (3) formulate a *structural* hypothesis that predicts exact sizes/offsets, (4) test it on every file of that type, (5) record the exceptions. All of these are re-runnable in `validate_assets.py`.

| Format | Hypothesis → result |
|---|---|
| `.MAT` | `4 + 46·count` fits all 23 files [V]; field map from `Draw3DSetMaterials` |
| `.SPR` | `16 + w·h` fits 828/886; the remaining 58 are exactly `+748` or `+559` = a trailing PAL block `4 + 3·n` [V]; header word +14 equals `16 + w·h` |
| `.PAL` | `4 + 3·248`, components ≤ 63 → 6-bit VGA [V] |
| `.SHP` | **First hypothesis (info = vertex count only, optional 6 B normals) matched 236/280.** The 44 failures contained polygons with bit 15 set; `CHITWA.SHP` showed 16 bytes of 2.14 (u,v) pairs after the indices → bit 15 = UVs. Revised stride matches **280/280**. Header: +8 = file size, polygons at 0x52, sort data directly after the vertices, materials to EOF, +0x38 = NOSORT [V] |
| `.ART` | hierarchical nodes; node size `0x1D0 + 16·refpoints`; nodes tile all 11 files exactly [V] |
| `.TRK` | header, 10 start positions, cell table (24-B records) ending at header+0x0E, corner pool `u16 count + count·8` to EOF [V]. First attempt assumed every cell record has valid prev/next links (only ~half do: "linked" kind vs decor kind with a TRD pointer) |
| `.TRC` | records chain exactly to the material table at EOF [V]. **First hypothesis (single polygon list per record) failed on 205/1211 records**: when both list offsets exist, list B sits before list A and they tile the tail of the record; confirmed 1211/1211 |
| `.TRD` | group chain ends at header+8; shape arrays lie inside groups [V]; entry sub-layouts partial |
| `.CAM` | 720 B = 60 × 3 × int32 [V] |
| `MATHS.BIN` | see §1.8 |
| `.SMP/.HMP/.BNK/.ST*/.ANN/.ZON/.FNT` | header sniffing only (see analysis §7.11) |

* **Mistakes caught during review:** (1) I first believed the name field read by `TrackSlotGetName` lives at `TRC record+2`; it is the TRD *piece entry's* `+2` field that points at the TRC record, and the name is at `record+0x1A` (validated: `GRID` at 0x26 = record(0xC)+0x1A). (2) I first listed the TRC polygon "bit 14" like SHP; TRC only distinguishes bit 15 (`and 0x7FFF` in the linker). (3) The ship parameter table's first pointer slot (index 0) is code bytes, not a table entry; ship ids are 1-based.

### 1.11 Vehicle physics survey (min 520–560)
* `RaceSlotControl` (0x5047C) message dispatcher; `RaceSlotMove` (0x51B0A) arithmetic read to extract the thrust-vs-speed relation and the per-ship parameter blocks at 0x502F8 (ten 28-byte blocks). The table values themselves are data and are recorded in the analysis; their semantic labels remain **medium/low**.

### 1.12 Packaging (min 560–600)
* Research tooling copied to `tools/re/` with `build.sh`, `genmap.py`, `validate_assets.py`, `data/names.json` (curated names with evidence), `data/gnames.json`. Rebuild from scratch reproduced the same database (2,788 functions; 262 names; 59 labels).
* `docs/function-map.md` is generated, not hand-edited.

---

## 2. Naming ledger

Policy: a function gets a **high** name only when (a) its own error text names it, or (b) a structural/behavioural property was verified against data or exact algebra (e.g. `Sin`, `RandomNext`, `ResFileOpen`). Descriptive names invented by us are **medium**; guesses are **low**. Names that merely paraphrase a nearby string are marked low. Every entry in `tools/re/data/names.json` stores `name`, `conf`, `evidence`, rendered in `function-map.md`.

| Batch | Basis | Count (cumulative) |
|---|---|---|
| A | Startup, DOS/Res/Slots/Track/Shape/Artic error strings, archive layer, timer, input | 78 |
| B | Automated harvest of every `Name - message` string + timing/input/maths/video helpers | 216 |
| C | Race flow (`RunRace`, `ReplayX`, `ErrorExit`, `GetLastError`), per-frame loop members, RaceSlotMove/AI/controls | 247 |
| D | `RaceLoadTrack` callees, `TrackInstall`, `VideoSetPalette`, `ParFileParse`, `RaceUnload`, `ConfigFile*` | 262 |

Statistics at the end of the session: **2,788 functions recovered; 262 named (151 high, 86 medium, 25 low); 59 data labels**.

---

## 3. Validation summary (what a skeptic can re-run)

`python3 tools/re/validate_assets.py <game dir>` → **30 checks, 0 failed**:

* 5 archives × (directory parse ends at EOF−4, contiguity, flags all 2) = 15 checks
* MAT (23 files), PAL (10), SPR (886 + trailing blocks), SHP (280 ×2 checks), ART (11), TRK (10 ×2), TRC (10), TRD (10), CAM (10), MATHS.BIN (3 tables)

Not validated mechanically (read from code only): all executable-flow claims (startup order, `DoGame3D` loop order, resolution order), real-mode stub roles, physics parameter semantics.

---

## 4. Open questions carried into the next phase

1. TRC polygon vertex source and scale; TRK corner coordinates (units of 12) and the `kind` word; TRD 0x22/0x46-byte entry fields and the three auxiliary lists; TRD group `+2` id. (Render a track to settle them.)
2. SHP: header 0x3A–0x51, sort-tree node format, polygon flag bits other than 4, order of the optional per-vertex arrays.
3. Material numeric fields; SPR header words +4/+6/+8.
4. What `[0x54000]` (set by `argv[1]=="5000"`) controls; whether `ParFileParse` is ever called.
5. `.SMP` sample rate; `.ZON/.ANN/.FNT` exact layouts; `SLIPSTRM.CFG`/`.SAV` formats.
6. Physics: roles of f4–f6, the AI controller (`RaceAIControl`), collision response, weapon effects tables (0x5C0F9–0x5D8B1).
7. Which real-mode stubs exist in object 2 besides the timer (0x70440) and keyboard (0x702C0) handlers; whether serial/IPX ISRs live there.
8. ≈22% of the code object is not reached by static discovery; some may be callbacks registered through tables I did not follow.

## 5. Suggested checks before relying on specific claims

* `RaceSlotMove` parameter roles: instrument by feeding the recorded-input model (when a runnable reference exists) or diff against DOSBox traces.
* `FrameClockUpdate`: the 70 Hz cap and `delta` clamping matter for physics parity; confirm by timing a DOSBox run.
* Mode numbers 0x11/0x12/0x13: only 0x13 (championship) has convincing evidence.

---

## 6. 2026-10-07 (second session) — Phase 3–6 first pass

* **Phase 1 inventory** written (`docs/original-file-inventory.md`), including GOG/DOSBox wrapper analysis; install tiers in `INSTALL.INI` confirm the RES variants (`slipmin/med/max.res` → `slipstrm.res`).
* **Track placement solved** (was the main unknown): TRC records carry a vertex block at `record+2` (`u16 n; n×s16[3]`, 86/86 records end exactly where the polygon lists start) and coordinates are stored `>> 6` — found in `0x38524` (bbox compare `movsx; shl 6`). TRD **piece** entries (0x22 B, `+2` TRC record offset, `+0x12` s32 position) place each record; `0x383F8` is the spatial query (grid cell → group → pieces → containing record). A pure-Python top-down plot of `piece.pos + (v<<6)` drew a closed Chicago circuit with its branch — visual confirmation, no rotation needed.
* **TRD scenery instance layout**: `+0` name[12], `+0x10` s32[3] position, `+0x24` 3×3 s16 matrix (2.14). Position values are in the same frame as pieces.
* **SHP polygon arrays order**: indices, **per-vertex normals, then UVs** (verified by vector lengths ≈ 16384 vs UV ranges).
* **+y is up**: floor polygons have ny = +0x4000; ship start y lies between floor and roof of the nearest piece.
* **Ship scale**: ART/SHP ships are ~64× smaller than track units; start-grid spacing (68.8k) ≈ ship length ×8, so the viewer displays ships ×8 (SPECULATIVE). ART shapes face −z (observed).
* Built the **C++20/SDL3 project** (`src/…`, CMake): parsers (RES/PAL/SPR/MAT/SHP/ART/TRK/TRC/TRD/MATHS), scene builder, software renderer, SDL3 app, headless screenshot/bench modes, CTest suite, macOS `.app` packaging. All 10 tracks render (textures, scenery, ships). One bug along the way: unbounded ART traversal looped forever on a cyclic child/sibling pointer; fixed with a visited set.
* Findings to carry forward: 1–2 TRC records on NORWAY/LONDON/EGYPT have no piece placement; backface culling by stored normals hides legitimate ground in Egypt → left off.
* **Ship scale revised (user feedback):** the earlier ×8 was too large. Rendering the Chicago grid at ×1/×2/×4 shows ×2 fits the painted start boxes. `--ship-scale` had also never taken effect (`buildScene` ignored it); fixed. Default is now 2.
* **Ship facing corrected (user feedback: "controlled backwards"):** I had inferred −z from a screenshot and rotated ships by π. ART reference points prove the opposite: exhaust (`smok`, z=−5400) and `fan1` (z=−2300) at the rear, `head`/`shld` at +z. Ships now render with their nose along the driving direction.
* **Blue rectangles between sectors (user report):** they were polygons of the material `Dummy` (palette index 250, outside the 248-colour palette; 23 materials across the MATs, ~28 polygons in Chicago's TRC). They are helper/portal surfaces closing the sector openings and are now not drawn (INFERRED, original draw path not read). Also: palette index 252 (`GreenNav`) gets a placeholder green; drive mode enables backface culling and a lower chase camera so tunnel roofs seen from outside do not hide the track.

## Session: "buildings drawn through the track" investigation (no code change)

Question: user reports scenery buildings appearing through the road at some points.

Checks performed (scratch scripts outside the repo):
- Scenery vertices/edges vs. road floor in a band 300..8000 above the floor (ship height): CHICAGO 0 hits (vertices), TOKYO 0, NEWYORK only `nyboat1` boats (on water, harmless), LONDON `lonpauls`/`lonbpal`/`lonben`/`stand2` placed with origin exactly on the road surface (y = 0x180000 = floor) — STRONGLY INFERRED legitimate (landmarks/stands straddling the road).
- Scenery matrix convention (v·M vs M·v): v·M fits better (unchanged).
- TRC polygon lists A/B are not LOD copies (A = road surface, B = walls/side geometry), so drawing both is correct.
- Renderer depth path (1/z buffer, near clip) reviewed: no defect found.
- CHICAGO: scenery baseline y = 0x100000 for every instance while road pieces run 0.95M–1.01M, i.e. the road dips up to ~1.4 units below the scenery baseline, so towers (e.g. `chitwb`) legitimately overlie tunnel sections. Looks like an underpass, not misplacement (INFERRED; needs comparison with the original under DOSBox).
- Conclusion: no confirmed defect. Specific location (track + approximate spot) needed to continue.

## Session: buildings through track walls / garbled decals (Chicago, user-supplied camera positions)

Findings (CONFIRMED by pixel probing of the software renderer):
- The garbled texture above tunnel mouths was **z-fighting** between a coplanar decal (`yellowbars`, hazard sign) and its wall (`Light Wall B`): equal depth, order-dependent noise. Fix: near-tie (relative 2e-5) depth tests let the later-drawn surface win.
- Scenery shape scale is correct (stored bounding radius in TRD scenery entry +0x1C ≈ √2 × max |vertex|<<shift matches). Towers are large (~230k–290k units, centred on their origin) and their volumes really overlap tunnel/road geometry in Chicago (road ~0.95–1.02M, tower base ~0.93M).
- Mitigation (HEURISTIC, not original behaviour): scenery polys are tagged; (a) scenery must be >30% nearer than a road fragment to cover it; (b) tall scenery whose box contains road vertices ("backdrop", e.g. towers over tunnels) never covers road; bridges (flat spans) excluded. How the original avoided this (portal/sector visibility via TRD groups and the Dummy portal polygons?) is UNKNOWN.

## Session: towers standing in tunnels (Chicago 2664956,979516,5678095) — general fix

CONFIRMED: TRD scenery towers (`chitwk` etc.) have volumes that physically contain parts of the road piece boxes (piece 10680 is an 8-vertex tube that the tower occupies); in the original they cannot block the road, so the way the original hides them (per-cell scenery lists: TRD groups 0–16 hold scenery in grid cells; visibility logic unknown) is not reproduced.
Fix (HEURISTIC, applies to all tracks): at scene-build time every tall scenery instance (height > 0.6 × horizontal extent; bridges excluded) has the part of its polygons inside any overlapping road-piece bounding box (+1%) cut away (convex polygon minus box, UVs interpolated). The earlier depth-layering tolerances in the renderer stay as a safety net. Buildings above a tunnel roof remain.

## Session: grey rectangle in a Chicago sector portal (4482423,1020419,3331339)

CONFIRMED by probing: the rectangle is material `Trench Ent` (MAT pattern `alltex.spr`, which does not exist in the data → flat-shaded, palette ramp 2–17). 12 such polygons exist in Chicago; they are vertical quads/hexagons at tunnel/sector boundaries. The 4-vertex one at the probed spot spans the whole road cross-section with drivable floor on both sides, so it cannot be a solid wall.
Fix (INFERRED): `Scene::hidePortalPolys` hides flat vertical polygons whose material name ends in " Ent" and which the road passes straight through (floor at the same height on both sides). A geometry-only version also matched start cages and walls on other tracks, so it is name-gated; no other track has an "Ent" material. Possible analogues on other tracks (`Tunnel Mouth` Norway, `Road Plug` London, `Gap` Tokyo, `Tunnel`) are NOT touched — unverified.

## Session: binary analysis of track drawing — visibility classes (CONFIRMED from code)

Reading the scenery draw routine `0x37931` (the only `ShapeDraw` caller for TRD shape instances) and the track cell draw code `0x39A6E / 0x39C58 / 0x3A5B6`:
- `[0x33EF0]` is the **camera's visibility class mask**. In the per-frame track draw (0x39D50–0x39D61) it is set to `TRC_record(camera cell)[+0x16] & 0x5F`; it is `0xFFFF` when there is no cell (0x39CD6).
- A scenery instance is drawn only if `entry[+0x36] & [0x33EF0] != 0` (0x37931–0x3793C). A track record is drawn only if `rec[+0x16] & mask != 0` (0x39A7C, 0x3A55C); records with class bit 8 additionally need bit 8 in the mask (0x39A89–0x39AA0).
- Scenery entry `+0x38 != 0` selects a camera-facing (billboard) orientation computed from the camera angle instead of the stored matrix (0x379C9–0x37A40); `+0x1C` (bounding radius) feeds a projected-size cull (`0x1A9A4` vs `[0x33CF2]`). Not implemented yet.
- Class bits seen: Chicago all records = 1 (so no effect there), London 0x40/0x08/0x01/0x02/…, Hawaii 0x01/0x10/0x08/…, Tokyo 0x01–0x09. Meaning of each bit (likely "area/level" groups such as upper road, tunnel, pit) UNKNOWN, but the mechanism is what hides sections that are not meant to be visible from the camera's area.
Implemented: `TrackRecord::visFlags`, `SceneryInstance::visMask`, `Scene::visMaskAt` (camera record = nearest piece box; INFERRED, the original uses the track-slot under the camera), renderer `visMask`; HUD shows the mask, **F5** toggles (env `SLIP_NOVIS=1` starts disabled). Chicago's tower-in-tunnel overlap is NOT explained by this (all class 1); the geometric cut-out heuristic stays.

## Session: scenery billboards and projected-size cull (CONFIRMED from code, implemented)

From `0x37931` (scenery instance draw), `0x1A9A4`, `0x350C7`, `0x227C4`:
- **Billboard** (TRD scenery entry `+0x38 != 0`): the stored matrix is ignored; the orientation is a yaw built from the normalised (object − camera) vector in x/z, (a,b) = (dx,dz)/len in 2.14, matrix rows (b,0,−a), (0,1,0), (a,0,b) applied as v·M (so model +z points away from the camera). Used by Hawaii's palms. Our two-sided renderer shows the same silhouette from every side (verified by rendering a palm from two positions). Whether the original model faces ±z does not matter for symmetric foliage; unverified for asymmetric ones.
- **Size cull**: `size = radius * 256 / z` (z = camera-space depth of the entry position, radius = entry `+0x1C` u32); drawn if `z <= near` (always) or `size > threshold`. The threshold `[0x33CF2]` is set by the Detail option in `0x350C7`: **0x20 / 0x14 / 0x0A / 0x05** for detail 0..3 (the same routine also selects four distance limits, `[0x33CE6/EA/EE/F6]` = {0x9880?…} not yet understood). We use the highest detail (5); `SoftwareRenderer::minScenerySize` holds it. At highest detail this removes only very distant scenery.
- Per-entry `+0x36` detail/visibility mask semantic updated: it is ANDed with the same camera class mask as the TRC records (see previous entry).

## Session: portal-based visibility (CONFIRMED structure, implemented; supersedes the "Trench Ent" name gate)

The track is drawn as a **portal engine** (`0x39C58` → recursive `0x39E67`, draw pass `0x3A491`):
1. The camera cell is found by `0x383F8`/`0x38524` (bbox + inner-side test of the list-A polygons without flag 0x40; see `docs/formats/trc.md`).
2. From that piece the engine walks the three links of each TRD piece entry (+4/+8/+0xC = {neighbour entry, portal polygon}). A portal polygon (TRC polygon flag bit 0) is followed only if the camera is on its normal side (backface test `0x193FF`/`0x1995B`); the neighbour is visited with the portal's screen rectangle intersected with the current window; revisits widen the window (`0x3A2BD..0x3A33C`); up to 0x300 pieces; the piece we came from (`[0x33E84]`) is skipped.
3. Visited pieces and the entities whose owner piece is visited (`0x3A5B6`/`0x3A7E8` check the visited list) are drawn through `0x3A491`, each inside its window. There is **no z-buffer**: draw order is the sorted list (painter's algorithm), which is why overlapping geometry (towers in tunnels) never showed in the original.
Port: `TrackPiece::links`, `TrackPolygon::flags/offset/list`, `Scene::pieceBoxes` (bbox, planes, links), `SoftwareRenderer::computePortalVisibility` (rectangular windows, per-piece scissor), pieces outside the graph (decor/crowd/grid) always drawn, no cell ⇒ draw everything. Portal polygons are not drawn: this replaces the earlier name-gated `Trench Ent` hack (Chicago's blocking quad is a portal with flag bit 0 and a non-Dummy material; other tracks have a few as well: Norway `Water Trrf`/`Rock1Top`, Can `TrackWall1`, New York `Refuel End`×2 — now hidden by the same rule, UNVERIFIED visually).
**Validation:** 10 tracks × ~12 pieces × 3 headings (camera on the road): culled vs unculled renders are pixel-identical in almost all cases (max 3k of 57k pixels, mostly 0), i.e. the portal graph, window logic and cell test reproduce what the z-buffer would show — strong confirmation of the decoding. F5 / `SLIP_NOVIS` / `SLIP_NOPORTAL` toggle for comparison.
Not done: scenery owner-piece gating (how a TRD scenery entry is attached to a cell is not decoded; the geometric cut-out of towers stays), painter's order, polygon-accurate window clipping.

## Session: scenery attachment, BSP painter order, polygon flags (CONFIRMED structure, implemented)

**How scenery attaches to the track** (0x375E3, 0x37601, 0x37819, 0x378AD, 0x3A5B6, 0x3A7E8, 0x2739C):
1. `.TRK` cell table is a **BSP tree** over a lattice of 2^20-unit cells (see `docs/formats/trk.md`); leaves point to TRD groups (34 leaves = 34 groups). Each group holds its pieces (+4 list) and scenery (+8 list); all of them lie inside the leaf box (checked).
2. The engine walks the tree **far side first** (children: +2 = high side, +4 = low side), culling leaves by the outcodes of their 8 corners, and processes each leaf's group. Inside a group, a **draw-order tree** (TRD group `+6`, same node format as SHP sort trees: 24-byte nodes `{u32 childA, u32 childB, u32 item, u16 type, s16 d (-1 = leaf), s16 normal[3]}`, A = positive side, type 2 = piece entry, else scenery entry) gives the far-to-near order of the pieces and scenery of that leaf.
3. Pieces are drawn only if the portal walk reached them; **scenery is drawn only once the traversal has passed the first leaf that contains a reached piece** (flag `[0x33EEC]`, set in 0x3A754, tested in 0x3A7E8): scenery in leaves farther than the farthest visible piece is skipped (in the original; the toggle F7 shows it anyway). Scenery is also subject to the class mask, the size cull and a frustum test of its radius.
4. There is **no global z-buffer**: items are painted in that order (later over earlier). This is what made the Chicago "towers inside tunnels" and the grey `Trench Ent` quad invisible in the original, and it fixes every case you reported without any of my earlier heuristics (the tower cut-out is now only available with `SLIP_CUT=1` for the legacy z-buffer path; the depth-layer tolerances only apply when `F6`/painter mode is off). Verified: all four reported Chicago camera positions render correctly with painter mode and no cut-out.
**Polygon flags** (0x3948C `test al,5`, 0x193FF): list-A polygons with flag bit 0 (portal) or bit 2 are not drawn; every drawn track polygon is back-face culled (so culling is now the default in track view). Amazon's/Canyon's sky-hiding "roof" polygons carry flag 0xC (hidden) — which is why they must not be drawn.
Port: `Track::bsp`, `Track::groupTrees`, `Scene::bspOrder/groupOrder`, `ViewerApp::renderTrackPainter`, `SoftwareRenderer::drawMesh(..., item)` (items overwrite each other, depth test only inside an item). Plane offsets of the draw-order trees are derived from the item positions (the stored `d` field scale is not decoded; ordering is robust unless the camera is between two items that straddle a plane).
Toggles: `F6` painter order, `F7` all scenery, `F5` portals+class masks; env `SLIP_NOPAINTER`, `SLIP_ALLSCENERY`, `SLIP_NOPORTAL`, `SLIP_NOVIS`, `SLIP_CUT`.
**Open:** the stored plane distance `d` (units), the non-drawn "far list" semantics of list B (+6) polygons, `[33ef0]==0x10` special class (pit?), the 4 distance limits chosen by the Detail setting, Watcom `0x36695` (non-reached pieces after the flag), polygon-accurate window clipping.

## Session: second binary-analysis round (CONFIRMED unless stated)

New facts, all from disassembly (addresses in brackets), then checked against the renderer:
1. **Tree plane** [0x273C8, 0x193FF]: a group tree node's `+0x10` is a *point index*, not a distance; plane = through that group point with the stored normal. `193FF` returns "front" iff `dot(n, p - cam) < 0`. Implemented exactly now (`GroupTreeNode::plane`), replacing the midpoint estimate.
2. **Polygon flags** [0x3948C list A: skip if `flags & 5`; 0x3872C list B: skip if `flags & 0x15`]; both lists back-face culled with the stored normal; flag 0x8000 switches to a screen-space winding test (`0x1995B`); the high flag byte (0x01..0x7F) selects a lighting/shading routine (`0x3F2C8`, "SD*" light materials, light/dark floors) — NOT implemented (our flat shading ignores it); flag bit 1 adds a decal/overlay pass (`0x1A198`). List B is drawn by default callback `[0x33E60]=0x3872C`; the piece draw by `[0x33E5C]=0x39A6E`, scenery by `[0x33E64]=0x37931`, group fallback `[0x33E68]=0x378AD` (values read from the exe data image).
3. **TRK header flags** [0x34524]: `+0x9E` = portal-only (see `trk.md`), implemented (`Track::portalOnly`, viewer draws frustum-visible unreached pieces on the other six tracks). `+0xA2` = cheaper rasteriser beyond 0x830E0/0xBEA00 units (not needed).
4. **Texture LOD** [0x3948C/0x3872C + 0x19354]: a textured polygon is drawn *flat* (material ramp) when its nearest vertex depth (units × `[0x180FC]` = 1.0) exceeds `[0x33CE6]`, set by the Detail option in `0x350C7` to 0.68 M / 3.9 M / 10.2 M / 24.4 M world units for detail 0..3 (and the scenery size thresholds 32/20/10/5). At detail 3 nothing is flattened. Not implemented (we run at the highest detail).
5. **Render-mode bits** [0x3AB7A]: per item, bits 2 and 4 of `[0x180CC]` (perspective-correct / quality) are cleared for items nearer than `[0x33CEA]`/`[0x33CEE]` depending on the Detail options — rasteriser quality only.
6. **Group order tree is the SHP sort tree** [0x2739C used by 0x25D80, 0x1247F and 0x37819]: the same `{A,B,item,type,d,normal}` node format orders SHP polygons — relevant for a future shape renderer.
Validation against the port: tests pass; all 4 user-reported Chicago camera views and London/Egypt/Tokyo/Norway/Cave/Can/Amazon/NY contact sheets re-rendered after the changes without regressions; exact tree planes agree with the earlier estimates (no visible change), London's ceiling polygons flagged `0x4` are now correctly hidden.
Still open: lighting routine `0x3F2C8` (colour accuracy), `[33EF0]==0x10` class (pit/special), the detail option plumbing, polygon-accurate window clipping, entity (ship) ordering through `0x37B46`.

## Session: skyline, window clipping, ship ordering, lighting (CONFIRMED, implemented unless noted)

1. **Why the painter mode lost the skyline** (found by re-reading the frame sequence 0x3924F): the frame runs (1) the portal walk + a first BSP traversal that collects reached pieces and flag-gated scenery, then (2) **0x3AA60**, which — unless the track is portal-only (TRK +0x9E) — runs a *second plain BSP traversal with the default draw callbacks* (0x39A6E pieces, 0x37931 scenery), drawing every frustum-visible item far-to-near inside the union window (and calls the per-track sky hook `[0x33D2C]` first), and only then (3) draws the collected list over it (0x3A491). The flag `[0x33EEC]` therefore only gates the *third* pass, not visibility. Implemented in `ViewerApp::renderTrackPainter`; skyline now matches the z-buffer reference. The window used by pass 2 is the bounding rectangle of the front-facing extent-frame polygons (flag 0x8) of the reached pieces, not the portal windows.
2. **Scenery normals were not rotated** by the instance matrix (found because back-face culling made the tower sliver-thin): fixed, also relevant for lighting.
3. **Windows are rectangles in the original** (0x18BD1 sets a clip rectangle; 0x1A6F5 returns the bounding box of the clipped polygon), so "polygon-accurate clipping" is not needed; the port now clips the projected portal polygon against the current rectangle before taking its bounding box (`SoftwareRenderer::polyRect`) — the same as 0x1A6F5.
4. **Ships** are entities of the piece they stand on and are drawn right after that piece's polygons (0x39B9C `call 0x37B46`) with the piece's window; ships on pieces not drawn that frame are not drawn. Implemented (ship drawn with the piece's item id).
5. **Lighting law** (0x1CCE9, 0x1C4B6, 0x1958F, 0x595DD–0x595F1; see `docs/formats/mat.md`): light straight down, D = 0x3333, A = 0x0CCC, no fog, material coefficients from the MAT record. Flat colour = ramp[start + (end−start)·shade]. Implemented for flat polygons (textured polygons keep our approximation; the original maps textures through lookup tables not decoded yet).
6. **Procedural polygon detail** (flag high byte; see `docs/formats/trc.md`): implemented panel lines for types 2–7 (non-textured polygons; `PanelDetail`) and the road-floor family 0x86–0x8B (border/lane polygons and seams). Template data are read from the user's executable at runtime. Not implemented: type 1 / 0x1E (animated lights, refuel pads), cage/trim types 0x80–0x85, 0x8C–0x93, 0x8F/0x90, the translucent variant (`[0x3F059] = -1`, bit 1 of the low flag byte), the animated SD* colours (`0x3F26E`).
7. Texture shading (palette lookup/specular tables, `0x1E136`), Gouraud polygons (MAT +0x13), Detail option plumbing and the "far" quality bits remain open.

## Session: texture shading, wire cages, animated lights (CONFIRMED, implemented)

1. **Textured polygons are not lit** in the original: `19E0D` → `1C753` hand the rasteriser only the texture pointer and the per-vertex UVs (no shade register); texture pixels are palette indices used as they are. The per-material shade ramps matter only for flat polygons and for the far-LOD fallback (texture dropped beyond `[0x33CE6]`). The port now draws textures at full brightness (it used to darken them with an invented 0.45–1.0 factor). The four texture slots per material (`+0x40 + [0x188E8]*4`, set by `1A38C`) are per-detail variants managed by a texture-memory budget (`0x19067`, scale `[0x191E4]`), not lighting.
2. **Wire-grid cages** (flag high byte 0x80–0x82, 0x84, 0x85, 0x8C–0x8E, 0x91, 0x92; handlers `0x40138…0x4175A`): the polygon is not drawn at all; a template of midpoints (same format as the panel templates) and a list of 2-point lines are drawn in the material's 80 % ramp colour (the SDCage material for 0x80–0x82). So the start cages are see-through wire fences, not solid walls. (The far branch of `0x3F3C4` projects first and interpolates in 2D; the port always interpolates in 3D.)
3. **Animated lights**: clock `[0x3F078]` accumulates the frame time in 2.14 seconds (`0x3F26E`). Chase phase = `((~t) & 0x1FFF) >> 11` (four steps per half second). Type 1 (tunnel roof lights, `0x3F5BE`): the base polygon, then 4 quad pairs of SDOrangeLight (the pair whose index equals the phase is lit = ramp end, the others ramp start), plus 8 more pairs at the highest detail (`1ABCB`). Type 0x8F (`0x3FA5C`): floor chase strip, 8 pairs of SDFloorLight (pair index & 3 == phase lit), no base polygon. Type 0x1E (`0x40865`, refuel pads): the base polygon plus 8 polygons pulsing in SDBlueLight: colour = start + (end−start)·f, f = 0x2000 + (tri(t)>>1), tri = triangle wave of period 1 s (0x3F26E). Type 0x90 (`0x4147E`): road floor with SDRoadLine centre/lane lines (two lane polygons at 80 % and one step darker). All polygon index lists and templates are read at runtime from the user's executable (`src/game/panel_detail.cpp`; relocated pointer tables hold object-relative offsets, VA = value + 0x10000).
4. Not implemented: type 0x93 / 0x14 (`0x41756`, `0x4069A`: sprite/sign overlays), the translucent mode (`[0x3F059] = -1`, low flag bit 1: blended "light pool" overlay), `SDWhiteLight`, gamepad-independent per-detail variants. `SLIP_TIME=<seconds>` sets the animation clock for screenshots.

## Session: translucent mode, sign overlays, SDWhiteLight, cages (investigation + implementation)

**Translucent mode / decal path is the door system, not a renderer effect.** Low flag byte bit 1 (re-derived at load, see below) marks upward-facing polygons; with `[0x3F059] = -1` (`0x39683`) the routine stores the polygon's plane (`0x3F306`), captures the clipped polygon (`0x1AB34`) and then runs `[0x1AB30]` = `0x3977A` (installed by `TrackInstall`), which calls the draw callback `[slot+0x40]` (`0x26B84`) of every *door slot* on the piece (`0x397FB`: `TrackInitDoors`/`CreateDoorSlot`, material/sprite `DOORS`, `DOORS.SPR`). No TRC polygon uses the DOORS material in any track; doors are runtime objects (collision cube, animation) created from the track slot data. So the mode only matters together with the door gameplay objects; not implemented (needs the door/slot system). Water reflections (`TrackDrawSetup`, `0x34200`) are behind `[0x33CDA]`, which nothing in the executable sets (off).
**Sign overlays:** the earlier note about types 0x93/0x14 was a mis-attribution (0x93 is just the next cage handler; 0x14 occurs in no track). 0x93 is now implemented as a cage (template 0x417A2, lines 0x41798). **SDWhiteLight:** the material is looked up at load (`0x3F23E`) but no routine ever reads it; polygons using it are ordinary flat polygons with its coefficients (ambient 1.0 → shade = ambient 0x0CCC), already handled by the lighting law.
**Load-time polygon flag rewrite (`TrackDrawSetup`/`0x3984E`, list A only):** file bits 0x80, 0x08, 0x40 and 0x02 are cleared and re-derived: 0x80 = material name starts with `WATE`; 0x08 = cage/chase-light types (0x80,81,82,84,85,8D,8F,91,92,93) or transparent textured slopes (`ny ≤ 0x3000`, texture with transparent colour, MAT +0x15 = 0); 0x40 = name starts with `TRNC` (not for portal/hidden polygons); 0x02 = faces the light (`ny ≥ 316`). Implemented (`deriveFlags` in scene.cpp). Consequences: the cell-test planes now exclude TRNC* polygons (the file bit 0x40 never occurs); the extent window `[0x33EB4..0x33EC0]` comes only from derived-0x08 polygons. **Open:** by this code Chicago has no extent polygons at all, so `[0x33EB0]` would stay 0 and scenery/pass 2 would never run there, which cannot be the game's behaviour; the port falls back to the union of the portal windows when no extent polygon exists (UNVERIFIED).
**SPR header +8 = transparent colour** (see `spr.md`): textures are now keyed on it instead of "index 0".
**Cages:** 0x93 added; the far branch of `0x3F3C4` (nearest vertex ≥ 0xA6CC0) interpolates midpoints in screen space; all procedural lines use a thickness of round(width/320) pixels (the original draws 1-pixel lines at 320x200) and are clipped to the window before sampling (long lines on huge polygons used to come out dotted).

## Session: door system analysis → translucent mode is ship shadows (CONFIRMED, implemented)

**Correction of the previous entry.** The "decal"/translucent path (low flag bit 1 = polygon faces the light, `[0x3F059] = -1`) draws the **ships' shadows**, not doors: `0x397B1/0x397FB` collect, for the current piece and its three neighbours, every slot that has a draw hook (`[slot+0x40]`, set by `SlotSetDrawHook 0x26F03`); the ship slot (`0x4A306`) installs `sub_11EEC` (Artic shadow draw: picks a shape LOD from the distance table, `0x25EFD` = ShapeDraw variant). `0x1AB34` captures the receiver polygon (plane, clipped outline, colour = `esi`), `0x1CC70` projects every vertex of the shadow shape **along the light direction (0,-1,0) onto the receiver plane**, `0x1C30A/0x19F1F` fill the projected polygons with the captured colour, clipped to the receiver. Colour = palette index passed by the receiver: the material's `+0x50` (MAT byte +0x12) for plain polygons (`0x39738`, `1A198`), the SDYellow/SDRoadLine `+0x50` for border/line sub-polygons and 0 (black) for the lane sub-polygons of the 0x86–0x8B/0x90 floors (`0x3F53B` with eax = `[0x3F030]`/0). Implemented: `ShadowCaster`, `SoftwareRenderer::drawShadowsOn` (a per-pixel receiver id buffer is the stencil), `ViewerApp::buildShadowCasters`; env `SLIP_NOSHADOW=1` disables.
**Door system (decoded, NOT implemented):** `FindDoors 0x3C324` scans list-A polygons with file flag **0x20** (all are Dummy portal quads `0x21`; Chicago 1, Hawaii 2, Tokyo 5, London 1, Egypt 2) and fills a table at `0x339A4` (count word + up to 8 records of 0x64 bytes: half-width `+0x3C`, half-height `+0x48`, closed centre `+0x30`, open position `+0x24`, piece `+0x10`, neighbour piece `+0x14`, slide direction `+0x5E` (floor → up, ceiling → down, wall → horizontal part of the normal, 22734/2260A), orientation matrix `+0x4C`). `TrackInitDoors 0x3C75C` (only if `[0x33CFA] != 0`) creates one slot per record (server `0x3BF86`: messages 0x102 destroy, 0x104 update, 0x106/0x107) and `TrackInitDoors_Slots 0x3C813` builds its shape from a 0x7A-byte template at `0x3CA24` (a quad textured with the DOORS material / `DOORS.SPR`, vertices patched with the half sizes) plus a collision cube ±0x7A0 (`CollideSlotSetMainCube`). The update tests `0x13474` (collision) and slides the panel by `speed·dt` (`[slot data] = 0x37DC`, `GetFrameDeltaSecs`) between the closed and open positions along the direction. Gameplay-side objects: left for the physics/collision phase.


## Session: physics, collision and doors (oracle-checked ship dynamics; collision geometry; door animation)

* **Oracle**: `tools/re/emu.py` (x86-32 interpreter) + `phys_emu.py` run the original `RaceSlotMove` on a synthetic slot. Bug found on the way: the atan table pointer is `[0x21194]` (MathsInstall copies header words +4/+0/+2 into `0x21194/98/9C`); a wrong table made the bank feedback look like a clamp.
* **Ship dynamics decoded and ported** (`ship_sim.cpp`, test `tests/physics_tests.cpp`): see docs/simulation.md. Key findings: steering is bank-driven (target bank = steer/2, 4/s approach) and the yaw rate adds the bank (`-m[1]`) back in; pitch is a manual control; `RaceSlotHover` only converts speed to velocity (no height control, no gravity); the keyboard ramps CX/DX at 4.0/s (`sub_59A05`).
* **Track collision**: callbacks installed by `TrackInstall` identified (`0x38C97` sweep, `0x38E84`, `TrackSlotCheckStatic` 0x34B97, `0x35642`, `TrackSlotsCheckLOS`); polygon sweep `0x3CAEA` decoded (margin 0x1E8, cos threshold 0x10, in-polygon test); piece membership for slots uses slack 0x200 (`0x37FCC`). The collision response (collide slot type 3 record, `CollideStep`) is NOT decoded; the port uses a placeholder (slide + speed scrub). Implemented in `ship_collide.cpp`; `F8` = legacy hover assist.
* **Doors**: `FindDoors` geometry and the 0x3BF86 state machine decoded (see simulation.md) and implemented as animated DOORS-textured panels (`doors.cpp`, drawn with their piece). The closed position leaves half the quad covered (midpoint of centre and open position, as decoded; UNVERIFIED against the running game — the GOG folder contains DOSBox, a later session could confirm visually). No ship-vs-door collision.
* Translucent mode (ship shadow decal path via door slots) is unrelated to the panels themselves; nothing changed there.

## Session: collision response decoded
Collide-slot type 3 -> `0x145B6` -> `SlotSendMessage(0x107)` -> ship handler 0x50A64: speed*0.75, slide velocity += speed * U, U = 67.5 deg from the surface towards the normal (`0x3BD82`, emulator-checked), then RaceSlotHover/RaceSlotDamage. Remaining frame time continues with the new velocity (no sliding along the plane). The `[+0x12]` second-hit explosion branch is documented but not ported. Port: `shipHitResponse`, `stepShip` sub-step loop; a 30 s straight run on Chicago now bounces along the track instead of sticking.

## Session: ship-to-ship collision
Decoded `0x15A46/0x14620/0x149A1/0x14A16/0x1656B/0x1453C` and the ship's 0x106 handler (0x50832..0x50A57): normals are relative-velocity based, not geometric; each ship is pushed by `max(1.5*|rel|, 0x37DC)` along the other's relative velocity, the rammer loses 37.5 % speed. The cube contact generator (0x16454, 0x14B6C, 0x14DC0, 0x15008, 0x1525C, 0x156F8, 0x154A4) was NOT decoded; the port uses a SAT box test. Ported as `resolveShipPairs`; verified in a headless run (player shoves the start grid, cascade of pushes) and by a unit test of the response numbers.

## Session: contact generator oracle, frame re-simulation, damage, wrecks, AI
* `tools/re/ss_emu.py` runs the original `0x14620` on two synthetic slots (needs the face-point pool of `0x133BE`). 193/193 random separated box pairs agree with the port's SAT time of impact (error <= 6 units); the harness exposed a scaling bug in the first port version. Overlapping starts are mostly "no contact" in the original.
* `resolveShipPairs` now follows the CollideStep loop (advance all to the earliest contact, respond, continue the remaining time).
* Damage model decoded (RaceSlotDamage + its consumers) and ported; second wall hit within 0.4 s wrecks the ship.
* AI decoded and ported (node chain in the TRD, see simulation.md); `Track::nodes`, `TrackPiece::node`, `ship_ai.cpp`; `F9` toggles the AI ships in the viewer. Found: `ShipParams::f6` (= 244000) is the AI's look-at-curvature distance, the 0x502D0 table is just the parameter-block pointer table.

## Session: AI behaviour and debris
Decoded `TrackSlotGetNeighbours` (0x3B6D0), the overtake test (0x515D2), the room test and lateral offset (0x515F2, 0x3BA0C, 0x227CF), the offset clamp and target-speed limiting in 0x51688, `TrackSlotCheckBranch`/`SetBranch`, `InitRefuel` (pit flag of nodes), the door trigger (`TrackSlotFindDoor` + 0x35564), the AI speed-factor tables (0x5409C, tier from start position via 0x586DB) and the human trailing boost, and the whole debris handler (0x3E8F2/0x3E9F5/0x3EE31). Port: `ship_ai.{hpp,cpp}` (RaceContext, neighbours, avoidance, branches, ranks, wrecks), wreck state in `ShipState`, per-ship `speedFactor`/boost/slow timers, box = ART extents / display scale (TrackSlotAdd limits the extent to 0x2250-0xC8, i.e. the display scale of 2 must not be applied to collision), 15-step bisection when the static piece check fails. New tool `tools/re/dis_range.py` (linear disassembly of a VA range; the function lister garbles inline data).

## Session: sound system
Decoded: `.SMP` = raw 8-bit unsigned mono at 11025 Hz (`SoundInit` 0x10AA4 passes 0x2B11); the Fx engine (`FxPlay` 0x4B92A queue of 20 entries x 0x14 bytes, processor 0x4B4DB, per-frame limit 4, attenuation `(0x11DF0-d)/0x11DF0`, own events full volume, engine rate `1+v/4/65536`, effect id -> sample table 0x4B7C4); `.HMP` = HMIMIDIP013195 (chunks from 0x388, inverted-bit delta VLQ, controllers 108-119 are HMI markers, 120 ticks/s). Song choice `random(3)` over INGAME2/3/4/6. Implemented `audio.{hpp,cpp}` (core), `mixer`, `music_player` (FluidSynth), `audio_system` (SDL3), `slipstream_audio_dump`, `tests/audio_tests.cpp`, `scripts/bundle_dylibs.py`.

## Session: sound system, second pass
Decoded the music flow (HMI loop controllers, no branch calls, HmiPlaySong callers: INTRO 0x55E86, WIN 0x5623E, LOSE 0x5A820, race 0x57A79/0x589xx), the race start timer in `DoGame3D` (`[0x54408]` 5..0: announcer list 1 at 5, effect 0xC at 3, announcer list 2 + engines at 1; `[0x54404]` 15 s bonus window), ambient loop selection (0x58CB6: refuel piece -> PITSLP, piece name CROW*/GRID* -> CROWDLP; ramp 0x4B658), per-track announcer lists (0x4B38F/0x4B3B7), the jet-by (0x45196, trackside TV camera) and that HIGH.SMP is never referenced. Port: `hmpToSegments` + MusicPlayer segment hand-off at the loop end tick, `AudioSystem::playSpeech/updateAmbient`, water hit, race countdown / laps / finish in `ViewerApp`.

## Session: weapons, pickups and voice cues
Decoded from the ship message handler (RaceSlotControl 0x50480..0x51247) and the weapon module (0x5C34D..0x5D5D5): weapon table 0x5D612 (field roles, see `docs/weapons.md`; `+0x30/+0x34` are the damage values, `+0x20/+0x24` energy refill/cost, `+0x2C` lock cone), launchers, the five projectile servers (beams 0x5C481/0x5C741, missiles 0x5D24B/0x5CB03/0x5D33A, mines 0x5D453), the victim handlers (0x202 beam hit, 0x106 with a weapon slot / bonus slot), the status-effect timers on the slot data and their readers in `RaceSlotMove`, the booster table 0x5BD44, the lock-on cone test 0x140BF, the AI fire decision (0x5154F..0x515D0), the bonus object (0x42A9A/0x42BD5, placement tables 0x5502C: 5..14 spots per track, BONUS0-5.SPR) and the voice cue system (0x52F10 `InitVoices`, 0x530B8 `VoiceCue`, mode 3 list of 85 cues, class tables 0x50881/0x509BA/0x50C03/0x50C2B/0x515A4/0x5A7A4/0x5A7CC).
* Corrections to earlier notes: weapon id 3 of the *selection* (`[+0x14] == 3`) is the **booster**, not mines; the pointers in the weapon table are object-relative (launchers at 0x5Cxxx/0x5Dxxx); `[0x54408]` masks the throttle and fire bits of every ship during the countdown (so the grid hold is no longer an inference).
* Port: `weapons.{hpp,cpp}` (+ status effects in `ship_sim.cpp`, projectile models in `scene.cpp`, overlay primitives in the renderer, `AudioSystem::playCue`). The pickup sprites were checked visually: the track-1 spot at (4582847, 1012426, 1567396) lands in the middle of the road and the six BONUS sprites read REPAIR, REPAIR, TURBO, (red arrows), $, BOOST - which agrees with the effect decode (type 3 is the nasty one).
* Open: Smoker / mines particle effects (0x4F79E, 0x4F61B, 0x4F414), the visual routine 0x4A4CF of the Scrambler, how the original filters owner-vs-projectile contacts, the unit of the sprite scale at slot +0x34, contact and passing voice lines (their triggers read slot data +2 / unknown ranks), the shop that fills the human loadout, difficulty switch [0x49F04].

## Session: first person camera
`0x44F64` (camera mode handler) places the camera at the ship's `head` ART reference point (`0x11D36`) with the ship's full orientation (matrix words negated = inverse, so it includes the bank); `0x44FB4` is the chase camera (distance 0x2250..0xBEA0 behind the ship, adjustable by keys). Port: cockpit view is the default (`V` toggles, `--chase`), `Camera::roll` added to the renderer (horizon tilts with the bank), the own ship is not drawn from inside. The chase view keeps the earlier placeholder distance (110000 behind) - the original's 0x2250..0xBEA0 range is not adopted yet.

## Session: race rules
Decoded RaceUpdate (0x5A4EC), the lap line (TRD header +4/+6, pieces A/B, `34C32`/`34C3F`/`34BE0`), record layout (stride 0x4E: +0xC retired, +0xD finished, +0xE race time, +0x12 clock, +0x16 best lap, +0x1C distance to go, +0x20 laps, +0x22 back flag, +0x24 rank, +0x26 previous rank, +0x28 piece), ranking, race end timer, the speed-factor composition (start bonus table 0x50252, intro-only table 0x5040C), the difficulty setting (CFG offset 157), the corrected trailing-boost condition and the human pass line (0x50B7E). Details in docs/simulation.md "Race rules". Port: `lapCrossing`, `assignRanks`, `startBonusForRank`, `RaceStatus` in `ship_ai.cpp`; unit tests in `tests/physics_tests.cpp`; headless check: with the AI steering the player, a 2 lap race on Chicago crosses the start line within 1-3.5 s, laps take ~53-70 s and the race ends five seconds after the second AI ship finishes.

## Session: ship-vs-door cube
Read the door slot server 0x3BF86 fully (0x106 contact -> open at 0x6FB8; 0x104: overlap at frame start -> open at 0x37DC, move via 0x3C17B with the overlap test 0x13474, a closing door that would overlap reverts and reopens) and TrackInitDoors 0x3C813 (cube = panel rectangle, thickness +-0x7A0, collider class 2). Port: `Doors::step(dt, ships)`, `proxy()`, `touch()`; doors enter `resolveShipPairs` as static boxes carrying the panel velocity; missiles stop at doors (`CombatContext::obstacles`). Headless 1-lap AI races on the door tracks (1, 2, 3, 9) still finish with the same lap times, so the AI is not blocked.

## Session: HUD and pause menu
Decoded the HUD init (0x43B4A: TIME.FNT, SPD.FNT, POS*, NSIGHT, TSIGHT*, CON?_T?* / CON?_B?*, CONS_EXT, TURBO_?), the frame draw 0x4429D, the timer / speed / position block 0x453B8, messages 0x44654, bars 0x572F2 / 0x5733B, weapon panel 0x5C12D / 0x5BE65, sights 0x5C234, the 3D window 0x44812, the camera pane table 0x42CAB (F1..F10 scancodes -> callbacks), the `.FNT` format and the static UI palette (0x54304). Sprite headers hold the screen position of the console parts. Findings and layout in docs/hud.md. Also: human damage beyond 100 ends the race via GAME OVER.

## Session: pit lane
Traced every use of the refuel piece (0x35E53 check, 0x50E97 repair, 0x58CBD ambient, 0x37C6B / 0x39ADE draw, 0x3D8C1 / 0x3DA9A init) and the branch machinery (0x3544F state, 0x353CF flag store, 0x3BEA2 target node, 0x3BF4C next node). Found that 0x3BEA2 tests the slot's branch flag (loaded at 0x3BEC5) and redirects the first node after a split to the alternative route: the port ignored the flag there, so no AI ship ever took a branch. Fixed, and the AI now decides at the node before the split like the original. Geometry of the refuel pieces per track documented in docs/simulation.md.

## Session: pit lane, second pass (Chicago) and piece lighting
User report: no pit on Chicago. Root cause: `InitRefuel` port compared the polygon material *index* with the TRC material table; polygons carry the table *id*. After the fix every track has a refuel piece on an alternative route (the earlier "Hawaii / Norway cannot be entered" notes were artefacts of that bug). Then decoded the per-piece lighting (0x39ADA..0x39B85: piece `+0x20` -> `[0x39C56]` -> 0x39427 scales the diffuse level and the ambient) and the pit flicker (random piece light, generator 0x3667B). Manual (`Manual.PDF`, "Re-charger Pit") confirms one pit per track with blue / white flashes.

* **Ship scale (2026-10-08, user request):** ships are drawn at the original model size (`--ship-scale` default 1, was 2) in the races and everywhere else; the collision boxes were already derived from the unscaled model, so physics did not change. The earlier ×2 was only a visual guess against the painted start boxes.

## Start bonus switched off by default
The start-phase speed bonus (`table 0x50252[rank]`, +75 % for rank 1 down to +1.6 % for rank 10, first 15 s, all ships, every mode except practice 0x11; CONFIRMED in the code) made the player's craft nearly twice as fast off the line (measured: 229k vs 130k speed units after 3 s on Chicago). The user found this wrong, so it is off by default (`--start-bonus` restores it). Deviation from the original, taken on request; whether the original's rank value at the start differs (e.g. the player not on pole) is UNVERIFIED.


## Championship reverse engineering (2026-10)
Decoded `0x559D8` (modes 0x11 practice / 0x12 single race / 0x13 championship), `0x561CF`, `0x5623E`, `0x56FF0`, `0x422EC`, `0x53536`, `0x573B7` (reporters), `0x57A79` (fly-through), `0x5816E`
(script loader), the face engine `0x41C0A..0x41F53`, the TV camera `0x45196`, `0x58448` (roster), `0x5AD44` (track list, unlock), the configuration screens. Tables: calendar `0x54E10`, points
`0x556E8`, prizes `0x556FC`, races `0x543E4`, flag positions `0x54FDC`, intro speed `0x5040C`, face layers `0x41A80`. `SLIPSTRM.CFG` words: `0x492E2` engine sounds, `0x492E4` effects,
`0x492E6` music, `0x492E8` language, `0x492EA` difficulty, `0x492EE` damage, `0x492F2` environment detail, `0x492FE` track map, `0x493E6` unlocked tracks (file offset = VA - 0x4924D).
**Corrections**: the globe flags used true city coordinates; the original's table gives stylised positions. The start speed bonus (earlier note) is unrelated.
Open question: the speed bonus table `0x50252` and the AI tier from the start slot suggest that in the original the human starts at the back; the port keeps ship number = slot.
