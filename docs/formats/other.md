# Other formats (partial)
* `.SMP` — raw unsigned 8-bit mono PCM, no header (rate UNKNOWN; mixer options 22/44 kHz).
* `.HMP` — HMI MIDI ("HMIMIDIP013195"); `.BNK` — HMI AdLib bank ("ADLIB-").
* `.FNT` — tag `FONT`, per-glyph (offset, advance) table, 8-bit bitmaps (7×8 for SMALL.FNT).
* `.ST0/.ST1/.ST2` — `TAG4, u16 len, text\0` records; English/French/German.
* `.ANN` — announcer/intro script bytecode (8-char id header); `.ZON` — hot-zone tables.
* `.GDV` (loose) — Gremlin Digital Video; `INTRO.GDV` magic `0x29111994`.
