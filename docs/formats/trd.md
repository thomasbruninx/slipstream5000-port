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
Pieces: each TRC record is placed by exactly one piece for most records (six records have none: Norway 80 / 81 are flat 12-gon `Dummy` portal discs (one polygon, flag 0x01, 12 vertices in the z = 0 plane, visibility class 1), London 111 / 112 are single huge `NEW BLOCK E` quads (+-524288 x +-33792, class 0x40, list B) and Egypt 25 is a 2 x 1M `ROCK6` ground plane with a quad hole (class 1, list B). No TRD or TRC word holds their offsets, so their use is not known: the portal discs look like tunnel mouths whose polygon the neighbouring pieces' links reach, the quads like distant backdrops. The port does not draw them). The lists at +4/+6/+8 hold s32 positions (doors/other objects) — not decoded. Engine evidence: `0x383F8` (spatial query), `0x36E06`, `0x347D3`, `0x3D568`.

## Piece entry links (CONFIRMED, portal traversal 0x3A14x)
`+4/+8/+0xC`: three `{u16 neighbour piece entry offset (TRD), u16 portal polygon offset (TRC, in this piece's record list A)}` pairs (offset 0 = none). The portal polygon has flag bit 0 and its normal points back into the owning piece. Verified on all 10 tracks (every non-empty link resolves to a list-A polygon with bit 0).
Scenery entry: `+0x1C` u32 bounding radius, `+0x36` u16 class mask, `+0x38` u16 billboard flag (see research-log).

## Group draw-order tree — plane definition CONFIRMED
Group `+2` = point list (`u16 n`, then 8-byte entries, `u16 x,y,z (<<6)` relative to the group origin at `+0xA/+0xE/+0x12` s32). Tree node `+0x10` (when not -1) is the **index of a group point**; the node's plane passes through that point with normal `+0x12..+0x16` (2.14). 98/98 Chicago and 100/100 Hawaii nodes separate their A/B item positions consistently with this; London 185/188 (the rest straddle).
