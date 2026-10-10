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
uniform int uFx;            // effects on (water shader)
uniform int uRipN;
uniform vec4 uRippleC[8];   // camera-space centre, age
uniform float uRippleS[8];  // strength
uniform float uTime;
uniform vec3 uSkyHorizon;
uniform vec3 uSkyZenith;
uniform vec3 uSkyAmbient;
uniform vec3 uGroundAmbient;
uniform float uExposure;
uniform float uIndoor;
uniform vec3 uSunDir;       // camera space, towards the sun
uniform vec3 uSunColor;
uniform vec3 uUpCam;        // world up in camera space
uniform int uLightN;
uniform vec3 uLightPos[64]; // camera space
uniform vec3 uLightCol[64];
uniform float uLightRad[64];
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
  vec2 texel = k == 0 ? 1.0 / vec2(textureSize(uShadow0, 0)) : 1.0 / vec2(textureSize(uShadow1, 0));   // (a sampler cannot be the operand of ?:, strict compilers reject it)
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

// ---- water: animated wave normals, fresnel reflection of the sky, sun glitter ----
vec2 waveGrad(vec2 p, float t) {
  vec2 g = vec2(0.0);
  g += vec2(0.9, 0.4) * cos(dot(p, vec2(0.9, 0.4)) * 2.1 + t * 0.9) * 0.55;
  g += vec2(-0.5, 0.85) * cos(dot(p, vec2(-0.5, 0.85)) * 3.3 - t * 1.3) * 0.40;
  g += vec2(0.2, -0.95) * cos(dot(p, vec2(0.2, -0.95)) * 5.7 + t * 1.9) * 0.28;
  g += vec2(-0.85, -0.3) * cos(dot(p, vec2(-0.85, -0.3)) * 9.1 - t * 2.6) * 0.16;
  return g;
}
vec3 skyColor(vec3 R) {
  float t = clamp(dot(R, uUpCam), 0.0, 1.0);
  return mix(uSkyHorizon, uSkyZenith, sqrt(t));
}
// ripple rings: o = offset from the ring's centre, age in seconds; returns the slope vector (in the plane of the offset) and the foam amount in .w
vec4 ripple(vec3 o, float age, float str) {
  float d = length(o);
  float k = 6.2831853 / 9000.0;
  float f = d - 55000.0 * age;
  float env = exp(-f * f / (2.0 * 16000.0 * 16000.0)) * exp(-age * 0.9) * str * (1.0 - smoothstep(1.8, 2.6, age)) * smoothstep(0.0, 4000.0, d);
  vec3 dir = o / max(d, 1.0);
  float c = cos(k * f);
  return vec4(dir * (env * c), env * smoothstep(0.5, 1.0, c) * 0.5);
}

vec3 waterShade(vec3 base, vec3 N, vec3 P, vec2 g) {
  vec3 V = -normalize(P);
  vec3 dp1 = dFdx(P), dp2 = dFdy(P);
  vec2 du1 = dFdx(vUv), du2 = dFdy(vUv);
  vec3 T = dp1 * du2.y - dp2 * du1.y;
  T = normalize(T - N * dot(N, T));
  vec3 B = cross(N, T);
  float dist = length(P);
  float amp = 0.10 / (1.0 + dist * 2.0e-6);              // waves flatten with distance (no shimmering)
  vec3 rg = vec3(0.0);
  float foam = 0.0;
  for (int i = 0; i < uRipN; ++i) { vec4 r = ripple(P - uRippleC[i].xyz, uRippleC[i].w, uRippleS[i]); rg += r.xyz; foam += r.w; }
  vec3 Nw = normalize(N - (T * g.x + B * g.y) * amp - rg * 0.55);
  float ndv = clamp(dot(Nw, V), 0.0, 1.0);
  float fres = 0.03 + 0.97 * pow(1.0 - ndv, 5.0);
  vec3 R = reflect(-V, Nw);
  vec3 refl = skyColor(R);
  vec3 body = base * (0.92 + 0.16 * g.x * g.y + 0.06 * g.x);
  vec3 c = mix(body, refl, clamp(fres * 0.9, 0.0, 0.85));
  float glint = pow(max(dot(R, uSunDir), 0.0), 220.0) * 3.0 + pow(max(dot(R, uSunDir), 0.0), 24.0) * 0.15;
  c += uSunColor * glint * (uSunColor.g > 0.05 ? 1.0 : 0.0);
  c = mix(c, vec3(0.92, 0.97, 1.0), clamp(foam, 0.0, 0.6));
  return c;
}

