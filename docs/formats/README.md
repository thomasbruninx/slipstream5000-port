# Original data formats

One document per format. Status tags: **CONFIRMED** (validated mechanically by `tools/re/validate_assets.py`, the C++ parsers in `src/original_formats/`, or visually by rendering), **STRONGLY INFERRED** (from engine code), **SPECULATIVE**.

| Format | Doc | Status |
|---|---|---|
| `.RES` archive | [res-archive.md](res-archive.md) | CONFIRMED |
| `.PAL` palette | [pal.md](pal.md) | CONFIRMED |
| `.SPR` sprite/texture | [spr.md](spr.md) | CONFIRMED (3 header words unknown) |
| `.MAT` materials | [mat.md](mat.md) | CONFIRMED structure; numeric fields partly unknown |
| `.SHP` 3D shape | [shp.md](shp.md) | CONFIRMED (sort tree, header 0x3A–0x51 unknown) |
| `.ART` ship descriptor | [art.md](art.md) | CONFIRMED structure; semantics partial |
| `.TRK` | [trk.md](trk.md) | CONFIRMED header/start grid; cells partly understood |
| `.TRC` | [trc.md](trc.md) | CONFIRMED (rendered) |
| `.TRD` | [trd.md](trd.md) | CONFIRMED pieces + scenery; other lists unknown |
| `.CAM` | [cam.md](cam.md) | CONFIRMED |
| `MATHS.BIN` | [maths-bin.md](maths-bin.md) | CONFIRMED |
| others (`.SMP .HMP .BNK .FNT .ST* .ANN .ZON`) | [other.md](other.md) | partial |

Reference implementation: `src/original_formats/{game_data,formats,track}.cpp`. Executable evidence: `docs/executable-analysis.md`.
