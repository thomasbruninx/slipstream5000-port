# Lighting and shadows (OpenGL renderer)

`--renderer opengl --lighting off|lights|shadows` (default `off` = the original's flat look; the software renderer has no real-time lighting).

The original has only one lighting law (light from above, flat shade ramps). Everything below is **the port's own addition** (INFERRED, not from the original). The decisions about *where* lights
and shadows belong are made when a track is loaded (`buildLighting` in `src/game/scene.cpp`), so every renderer can use them (`Scene::lights`, `Scene::env`, `PieceBox::roofed`).

## Where light comes from
| situation | rule |
|---|---|
| **Open air** (every piece that is not a tunnel) | sun + sky ambient (brighter from above, bounce from below). Sun direction / strength / sky ambient are per track (table `kEnv`): bright mid-day for Chicago / Hawaii / New York-day look, low sun for Norway / London / Egypt, no sun for the night tracks (Tokyo, New York) and the cave |
| **Tunnels** (a piece whose ceiling covers at least 30 % of its footprint, or the path node lies under a ceiling) | no sun, a low indoor ambient, and one warm lamp per tunnel piece at the ceiling of the path node, radius about one piece |
| **Lamp panels** (ceiling lights, `ChaseOrange`) and **floor lights** (`ChaseFloor`, New York has 102) | a light at the panel in the colour of its material's brightest ramp entry; the panel itself is drawn unlit and bright (emissive) |
| **Refuel pads** (`Refuel`) | a blue light |
| **Dynamic** | blasters (red) / disrupter (blue) / missiles (orange) light up their surroundings, explosions flash (fading with the fireball), a burning booster glows blue |

The 48 most relevant lights of a frame are used (dynamic ones first, then fixed ones by distance / reach). A light falls off smoothly to zero at its radius.

## Where shadows fall
`--lighting shadows` adds one sun shadow map (2048x2048, orthographic around the camera, snapped to its texel grid so it does not swim, 3x3 PCF) holding the whole track mesh and the ships. Only
**sunlit surfaces** use it: tunnels (lit by lamps) and the night tracks have no sun, hence no sun shadows. Ships cast shadows onto the road and walls, towers and bridges shade the road beside them.
Lamps cast no shadows (cost). The original's blob shadow under the ships (software renderer) is still drawn in addition.

## How surfaces are lit
The colour of a surface is always **the original's colour** (texture, or the shade-ramp entry of the original lighting law), so textures, flat polygons and the black craft keep their look. Real-time lighting modulates it
around the original's brightness: `colour * (mix(1, ambient + sun * N.L * shadow, 0.7) * exposure + lamps) + specular`. `exposure` (per track) makes an average open-air surface keep the brightness of the original; tunnel
surfaces sit at `indoorLevel` away from lamps. Lamps are summed softly (many panels do not blow out), lamp panels themselves are emissive (1.8x) and feed the bloom. Surfaces facing away from the viewer are lit on the
side that faces it; polygons without a normal get their geometric one; lines, shadows and sprites are not lit.

* **Specular**: Blinn-Phong from the sun and the lamps; strong on craft and doors (0.6), water (0.8), faint on walls (0.1).
* **Shadows**: two sun shadow maps (4096 px around the camera, 2048 px wide), 5x5 PCF, slope-scaled normal offset against acne.
* **Ambient occlusion** (`--no-ao` turns it off): screen-space, depth based, 16 samples in a world-space radius of 26000 units; darkens corners, undersides and contact areas.
* **Bloom** (`--no-bloom`): bright / emissive parts (lamp panels, specular glints, explosions) glow; half-resolution blur, added before a soft tone-mapping shoulder (the lit picture is HDR, RGBA16F).
* Ship cards (garage, best drivers, results) are drawn without real-time lighting, with the original colours.

## Draw distance
The *Draw distance* option (*Configuration > Detail > More effects*, applied at once) scales how far lights and shadows reach: the lamps considered around the camera (base 500,000 units beyond their own radius), the wide sun shadow map (radius 650,000 units) and the sharp one (110,000, scaled by the square root). 0.6x / 1x / 2x / 3.5x for Short / Medium / Long / Maximum. A short distance pops lamps and far shadows in earlier; the cost of the maximum is small.

## Tuning
All values are in `buildLighting` (environment table, lamp colour / radius / indoor level) and the light multiplier in `GlRenderer::prepareLighting`; the shading itself is in `scene.frag` (replaceable, see renderers.md).

## Not done
* Lamps and dynamic lights cast no shadows; the ambient occlusion is depth-only (no normals), so it is soft.
* The environment table is estimated from the sky colours; per-track tuning by eye is welcome.
