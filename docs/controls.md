# Controls

## Race (the original's defaults, CFG words `0x492BC..0x492C8`, READ.ME / readme.txt)
| Action | Default key | Notes |
|---|---|---|
| Up (nose down) | Cursor up | the original's flight stick convention: up = nose down |
| Down (nose up) | Cursor down | |
| Left / Right | Cursor left / right | |
| Accelerate | Space | release to coast; **Reverse Accelerator** (Controls page) accelerates while the key is *not* held |
| Fire | Alt (Option) | weapon trigger; with the booster selected it switches the booster |
| Select weapon | Ctrl | cycles Blaster -> A -> B -> booster |
| Brake | *unbound* | the original has no brake; the port keeps its own extra action, bind it if you want it |
| Cameras | F1 cockpit, F2 chase (again: far chase), F3 rear, F4 TV, F5 free | not remappable (also in the original). The free camera orbits the ship: keypad Ins / Del turn it, PgUp / PgDn raise / lower, + / - zoom (Insert / Delete / PageUp / PageDown / = / - work too) |
| Pause | Esc | opens the pause menu; after finishing a race: the results screen |
| Results | Enter | after finishing |
| Quit | Ctrl+Q | at any point (READ.ME) |
| Multiplayer menu | F10 | the port's network game; Tab shows the player list |

**Remapping**: main menu -> Configuration -> Controls -> Player 1 Controls. Select a row, press the new key (Esc cancels); a key that another action already uses is swapped over.
The bindings and "Reverse Accelerator" are stored in `config.txt` in the per-user folder (`~/Library/Application Support/Slipstream`, `%APPDATA%\Slipstream`, `~/.config/slipstream`; see docs/building.md) as SDL scancodes (`key0`..`key7` = Up, Down, Left, Right, Select, Fire, Accel, Brake).
The original's joystick calibration pages are not ported; controllers use SDL3's gamepad API instead (below).

## Controllers (USB / Bluetooth, SDL3 gamepad API)
Any pad SDL knows works (its built-in mapping database; for others put SDL mapping lines in `gamecontrollerdb.txt` in the per-user folder or set `SDL_GAMECONTROLLERCONFIG`). All connected pads are open at once and are read **together with the keyboard and mouse every frame**, so you can switch between them at any moment (also plugging / unplugging while the game runs); there is no mode switch.

| Control | Default | Remappable (Configuration > Controls > Controller) |
|---|---|---|
| Steer / pitch | left stick or D-pad (stick up = nose down like the cursor keys; "Invert pitch" flips it) | no (invert only) |
| Accelerate / brake | right / left trigger | yes (any button or trigger) |
| Fire | X / square (West) | yes |
| Select weapon | B / circle (East) | yes |
| Camera | Y / triangle (North): cockpit, chase, far chase, rear, TV | yes |
| Pause | Start (also leaves a replay / opens the results once the race is over) | yes |
| Free camera (F5) | right stick turns / raises, bumpers zoom | no |
| Menus | D-pad or left stick (with repeat) move, A / Start select, B / Back go back; the fly-through ends on A / B / Start | no |

Bindings are stored in config.txt as `pad0`..`pad5` (0..14 = SDL gamepad button, 1000 + axis = trigger) and `padInvertPitch`. Not verified against real hardware in this session: only the key-binding page and the build were exercised.

## Debug / viewer shortcuts (moved to Ctrl+Shift+key)
The port's older shortcuts sat on plain keys that the original uses for the race (S = select weapon, F1..F5 cameras, ...), so they now need **Ctrl+Shift**:

| Old key | New | Function |
|---|---|---|
| H | Ctrl+Shift+H | HUD on / off |
| M / Shift+M | Ctrl+Shift+M / Ctrl+Shift+U | track map / music |
| N | Ctrl+Shift+N | effects on / off |
| C, Cmd+C | Ctrl+Shift+C | copy the debug text |
| Tab | Ctrl+Shift+V (viewer: Tab) | scenery culling (Tab still shows the player list in multiplayer) |
| F5 / F6 / F7 / F8 / F9 | Ctrl+Shift+B / P / S / A / I | visibility mask / painter / all scenery / assist / AI |
| Space (viewer) | Ctrl+Shift+D | leave / enter driving |
| [ and ] | Ctrl+Shift+[ and ] | previous / next track, model or sprite |
| 1..9, 0 | Ctrl+Shift+1..0 | choose the ship |
| V | Ctrl+Shift+T | cycle the camera (also F1..F5) |

Outside a race (`--viewer`) the old plain keys still work for the viewer's own functions: F1 / F2 / F3 modes, [ ] next item, Space drive, Tab culling, WASD / E Q free fly.