vec3 lighting(vec3 albedo) {
  vec3 N = vNormal;
  if (dot(N, N) < 0.25) return albedo;                  // no normal (lines, shadows): unlit
  N = normalize(N);
  vec3 P = vPos;
  if (dot(N, P) > 0.0) N = -N;                          // light the side that faces the viewer
  int fl = int(vFx.x + 0.5);
  bool indoor = (fl & 1) != 0;
  bool emissive = (fl & 2) != 0;
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
  bool water = uFx != 0 && ((int(vFx.x + 0.5) & 4) != 0) && dot(vNormal, vNormal) > 0.25;
  vec2 wg = vec2(0.0);
  vec2 uvw = vUv;
  if (water) { wg = waveGrad(vUv * 6.2831853 * 0.35, uTime); uvw = vUv + wg * 0.004 + vec2(uTime * 0.004, uTime * 0.0025); }
  if (vRect.z > 0.0) {
    vec4 r = round(vRect * vec4(uAtlasSize, uAtlasSize));
    if (uFilter == 0) c = nearest(uvw, r);
    else if (uFilter == 1) c = bilinear(uvw, r);
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
  if (water) {
    vec3 Nn = normalize(vNormal);
    if (dot(Nn, vPos) > 0.0) Nn = -Nn;
    c.rgb = waterShade(c.rgb, Nn, vPos, wg);
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
uniform int uWaterGround;   // the ground is a sea (Hawaii, New York): animated water instead of the flat colour
uniform float uTime;
uniform vec3 uCamXZ;        // camera world position (x, y, z) in scene units
uniform vec3 uSunDirW;
uniform vec3 uSunCol;
uniform int uRipN;
uniform vec4 uRippleW[8];   // centre relative to the camera (world axes), age
uniform float uRippleS[8];
// ripple rings: o = offset from the ring's centre, age in seconds; returns the slope vector (in the plane of the offset) and the foam amount in .w
vec4 ripple(vec3 o, float age, float str) {
  float d = length(o);
  float k = 6.2831853 / 9000.0;
  float f = d - 55000.0 * age;
  float env = exp(-f * f / (2.0 * 16000.0 * 16000.0)) * exp(-age * 0.9) * str * (1.0 - smoothstep(1.8, 2.6, age)) * smoothstep(0.0, 4000.0, d);
  vec3 dir = o / max(d, 1.0);
  float c = cos(k * f);
  return vec4(dir * (env * c), env * smoothstep(0.5, 1.0, c) * 0.5);
}

layout(location = 0) out vec4 oColor;
void main() {
  float dx = gl_FragCoord.x - uCenter.x;
  float dy = uCenter.y - (uFrame.y - gl_FragCoord.y);
  float ry = uFwd.y * uFocal + uUp.y * dy + uRight.y * dx;
  vec3 c;
  if (ry <= 0.0) {
    c = uGround;
    if (uWaterGround != 0) {
      vec3 d = normalize(uRight * dx + uUp * dy + uFwd * uFocal);
      float t = 70000.0 / max(-d.y, 0.002);
      vec2 w = (uCamXZ.xz + d.xz * t) * 0.00006;
      vec2 g = vec2(0.0);
      g += vec2(0.9, 0.4) * cos(dot(w, vec2(0.9, 0.4)) * 6.0 + uTime * 0.9) * 0.55;
      g += vec2(-0.5, 0.85) * cos(dot(w, vec2(-0.5, 0.85)) * 9.5 - uTime * 1.3) * 0.40;
      g += vec2(0.2, -0.95) * cos(dot(w, vec2(0.2, -0.95)) * 16.0 + uTime * 1.9) * 0.28;
      float flat_ = clamp(1.0 - t * 1.5e-6, 0.15, 1.0);
      vec3 rg = vec3(0.0);
      float foam = 0.0;
      for (int i = 0; i < uRipN; ++i) { vec4 r = ripple(vec3(d.x * t - uRippleW[i].x, 0.0, d.z * t - uRippleW[i].z), uRippleW[i].w, uRippleS[i]); rg += r.xyz; foam += r.w; }
      vec3 n = normalize(vec3(-g.x * 0.12 * flat_ - rg.x * 0.5, 1.0, -g.y * 0.12 * flat_ - rg.z * 0.5));
      vec3 V = -d;
      float fres = 0.03 + 0.97 * pow(1.0 - clamp(dot(n, V), 0.0, 1.0), 5.0);
      vec3 R = reflect(d, n);
      vec3 sky = mix(uRampN > 0 ? uRamp[uRampN - 1] : uSky, uRampN > 0 ? uRamp[0] : uSky, sqrt(clamp(R.y, 0.0, 1.0)));
      c = mix(uGround * (0.85 + 0.2 * g.x * g.y), sky, clamp(fres * 0.9, 0.0, 0.8));
      c += uSunCol * pow(max(dot(R, uSunDirW), 0.0), 180.0) * 2.5 * (uSunCol.g > 0.05 ? 1.0 : 0.0);
      c = mix(c, vec3(0.92, 0.97, 1.0), clamp(foam, 0.0, 0.6));
    }
  }
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
// ssao.frag: depth-based ambient occlusion. The surface normal is rebuilt from the depth, and a neighbour only occludes when it lies clearly in front of the tangent plane of the pixel, so
// flat floors and walls (also at grazing angles) do not shade themselves; corners, undersides and contact areas do.
inline constexpr const char* kSsaoFrag = R"(#version 330 core
uniform sampler2D uScene;
uniform sampler2D uDepth;   // reverse depth near / z (0 = nothing drawn)
uniform vec2 uSize;
uniform vec2 uCenter;       // projection centre, pixels, y down
uniform float uNear;
uniform float uFocal;
uniform float uRadius;      // world units
uniform float uStrength;
layout(location = 0) out vec4 oColor;
float hash(vec2 p) { return fract(sin(dot(p, vec2(12.9898, 78.233))) * 43758.5453); }
float depthAt(ivec2 p) { return texelFetch(uDepth, clamp(p, ivec2(0), ivec2(uSize) - 1), 0).r; }
vec3 posAt(ivec2 p, float d) {
  float z = uNear / d;
  return vec3((float(p.x) + 0.5 - uCenter.x) * z / uFocal, (float(p.y) + 0.5 - (uSize.y - uCenter.y)) * z / uFocal, z);
}
void main() {
  ivec2 p = ivec2(gl_FragCoord.xy);
  vec4 c = texelFetch(uScene, p, 0);
  float d0 = depthAt(p);
  if (d0 <= 1e-9) { oColor = c; return; }
  vec3 P = posAt(p, d0);
  // normal from the smaller depth step on each axis (keeps edges clean)
  float dr = depthAt(p + ivec2(1, 0)), dl = depthAt(p - ivec2(1, 0)), du = depthAt(p + ivec2(0, 1)), dd = depthAt(p - ivec2(0, 1));
  vec3 ex = (abs(1.0 / max(dr, 1e-9) - 1.0 / d0) < abs(1.0 / max(dl, 1e-9) - 1.0 / d0) && dr > 1e-9) ? posAt(p + ivec2(1, 0), dr) - P : (dl > 1e-9 ? P - posAt(p - ivec2(1, 0), dl) : vec3(1, 0, 0));
  vec3 ey = (abs(1.0 / max(du, 1e-9) - 1.0 / d0) < abs(1.0 / max(dd, 1e-9) - 1.0 / d0) && du > 1e-9) ? posAt(p + ivec2(0, 1), du) - P : (dd > 1e-9 ? P - posAt(p - ivec2(0, 1), dd) : vec3(0, 1, 0));
  vec3 N = normalize(cross(ex, ey));
  if (dot(N, P) > 0.0) N = -N;
  float rpx = clamp(uRadius * uFocal / P.z, 2.0, 48.0);
  float ang = hash(gl_FragCoord.xy) * 6.2831853;
  float occ = 0.0;
  for (int i = 0; i < 16; ++i) {
    float t = (float(i) + 0.5) / 16.0;
    float a = ang + float(i) * 2.399963;
    ivec2 q = p + ivec2(vec2(cos(a), sin(a)) * rpx * sqrt(t));
    float ds = depthAt(q);
    if (ds <= 1e-9) continue;
    vec3 v = posAt(q, ds) - P;
    float dist = length(v);
    if (dist < 1.0 || dist > uRadius * 1.3) continue;
    float h = dot(N, v);                               // height of the neighbour above the tangent plane (thin decals and lane lines lie within the bias)
    occ += max(h / uRadius - 0.15, 0.0) * (1.0 - dist / (uRadius * 1.3));
  }
  float ao = 1.0 - uStrength * clamp(occ / 16.0 * 4.0, 0.0, 1.0);
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

// fx.frag: particles. Mode 0 smoke (soft, lit, dissolving), 1 fire / explosion (additive, HDR), 2 spark streak (additive), 3 glow (additive). Soft particles: they fade out where the scene is
// close behind their plane and are hidden by anything nearer (the depth copy), instead of the hard cut of the plain sprites.
inline constexpr const char* kFxFrag = R"(#version 330 core
uniform sampler2D uTex;
uniform sampler2D uDepth;
uniform int uMode;
uniform float uLife;      // 0 young .. 1 gone
uniform float uSeed;
uniform float uNear;
uniform float uSoft;      // soft range in world units
uniform vec3 uSunDir;
uniform vec3 uSunColor;
uniform vec3 uAmb;
in vec2 vUv;
in vec4 vCol;
in float vD;
layout(location = 0) out vec4 oColor;
float hash(vec2 p) { return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453); }
float noise(vec2 p) {
  vec2 i = floor(p), f = fract(p);
  f = f * f * (3.0 - 2.0 * f);
  return mix(mix(hash(i), hash(i + vec2(1, 0)), f.x), mix(hash(i + vec2(0, 1)), hash(i + vec2(1, 1)), f.x), f.y);
}
void main() {
  float soft = 1.0;
  float s = texelFetch(uDepth, ivec2(gl_FragCoord.xy), 0).r;
  if (s > 1e-9) soft = clamp((min(uNear / s, 1.0e9) - uNear / max(vD, 1e-9)) / uSoft * 0.5 + 0.5, 0.0, 1.0);
  if (soft <= 0.0) discard;
  if (uMode <= 1 || uMode == 4) {
    vec4 t = texture(uTex, vUv);
    if (t.a < 0.5) discard;
    float lum = dot(t.rgb, vec3(0.3, 0.59, 0.11));
    vec2 p = vUv * 2.0 - 1.0;
    float r = length(p);
    float n = noise(vUv * 5.0 + uSeed * 17.0) * 0.6 + noise(vUv * 11.0 + uSeed * 5.0) * 0.4;
    if (uMode == 0 || uMode == 4) {
      float body = clamp(lum * 1.35 + 0.1, 0.0, 1.0) * (1.0 - smoothstep(0.6, 1.0, r));
      float erode = smoothstep(uLife - 0.3, uLife + 0.05, n);
      float alpha = body * erode * 0.85 * (1.0 - 0.45 * uLife) * soft;
      vec3 nn = vec3(p, sqrt(max(1.0 - r * r, 0.0)));
      float diff = max(dot(nn, uSunDir), 0.0);
      vec3 col = t.rgb * (max(uAmb, vec3(0.8)) + uSunColor * diff * 0.5) * (0.8 + 0.3 * n);
      if (uMode == 4) col = mix(col, vec3(0.9, 0.96, 1.0) * (max(uAmb, vec3(0.8)) + uSunColor * 0.3), 0.75);
      oColor = uMode == 4 ? vec4(col * 0.95, min(alpha * 0.55, 0.42)) : vec4(col, min(alpha * 1.6, 0.95));
    } else {
      float heat = clamp(lum * 1.2, 0.0, 1.0);
      float fade = (1.0 - uLife) * (1.0 - uLife);
      float edge = 1.0 - smoothstep(0.7, 1.0, r);
      vec3 hot = mix(vec3(1.0, 0.32, 0.05), vec3(1.0, 0.92, 0.55), heat);
      vec3 col = (t.rgb * 0.85 + hot * heat * 0.45) * (0.8 + 0.4 * n) * (0.25 + 1.15 * fade);
      oColor = vec4(col * edge * soft, 1.0);
    }
  } else if (uMode == 5) {   // water droplet: a lens of water, bright rim, white glint, translucent
    vec2 p = vUv * 2.0 - 1.0;
    float body = 1.0 - smoothstep(0.55, 1.0, length(p * vec2(0.8, 1.15)));
    float along = 1.0 - vUv.x;
    float glint = pow(max(1.0 - length(p - vec2(-0.2, -0.3)) * 2.2, 0.0), 2.0);
    vec3 col = mix(vCol.rgb * 1.1, vec3(0.9, 0.96, 1.0), 0.45) * (0.85 + 0.5 * glint) + vec3(glint) * 0.9;
    oColor = vec4(col, body * (0.35 + 0.5 * along) * (1.0 - uLife * 0.7) * soft);
  } else if (uMode == 2) {
    float across = 1.0 - abs(vUv.y * 2.0 - 1.0);
    float along = (1.0 - vUv.x);
    float k = across * across * pow(along, 1.4) * (1.0 - uLife);
    oColor = vec4(vCol.rgb * 2.2 * k * soft, 1.0);
  } else {
    float r = length(vUv * 2.0 - 1.0);
    float k = pow(max(1.0 - r, 0.0), 2.0) * (1.0 - uLife);
    oColor = vec4(vCol.rgb * 1.6 * k * soft, 1.0);
  }
  // a NaN / infinity in an HDR target would spread through the blur of the bloom as a white square
  if (any(isnan(oColor)) || any(isinf(oColor))) discard;
  oColor.rgb = min(oColor.rgb, vec3(6.0));
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
