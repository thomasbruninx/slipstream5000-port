# `.TRK` — header CONFIRMED, cells partly understood (10 files)
```
+0 u16 size  +2 u16 version 0x2B  +4 u16 linked flag 0xFFFF  +6..0xB ?
+0x0C u16 cell table  +0x0E u16 corner pool (== end of cell table)  +0x10.. ?
+0x18 10 × s32[3]  START GRID positions (world coords; consecutive ships ~68.8k apart, ship 0 leads)   (CONFIRMED by rendering)
cell table: u16 n; n × 24 B {u16 kind, u16 prev, u16 next, u16 trdOffset, u16 corner[8]}
corner pool: u16 n; n × 8 B {u16 x,y,z (lattice units of 12), u16 ?}
```
The spatial grid is 12×4×20 cells of 2^20 world units: `gx=x/12, gy=y/12-13, gz=z/12-18`. The renderer does **not** need the cell table (placement comes from TRD pieces). `TrackLoad` reads `+0x9E` (portal-only draw, CONFIRMED below) and `+0xA2` (far-detail distance, CONFIRMED below). The cell table is the BSP described next; header words `+6..+0xB` and `+0x10..+0x17` and the last word of the 8-byte corner entries are not used by the code read so far (SPECULATIVE: padding / editor data).

## Cell table = BSP over a 2^20-unit lattice — CONFIRMED (supersedes the "circular linked list" reading above)
Each 24-byte record is a BSP node: `kind` 0xFFFF = leaf with `trdOffset` = TRD group whose pieces and scenery lie inside the leaf's lattice box (verified on Chicago: all items inside their leaf box; 34 leaves = 34 TRD groups). Otherwise `kind` selects a lattice plane: byte offset `2i` into three tables (x: 11 planes `i=0..10`, y: 3 planes at `kind` 22.., z: 19 planes at `kind` 28..), plane `i` at lattice point `i+1`, world coordinate `point × 2^20` (cell coordinates are `corner/12 − (0,13,18)`). Child `+2` is the HIGH side, `+4` the LOW side (verified on 3 tracks). The engine (0x37601) walks it far-side-first from the camera; this is the painter's order of the track.

## Header flags used by the draw code — CONFIRMED (0x34524–0x34559, 0x3A60C)
* `+0x9E` s16 → `[0x33D08]`: **portal-only** (non-zero on Norway, Cave, Canyon, Amazon): only pieces reached by the portal walk are drawn. When 0 (Chicago, Hawaii, Tokyo, London, Egypt, New York) pieces the walk did not reach are still drawn if they lie in the view frustum and come after the first reached piece in BSP order (`0x36695` tests the record's bounding volume: sphere `rec+0x14<<6` via `0x19FC5`, then the 8 bbox corners via outcodes `0x198B5`).
* `+0xA2` s16 → `[0x33D0C]`: selects the distance `[0x33CC6]` = 0x830E0 (0) or 0xBEA00 (≠0, Norway) beyond which polygons are rasterised with render-flag 0x20 (a cheaper span routine, `0x1AECA`).
