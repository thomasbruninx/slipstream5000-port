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
