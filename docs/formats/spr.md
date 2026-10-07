# `.SPR` — CONFIRMED (header words +4/+6/+8 UNKNOWN)
```
+0 u16 width   +2 u16 height
+4 u16 ?  +6 u16 ?  +8 u16 0xFFFF|0   (UNKNOWN; possibly hot-spot / flags)
+10 u16 0, +12 u16 0
+14 u16 = 16 + w*h (mod 65536) = offset of optional trailing block
+16 w*h bytes: 8-bit palette indices, row-major
[optional] {u16 first, u16 count, count × RGB(6-bit)}   (748 B or 559 B in the data set)
```
Index 0 behaves as transparent in the viewer (INFERRED; many textures use 0 for holes). Textures are referenced from materials by wildcard pattern (`chiclwa*.spr`); 438 of 454 patterns match exactly one file. UV mapping: `u,v` stored as 2.14 where 0x4000 = one repeat (CONFIRMED by rendering).
