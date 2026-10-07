# `.SHP` — CONFIRMED (280/280 files)
```
+0x00 u16 version = 12        +0x02 u16 vertex_shift n (vertex = stored << n)   +0x04 u16 flags (bit0 linked at runtime)
+0x06 u16 ?   +0x08 u32 file size   +0x0C u32 sort-tree offset (0 = none; follows the vertices)
+0x10 u32 vertices   +0x14 u32 polygons (always 0x52)   +0x18 u32 material table (runs to EOF)
+0x1C s32 radius   +0x20.. s32 xmin,xmax,ymin,ymax,zmin,zmax   +0x38 u16 NOSORT (non-zero => no sort tree)   +0x3A..0x51 ? (UNKNOWN)
vertices:  u16 n, n×{s16 x,y,z}
polygons:  u16 n, then
   u16 info (bits0-13 = N, bit14 = per-vertex normals, bit15 = UVs)
   s16 nx,ny,nz (2.14)  u16 material(local)  u16 flags (bit2 = hidden)
   u16 index[N]
   [bit14] N × s16[3] vertex normals   [bit15] N × {u16 u,u16 v} (0x4000 = 1.0)      (CONFIRMED order: indices, normals, UVs)
materials: u16 n, n×{char name[16], u16 id}
```
Coordinates: +y is up (floors have ny=+0x4000). Ships (`RACER*.SHP`, shift 0) are about 64× smaller than the track unit; the reimplementation displays them ×2 (chosen visually against the start-box markings; `--ship-scale` overrides). Ship models face **+z** (ART reference points: `smok` z=−5400 and `fan1` z=−2300 are at the rear, `head`/`shld` at +z). Sort-tree node format UNKNOWN (not needed with a depth buffer).
