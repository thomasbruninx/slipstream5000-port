# Weapons, pickups and pilot voices

Port: `src/game/weapons.{hpp,cpp}` (simulation, SDL-free), `tests/weapons_tests.cpp`, the overlay in `src/platform/viewer_app.cpp`,
voice cues in `src/audio/audio_system.cpp`. Every number below is read from the user's `SLIPSTRM.EXE` at run time.
Labels: **CONFIRMED** = read straight from the disassembly, **INFERRED** = follows from the code but a detail is assumed,
**SPECULATIVE** = guess (marked in the code too). Addresses are virtual addresses of the LE image; pointer tables hold
object-relative offsets (+0x10000).

## 1. Weapon table (CONFIRMED, 12 records x 0x38 bytes at 0x5D612)
`+0` name (16 B) · `+0x10/+0x14/+0x18` shop price per difficulty level · `+0x1C` rounds per purchase (-1 = unlimited) ·
`+0x20` energy refill per second (2.14) · `+0x24` energy a shot needs (2.14, 0x4000 = a full pool) · `+0x28` launcher address
(relocated) · `+0x2C` lock-on cone half angle (1/65536 turn; 0 = no lock-on) · `+0x30` / `+0x34` engine / steering damage
(16.16 percentage points, `RaceSlotDamage` units).

| id | name | refill/s | cost | cone | damage engine / steering | launcher | projectile |
|---|---|---|---|---|---|---|---|
| 0 | Blaster | 0.125 | 0.1875 | 0x145 | 1 / 1 (x2 when a beam hits, x4 at difficulty 2) | 0x5C3BF | 2 beams, server 0x5C481 |
| 1 | Disrupter | 1 | 1 | 0x145 | (none, see 3) | 0x5C66C | 2 beams, server 0x5C741 |
| 2 | Frag | 1 | 1 | 0x145 | 2 / 15 | 0x5CE0A | missile, server 0x5D24B |
| 3 | Super Frag | 1 | 1 | 0x145 | 4 / 25 | 0x5CF17 | missile 0x5D24B |
| 4 | Seeker | 1 | 1 | 0x145 | 15 / 2 | 0x5CBF0 | missile 0x5D24B |
| 5 | Super Seeker | 1 | 1 | 0x145 | 25 / 4 | 0x5CCFD | missile 0x5D24B |
| 6 | Ambler | 1 | 1 | 0x145 | 1 / 1 | 0x5C92C | missile, server 0x5CB03 |
| 7 | Scrambler | 1 | 1 | 0x145 | 25 / 25 | 0x5D131 | missile, server 0x5D33A |
| 8 | Hyper Neuro | 1 | 1 | 0x145 | 1 / 1 | 0x5CA24 | missile, server 0x5CB03 |
| 9 | Smoker | 1 | 1 | 0 | - | 0x5C34D | smoke effect (0x4F79E) |
| 10 | Bomber | 1 | 1 | 0x145 | 1 / 1 | 0x5D024 | missile 0x5D24B |
| 11 | Mini Mines | 1 | 1 | 0 | 10 / 10 | 0x5D4C6 | 4 mines, server 0x5D453 |

Boosters: table 0x5BD44, 5 items x 0x24 bytes: name, `+0x18` price, `+0x1C` speed factor (1.10, 1.15, 1.20, 1.25, 1.30), `+0x20` fuel
burn per second (0.25, 0.1875, 0.156, 0.125, 0.0625). Item names: Delphine Injection, Corolis Dynamic, Dual Derwent, Cleric
Quinn, Tech Tech 301.

## 2. Ship state used by the weapon code (CONFIRMED, slot data = slot+0x60, record = `[slot data+0x20]`)
* `+0x14` selected weapon: 0 blaster, 1 weapon A (record +0x32), 2 weapon B (+0x36), 3 booster (record +0x42 >= 0). Cycling (0x51248) goes
  0 -> A -> B -> booster -> 0, skipping empty slots. Record: `+0x3A/+0x3E` rounds (negative = unlimited), `+0x46` flag 1 = pools and
  the blaster cooldown refill twice as fast, flag 2 = lock cone doubled (the original tests that bit on the *slot* data by mistake at
  0x51054; the port uses the record flag as intended).
