// Default GLSL (330 core) of the OpenGL renderer. Each of them can be replaced by a file of the same name in <user folder>/shaders (see GlRenderer::shaderSource).
#pragma once

namespace slip::glsl {

// scene.vert: camera-space vertex (x right, y up, z forward) -> clip space with the projection of the software renderer (projection centre and focal length in pixels). The depth value is
// the reverse depth `near / z` scaled per vertex (ds): painter's layering and decals are expressed by those scales; the actual depth is written by the fragment shader.
inline constexpr const char* kSceneVert = R"(#version 330 core
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec2 aUv;
layout(location = 2) in vec4 aCol;
layout(location = 3) in vec4 aRect;
layout(location = 4) in float aDs;
layout(location = 5) in vec3 aNormal;   // camera-space unit normal (0 = unlit)
layout(location = 6) in vec2 aFx;       // x: bit 0 inside a tunnel, bit 1 emits light; y: specular strength
uniform vec4 uProj;   // 2f/W, 2cx/W - 1, 2f/H, (H - 2cy)/H
uniform float uNear;
out vec2 vUv;
out vec4 vCol;
out vec3 vPos;
out vec3 vNormal;
flat out vec4 vRect;
flat out float vDs;
flat out vec2 vFx;
void main() {
  vUv = aUv; vCol = aCol; vRect = aRect; vDs = aDs; vPos = aPos; vNormal = aNormal; vFx = aFx;
  gl_Position = vec4(uProj.x * aPos.x + uProj.y * aPos.z, uProj.z * aPos.y + uProj.w * aPos.z, uNear * aDs, aPos.z);
}
)";

// scene.frag: textured polygons sample the material atlas (colour key = alpha 0), the others are flat. The colour is the ORIGINAL's colour of the surface (texture, or the shade ramp entry of
// the lighting law); real-time lighting (uLighting) modulates it around the original's brightness (an average surface keeps its look), adds lamps, specular and emission. Outputs the colour (HDR
// when lighting is on), the unscaled reverse depth for the overlays / ambient occlusion (attachment 1) and the scaled one as depth.
inline constexpr const char* kSceneFrag = R"(#version 330 core
uniform sampler2D uAtlas;
uniform float uNear;
uniform int uFilter;        // 0 nearest (the original's look), 1 bilinear, 2 smooth (bilinear + a footprint filter that tames shimmering in the distance)
uniform vec2 uAtlasSize;
uniform int uLighting;
uniform vec3 uSkyAmbient;
uniform vec3 uGroundAmbient;
uniform float uExposure;
uniform float uIndoor;
uniform vec3 uSunDir;       // camera space, towards the sun
uniform vec3 uSunColor;
uniform vec3 uUpCam;        // world up in camera space
uniform int uLightN;
uniform vec3 uLightPos[48]; // camera space
uniform vec3 uLightCol[48];
uniform float uLightRad[48];
uniform int uShadowOn;
uniform sampler2D uShadow0;
uniform sampler2D uShadow1;
uniform vec3 uShadowX[2];   // light-space axes in camera space
uniform vec3 uShadowY[2];
uniform vec3 uShadowZ[2];
uniform vec3 uShadowOff[2]; // light-space position of the camera
uniform vec3 uShadowParams[2]; // half extent of the map, half depth range, texel size in world units
in vec2 vUv;
in vec4 vCol;
in vec3 vPos;
in vec3 vNormal;
flat in vec4 vRect;
flat in float vDs;
flat in vec2 vFx;
layout(location = 0) out vec4 oColor;
layout(location = 1) out float oDepth;

