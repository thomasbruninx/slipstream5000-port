# Renderers

The game can draw its 3D view with different back ends, chosen with `--renderer NAME`:

| name | what it is |
|---|---|
| `software` (default) | the compatibility renderer: a CPU rasteriser that follows the original's structure (painter's order per track piece, palette shade ramps, no filtering) |
| `opengl` | the same picture drawn by the GPU (OpenGL 3.3 core), much faster (about 4 ms per frame instead of 18 on an M4), with shader support |

If a back end cannot start (no display, no OpenGL 3.3, a broken shader) the game says why and falls back to the software renderer. `--shader NAME` selects a post-processing shader for the OpenGL
back end, `--filter MODE` the texture filtering and `--aa N` multisample anti-aliasing (see below).

## Architecture

```
ViewerApp ──► slip::Renderer  (src/renderer/renderer.hpp, renderer.cpp)   <- the front end, shared by every back end
                 │  camera basis, portal visibility, polygon culling / clipping, the original's lighting law, panel detail (lamps, lines, floors), ship shadows,
                 │  and a stream of primitives: triangles, lines, sprites, rectangles
                 ├── SoftwareRenderer (software_renderer.cpp)   rasterises them on the CPU (z-buffer, painter items, id buffer for shadows)
                 └── GlRenderer       (gl_renderer.cpp)         batches them for the GPU (gl_loader.cpp loads the OpenGL functions through SDL)
```

* The **front end** knows the game (`Scene`, materials, pieces, portals) and nothing about pixels. It calls a small set of pure virtual functions that a back end implements:
  `resize`, `onBeginFrame` (backdrop), `rasterTri`, `drawLine3D`, `shadowBegin` / `shadowEnd`, `drawSky`, `drawSpriteWorld`, `drawLineWorld`, `drawStarWorld`, `drawRectScreen`, `finishScene`, `setScene`.
* The **framebuffer** (`framebuffer()` / `pixels()`, 0xFFRRGGBB) stays on the CPU for every back end: the HUD, the menus, the movies and the presentation draw into it. A back end that renders elsewhere
  delivers its picture into it in `finishScene()`, which the application calls at the end of every 3D pass (main view, rear monitor, ship preview) before the HUD is drawn.
* The application only holds a `std::unique_ptr<Renderer>`; `createRenderer(name, options, &warning)` (`renderer_factory.cpp`) is the only place that knows the concrete classes.

### Adding a back end
1. Derive from `slip::Renderer`, implement the pure virtual functions (the software renderer is the reference, `docs` of each function are in `renderer.hpp`), allocate `color_` in `resize`.
2. Register the name in `renderer_factory.cpp` (`rendererNames`, `createRenderer`) and add the sources to `CMakeLists.txt` (`slipstream_renderers`).
3. Compare with the software renderer: `slipstream --viewer --track N --cam x,y,z,yaw,pitch --screenshot a.ppm` with each `--renderer`, then diff the two images (they should differ by well under 1 %).

## The OpenGL renderer

* A hidden SDL window carries the OpenGL 3.3 core context (macOS: 4.1 core through Apple's driver). Everything is drawn into an offscreen framebuffer at the internal resolution (`--res`, default
  960x540) and read back into the CPU framebuffer once per 3D pass, so nothing else in the game changes.
* **Same look**: palette indices are converted to RGBA when a track is loaded (one texture atlas per scene, nearest filtering, colour key = alpha 0, repeat by `fract()` in the shader); flat polygons get
  their colour on the CPU from the lighting law of the original; the sky is a shader (horizon split, ramp gradient) plus the cloud / hill sprites.
* **Painter's order**: the original draws one track piece after the other without a global depth buffer. A new item clears the depth inside its window (scissor = the portal window of the piece), so it
  overwrites everything before it and is depth tested only against itself. Later polygons of an item get a slightly nearer depth (decals win over the wall they lie on), lamps and shadows a little more.
* **Overlays** (sprites, beams, sparks, markers) are tested against a copy of the real depth of the scene (second colour attachment), like the software renderer, and are hidden by anything clearly nearer.
* **Lines** are drawn as quads 1/320 of the frame width thick, as in the original's 320x200 proportions.
* Ship shadows are the projected ship polygons clipped to the receiving polygon (the software renderer uses an id buffer instead).

### Texture filtering and anti-aliasing
Both are off by default so that the picture is the original's crisp pixels; they only exist in the OpenGL renderer.

