# In-race HUD and pause menu

Port: `src/game/hud.{hpp,cpp}` (layout, bars, text), `src/game/pause_menu.{hpp,cpp}`, `HudAssets` (art read from the user's game files),
`Font` / string tables in `src/original_formats/formats.*`, viewport support in the software renderer, wiring in `viewer_app.cpp` / `main.cpp`.
Everything is laid out on the original's 320x200 screen and scaled to the framebuffer (x by W/320, y by H/200). Labels: CONFIRMED =
read from the disassembly, INFERRED, SPECULATIVE.

## Screen (CONFIRMED, 0x44812 / 0x4429D / 0x44458)
* 3D window x 4..315, y 8..166, projection centre (160, 87); the window centre jitters by -3..+4 px (random, per frame) for 0x12C ms after the player is hit (0x440F3, 0x448E6).
* Frame colour 7 (palette index): top bar y 0..7, left x 0..3 and right x 316..319 (y 8..192), bottom bar y 193..199.
* Cockpit view (camera callback 0x44AF6 -> console frame 1 or 2): `CON<ship>_TN<f>.SPR` (312x15) at (4, 152) with transparent colour 144, over the bottom of the window, and
  `CON<ship>_BN<f>.SPR` (312x26) at (4, 167). The sprite headers (+4, +6) hold exactly these positions. Frame 2 = dark piece: `TrackSlotGetLight` (0x3526C) returns TRD piece
  entry `+0x20` (0..0x4000, `TrackPiece::light`), below 0x2000 the dark frame is used.
* External views: a black line at y 167 and `CONS_EXT.SPR` (312x25) at (4, 168).
* Palette entries 248..255 (HUD text colours, also used by track materials) are set by `VideoSetPalette(0x54304)`: black, black, black, grey (128), green, red, yellow, white. The port reads them
  from the executable (this replaces the earlier placeholder colours).

## Elements (CONFIRMED positions and colours unless noted)
| element | where / how | source |
|---|---|---|
| speed | `SPD.FNT`, green (252), at (14, 14): `speed / 715` followed by the glyph ':' (the font draws "mph"); ';' = kph (`/ 444`) | 0x453B8 |
| position | `POS<rank-1>.SPR` (22x27) at (289, 12) | 0x453B8 |
| lap time | `TIME.FNT`, yellow (254), "M:SS;cc" at (255, 14) (the ':' / ';' glyphs read 0'00"00); last lap time again at (255, 32) for 4 s after a lap | 0x453B8, 0x4422D |
| lap | "=%2d" centred in x 253..291 at y 23 (the '=' glyph reads "LAP") | 0x453B8 |
| messages | TIME.FNT centred over the screen at y 13: "Prepare to Race N" (yellow, countdown), "Final Lap!!" (white, 2 s when the last lap starts), "GAME OVER" (white, 4 s), "Finished Position N" (yellow) | 0x44654, 0x453B8, 0x44CCC |
| damage bars | 3 pixel rows (outer colour 0x33, middle 0x39): engine x 24..67, control x 252..295, y 184 (cockpit) / 185 (external); filled width = damage percent; the control bar reads 100 while steering is scrambled (reverse / hypersensitive) | 0x44557, 0x5219D, 0x572F2 |
| weapon panel | SMALLEST.FNT centred in x 128..191: name (+ "[ammo]") at y 168 (169 without ammo count), "READY" / "LOADING" at y 174 (energy full or not); weapons without an ammo count draw the energy bar x 137..182 at y 175 (colours 0x85 / 0xFC filled, 0x78 / 0xFC when full, 0x33 / 0x39 empty). External views shift the text down 7 px | 0x5C12D, 0x5733B |
| booster panel | selected booster: "Turbo" at y 169 and the fuel bar like above | 0x5BE65 |
| turbo indicator | `TURBO_N.SPR` at (194, 168) (173 external) while a booster burns or the free boost runs | 0x521E4 |
| sights | `NSIGHT.SPR` centred on the projection centre (-14, -11) for weapons with a lock cone; `TSIGHT0/1.SPR` flashing at the locked ship (-6, -6) | 0x5C234, 0x522C0 |
| shake | window centre jitter after damage | 0x440F3 |

## Rules found on the way
* Damage beyond 100 on the human ship: `0x4411A` shows "GAME OVER" for 4 s (`[0x42DFC]` timer 0xFA0), the loop then ends the race (0x44022 -> 0x591D9). The port ends the race the same way (LOSE music).
* Camera keys of the original: F1..F5 (player 1) / F6..F10 (player 2) select camera callbacks 0x44AF6 (cockpit with console), 0x44DFE (smoothed chase), 0x44F64 (rear view: the ship matrix negated, label "Rear" 0x44A25),
  0x45196 (TV camera, jet-by sound), 0x44FB4 (close chase, distance 0x2250..0xBEA0). The port keeps its own `V` cycle.
* Not ported: the pilot portraits that appear with the voice cues (pane table 0x42CAB, DRIVER / EYES / MOUTH sprites), the two-player layout (`[0x543F4] == 1`, CONS_BH / CONS_TH, TURBO_H), the optional smaller window (`[0x49E67]`: y 32..166, centre y 99, frame colour 0x1F), the fps text.

## Fonts and string tables (CONFIRMED)
`.FNT`: "FONT", u16 cell width (bytes per glyph row), u16 height, u8 first char, u8 last char, u16 glyph table offset; table entry per char = u16 bitmap offset (height rows x cell width,
non-zero byte = ink in the current colour) + u16 advance (blitter 0x32B94, metrics 0x2A66A / 0x2A6A3). TIME.FNT is a 5 px font in 23 x 7 cells, SPD.FNT digits are 5 x 11 px in 30 x 15 cells.
`.ST0/1/2`: records `TAG4 u16 length text\0` ended by 0xFFFFFFFF (`parseStringTable`).

## Pause menu
The original's pause state `[0x592DA]` (0x58D5E..0x58F9C): the pause key sets 1, the race clocks stand still (0x20104 / 0x20111), the popup menu (0x5B84E) returns 1 continue, 2 configuration, 3 quit the race (0x591CE),
4 exit to DOS (0x55DEE). Texts from `PAUSED.ST0` ("Continue Race / Configuration / Quit Race; the fourth entry reads "Exit Game" instead of the original's "Exit To Dos"") and `CONFIG.ST0` ("General / Controls / Detail / Continue / Difficulty / Sound").
The port: `Esc` (gamepad Start) opens the menu while driving; Up/Down (W/S), Left/Right (A/D) adjust values, Enter / Space select, `Esc` goes back; Quit Race returns to the track viewer, Exit leaves the program.
Configuration pages (the port's own layout): Sound (music / effects volume), General (mph / kph), Difficulty (0..2, live like `[0x49F04]`), Detail (scenery size thresholds 32 / 20 / 10 / 5, 0x350C7), Controls (key list).
The menu is drawn with MENUFONT.FNT on a dimmed frame; the original's menu zone engine (ZON files, buttons) is not ported. `H` hides the HUD (and shows the debug text again).