vec4 texel(ivec2 p, vec4 r) {
  ivec2 size = ivec2(r.zw);
  ivec2 w = ivec2(mod(vec2(p), vec2(size)));
  return texelFetch(uAtlas, ivec2(r.xy) + w, 0);
}
vec4 nearest(vec2 uv, vec4 r) { return texel(ivec2(floor(fract(uv) * r.zw)), r); }
vec4 bilinear(vec2 uv, vec4 r) {
  vec2 t = fract(uv) * r.zw - 0.5;
  vec2 i = floor(t), f = t - i;
  ivec2 p = ivec2(i);
  vec4 c00 = texel(p, r), c10 = texel(p + ivec2(1, 0), r), c01 = texel(p + ivec2(0, 1), r), c11 = texel(p + ivec2(1, 1), r);
  c00.rgb *= c00.a; c10.rgb *= c10.a; c01.rgb *= c01.a; c11.rgb *= c11.a;
  vec4 c = mix(mix(c00, c10, f.x), mix(c01, c11, f.x), f.y);
  c.rgb /= max(c.a, 1e-4);
  return c;
}

float cascadeShadow(int k, vec3 P, vec3 N, float ndl) {
  vec3 par = uShadowParams[k];
  vec3 q = P + N * (par.z * (2.0 + 3.0 * (1.0 - ndl)));   // normal offset, larger at grazing angles
  vec3 l = uShadowOff[k] + vec3(dot(uShadowX[k], q), dot(uShadowY[k], q), dot(uShadowZ[k], q));
  vec2 uv = l.xy / (2.0 * par.x) + 0.5;
  if (uv.x < 0.02 || uv.y < 0.02 || uv.x > 0.98 || uv.y > 0.98) return -1.0;   // outside this cascade
  float depth = (par.y - l.z) / (2.0 * par.y);
  vec2 texel = 1.0 / vec2(textureSize(k == 0 ? uShadow0 : uShadow1, 0));
  float bias = 0.00025 + 0.0006 * (1.0 - ndl);
  float lit = 0.0;
  for (int y = -2; y <= 2; ++y)
    for (int x = -2; x <= 2; ++x) {
      vec2 o = uv + vec2(x, y) * texel;
      float m = k == 0 ? texture(uShadow0, o).r : texture(uShadow1, o).r;
      lit += (depth - bias > m) ? 0.0 : 1.0;
    }
  return lit / 25.0;
}
float sunShadow(vec3 P, vec3 N, float ndl) {
  float s = cascadeShadow(0, P, N, ndl);
  if (s >= 0.0) return s;
  s = cascadeShadow(1, P, N, ndl);
  return s >= 0.0 ? s : 1.0;
}

vec3 lighting(vec3 albedo) {
  vec3 N = vNormal;
  if (dot(N, N) < 0.25) return albedo;                  // no normal (lines, shadows): unlit
  N = normalize(N);
  vec3 P = vPos;
  if (dot(N, P) > 0.0) N = -N;                          // light the side that faces the viewer
  bool indoor = mod(vFx.x, 2.0) >= 1.0;
  bool emissive = vFx.x >= 2.0;
  if (emissive) return albedo * 1.8;                    // lamps shine (and bloom)
  vec3 V = -normalize(P);
  float ks = vFx.y;
  float shin = mix(18.0, 80.0, clamp(ks, 0.0, 1.0));
  vec3 base;
  vec3 spec = vec3(0.0);
  if (indoor) base = vec3(uIndoor);
  else {
    float up = dot(N, uUpCam) * 0.5 + 0.5;
    float ndl = max(dot(N, uSunDir), 0.0);
    float sh = (uShadowOn != 0 && ndl > 0.0) ? sunShadow(P, N, ndl) : 1.0;
    base = (mix(uGroundAmbient, uSkyAmbient, up) + uSunColor * ndl * sh) * uExposure;
    spec += uSunColor * sh * pow(max(dot(N, normalize(uSunDir + V)), 0.0), shin) * ks * step(0.001, ndl);
  }
  vec3 lit = mix(vec3(1.0), base, 0.7);                // the original's look stays the reference
  vec3 lamps = vec3(0.0);
  for (int i = 0; i < uLightN; ++i) {
    vec3 d = uLightPos[i] - P;
    float dist2 = dot(d, d);
    float r = uLightRad[i];
    float att = max(1.0 - dist2 / (r * r), 0.0);
    att *= att;
    vec3 Ld = d * inversesqrt(max(dist2, 1.0));
    float ndl = max(dot(N, Ld), 0.0) * 0.85 + 0.15;
    lamps += uLightCol[i] * att * ndl;
    spec += uLightCol[i] * att * pow(max(dot(N, normalize(Ld + V)), 0.0), shin) * ks * 0.9;
  }
  lamps = 0.8 * (1.0 - exp(-lamps / 0.8));            // many lamps add up softly
  spec = 0.6 * (1.0 - exp(-spec / 0.6));
  return albedo * (lit + lamps) + spec;
}