| option | effect |
|---|---|
| `--filter nearest` | default: one texel per pixel, like the original |
| `--filter bilinear` | smooth magnification (the repeat of the polygon texture is kept, the colour key does not leave dark fringes) |
| `--filter smooth` | bilinear plus a four-tap footprint filter when a texture is minified, so distant walls do not shimmer |
| `--aa 2` / `4` / `8` | multisample anti-aliasing of the 3D view (clamped to what the driver offers; falls back to off with a message); it smooths polygon and line edges, not texture content |
| `--shader fxaa` | cheap edge-aware post-process anti-aliasing (also smooths the colour-key edges that MSAA cannot) |

They combine, e.g. `--renderer opengl --aa 4 --filter smooth`. The cost is small (about 0.4 ms per frame at 960x540 on an M4 with 4x MSAA and `smooth`). The filter is done in the scene shader with `texelFetch` on the
atlas (no mip maps, no bleeding between the textures of the atlas); the depth the overlays are tested against is resolved from the multisampled target before use.

### Effects shaders (`--fx`)
Water, smoke, fire and collision sparks get their own shaders (OpenGL renderer; independent of `--lighting`, they combine):

| effect | what it does |
|---|---|
| **Water** (London, Arizona, Cave, Amazon, Norway, New York: the `Water*` materials) | animated wave normals (four moving wave trains, flattened with distance so it does not shimmer), the texture is refracted by the waves, fresnel reflection of the track's sky colours, sun glitter (bloom picks it up with `--lighting`) |
| **Sea** (Hawaii, New York: the flat ground below the horizon) | a procedural water plane with the same waves, fresnel reflection of the sky ramp and sun glitter |
| **Smoke** (missile trails, smoke screens, damage smoke) | soft particles: faded where they touch geometry, dissolving with a noise erosion as they age, shaded like a ball (sun + ambient) instead of the flat sprite |
| **Fire / explosions** | additive, hot-core colouring with noise flicker; overexposed cores bloom with `--lighting` |
| **Sparks** (wall scrapes, water droplets, debris chips) | motion-aligned glowing streaks plus a small glow instead of a plus-shaped star |

The particles still come from the original's logic and sprites; only how they are drawn changes. A particle behind a wall is hidden, one inside geometry fades out instead of being cut.

### Lighting
`--lighting lights|shadows` adds real-time lighting (tunnel lamps, lamp panels, sun, sky, dynamic lights) and sun shadows; where lights and shadows belong is described in `docs/lighting.md`.

### Shaders
The default GLSL 330 sources are in `src/renderer/gl_shaders.hpp`. Any of them can be replaced by a file of the same name in `<user folder>/shaders` (macOS `~/Library/Application Support/Slipstream/shaders`,
Windows `%APPDATA%\Slipstream\shaders`, Linux `~/.config/slipstream/shaders`) or in the folder named by `SLIP_SHADERS`:

| file | stage | interface |
|---|---|---|
| `scene.vert`, `scene.frag` | track, scenery, ships | vertex: camera-space position, uv, colour, atlas rectangle, depth scale; keep the outputs and the two colour outputs (colour, reverse depth) |
| `sky.frag` | backdrop | uniforms `uFrame uCenter uFocal uRight uUp uFwd uSky uGround uBand uRampN uRamp[]` |
| `overlay.frag` | sprites, beams, rectangles | `uTex`, `uDepth` (scene depth copy), `uUseTex`, `uUseDepth` |
| `post_NAME.frag` | post-processing | selected with `--shader NAME`; `uniform sampler2D uScene; vec2 uSize; vec4 uRect;` and `out vec4 oColor`. A file without `#version` gets the common header (version, uniforms, `fetch(px)`) added |

Built-in post shaders: `crt` (scanlines, vignette, glow), `smooth` (edge-aware blur), `sharpen`, `fxaa` (edge anti-aliasing). Example of a user shader (`post_red.frag`):

```glsl
void main() { vec3 c = fetch(gl_FragCoord.xy); oColor = vec4(c.r, c.g * 0.4, c.b * 0.4, 1.0); }
```

### Not done / ideas
* The HUD, menus and movies are CPU drawn; only the 3D view is on the GPU (a GPU presentation path would remove the read-back).
* No real mip maps (the `smooth` filter approximates them), no real-time lighting beyond the original's.
* A Vulkan / Metal back end would be another `Renderer` subclass.
