# `.ART` — CONFIRMED structure (11 files)
Header 0x58 B: `u32 version = 18`; `s32 min xyz` (+4), `s32 max xyz` (+0x10) of the main body; `u32 root_offset` (+0x20, always 0x58); two 8×u32 tables (+0x28, +0x48; look like performance/step tables, UNKNOWN). Then nodes (size `0x1D0 + 16*ref_count`, tiling the file):
```
+0 u32 tag (little-endian multichar: bytes 'niam' => "main"; also fan1.., jet1.., shld/shl1.., drv1..)
+4 u32 child offset   +8 u32 sibling offset   +0x0C s32 ?   +0x10 s32[3] offset relative to parent
+0x1C 8×char[14]  shape names (first = intact body, others damage variants?)
+0x8C 8×char[14]  shadow/detail variants
+0xFC 4×{char[14] name, s32[3]} debris;  +0x164 4×{...} more debris
+0x1CC u32 ref point count;  +0x1D0 16 B each: u32 tag ("lasl","lasr","weap","head","smok","fan1","shld","drv1"...), s32[3] position
```
Part positions in the reimplementation are summed down the tree (SPECULATIVE whether the rendered assembly matches the original).