void main() {
  vec4 c;
  if (vRect.z > 0.0) {
    vec4 r = round(vRect * vec4(uAtlasSize, uAtlasSize));
    if (uFilter == 0) c = nearest(vUv, r);
    else if (uFilter == 1) c = bilinear(vUv, r);
    else {
      vec2 dx = dFdx(vUv * r.zw), dy = dFdy(vUv * r.zw);
      float foot = max(length(dx), length(dy));
      if (foot <= 1.0) c = bilinear(vUv, r);
      else {
        vec2 ux = dFdx(vUv), uy = dFdy(vUv);
        c = (bilinear(vUv + 0.25 * ux + 0.25 * uy, r) + bilinear(vUv - 0.25 * ux + 0.25 * uy, r) + bilinear(vUv + 0.25 * ux - 0.25 * uy, r) + bilinear(vUv - 0.25 * ux - 0.25 * uy, r)) * 0.25;
      }
    }
    if (c.a < 0.5) discard;
    c.a = 1.0;
  } else {
    c = vec4(vCol.rgb, 1.0);
  }
  if (uLighting != 0) c.rgb = lighting(c.rgb);
  float d = uNear * gl_FragCoord.w;
  oColor = c;
  oDepth = d;
  gl_FragDepth = d * vDs;
}
)";

inline constexpr const char* kFullscreenVert = R"(#version 330 core
void main() {
  vec2 p = vec2((gl_VertexID << 1) & 2, gl_VertexID & 2);
  gl_Position = vec4(p * 2.0 - 1.0, 0.0, 1.0);
}
)";

// sky.frag: the backdrop. A pixel is sky when its view ray points above the horizontal plane; with a ramp the sky colour follows the elevation of the ray (zenith -> horizon).
inline constexpr const char* kSkyFrag = R"(#version 330 core
uniform vec2 uFrame;     // internal resolution
uniform vec2 uCenter;    // projection centre (pixels, y down)
uniform float uFocal;
uniform vec3 uRight;
uniform vec3 uUp;
uniform vec3 uFwd;
uniform vec3 uSky;
uniform vec3 uGround;
uniform float uBand;
uniform int uRampN;
uniform vec3 uRamp[40];
layout(location = 0) out vec4 oColor;
void main() {
  float dx = gl_FragCoord.x - uCenter.x;
  float dy = uCenter.y - (uFrame.y - gl_FragCoord.y);
  float ry = uFwd.y * uFocal + uUp.y * dy + uRight.y * dx;
  vec3 c;
  if (ry <= 0.0) c = uGround;
  else if (uRampN > 0) {
    float s = ry / sqrt(uFocal * uFocal + dx * dx + dy * dy);
    int i = int(floor(float(uRampN - 1) * (1.0 - min(s / uBand, 1.0)) + 0.5));
    c = uRamp[clamp(i, 0, uRampN - 1)];
  } else c = uSky;
  oColor = vec4(c, 1.0);
}
)";

// depthrestore.frag: rebuilds the depth buffer from the scene depth copy (painter items clear the depth buffer; whatever is drawn without an item afterwards - ships, doors - is tested
// against everything drawn so far, like the software renderer's global depth).
inline constexpr const char* kDepthRestoreFrag = R"(#version 330 core
uniform sampler2D uDepth;
layout(location = 0) out vec4 oColor;
void main() {
  oColor = vec4(0.0);
  gl_FragDepth = texelFetch(uDepth, ivec2(gl_FragCoord.xy), 0).r;
}
)";

