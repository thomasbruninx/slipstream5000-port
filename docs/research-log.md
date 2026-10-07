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
