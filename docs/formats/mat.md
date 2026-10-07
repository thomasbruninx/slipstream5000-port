# `.MAT` — CONFIRMED structure
`u16 count, u16 version(=1)`, `count × 46 B`:
`+0 char name[16]` · `+0x10 u8 palStart` · `+0x11 u8 palEnd` (shade ramp; flat surfaces use `start + (end-start)*light`) · `+0x12 u8 ?` · `+0x13 s8 ?` · `+0x14/0x15 s8 ?` · `+0x16,0x18,0x1A,0x1C u16 ?` · `+0x1E u16 shift (ramp end adjusted by (1<<shift)-1)` · `+0x20 u16 (unused)` · `+0x22 char pattern[12]` texture file pattern or empty.
Loaded as track MAT then `CARS.MAT` (ship materials). Material numbers are global indices into that concatenation; shapes/TRC refer to them by **name** via their own `(name,id)` tables. Unknown: transparency/specular/other flags (the unlabeled bytes).