* `+0x16/+0x18/+0x1A` energy pools (blaster, A, B), full = 0x4000; refilled by `table +0x20 * dt` (0x50EF6..0x50FC4).
* `+0x30` blaster cooldown 0x1F4 ms (0x12C with flag 1); `+0x3C/+0x3D` fire request now/previous frame; `+0x1C` lock target;
  `+0x4C` = 0x3A98 ms at the start: a *human* ship cannot lock on with a non-blaster weapon for the first 15 s (0x50FDA).
* Fire (executed in the draw message, 0x50C5E..0x50D99): blaster needs cooldown 0 and energy >= 0xC00, then spends it; weapons A/B need
  energy >= cost and rounds left (0 = cannot fire), spend one round and the energy; when the last round goes the record slot is set to
  -1, the selection moves on and the human hears cue 0. Selecting the booster and pressing fire toggles it (edge triggered, needs fuel).
  While the countdown flag `[0x54408]` is set the control bits throttle and fire are masked for every ship (0x51165): this is the
  original's grid hold (it replaces the earlier "INFERRED" statement).
* Booster (0x510CB): fuel `+0x32` (0x4000 at the start if the record has a booster) falls by `item.burn * dt`; it switches itself off
  at 0. The speed factor of `RaceSlotMove` gains `item.factor - 1` while the booster burns or the free-booster timer `+0x2E` runs.
* Pit lane (0x50E97..): inside the refuel piece both damage counters fall by 25 points/s and the booster fuel refills at 1/s.
* Timers on the slot data (ms): `+0x26` reversed steering and pitch, `+0x28` speed cap / 2, `+0x2A` throttle forced on, `+0x2C` steering
  and pitch x16 clamped to +-1 (also while `+0x40`, the 0.25 s jolt of a blaster hit on a human), `+0x2E` free booster. They are read in
  `RaceSlotMove` (0x51B10..0x51D5C): `ShipState::reverseTime/halfCapTime/forceThrottleTime/hyperTime/boosterFreeTime`.

## 3. Lock-on (CONFIRMED, 0x50FC4..0x510C7 and 0x140BF)
Only weapons with a non-zero cone. Origin = the ship's `head` reference point (ART root node), forward = ship forward. A ship is a
candidate when, in the shooter frame (x right, y up, z forward) with `e` = the candidate's extent: `z + e >= 0x988`,
`z*sin(a) +- x*cos(a) + e >= 0` and the same for y (a = cone angle), and its distance <= 0xEE480 (975 000). The nearest candidate wins
(`CombatState::lockTarget`). The AI uses the same test every update.

## 4. Projectiles
Created at the launcher (`ref points`: `lasl`/`lasr` for beams, `weap` for missiles, `smok` for the Smoker; ART root node) with the
owner's orientation; the owner's sound: effect 4 (beams), 5 (missiles, smoker), 7 (mines).

* **Beams (Blaster, Disrupter)** CONFIRMED: speed 0x77240 units/s along the heading, lifetime 0x1388 ms. If the owner had a lock the
  beam is aimed at the target when created and re-aimed every frame while the target is within 0x3400/0x4000 (cos 0.8125, ~36 degrees) of
  the heading. Each frame the head moves along the heading and the segment is tested against the other ships (0x139AD); a hit sends
  message 0x202 (weapon id 0/1, owner) to the victim and ends the beam. Beams ignore walls (no 0x107 handler). The drawn line colours
  are placeholders (SPECULATIVE).
* **Missiles** CONFIRMED: start speed = owner speed + 0x22E98 (Frag, Super Frag, Seeker, Super Seeker, Bomber), + 0x45D30 (Ambler, Hyper Neuro)
  or + 0x1174C (Scrambler); acceleration 0x22E98 units/s^2 up to 0x9D1AC (643 500). With a target the heading turns towards it at
  0x4000 angle units/s (0.25 turn/s = 90 degrees/s, 0x212F8). The Scrambler only homes within 0x2FA80 of its target (0x5D3C9; farther
  away the original calls a visual routine, 0x4A4CF, and keeps flying straight). No lifetime. Hitting a ship (message 0x106) or a wall
  (0x107) ends the missile; a wall hit spawns an explosion effect (0x4F7BC). Projectile models: AIRMINE, AMBLER, BOMBER, FRAG, HYPER,
  SCRAMBLE, SEEKER (.SHP names at 0x5BF5E, scaled like the ships).
