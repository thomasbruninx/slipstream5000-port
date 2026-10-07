# tools/re — reverse-engineering workbench (research only)

Static-analysis tooling used for Phase 2. **This is not port code.** It reads the original game files and never
modifies them; nothing here executes the game.

> `work/` holds regenerable derived data (≈13 MB: pickles and an annotated disassembly listing of the original
> executable). It is git-ignored on purpose — do not commit or redistribute it. `./build.sh` recreates it in seconds.

## Requirements
* Python 3.9+
* `libcapstone` (Homebrew: `brew install capstone`); the scripts load it through `ctypes`.
  Override with `CAPSTONE_LIB=/path/to/libcapstone.dylib`. Default: `/opt/homebrew/Cellar/capstone/5.0.9/lib/libcapstone.dylib`.
* The original `SLIPSTRM.EXE`: `SLIP_EXE=/path/to/SLIPSTRM.EXE` (default: `~/Downloads/slip5000/SLIPSTRM.EXE`).

## Pipeline
```
./build.sh                     # discovery + function registry + listing  -> work/
python3 show.py 586f2          # annotated disassembly of one function (hex VA)
python3 lin.py 5a7f8 5a860     # annotated linear listing of a range
python3 tree.py 586f2 2        # call tree with string references
python3 genmap.py > ../../docs/function-map.md
python3 validate_assets.py <game dir>     # 30 structural checks on the asset formats
```

| File | Role |
|---|---|
| `le.py` | LE (Linear Executable) parser: header, object table, page map, fixups |
| `an.py` | Image with fixups applied (absolute pointers), capstone wrapper, `cstr`, `dis` |
| `disc.py`, `disc2.py` | Code discovery (recursive descent, then validated tentative decoding of gaps) |
| `db.py` | Function registry (flood-fill), call graph, relocation-based data xrefs, orphan promotion |
| `xr.py` | Function → string references |
| `show.py`, `lin.py`, `tree.py` | Annotated viewers (use `data/names.json`, `data/gnames.json`) |
| `res.py` | Minimal `.RES` directory dumper (the parser in `validate_assets.py` is the maintained one) |
| `nm.py` | CLI to add a name: `nm.py add ADDR NAME high\|medium\|low "evidence"` |
| `genmap.py` | Renders `docs/function-map.md` from the JSON |
| `validate_assets.py` | Re-proves the format claims in `docs/executable-analysis.md` §7 |
| `data/names.json` | Curated function names: `{VA: {name, conf, evidence}}` — the source of truth |
| `data/gnames.json` | Curated data/global labels |
| `dis.c` | Stand-alone capstone disassembler (flat file) used early on |

## Conventions
* VA = LE object 1 base `0x10000` + offset; data object at `0x80000`. Absolute addresses require the fixups, which `an.py` applies.
* Confidence: `high` (original string or verified structure), `medium` (strong behaviour), `low` (inference).
* Do not edit `docs/function-map.md` by hand; edit `data/names.json` and regenerate.
