# Front end (menus, movies, vehicle choice, garage)

Port: `src/game/frontend.{hpp,cpp}` (SDL-free; draws a 320x200 buffer that `ViewerApp::renderFront` shows at 4:3), `src/original_formats/gdv.*`, wiring in `viewer_app.cpp` / `main.cpp`. A plain `slipstream` launch now starts here; `--viewer` (or `--track`, `--drive`, `--models`, `--sprites`, `--host`, `--join`) keeps the old track viewer, `--skip-intro` skips the movies, `--front --front-sim S --front-keys "dds." --screenshot f.ppm` renders a screen headlessly (u d l r s=select b=back .=0.5 s).

All art, texts and hit maps come from the user's files; the layout positions are the sprite headers' own position words (`hdr4` x, `hdr6` y, `hdr8` transparent colour). Labels: CONFIRMED = read from data, INFERRED, SPECULATIVE = the port's own design.

| screen | art / data | status |
|---|---|---|
| Gremlin flag | `LOGO_S.GDV` (LOGO.EXE is the GDV player, SLIP.BAT runs it first): 320x200, 82 frames, its own sound | CONFIRMED (method-2 frames carry a u16 prefix, see gdv.md) |
| Intro | `INTRO.GDV` (docs/formats/gdv.md), 320x180 centred, skippable | CONFIRMED decode |
| Gremlin logo | `GREMLOGO.SPR` (IntroLogos 0x565FE: shown until Enter; the port waits 4 s) | CONFIRMED sprite, timing INFERRED |
| Credits (Software Refinery) | CreditsScreen 0x56766: `SOFTLOGO.SPR` backdrop, `GLOBE.SHP` drawn with `SPDTEST.MAT` where all four Earth materials use `SOFTLOGO.SPR` as texture (a ball textured with the logo), projection centre (260, 40), distance 0xC8 -> 0x4074 (at least 0x898) so the ball flies away, texture phase +0x5000/s, `REFINERY.SMP`, the credit texts (read from the executable at 0x56B06, two lines, `SHADE1.FNT`, every 2 s) | structure CONFIRMED; flight duration (7 s) and the shading are INFERRED |
| Main menu | `MAINMENU.SPR`, buttons `MAINBT_1..6.SPR` / `MAINBTH1..6.SPR` at their header positions, labels `MAINMENU.ST0` (OPT1..5) in `MENUFONT.FNT`, music `INTRO.HMP` | CONFIRMED art / texts; the 6th button reads "Exit Game" |
| One Player | the same buttons with `OP11..13` (Practice, Single Race, Championship) | Championship disabled (not implemented) |
| Two Players | the original's link menu (split screen, serial, modem, network) is replaced by the port's network game menu (docs/multiplayer.md) | design |
| Choose Track | `STARS.SPR`, `GLOBE.SHP` with `GLOBE.MAT` (Earth1..4 = `EARTH1..4.SPR` are the four texture quarters of a real textured sphere, 128 polygons), the globe turns to the selected track and plants `FLAG.SHP` (cloth `EARTFLAG.SPR`) on it, banner `CH_TRACK.SPR` + `CHTRACK.ST0`, buttons `TRKBT_0..9` / `TRKBTH0..9` | model / textures CONFIRMED; the city coordinates, the texture seam (longitude 175 E at yaw 0, calibrated on the rendered map) and the flag orientation are the port's |
| Select your vehicle (parking lot) | `CH_TEAM.SPR` and the hit map `CH_TEAMZ.ZON`, title `CH_TEAM.ST0` | CONFIRMED |
| Pilot information | `VIEWCAR<n>.SPR` (name card) with the biography `VIEWCAR.ST0 CAR<n>` in `VIEWDESC.FNT`, alternating with the portrait card `DRIVER<n>.SPR`; Accept / Cancel from `CH_TEAM.ST0` | layout INFERRED |
| Garage | `GARAGE<n>A/B.SPR`, panel `GARBOX4` (Weapons, Turbo, Systems, Start Race = `GARAGE.ST0` BUT1..4), pod window `GARBOX1`, weapon grid `GARBOX2` (the 11 purchasable weapons in table order), turbo grid `GARBOX3` (the 5 boosters), prices from the executable tables (per difficulty), pack sizes = rounds per purchase | art / prices CONFIRMED; **starting money (5000) and the two "Systems" upgrades (fast recharge 1000, wide lock-on 1200 = the record flags of docs/weapons.md) are SPECULATIVE** |
| Best Drivers | `BESTBACK.SPR`, `BESTF<n>` pilot faces, `BESTDRV.ST0` titles, five fastest laps per track saved in `~/Library/Application Support/Slipstream/records.txt` | layout SPECULATIVE; the original's default records are not read |
| Race results | `RACERES.SPR`, `RACERES.ST0` titles, faces scaled, finish times (projected ones marked `*`), shown 8 s after the race end | layout SPECULATIVE |

## ZON hit maps (CONFIRMED structure)
`u16 rows (200)`, `u16 offset[rows]`, per row a list of 3-byte entries `{u8 zone id, u16 end x}` (exclusive, the last entry ends at 0xFFFF): the zone of pixel (x, y) is the first entry whose end is greater than x. `CH_TEAMZ.ZON` marks the ten craft (ids 1..10 = ship 0..9); `RESGAMEZ.ZON` has 7 zones for the results screen (not used yet).

## Flow
Main -> One Player -> Practice / Single Race -> Track -> Vehicle -> (information card) -> Garage -> race -> results -> main menu. Esc steps back; in a race Esc opens the pause menu, Quit Race returns to the main menu. Mouse and keyboard (arrows / WASD, Enter / Space, Esc) work; mouse look is released while a menu is open.

## Not done
Championship mode and its screens (CH_TEAMZ, CHAMPPOS, CWL pass codes, `SAVED` saved games), configuration pages from the main menu (use the pause menu), the commentators' fly-through (`*INT` / `*PREV` string tables, `*.ANN`), animated pilot faces (`FACE` / `MOUTH` / `EYES` sprites), the two-player screens, practice mode differences, the shop's money rules of the original.

## Fonts (CONFIRMED, text routine 0x29DF4 -> 0x2A439 -> 0x32A14 / 0x32B94)
Menu fonts (`MENUFONT`, `CNFFONT`, `GARWEAP`, `VIEWDESC`, `TEAMFONT`, `BESTDRV`, `RESULTS`, `SHADE1`) are **colour fonts**: the glyph bytes are palette indices of the screen's palette and are drawn as they are (text colour -1, `0x2A62A(0xFFFF)`). Only fonts whose glyphs hold a single value (`SMALL`, `TIME`, `SMALLEST` ...) are painted with an explicit colour (0x32B94). The port's first version painted every glyph in one colour, which made the menu text look wrong. All screen palettes receive the executable's UI colours in 248..255 (grey, green, red, yellow, white).

## Pause popup (0x5A34C, 0x5A3CB, hit table 0x5401C)
Four bevelled buttons directly over the frozen race (x 101..220, y 46 / 64 / 83 / 100, height 15): light edge 0x1C, dark edge 0x0C, fill 0x14, the selected button 0xFD, `PAUSED.ST0` text in `SMALL.FNT` colour 0xFF. The original opens a full screen configuration menu (hangar backdrop `GARAGEA.SPR`, `CNFBLOCK.SPR` buttons, `CNFFONT.FNT`) from "Configuration"; the port still uses its own pages in the same button style.