* **Mini Mines** CONFIRMED: four mines at (+-4880, +-4880, -9760) in the ship frame (table 0x5D5D6), lifetime 0x2710 ms, no speed. A ship that
  touches one takes 10/10 damage and the mine explodes (0x4F61B).
* **Smoker**: only an effect call (0x4F79E, 4 s) in the code read so far. The port draws a smoke cloud, nothing else (INFERRED).
* Collision of projectiles with ships: segment (beams) or sphere-swept segment (missiles, radius = half the largest model extent)
  against the ship's collision box; the owner is excluded (INFERRED, the original's pair filter was not found; mines can hit their
  owner after 1 s in the port); missiles also stop at track polygons using the ship sweep with a tiny box.

## 5. What a hit does (victim side, CONFIRMED)
* Message 0x202 (beams, 0x50651): human victim -> boost timer `+0x3E` cleared and `+0x40 = 0xFA` ms (slow + jittery steering).
  Blaster: damage 2/2 (table value << 1, 0x5C0F9; << 2 at difficulty level 2), effect 10 (LASERHIT), the human hears cue 0x3F. Disrupter: no damage, `+0x26 = 0x1388` (steering and pitch
  reversed for 5 s), effect 11 (DISRUPTR).
* Message 0x106 with a weapon slot (0x5082F..0x50A57): effect by weapon: Ambler `+0x28 = 0x2710` (10 s, effect 16), Bomber `+0x2A = 0xFA0`
  (4 s, effect 13), Hyper Neuro `+0x2C = 0x2710` (10 s, effect 15), Scrambler effect 14, Mini Mines effect 9 (+ cue 0x40 for the human),
  everything else effect 9 (EXPLOSN); then `RaceSlotDamage(table +0x30, +0x34)` (3 s immunity applies). When the *human* fired the weapon
  the pilot of the victim's class answers with a line (cue 13+class, table 0x50881).
* Not ported: the push along the contact normal and the
  0.625 speed factor of the pair response for projectile contacts, screen shake and flash (0x440F3, 0x4F3A0), and the particle effects
  (see "Particle effects" below).

## 6. Bonus objects (CONFIRMED structure)
Placement table `0x5502C` (per track 1..10: count + 16-byte entries x, y, z, type; type -1 = random, 0x42A9A picks from {0,1,3,2,5}). The
object is a camera-facing sprite `BONUS<type>.SPR` (65x63; REPAIR, REPAIR, TURBO, a red arrow cross, $, BOOST) with a +-0x2620 collision cube; it is consumed by
the first ship that touches it (0x42BD5). Types (0x50724..0x5082B): 0 engine damage = 0, 1 steering damage = 0, 2 booster fuel refilled, 3
**reversed steering and pitch for 5 s** (the red icon; it affects AI ships too), 4 credits + 50 (record +4), 5 free booster for 5 s. Effects
play sound 6 (types 0,1,2,4), 11 (type 3) or 12 (type 5). Sprite size on screen = the collision cube (INFERRED; the original stores
a scale 0x1C98 at slot +0x34 whose unit is not decoded). Pickups are placed once, there is no respawn.

## 7. AI weapon use (CONFIRMED structure, timing INFERRED)
The AI loadout (0x58641): weapon A by ship class `{4,4,7,5,2,2,3,1,8,2}` (table 0x58675), 6 rounds (0x5869D), no B, booster item 0. Every
update (about 30/s, the port decides every 1/30 s): with probability 0x2000/0x10000 the weapon is cycled (0x5155E), otherwise the AI fires when
the selection is not the booster and it has a lock (0x51570..0x5157E). A finished ship does not shoot. The pilot of an AI ship that has the
human locked says a line (cue 43+class, table 0x515A4, checked every update, rate-limited by the cue rules). The human's default loadout in the port is the original's cheat-mode
loadout ([0x53FF8]: Seeker + Scrambler, 9 rounds each, booster 0); the real game fills it from the shop (not ported). `--weapons none`
gives a bare ship, `--weapons seeker:5,mines:8,booster:3` anything else.

