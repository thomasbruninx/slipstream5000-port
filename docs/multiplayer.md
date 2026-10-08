# Multiplayer (peer to peer over TCP/IP, up to 10 players)

New in this port; the original game's own network layer (two-human flags `[0x543DD]`/`[0x543DF]`, player mode `[0x543F4]`, packet magic 0x7FF0)
is **not** reproduced. Nothing here is derived from the executable, so there are no CONFIRMED / INFERRED labels: it is a design.

## How to play
* **Menu** (F10 in the track viewer / before a race): Host Game, Join LAN Game (lists games announced on the subnet), Join By Address (`HOST` or `HOST:PORT`).
* **Lobby**: players with their ships, race settings (host only: track, laps, difficulty), my ship (each ship number can be taken once), Ready / Start Race, Leave. The host can start when every other player is ready. Empty seats are filled with AI ships (flown by the host).
* **Command line**: `--host [--players N] [--track T] [--laps L] [--name NAME] [--port P]`, `--join HOST[:PORT]`. `--players N` starts the race by itself as soon as N humans are in the lobby; `--net-run SEC` runs headless (autopilot, no window / sound) and prints a result table (used by `tools/net_race_test.sh`).
* Everybody needs the **same game files** (the handshake compares a hash of the data set) and the same version of the port (`kProtocolVersion`).
* The pause menu does not stop a multiplayer race; Quit Race / Exit Game leave the session. A player who leaves is replaced by an AI ship flown by the host. **Host migration:** when the host's connection drops, every peer picks the lowest remaining player id as the new host (all peers hold the same player list, so no election messages); the new host keeps its copy of the lobby, announces the game again, flies the AI ships (and the vacated seat) from their last replicated pose, decides the race end and is the clock reference. A notice is shown for 6 s. Hold **Tab** during a race for the player list: place, name, lap and ping (round trip to the player simulating that ship; every peer pings every peer once a second).
* macOS asks for "local network" access the first time (`NSLocalNetworkUsageDescription` is in the app's Info.plist). Firewalls must allow TCP 51500 (game) and UDP 51501 (discovery); `--port` changes the TCP port.

## Architecture
| layer | files | job |
|---|---|---|
| socket / transport | `src/net/socket.*`, `transport.*` | POSIX non-blocking TCP, 16-bit length-prefixed frames, `ITransport` with opaque peer ids |
| protocol | `src/net/protocol.*`, `byte_io.hpp` | versioned little-endian messages, explicit field serialisation (no struct dumps) |
| discovery | `src/net/discovery.*` | `IDiscovery`; `LanDiscovery`: UDP announcement of the lobby every second to the subnet broadcast address, listeners expire entries after 4 s |
| session | `src/net/session.*` | handshake, lobby, full mesh formation, Ping/Pong clock sync, message routing |
| game side | `src/game/netplay.*`, `viewer_app.cpp` | slot ↔ player mapping, snapshots, interpolation, combat relay |

**Internet play later:** the game code only sees `ITransport` (replace TCP with a relay / hole-punching transport), `IDiscovery` (replace broadcast with a matchmaking client: register a session, fetch the list) and the session id / protocol version / data hash that are already in the handshake. Not done: NAT traversal, authentication, anti-cheat, host migration.

### Topology
Full TCP mesh (at most 10 peers, 45 connections). A joiner connects to the host (`Hello` with no player id), receives its player id and the addresses of the other players (`Welcome`) and dials them (`Hello` with its id). The lobby host (player 0) is the authority for **non-physical decisions only**: track, laps, difficulty, grid (a player's ship number = its grid slot), seed, start time, AI fill, and the end of the race.

### Simulation model
* Every peer simulates **its own ship** (the existing physics, collision with the track, weapons, pit, pickups) and broadcasts a `State` snapshot 30 times a second (position, 2.14 matrix, speed, slide, damage, status timers, laps / finish, selected weapon, lock target).
* The host also simulates the **AI ships** (and ships of players who left) with the existing AI and broadcasts them the same way. Other peers see them as remote ships.
* A remote ship is played back 100 ms behind its newest snapshot with linear interpolation (matrix rows re-normalised) and, when data runs out, up to 250 ms of extrapolation. Sender clocks are mapped with a minimum-delay filter on the arrival times. The session clock (host time) comes from Ping/Pong rounds (NTP style, lowest RTT sample wins) and only places the start.
* Contacts between ships: the local ship collides with a copy of the remote ship (box, moving with its snapshot velocity) and reacts; the remote ship is not pushed here (its owner computes its own reaction against our snapshot). Doors open for remote ships through the same copy.
* **Start**: the host sends `Start` (track, laps, difficulty, seed, slot table, `startAtMs` on the host clock 2.5 s ahead); each peer waits for that instant and then runs the normal 5 s countdown, so all grids leave within a few ms of each other. The seed fixes the random pickup types.
* **Weapons**: the shooter creates the projectiles and sends `ProjSpawn` (id = owner / launch / index, kind, pose, speed, life, lock target). Other peers create flying copies (they home on the replicated poses) and play the launch sound. **The victim's peer resolves the hit** (it knows its own pose best): damage and status effects apply to the ship it simulates, and `ProjHit` removes the projectile everywhere and plays the explosion. A projectile whose victim is simulated on another peer does not collide here. Walls remove projectiles locally on every peer.
* **Pickups**: touching one consumes it locally at once and sends `PickupTaken`; the others remove it. There is no arbitration, so two ships touching the same pickup within one network delay both get it (rare, accepted; the planned host arbitration is not needed for play).
* **Race rules**: every peer evaluates the lap line and finish for the ships it simulates (existing `updateRace`, `lapCrossing`) and sends laps / finished / projected flags in the snapshot. Ranks are computed on every peer from the replicated lap counters and the distance to go. Finishing places claimed at the same moment are renumbered by (claimed place, slot) identically everywhere. The host ends the race 5 s after the second finisher (any human or AI) or when everybody is done and broadcasts `RaceOver`; ships still racing then get the projected finish. A player whose ship is destroyed (GAME OVER) retires with its current place; the race goes on for the others.
* `DoorSync` is defined in the protocol but unused: doors are driven by the (replicated) ship poses on every peer.

## Verification
* `slipstream_net_tests` (ctest `net_tests`): message round trips, framing with partial reads and large messages, a 10-peer localhost mesh, session lobby / start / data-hash rejection / leave, loopback discovery.
* `tests/weapons_tests.cpp`: remote ships are no victims, launches / hits are logged, ids unique, remote hit removes the projectile.
* `tools/net_race_test.sh [players] [track] [laps]`: starts one host and N−1 joiners as headless processes on this machine, runs a race with the autopilot (`SLIP_FIRE=1`: everybody shoots; `KILL=1`: one joiner is killed mid-race) and checks that all peers print the same laps / ranks / finish places. Last runs: 4 players track 2, 6 players track 3, 10 players track 1 with weapons: all peers agree.
* Not yet exercised on two real machines (firewall prompt, Wi-Fi broadcast filtering): `Join By Address` is the fallback when a network drops broadcasts.

## Known limitations
* No names above ships, no chat, no spectators; the result screen is the single-player one.
* After a race the session ends (leave and host again); returning to the lobby is not implemented.
* Migration needs the full mesh (it is formed in the lobby); a ship's pickups / hits in flight at the moment the host drops may be lost.
* Latecomers cannot join a running race. Clock skew between peers is only corrected for the start time.
* The pilot portraits / voice cues of remote events are not mirrored (only your own events speak).
