# `.TRD` — pieces and scenery CONFIRMED; rest UNKNOWN
```
+0 u16 size  +2 u16 group list (0x0A)  +4,+6 u16 other lists ?  +8 u16 end of group chain
group list: u16 n; groups chained by size word
group: +0 u16 size  +2 u16 ?  +4 u16 PIECE list (0 = none)  +8 u16 SCENERY list  (+0x0A.. ?)
piece list: u16 n; n × 0x22 B: +0 u16 index  +2 u16 TRC record offset  +4/+8/+0xC neighbour piece refs  +0x10 u16 (runtime)
            +0x12 s32[3] WORLD POSITION  +0x1E u16 (0x3930 in all samples)  +0x20 u16 0x4000
scenery list: u16 n; n × 0x46 B: +0 char[12] shape name  +0xC u16 (runtime) +0x10 s32[3] world position
              +0x1C,+0x20 (runtime extents)  +0x24 s16[9] 2.14 matrix (orientation convention SPECULATIVE: v' = v*M)
```
Pieces: each TRC record is placed by exactly one piece for most records (a few records per some tracks have none — mechanism unknown). The lists at +4/+6/+8 hold s32 positions (doors/other objects) — not decoded. Engine evidence: `0x383F8` (spatial query), `0x36E06`, `0x347D3`, `0x3D568`.