## 8. Voice cues (CONFIRMED mechanism and tables; every trigger of the executable is wired, see the end of this section)
`VoiceCue` 0x530B8, list of mode 3 (initialised with `InitVoices(3, 0, no-repeat=1)` at race start): 85 entries `{name, ..., pilot}` (stride
0x1C, list pointer at `[0x52EE4 + 3*4]`): 0-1 announcer/winner lines (EF93, EF104), 2-3 "breaking up" (EM38, EM41), 4-13 contact (table 0x509BA: the original only plays it when the high word of the slot's slide x (slot data +2) is 2 after a ship contact, `shipPairResponse` reproduces that quirk), 14-23 victim answers when the human hits a ship, 24-43 passing lines (tables 0x50C03/0x50C2B: when the human passes the best AI ship, random line 1 / 2 of the human's own pilot, 0x50BE7),
44-53 AI taunts, 54-64 weapon announcer (0x36 Disrupter .. 0x3C Scrambler, 0x3E Bomber, 0x3F "under fire", 0x40 mine hit), 65-74 EPS0-9 (position), 75-84 finish lines.
Rules: a cue is dropped if one of the last four cues equals it, and dropped while the previous line is still playing (no queue).
Triggers in the port: launch of the player's weapon, rounds exhausted (cue 0), hit on the player (0x3F / 0x40), player hits a ship (14+),
an AI ship locks the player (44+), lap line (position announcement 64+rank, 0x5A5FE), finish (75+class, 0x5A6AA, dropped while the position
line plays) and 4 s later the result line (rank 1: cue 0/1 at random, otherwise rank+1, 0x5A9A9), damage beyond 100: cue 2 or 3 at random.
`--no-voices` silences them.

## 9. Controls
`F` fire (gamepad X/west), `X` next weapon (gamepad B/east). With the booster selected `F` switches it. HUD line: energy pools, rounds,
booster fuel, lock, active effects. Red square = lock target.

## Drones (`0x4A240..0x4A847`, `DRONE.ART` / `DRONE.SHP`) - added with the championship phase
Little white-and-red craft (`DRONE.SHP`: WHITEPLASTIC, REDMETAL, BLUEGLASS; `DRONEA.SHP` is the damaged level of detail, `DRONES / DRONESS` the shadow shapes, `DRFRG00..07` debris) that fly along the track.
* **Spawning** (`0x4A291`): the first one 1 s after the race starts, then one every 10 s (`[0x4A22C]` = `0x3E8` / `0x2710`) while fewer than 6 live (`0x4A29F`); it is created 10 path nodes ahead of the human
  ship (`0x36282`, ECX = 10; if the chain is shorter the attempt repeats after 1 s). Not while the ships wait on the grid. Not in multiplayer (the port keeps drones local).