// shadowmap.vert: depth only, from the sun. aWorld is relative to the scene origin; uOrigin the map centre (scene-relative), uAxes the light-space axes.
inline constexpr const char* kShadowVert = R"(#version 330 core
layout(location = 0) in vec3 aWorld;
uniform vec3 uOrigin;
uniform vec3 uAxX;
uniform vec3 uAxY;
uniform vec3 uAxZ;
uniform vec3 uParams;   // half extent, half depth range
void main() {
  vec3 p = aWorld - uOrigin;
  vec3 l = vec3(dot(p, uAxX), dot(p, uAxY), dot(p, uAxZ));
  gl_Position = vec4(l.x / uParams.x, l.y / uParams.x, -l.z / uParams.y, 1.0);
}
)";

inline constexpr const char* kShadowFrag = R"(#version 330 core
void main() {}
)";

inline constexpr const char* kOverlayVert = R"(#version 330 core
layout(location = 0) in vec3 aPos;   // pixel x, pixel y (down), depth value
layout(location = 1) in vec2 aUv;
layout(location = 2) in vec4 aCol;
uniform vec2 uSize;
out vec2 vUv;
out vec4 vCol;
out float vD;
void main() {
  vUv = aUv; vCol = aCol; vD = aPos.z;
  gl_Position = vec4(aPos.x / uSize.x * 2.0 - 1.0, 1.0 - aPos.y / uSize.y * 2.0, 0.0, 1.0);
}
)";

// overlay.frag: sprites, lines and rectangles drawn after the scene. They are hidden by anything clearly nearer than them in the scene (the depth copy), like in the software renderer.
inline constexpr const char* kOverlayFrag = R"(#version 330 core
uniform sampler2D uTex;
uniform sampler2D uDepth;
uniform int uUseTex;
uniform int uUseDepth;
in vec2 vUv;
in vec4 vCol;
in float vD;
layout(location = 0) out vec4 oColor;
void main() {
  if (uUseDepth != 0) {
    float s = texelFetch(uDepth, ivec2(gl_FragCoord.xy), 0).r;
    if (vD < s * 0.98) discard;
  }
  vec4 c = vCol;
  if (uUseTex != 0) {
    c = texture(uTex, vUv);
    if (c.a < 0.5) discard;
  }
  oColor = vec4(c.rgb, 1.0);
}
)";

// post_*.frag: optional post-processing of the 3D picture (same interface for built-in and user shaders): uScene = the rendered frame, uSize = internal resolution in pixels,
// uRect = x, y, w, h of the 3D window (screen pixels, y down).
inline constexpr const char* kPostHeader = R"(#version 330 core
uniform sampler2D uScene;
uniform vec2 uSize;
uniform vec4 uRect;
layout(location = 0) out vec4 oColor;
vec3 fetch(vec2 px) { return texelFetch(uScene, ivec2(clamp(px, vec2(0.0), uSize - 1.0)), 0).rgb; }
)";

inline constexpr const char* kPostCrt = R"(
void main() {
  vec2 p = gl_FragCoord.xy;
  vec3 c = fetch(p);
  float row = mod(floor(uSize.y - p.y), 3.0);
  float scan = row < 1.0 ? 0.78 : 1.0;           // dark scanline every third pixel row
  vec2 uv = (p - uRect.xy) / uRect.zw;
  float vig = 1.0 - 0.35 * dot(uv - 0.5, uv - 0.5) * 2.0;
  vec3 glow = 0.15 * (fetch(p + vec2(1.0, 0.0)) + fetch(p - vec2(1.0, 0.0)));
  oColor = vec4((c + glow) * scan * vig * 1.08, 1.0);
}
)";

