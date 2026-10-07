# `.TRK` — header CONFIRMED, cells partly understood (10 files)
```
+0 u16 size  +2 u16 version 0x2B  +4 u16 linked flag 0xFFFF  +6..0xB ?
+0x0C u16 cell table  +0x0E u16 corner pool (== end of cell table)  +0x10.. ?
+0x18 10 × s32[3]  START GRID positions (world coords; consecutive ships ~68.8k apart, ship 0 leads)   (CONFIRMED by rendering)
cell table: u16 n; n × 24 B {u16 kind, u16 prev, u16 next, u16 trdOffset, u16 corner[8]}
corner pool: u16 n; n × 8 B {u16 x,y,z (lattice units of 12), u16 ?}
```
The spatial grid is 12×4×20 cells of 2^20 world units: `gx=x/12, gy=y/12-13, gz=z/12-18`. The renderer does **not** need the cell table (placement comes from TRD pieces). `TrackLoad` also reads `+0x9E` (s16) and `+0xA2` (flag); meaning UNKNOWN.