* **Flight** (`0x4A4CF`): along the path nodes; the node counts as reached within `0x11DF0`; speed `0xAE8F8` while the node is farther than `0x3B920`, else `0x1F6BC + 0x2F21A * (0x4000 - sum (0x8000 - node.straight) over the next 0x5F500 units) / 0x4000`
  (the AI's formula with fixed constants), never above `0x2BA3E`. The port turns the heading smoothly (rate 2.5 / s) and removes a drone that is 3.5 million units away from the human (own rule).
* **Hits** (`0x4A3B2`): message `0x202` (a **beam**: blaster or disrupter) -> explosion + a bonus object (`0x42A9A`, lifetime `0x3A98` ms = 15 s) whose type is `table[Random(count - 1)]`: single races `{0, 1, 3, 2, 5}`,
  championship `{0, 1, 3, 2, 4, 5}` (repair engine, repair steering, reversed controls, booster fuel, +50 credits, free booster); `Random(count - 1)` never reaches the last entry, so the free booster
  never drops. Message `0x106` (a missile, mine or ship touching it) -> explosion only. Message `0x107` (wall) -> a burst. The hit sphere is 0.9 of the model's radius (own estimate).
Own additions (user request, not in the original as far as decoded): the human's lock-on cone (`updateLock`) also picks drones (a nearer drone beats a farther ship; `CombatState::lockDrone`, the HUD marker follows it) and locked missiles / beams home on them; anything the human shoots down leaves a bonus, not only beams.
Port: `src/game/drones.*` (flight, spawning, ship collisions), `CombatWorld::stepProjectile` (drone hits), `dropPickup / randomBonusType`; test in `tests/weapons_tests.cpp`.

## Tactical AI (own addition, `CombatWorld::aiDecide`)
The original's AI (0x5154F..0x515D0) cycles its weapon at random (1/8 per decision, 30 decisions a second) and fires whatever is selected at any lock-on, so it spends its heavy weapons in the first seconds.
The port replaces that by default (`SLIP_CLASSIC_AI=1` brings the original logic back; nothing else in the weapons or the AI driving changes):
* **Aggression** = pilot temper (0.8..1.2 per ship) x (0.12 + 0.55 x race progress) + **anger**. Race progress is 0 for the first 15 s and reaches 1 after another 150 s. Anger rises with every hit the pilot takes from anyone
  (beam 0.15, missile 0.4, mine 0.25), remembers who did it and fades by 0.012 per second. So the field starts calm and gets nastier with time and with provocation, the human and other AI ships alike.
* **Blaster** only when aggression >= 0.15, with a firing chance per decision of 0.02 + 0.10 x aggression (scaled down to 30 % when the pilot is calm and the target is not the one it blames).
* **Heavy weapons** (weapon A / B with a lock-on cone) only when aggression >= 0.45 or for revenge on the ship it blames, with a lock closer than 55 % of the lock range, one shot and then a pause of 5..11 s
  (shorter when angrier). The pilot selects the weapon itself (blaster otherwise), it no longer cycles at random.
* **Defensive weapons** (no cone: mines, smoke): used when aggression >= 0.35 and a rival is within 60000 units right behind, with a 4 s pause.
* The booster is not touched (as in the original, where the AI never fires with the booster selected).
Test: `tests/weapons_tests.cpp` ("tactical AI").

All callers of `VoiceCue` (0x530B8) in the executable were listed: 0x46C95 (vehicle card), 0x50689 / 0x506D2 / 0x50940 (under fire / mine), 0x5087E (victim answer), 0x509B7 (contact), 0x50C00 (passing), 0x50D3A / 0x50D7C (out of ammo),
0x515A1 (AI taunt), 0x5213A / 0x52184 (breaking up), 0x5A613 / 0x5A6B4 / 0x5A9CC (position, finish, result), 0x5C689 .. 0x5D147 (weapon announcer; the Hyper Neuro launcher has none) and 0x5D5CF (mines: cue 1). All of them are implemented.

## Beam colours (CONFIRMED, 0x5C569 / 0x5C829)
Both beams are drawn as a segment from the previous to the new head position (speed 0x77240 per second, about 30 frames a second) through the line queue 0x3D4B7 with two colour indices packed in `esi`: the blaster `0x00FD00FE`
(0xFE yellow head, 0xFD red tail: the executable's UI colours 248..255 are grey, black, black, grey 0x82, green, red, yellow, white) and the disrupter `0x0040004F` (palette entries 0x4F / 0x40 of the track's palette). Bit 31 is set when the
beam ended or hit something. The port draws the head half in the low word's colour and the tail half in the high word's (the split is INFERRED, the line drawer 0x3E26C was not decoded to the last detail).

## Particle effects (RaceBang; CONFIRMED structure, INFERRED details)
`src/game/particles.{hpp,cpp}`. The original's effect engine (0x274A0..0x27F60, effect table 0x4F14C, sprites installed by `RaceBangInstallSprites` 0x4FB76: `Expl*`, `ExplF*`, `SmkBlk*`, `SmkBlkF*`, `SmkGry*`, `SmkGryF*`, `Fire*`):
* **Emitter** (0x4F79E follows a ship / missile, 0x4F7BC sits still): life in ms, one smoke puff every `period` ms at its position (the effect record holds eleven dwords `{size0, size1, sizeEnd, tGrow, tHold, tFade, listA, listB, flameList, period,
  jitter}`; the engine reads them in 0x27A03 / 0x27C38 / 0x275F7 / 0x27ED7, CONFIRMED). Four records: 0 missile trail (grey smoke, a Fire flame at the tail, period 200 ms; 488 -> 976 in 100 ms, hold 400 ms, fade 200 ms to 976),
  1 black smoke (250 ms; 488 -> 3904 in 250 ms, hold 1500, fade 300 to 4392), 2 black smoke (unused: 1464 -> 1952, 400 / 1200 / 300 ms), 3 smoke screen (400 ms; 2440 -> 7808 in 500 ms, hold 2000, fade 1000 to 9760, lateral jitter 5856).
* **Puff**: an animated sprite (half side = size, a square of side 2 x size, 0x19ACC) with three phases: while it grows (tGrow) and holds (tHold) it shows a random frame of the first list every 30 ms (SmkGry) / 50 ms (SmkBlk); then the
  second list plays in order over tFade while the size moves on to sizeEnd. Start / end sizes are randomised by +-1/16 / +-1/8. The explosion smoke rises with an acceleration of 28600 units/s^2 up to 28600 units/s (0x27BC5, bp = 0x6FB8).
* **Fireball** (0x4F61B -> 0x1E774): size 0x2620, 3 s, grows from a quarter to full size, 75 % random `Expl` frames every 100 ms, 25 % `ExplF` in order.
* **Debris** (0x4F3A0 -> 0x4F7DE, at most 4 pieces; the piece models are `R<n>FRG00..03` / `DRFRG00..03`): flying off at 10725..28600 units/s in random directions, tumbling at 0.19..0.25 turn/s, gravity 15696 units/s^2 down to a terminal speed of 28600,
  gone after 10 s or when they touch the track (the port sweeps a tiny box).
* Call sites: missile exhaust (Frag, Super Frag, Seeker, Super Seeker, Bomber, Scrambler: effect 0, 5 s, 2151 units behind the missile) / smoker (effect 3, 4 s at the `smok` point) / a hit of 9.0 or more damage on the engine leaves effect 1 at `smok` for
  4 s (RaceSlotDamage 0x52090) / missile hit on a ship: 4 debris pieces / missile against a wall: black smoke 1 s / mine: fireball + 2 s black smoke + debris / blaster hit: 44 % chance of 3 debris pieces / drone destroyed: fireball + 4 DRFRG pieces.
* **Debris start points** (CONFIRMED, 0x12183 / 0x4F81F): a hit throws off pieces `R<n>FRG50..53` (the ART node's debris list at +0x164: `{char[14] shape, s32[3] position}`, 4 entries; the other list at +0xFC holds `R<n>FRG00..03`, which the original uses when a craft is
  destroyed - not used by the port) and each piece starts at one of those four points, taken in the craft's frame. Drones use `DrFrg50..53` of `DRONE.ART` the same way.
* **Sparks and droplets** (CONFIRMED structure, 0x4FD93 / 0x4FF2E / 0x27F72 / 0x28168 / 0x28253 / 0x50080), triggered by every wall contact (message 0x107, 0x50A64 with count 6): 32 sparks (material `Spark`, ramp 80..95) or, for a `WATE*` surface, 6 droplets
  (`Splash`, ramp 75..79, a single pixel), plus 6 chips (244 units) in the colour of the surface hit. They start 488 units off the wall, fly in a cone of +-14 degrees about the surface normal (two random turns of up to 0xA00) at the ship's speed + 17875,
  live 1.5..1.9 s, fall with the debris gravity and vanish when they touch the track. A spark is drawn as two crossing lines (a star) of half length 0.5..1 x 976 units turned at 0.125..0.25 turn/s, a single pixel when it projects to 2 pixels or less; its
  colour is the next to last entry of the material's ramp. The contact point is the box point nearest to the wall (the port has no contact point; the original passes it in the collision record).
* **A destroyed craft** (damage beyond 100: `RaceSlotDamage` 0x52107 -> 0x4F414, CONFIRMED): the original replaces the ship's handler by the dying object 0x4F452, which keeps flying, bounces 2 or 3 times on walls and then throws four pieces
  `R<n>FRG00..03` (ART debris list 0, `0x4F7DE` with ebx = -1) and turns into the bang object 0x4F639: every 200 ms a fireball (size 0x16E0, 0.4 s) at a random offset of up to +-0xB70 per axis, for 2 s. The port, whose ships go on driving, plays the show at once
  (pieces + bang following the craft, at most once per 8 s per ship); the bounces are not reproduced.
* INFERRED / not ported: the head puff sprite swap of the missile trail, the unknown call `0x3BD82` in front of the spark setup, the dying object's bounces. Test hook: `SLIP_PARTICLE_DEMO=1` shows one of each effect 0.5 s into a race.
