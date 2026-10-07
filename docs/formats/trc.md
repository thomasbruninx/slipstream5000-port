# `.TRC` — CONFIRMED (rendered; 1211/1211 records)
```
+0 u16 size  +6 u16 record list (0x0A)  +8 u16 material table (ends at EOF)
list: u16 n; records chained by their size word
record: +0 u16 size  +2 u16 offset of VERTEX BLOCK  +4 u16 polygon list A  +6 u16 polygon list B (B before A when both)
        +8 s16 bbox xmin,xmax,ymin,ymax,zmin,zmax  (<<6)  +0x14 s16 radius  +0x16 u16 ?  +0x18 u16 name flag  +0x1A char[8] name ("GRID","CROWD")
vertex block: u16 n; n × s16[3]  — each coordinate is stored >> 6 (world offset = value << 6)      (CONFIRMED: engine compares bbox<<6; roads line up)
polygon list: u16 n; polygons: u16 info (bit15 = UVs, N = info&0x7FFF), s16 normal[3], u16 ?(+8), u16 material (+0xA, local id), u16 index[N], [N×{u16 u,u16 v}]
material table: u16 n; n × {char[16], u16 id}
```
World vertex = `piece.pos + (vertex << 6)`; no rotation (CONFIRMED by rendering all ten tracks). 

## Polygon flag word (+8) and record class word (+0x16) — CONFIRMED by engine code (0x39C58–0x3A468, 0x38524)
* polygon `+8` bit 0 (0x01): **portal** — opening to the neighbouring piece; never drawn (169–190 per track, almost all material `Dummy`; Chicago's `Trench Ent` quad that blocked the road is one too). Bit 3 (0x08): piece-extent frame polygon (used for the piece's screen extent). Bit 6 (0x40): excluded from the cell-bounding planes. Other bits (0x2, 0x100–0x800, 0x8000…) are unknown/draw attributes.
* record `+0x16` = visibility class bits (see research-log); `+8..+0x12` = bounding box (s16, <<6, piece-relative).
* **Cell test (0x38524):** a point is inside a piece if it is inside the record bbox and on the inner side (dot(n,p−v0) ≥ −256) of every list-A (+4) polygon that lacks flag 0x40.

## Polygon flag high byte = procedural detail type (0x3F2C8 jump tables at 0x3F080 / 0x3F10C) — structure CONFIRMED
Only used for polygons WITHOUT texture coordinates. Bit 7 clear (types 1–7, 0x13/0x14, 0x19, 0x1E): the base polygon is drawn flat, then a handler adds detail: types **2–7** draw panel **lines** over it (template = base polygon + points that are midpoints of earlier points; line list `{a, b, flag}` in colour ramp+1 (flag 0) / ramp−1, only if the nearest vertex is ≤ 0x17D400); type 1 and 0x1E are light/refuel decorations (animated, timer `[0x3F078]`). Bit 7 set (types 0x80–0x93): the base polygon is **not drawn** (0x39640), the handler draws everything: types **0x86–0x8B** are road floors (far, > 0x29B300: the polygon flat in the 80 % ramp colour; near: two border polygons in the `SDYellow` colour, the lane polygon in the material's 80 % colour, plus seam lines); 0x80–0x82, 0x84, 0x85, 0x8C–0x8E, 0x91–0x93 are start-cage/trim variants (`0x3F3C4` + `0x1A1C8` + `0x19F48`), 0x8F/0x90 flashing-light rows. The SD* materials (SDCage, SDRoadLine, SDOrangeLight, SDFloorLight, SDBlueLight, SDWhiteLight, SDYellow) are looked up by name at track load (`0x3F15C`) and animated by `0x3F26E`.
Template tables live in the executable (VAs in `src/game/panel_detail.cpp`) and are read at runtime from the user's own `SLIPSTRM.EXE`.