inline constexpr const char* kPostSmooth = R"(
void main() {
  vec2 p = gl_FragCoord.xy;
  vec3 c = fetch(p) * 4.0 + fetch(p + vec2(1, 0)) + fetch(p - vec2(1, 0)) + fetch(p + vec2(0, 1)) + fetch(p - vec2(0, 1));
  float edge = length(fetch(p + vec2(1, 0)) - fetch(p - vec2(1, 0))) + length(fetch(p + vec2(0, 1)) - fetch(p - vec2(0, 1)));
  vec3 base = fetch(p);
  oColor = vec4(mix(base, c / 8.0, clamp(edge * 2.0, 0.0, 1.0)), 1.0);
}
)";

// ---- built-in post passes of the lighting pipeline (not user replaceable) ----
// ssao.frag: depth-based ambient occlusion: a pixel is darkened by neighbours that are clearly nearer to the camera within a world-space radius (contact shadows in corners and under overhangs).
inline constexpr const char* kSsaoFrag = R"(#version 330 core
uniform sampler2D uScene;
uniform sampler2D uDepth;   // reverse depth near / z (0 = nothing drawn)
uniform vec2 uSize;
uniform float uNear;
uniform float uFocal;
uniform float uRadius;      // world units
uniform float uStrength;
layout(location = 0) out vec4 oColor;
float hash(vec2 p) { return fract(sin(dot(p, vec2(12.9898, 78.233))) * 43758.5453); }
void main() {
  ivec2 p = ivec2(gl_FragCoord.xy);
  vec4 c = texelFetch(uScene, p, 0);
  float d0 = texelFetch(uDepth, p, 0).r;
  if (d0 <= 0.0) { oColor = c; return; }
  float z0 = uNear / d0;
  float rpx = clamp(uRadius * uFocal / z0, 2.0, 48.0);
  float ang = hash(gl_FragCoord.xy) * 6.2831853;
  float occ = 0.0;
  for (int i = 0; i < 16; ++i) {
    float t = (float(i) + 0.5) / 16.0;
    float a = ang + float(i) * 2.399963;
    vec2 o = vec2(cos(a), sin(a)) * rpx * sqrt(t);
    float ds = texelFetch(uDepth, clamp(p + ivec2(o), ivec2(0), ivec2(uSize) - 1), 0).r;
    if (ds <= 0.0) continue;
    float dz = z0 - uNear / ds;                       // > 0: the neighbour is nearer to the camera than this pixel
    float lim = uRadius * 1.2;
    if (dz > uRadius * 0.04 && dz < lim) occ += 1.0 - dz / lim;
  }
  float ao = 1.0 - uStrength * clamp(occ / 16.0 * 2.0, 0.0, 1.0);
  oColor = vec4(c.rgb * ao, c.a);
}
)";

inline constexpr const char* kBloomExtractFrag = R"(#version 330 core
uniform sampler2D uScene;
uniform vec2 uSize;        // size of the source
uniform float uThreshold;
layout(location = 0) out vec4 oColor;
void main() {
  vec2 p = gl_FragCoord.xy * 2.0 - 1.0;                // top-left texel of the 2x2 block
  vec3 s = vec3(0.0);
  for (int y = 0; y < 2; ++y)
    for (int x = 0; x < 2; ++x) {
      vec3 c = texelFetch(uScene, ivec2(clamp(p + vec2(x, y) + 0.5, vec2(0.0), uSize - 1.0)), 0).rgb;
      float m = max(max(c.r, c.g), c.b);
      float k = clamp((m - uThreshold) / 0.4, 0.0, 1.0);
      s += c * k * k * (3.0 - 2.0 * k);
    }
  oColor = vec4(s * 0.25, 1.0);
}
)";

inline constexpr const char* kBlurFrag = R"(#version 330 core
uniform sampler2D uSrc;
uniform vec2 uDir;         // (1,0) or (0,1)
uniform vec4 uRect;        // x0, y0, x1, y1 (inclusive) of the valid area
layout(location = 0) out vec4 oColor;
void main() {
  vec3 s = vec3(0.0);
  const float w[5] = float[](0.2270270, 0.1945946, 0.1216216, 0.0540541, 0.0162162);
  for (int i = -4; i <= 4; ++i) {
    vec2 q = clamp(gl_FragCoord.xy - 0.5 + uDir * float(i) * 1.6, uRect.xy, uRect.zw);
    s += texelFetch(uSrc, ivec2(q), 0).rgb * w[abs(i)];
  }
  oColor = vec4(s, 1.0);
}
)";

