# `.RES` archive — CONFIRMED
Files: `SLIPSTRM.RES` (= `SLIPMAX.RES`), `SLIPCD.RES` (+`MATHS.BIN`), `SLIPMED.RES`, `SLIPMIN.RES`. Little-endian. No compression.

```
data...                         entries packed back to back
diroff = u32 @ (size-4)
@diroff  u32 header             bit31 = names XOR-obfuscated; bits0..30 = entry count (<= 5000)
@diroff+4 entry[count] (28 B):  u32 flags (always 2)
                                char name[16]  "NAME    .EXT" + 4 NULs; if bit31: byte i ^= "SOFTWAREREFINERY"[i]
                                u32 offset (absolute), u32 size
```
Verified: directory ends exactly at EOF-4; entries contiguous; unsorted; unique names. Lookup in the original is a linear scan on the 12-char name. Resolution order: primary archive, secondary (`SLIPCD.RES`), loose file. Example values: `SLIPSTRM.RES` hdr `0x8000098F` (2447 entries), diroff `0x37F38D0`. Implementation: `GameData` (`src/original_formats/game_data.cpp`).