// composite.frag: scene + bloom, then a soft shoulder instead of a hard clip (the lit picture is HDR).
inline constexpr const char* kCompositeFrag = R"(#version 330 core
uniform sampler2D uScene;
uniform sampler2D uBloom;
uniform vec2 uSize;
uniform vec4 uRect;        // 3D window in full-resolution pixels
uniform float uBloomAmount;
layout(location = 0) out vec4 oColor;
vec3 shoulder(vec3 x) {
  vec3 hi = 0.8 + 0.2 * (1.0 - exp(-(max(x, 0.8) - 0.8) / 0.2));
  return mix(x, hi, step(0.8, x));
}
void main() {
  ivec2 p = ivec2(gl_FragCoord.xy);
  vec3 c = texelFetch(uScene, p, 0).rgb;
  if (uBloomAmount > 0.0) {
    vec2 uv = (gl_FragCoord.xy - 0.5) * 0.5;
    ivec2 i = ivec2(floor(uv));
    vec2 f = uv - vec2(i);
    ivec2 lim = ivec2(uSize * 0.5) - 1;
    vec3 b = mix(mix(texelFetch(uBloom, clamp(i, ivec2(0), lim), 0).rgb, texelFetch(uBloom, clamp(i + ivec2(1, 0), ivec2(0), lim), 0).rgb, f.x),
                 mix(texelFetch(uBloom, clamp(i + ivec2(0, 1), ivec2(0), lim), 0).rgb, texelFetch(uBloom, clamp(i + ivec2(1, 1), ivec2(0), lim), 0).rgb, f.x), f.y);
    c += b * uBloomAmount;
  }
  oColor = vec4(shoulder(c), 1.0);
}
)";

inline constexpr const char* kPostFxaa = R"(
float luma(vec3 c) { return dot(c, vec3(0.299, 0.587, 0.114)); }
void main() {
  vec2 p = gl_FragCoord.xy;
  vec3 m = fetch(p), n = fetch(p + vec2(0, 1)), s = fetch(p - vec2(0, 1)), e = fetch(p + vec2(1, 0)), w = fetch(p - vec2(1, 0));
  float lm = luma(m), ln = luma(n), ls = luma(s), le = luma(e), lw = luma(w);
  float lo = min(lm, min(min(ln, ls), min(le, lw))), hi = max(lm, max(max(ln, ls), max(le, lw)));
  if (hi - lo < max(0.0625, hi * 0.125)) { oColor = vec4(m, 1.0); return; }   // no edge
  vec3 nw = fetch(p + vec2(-1, 1)), ne = fetch(p + vec2(1, 1)), sw = fetch(p + vec2(-1, -1)), se = fetch(p + vec2(1, -1));
  float edgeH = abs(luma(nw) + 2.0 * ln + luma(ne) - luma(sw) - 2.0 * ls - luma(se));
  float edgeV = abs(luma(nw) + 2.0 * lw + luma(sw) - luma(ne) - 2.0 * le - luma(se));
  vec2 dir = edgeH >= edgeV ? vec2(0, 1) : vec2(1, 0);   // blur across the edge
  vec3 a = fetch(p + dir), b = fetch(p - dir);
  oColor = vec4(mix(m, (m + a + b) / 3.0, 0.85), 1.0);
}
)";

inline constexpr const char* kPostSharpen = R"(
void main() {
  vec2 p = gl_FragCoord.xy;
  vec3 c = fetch(p) * 5.0 - fetch(p + vec2(1, 0)) - fetch(p - vec2(1, 0)) - fetch(p + vec2(0, 1)) - fetch(p - vec2(0, 1));
  oColor = vec4(clamp(mix(fetch(p), c, 0.5), 0.0, 1.0), 1.0);
}
)";

}  // namespace slip::glsl
