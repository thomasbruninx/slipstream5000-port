#include "renderer/gl_renderer.hpp"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>

#include "original_formats/user_dir.hpp"
#include "renderer/gl_shaders.hpp"

namespace slip {

namespace {
constexpr double kTau = 6.283185307179586;
inline void unpack(uint32_t c, uint8_t* r, uint8_t* g, uint8_t* b) { *r = uint8_t(c >> 16); *g = uint8_t(c >> 8); *b = uint8_t(c); }
}  // namespace

GlRenderer::GlRenderer() = default;

GlRenderer::~GlRenderer() {
  if (ctx_ && window_) {
    SDL_GL_MakeCurrent(window_, ctx_);
    if (ok_) {
      destroyTargets();
      for (auto& kv : sprites_) gl_.DeleteTextures(1, &kv.second);
      for (auto& kv : atlases_) gl_.DeleteTextures(1, &kv.second.tex);
      gl_.DeleteVertexArrays(1, &vao_); gl_.DeleteVertexArrays(1, &ovao_); gl_.DeleteVertexArrays(1, &emptyVao_);
      gl_.DeleteBuffers(1, &vbo_); gl_.DeleteBuffers(1, &ovbo_);
      for (int k = 0; k < 2; ++k) { if (shadowFbo_[k]) gl_.DeleteFramebuffers(1, &shadowFbo_[k]); if (shadowTex_[k]) gl_.DeleteTextures(1, &shadowTex_[k]); }
      if (shadowVbo_) gl_.DeleteBuffers(1, &shadowVbo_);
      if (shadowDynVbo_) gl_.DeleteBuffers(1, &shadowDynVbo_);
      if (shadowVao_) gl_.DeleteVertexArrays(1, &shadowVao_);
      if (shadowDynVao_) gl_.DeleteVertexArrays(1, &shadowDynVao_);
      for (Program* p : {&scene_, &sky_, &overlay_, &post_, &restore_, &shadowProg_, &fxProg_, &ssao_, &bloomX_, &blur_, &comp_}) if (p->id) gl_.DeleteProgram(p->id);
    }
  }
  if (ctx_) SDL_GL_DestroyContext(ctx_);
  if (window_) SDL_DestroyWindow(window_);
}

std::string GlRenderer::shaderSource(const char* file, const char* fallback) const {
  std::vector<std::string> dirs;
  if (const char* e = std::getenv("SLIP_SHADERS")) dirs.push_back(e);
  dirs.push_back(userDataDir() + "/shaders");
  dirs.push_back("resources/shaders");
  for (const std::string& d : dirs) {
    std::ifstream f(d + "/" + file, std::ios::binary);
    if (f) { std::stringstream ss; ss << f.rdbuf(); std::fprintf(stderr, "opengl: using shader %s/%s\n", d.c_str(), file); return ss.str(); }
  }
  return fallback;
}

bool GlRenderer::buildProgram(Program* p, const std::string& vs, const std::string& fs, const char* label, std::string* error) {
  auto compile = [&](gl::GLenum type, const std::string& src, const char* what) -> gl::GLuint {
    const gl::GLuint sh = gl_.CreateShader(type);
    const char* c = src.c_str();
    gl_.ShaderSource(sh, 1, &c, nullptr);
    gl_.CompileShader(sh);
    gl::GLint ok = 0;
    gl_.GetShaderiv(sh, gl::COMPILE_STATUS, &ok);
    if (!ok) {
      gl::GLint len = 0;
      gl_.GetShaderiv(sh, gl::INFO_LOG_LENGTH, &len);
      std::string log(size_t(std::max(len, 1)), '\0');
      gl_.GetShaderInfoLog(sh, len, nullptr, log.data());
      if (error) *error = std::string(label) + " " + what + " shader: " + log;
      gl_.DeleteShader(sh);
      return 0;
    }
    return sh;
  };
  const gl::GLuint v = compile(gl::VERTEX_SHADER, vs, "vertex");
  if (!v) return false;
  const gl::GLuint f = compile(gl::FRAGMENT_SHADER, fs, "fragment");
  if (!f) { gl_.DeleteShader(v); return false; }
  const gl::GLuint prog = gl_.CreateProgram();
  gl_.AttachShader(prog, v);
  gl_.AttachShader(prog, f);
  gl_.LinkProgram(prog);
  gl_.DeleteShader(v);
  gl_.DeleteShader(f);
  gl::GLint ok = 0;
  gl_.GetProgramiv(prog, gl::LINK_STATUS, &ok);
  if (!ok) {
    gl::GLint len = 0;
    gl_.GetProgramiv(prog, gl::INFO_LOG_LENGTH, &len);
    std::string log(size_t(std::max(len, 1)), '\0');
    gl_.GetProgramInfoLog(prog, len, nullptr, log.data());
    if (error) *error = std::string(label) + " program: " + log;
    gl_.DeleteProgram(prog);
    return false;
  }
  if (p->id) gl_.DeleteProgram(p->id);
  p->id = prog;
  return true;
}

bool GlRenderer::init(const GlOptions& opt, std::string* error) {
  opt_ = opt;
  fxOn_ = opt.fx;
  hdr_ = opt.lighting == "lights" || opt.lighting == "on" || opt.lighting == "shadows";
  aoOn_ = hdr_ && opt.ao; bloomOn_ = hdr_ && opt.bloom;
  lightingMode = opt.lighting == "lights" || opt.lighting == "on" ? 1 : opt.lighting == "shadows" ? 2 : 0;
  filterMode_ = opt.filter == "bilinear" ? 1 : opt.filter == "smooth" ? 2 : 0;
  if (!opt.filter.empty() && opt.filter != "nearest" && opt.filter != "bilinear" && opt.filter != "smooth") std::fprintf(stderr, "opengl: unknown texture filter '%s' (nearest, bilinear, smooth), using nearest\n", opt.filter.c_str());
  auto fail = [&](const std::string& m) { if (error) *error = m; return false; };
  if (!SDL_InitSubSystem(SDL_INIT_VIDEO)) return fail(std::string("SDL video: ") + SDL_GetError());
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, SDL_GL_CONTEXT_FORWARD_COMPATIBLE_FLAG);
  SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 0);
  window_ = SDL_CreateWindow("slipstream gl", 64, 64, SDL_WINDOW_OPENGL | SDL_WINDOW_HIDDEN);
  if (!window_) return fail(std::string("OpenGL window: ") + SDL_GetError());
  ctx_ = SDL_GL_CreateContext(window_);
  if (!ctx_) return fail(std::string("OpenGL 3.3 context: ") + SDL_GetError());
  SDL_GL_MakeCurrent(window_, ctx_);
  SDL_GL_SetSwapInterval(0);
  const char* missing = nullptr;
  if (!gl::loadApi(&gl_, &missing)) return fail(std::string("OpenGL function missing: ") + (missing ? missing : "?"));
  info_ = std::string(reinterpret_cast<const char*>(gl_.GetString(gl::RENDERER))) + " / OpenGL " + reinterpret_cast<const char*>(gl_.GetString(gl::VERSION));
  std::string err;
  if (!buildProgram(&scene_, shaderSource("scene.vert", glsl::kSceneVert), shaderSource("scene.frag", glsl::kSceneFrag), "scene", &err)) return fail(err);
  if (!buildProgram(&sky_, glsl::kFullscreenVert, shaderSource("sky.frag", glsl::kSkyFrag), "sky", &err)) return fail(err);
  if (!buildProgram(&overlay_, glsl::kOverlayVert, shaderSource("overlay.frag", glsl::kOverlayFrag), "overlay", &err)) return fail(err);
  auto loc = [&](Program& p, std::initializer_list<const char*> names) { int i = 0; for (const char* n : names) p.loc[i++] = gl_.GetUniformLocation(p.id, n); };
  loc(scene_, {"uProj", "uNear", "uAtlas", "uFilter", "uAtlasSize", "uLighting", "uSkyAmbient", "uGroundAmbient", "uExposure", "uIndoor", "uSunDir", "uSunColor", "uUpCam", "uLightN", "uLightPos[0]",
               "uLightCol[0]", "uLightRad[0]", "uShadowOn", "uShadow0", "uShadow1", "uShadowX[0]", "uShadowY[0]", "uShadowZ[0]", "uShadowOff[0]", "uShadowParams[0]", "uFx", "uTime", "uSkyHorizon", "uSkyZenith", "uRipN", "uRippleC[0]", "uRippleS[0]"});
  if (hdr_) {
    if (!buildProgram(&ssao_, glsl::kFullscreenVert, glsl::kSsaoFrag, "ssao", &err) || !buildProgram(&bloomX_, glsl::kFullscreenVert, glsl::kBloomExtractFrag, "bloom", &err) ||
        !buildProgram(&blur_, glsl::kFullscreenVert, glsl::kBlurFrag, "blur", &err) || !buildProgram(&comp_, glsl::kFullscreenVert, glsl::kCompositeFrag, "composite", &err))
      return fail(err);
    loc(ssao_, {"uScene", "uDepth", "uSize", "uNear", "uFocal", "uRadius", "uStrength"});
    loc(bloomX_, {"uScene", "uSize", "uThreshold"});
    loc(blur_, {"uSrc", "uDir", "uRect"});
    loc(comp_, {"uScene", "uBloom", "uSize", "uRect", "uBloomAmount"});
  }
  if (lightingMode >= 2) {
    if (!buildProgram(&shadowProg_, glsl::kShadowVert, glsl::kShadowFrag, "shadow map", &err)) { std::fprintf(stderr, "opengl: %s (shadows off)\n", err.c_str()); lightingMode = 1; }
    else loc(shadowProg_, {"uOrigin", "uAxX", "uAxY", "uAxZ", "uParams"});
  }
  loc(sky_, {"uFrame", "uCenter", "uFocal", "uRight", "uUp", "uFwd", "uSky", "uGround", "uBand", "uRampN", "uRamp[0]", "uWaterGround", "uTime", "uCamXZ", "uSunDirW", "uSunCol", "uRipN", "uRippleW[0]", "uRippleS[0]"});
  loc(overlay_, {"uSize", "uTex", "uDepth", "uUseTex", "uUseDepth"});
  if (fxOn_) {
    if (!buildProgram(&fxProg_, glsl::kOverlayVert, glsl::kFxFrag, "fx", &err)) { std::fprintf(stderr, "opengl: %s (effects off)\n", err.c_str()); fxOn_ = false; }
    else loc(fxProg_, {"uSize", "uTex", "uDepth", "uMode", "uLife", "uSeed", "uNear", "uSoft", "uSunDir", "uSunColor", "uAmb"});
  }
  if (!buildProgram(&restore_, glsl::kFullscreenVert, glsl::kDepthRestoreFrag, "depth restore", &err)) return fail(err);
  loc(restore_, {"uDepth"});
  // post-processing shader (optional)
  std::string post = opt.postShader;
  std::transform(post.begin(), post.end(), post.begin(), [](unsigned char c) { return char(std::tolower(c)); });
  if (!post.empty() && post != "none") {
    std::string body;
    if (post == "crt") body = glsl::kPostCrt;
    else if (post == "smooth") body = glsl::kPostSmooth;
    else if (post == "sharpen") body = glsl::kPostSharpen;
    else if (post == "fxaa") body = glsl::kPostFxaa;
    const std::string file = "post_" + post + ".frag";
    std::string src = shaderSource(file.c_str(), "");
    if (src.empty()) {
      if (body.empty()) { std::fprintf(stderr, "opengl: no post shader '%s' (built in: crt, smooth, sharpen, fxaa; or %s in the shader folder)\n", post.c_str(), file.c_str()); }
      else src = std::string(glsl::kPostHeader) + body;
    }
    if (!src.empty() && src.find("#version") == std::string::npos) src = std::string(glsl::kPostHeader) + src;  // a user shader may leave out the common header (version, uniforms, fetch())
    if (!src.empty()) {
      if (!buildProgram(&post_, glsl::kFullscreenVert, src, "post", &err)) std::fprintf(stderr, "opengl: %s (post shader disabled)\n", err.c_str());
      else { loc(post_, {"uScene", "uSize", "uRect"}); havePost_ = true; }
    }
  }
  gl_.GenVertexArrays(1, &vao_);
  gl_.GenBuffers(1, &vbo_);
  gl_.BindVertexArray(vao_);
  gl_.BindBuffer(gl::ARRAY_BUFFER, vbo_);
  const gl::GLsizei stride = sizeof(SceneVertex);
  gl_.EnableVertexAttribArray(0); gl_.VertexAttribPointer(0, 3, gl::FLOAT, gl::FALSE_, stride, reinterpret_cast<void*>(offsetof(SceneVertex, x)));
  gl_.EnableVertexAttribArray(1); gl_.VertexAttribPointer(1, 2, gl::FLOAT, gl::FALSE_, stride, reinterpret_cast<void*>(offsetof(SceneVertex, u)));
  gl_.EnableVertexAttribArray(2); gl_.VertexAttribPointer(2, 4, gl::UNSIGNED_BYTE, gl::TRUE_, stride, reinterpret_cast<void*>(offsetof(SceneVertex, r)));
  gl_.EnableVertexAttribArray(3); gl_.VertexAttribPointer(3, 4, gl::FLOAT, gl::FALSE_, stride, reinterpret_cast<void*>(offsetof(SceneVertex, rx)));
  gl_.EnableVertexAttribArray(4); gl_.VertexAttribPointer(4, 1, gl::FLOAT, gl::FALSE_, stride, reinterpret_cast<void*>(offsetof(SceneVertex, ds)));
  gl_.EnableVertexAttribArray(5); gl_.VertexAttribPointer(5, 3, gl::FLOAT, gl::FALSE_, stride, reinterpret_cast<void*>(offsetof(SceneVertex, nx)));
  gl_.EnableVertexAttribArray(6); gl_.VertexAttribPointer(6, 2, gl::FLOAT, gl::FALSE_, stride, reinterpret_cast<void*>(offsetof(SceneVertex, flags)));
  gl_.GenVertexArrays(1, &ovao_);
  gl_.GenBuffers(1, &ovbo_);
  gl_.BindVertexArray(ovao_);
  gl_.BindBuffer(gl::ARRAY_BUFFER, ovbo_);
  const gl::GLsizei ostride = sizeof(OverlayVertex);
  gl_.EnableVertexAttribArray(0); gl_.VertexAttribPointer(0, 3, gl::FLOAT, gl::FALSE_, ostride, reinterpret_cast<void*>(offsetof(OverlayVertex, x)));
  gl_.EnableVertexAttribArray(1); gl_.VertexAttribPointer(1, 2, gl::FLOAT, gl::FALSE_, ostride, reinterpret_cast<void*>(offsetof(OverlayVertex, u)));
  gl_.EnableVertexAttribArray(2); gl_.VertexAttribPointer(2, 4, gl::UNSIGNED_BYTE, gl::TRUE_, ostride, reinterpret_cast<void*>(offsetof(OverlayVertex, r)));
  gl_.GenVertexArrays(1, &emptyVao_);
  gl_.BindVertexArray(0);
  ok_ = true;
  return true;
}

void GlRenderer::destroyTargets() {
  if (fbo_) gl_.DeleteFramebuffers(1, &fbo_);
  if (copyFbo_) gl_.DeleteFramebuffers(1, &copyFbo_);
  if (postFbo_) gl_.DeleteFramebuffers(1, &postFbo_);
  for (int k = 0; k < 2; ++k) { if (bloomFbo_[k]) gl_.DeleteFramebuffers(1, &bloomFbo_[k]); if (bloomTex_[k]) gl_.DeleteTextures(1, &bloomTex_[k]); bloomFbo_[k] = bloomTex_[k] = 0; }
  if (colorTex_) gl_.DeleteTextures(1, &colorTex_);
  if (depthTex_) gl_.DeleteTextures(1, &depthTex_);
  if (copyTex_) gl_.DeleteTextures(1, &copyTex_);
  if (postTex_) gl_.DeleteTextures(1, &postTex_);
  if (depthRb_) gl_.DeleteRenderbuffers(1, &depthRb_);
  if (msFbo_) gl_.DeleteFramebuffers(1, &msFbo_);
  if (msColorRb_) gl_.DeleteRenderbuffers(1, &msColorRb_);
  if (msDepthIdRb_) gl_.DeleteRenderbuffers(1, &msDepthIdRb_);
  if (msDepthRb_) gl_.DeleteRenderbuffers(1, &msDepthRb_);
  msFbo_ = msColorRb_ = msDepthIdRb_ = msDepthRb_ = 0;
  fbo_ = copyFbo_ = postFbo_ = colorTex_ = depthTex_ = copyTex_ = postTex_ = depthRb_ = 0;
}

void GlRenderer::createTargets() {
  destroyTargets();
  auto tex = [&](gl::GLuint* t, gl::GLenum internal, gl::GLenum format, gl::GLenum type) {
    gl_.GenTextures(1, t);
    gl_.BindTexture(gl::TEXTURE_2D, *t);
    gl_.TexImage2D(gl::TEXTURE_2D, 0, gl::GLint(internal), tw_, th_, 0, format, type, nullptr);
    gl_.TexParameteri(gl::TEXTURE_2D, gl::TEXTURE_MIN_FILTER, gl::GLint(gl::NEAREST));
    gl_.TexParameteri(gl::TEXTURE_2D, gl::TEXTURE_MAG_FILTER, gl::GLint(gl::NEAREST));
    gl_.TexParameteri(gl::TEXTURE_2D, gl::TEXTURE_WRAP_S, gl::GLint(gl::CLAMP_TO_EDGE));
    gl_.TexParameteri(gl::TEXTURE_2D, gl::TEXTURE_WRAP_T, gl::GLint(gl::CLAMP_TO_EDGE));
  };
  const gl::GLenum colorFmt = hdr_ ? gl::RGBA16F : gl::RGBA8;
  tex(&colorTex_, colorFmt, gl::RGBA, gl::UNSIGNED_BYTE);
  tex(&depthTex_, gl::R32F, gl::RED, gl::FLOAT);
  tex(&copyTex_, gl::R32F, gl::RED, gl::FLOAT);
  gl_.GenRenderbuffers(1, &depthRb_);
  gl_.BindRenderbuffer(gl::RENDERBUFFER, depthRb_);
  gl_.RenderbufferStorage(gl::RENDERBUFFER, gl::DEPTH_COMPONENT32F, tw_, th_);
  gl_.GenFramebuffers(1, &fbo_);
  gl_.BindFramebuffer(gl::FRAMEBUFFER, renderFbo());
  gl_.FramebufferTexture2D(gl::FRAMEBUFFER, gl::COLOR_ATTACHMENT0, gl::TEXTURE_2D, colorTex_, 0);
  gl_.FramebufferTexture2D(gl::FRAMEBUFFER, gl::COLOR_ATTACHMENT1, gl::TEXTURE_2D, depthTex_, 0);
  gl_.FramebufferRenderbuffer(gl::FRAMEBUFFER, gl::DEPTH_ATTACHMENT, gl::RENDERBUFFER, depthRb_);
  if (gl_.CheckFramebufferStatus(gl::FRAMEBUFFER) != gl::FRAMEBUFFER_COMPLETE) std::fprintf(stderr, "opengl: scene framebuffer incomplete\n");
  samples_ = 0;
  if (opt_.msaa >= 2) {
    gl::GLint maxSamples = 0;
    gl_.GetIntegerv(gl::MAX_SAMPLES, &maxSamples);
    const int want = std::min<int>(opt_.msaa, maxSamples);
    if (want >= 2) {
      auto rb = [&](gl::GLuint* r, gl::GLenum fmt) {
        gl_.GenRenderbuffers(1, r);
        gl_.BindRenderbuffer(gl::RENDERBUFFER, *r);
        gl_.RenderbufferStorageMultisample(gl::RENDERBUFFER, want, fmt, tw_, th_);
      };
      rb(&msColorRb_, colorFmt);
      rb(&msDepthIdRb_, gl::R32F);
      rb(&msDepthRb_, gl::DEPTH_COMPONENT32F);
      gl_.GenFramebuffers(1, &msFbo_);
      gl_.BindFramebuffer(gl::FRAMEBUFFER, msFbo_);
      gl_.FramebufferRenderbuffer(gl::FRAMEBUFFER, gl::COLOR_ATTACHMENT0, gl::RENDERBUFFER, msColorRb_);
      gl_.FramebufferRenderbuffer(gl::FRAMEBUFFER, gl::COLOR_ATTACHMENT1, gl::RENDERBUFFER, msDepthIdRb_);
      gl_.FramebufferRenderbuffer(gl::FRAMEBUFFER, gl::DEPTH_ATTACHMENT, gl::RENDERBUFFER, msDepthRb_);
      if (gl_.CheckFramebufferStatus(gl::FRAMEBUFFER) == gl::FRAMEBUFFER_COMPLETE) samples_ = want;
      else {
        std::fprintf(stderr, "opengl: %dx multisampling is not supported here, anti-aliasing off\n", want);
        gl_.DeleteFramebuffers(1, &msFbo_); gl_.DeleteRenderbuffers(1, &msColorRb_); gl_.DeleteRenderbuffers(1, &msDepthIdRb_); gl_.DeleteRenderbuffers(1, &msDepthRb_);
        msFbo_ = msColorRb_ = msDepthIdRb_ = msDepthRb_ = 0;
      }
    } else std::fprintf(stderr, "opengl: no multisampling available, anti-aliasing off\n");
  }
  gl_.GenFramebuffers(1, &copyFbo_);
  gl_.BindFramebuffer(gl::FRAMEBUFFER, copyFbo_);
  gl_.FramebufferTexture2D(gl::FRAMEBUFFER, gl::COLOR_ATTACHMENT0, gl::TEXTURE_2D, copyTex_, 0);
  if (havePost_ || hdr_) {
    tex(&postTex_, colorFmt, gl::RGBA, gl::UNSIGNED_BYTE);
    gl_.GenFramebuffers(1, &postFbo_);
    gl_.BindFramebuffer(gl::FRAMEBUFFER, postFbo_);
    gl_.FramebufferTexture2D(gl::FRAMEBUFFER, gl::COLOR_ATTACHMENT0, gl::TEXTURE_2D, postTex_, 0);
  }
  if (hdr_) {
    const int bw = std::max(1, tw_ / 2), bh = std::max(1, th_ / 2);
    for (int k = 0; k < 2; ++k) {
      gl_.GenTextures(1, &bloomTex_[k]);
      gl_.BindTexture(gl::TEXTURE_2D, bloomTex_[k]);
      gl_.TexImage2D(gl::TEXTURE_2D, 0, gl::GLint(gl::RGBA16F), bw, bh, 0, gl::RGBA, gl::UNSIGNED_BYTE, nullptr);
      gl_.TexParameteri(gl::TEXTURE_2D, gl::TEXTURE_MIN_FILTER, gl::GLint(gl::NEAREST));
      gl_.TexParameteri(gl::TEXTURE_2D, gl::TEXTURE_MAG_FILTER, gl::GLint(gl::NEAREST));
      gl_.TexParameteri(gl::TEXTURE_2D, gl::TEXTURE_WRAP_S, gl::GLint(gl::CLAMP_TO_EDGE));
      gl_.TexParameteri(gl::TEXTURE_2D, gl::TEXTURE_WRAP_T, gl::GLint(gl::CLAMP_TO_EDGE));
      gl_.GenFramebuffers(1, &bloomFbo_[k]);
      gl_.BindFramebuffer(gl::FRAMEBUFFER, bloomFbo_[k]);
      gl_.FramebufferTexture2D(gl::FRAMEBUFFER, gl::COLOR_ATTACHMENT0, gl::TEXTURE_2D, bloomTex_[k], 0);
    }
  }
  gl_.BindFramebuffer(gl::FRAMEBUFFER, 0);
}

void GlRenderer::resize(int w, int h) {
  w_ = w;
  h_ = h;
  color_.assign(size_t(w) * size_t(h), 0xff000000u);
  if (!ok_) return;
  SDL_GL_MakeCurrent(window_, ctx_);
  tw_ = w;
  th_ = h;
  createTargets();
}

void GlRenderer::setScene(const Scene* scene) {
  if (scene && ok_ && scene != atlasScene_) { SDL_GL_MakeCurrent(window_, ctx_); uploadAtlas(*scene); }
}

void GlRenderer::uploadAtlas(const Scene& scene) {
  flush();
  atlasScene_ = &scene;
  uint64_t key = 1469598103934665603ull;
  auto mix = [&](uint64_t v) { key = (key ^ v) * 1099511628211ull; };
  mix(scene.textures.size());
  for (const Texture& t : scene.textures) { mix(uint64_t(t.w) << 20 ^ uint64_t(t.h)); mix(uint64_t(int64_t(t.transparent) + 2)); mix(t.index.empty() ? 0 : t.index[t.index.size() / 2] ^ (uint64_t(t.index.back()) << 8)); }
  for (uint32_t c : scene.palette.rgba) mix(c);
  if (auto it = atlases_.find(key); it != atlases_.end()) { atlas_ = it->second.tex; rects_ = it->second.rects; atlasW_ = it->second.w; atlasH_ = it->second.h; return; }
  rects_.assign(scene.textures.size(), {0, 0, 0, 0});
  // shelf packing, tallest first
  std::vector<size_t> order(scene.textures.size());
  for (size_t i = 0; i < order.size(); ++i) order[i] = i;
  std::sort(order.begin(), order.end(), [&](size_t a, size_t b) { return scene.textures[a].h > scene.textures[b].h; });
  int aw = 2048, ah = 0;
  std::vector<std::array<int, 2>> pos(scene.textures.size());
  for (int attempt = 0; attempt < 2; ++attempt) {
    int x = 0, y = 0, rowH = 0;
    ah = 0;
    for (size_t i : order) {
      const Texture& t = scene.textures[i];
      if (t.w <= 0 || t.h <= 0) continue;
      if (x + t.w > aw) { x = 0; y += rowH; rowH = 0; }
      pos[i] = {x, y};
      x += t.w;
      rowH = std::max(rowH, t.h);
      ah = std::max(ah, y + t.h);
    }
    if (ah <= 4096 || aw >= 4096) break;
    aw = 4096;
  }
  ah = std::max(ah, 1);
  atlasW_ = float(aw); atlasH_ = float(ah);
  std::vector<uint32_t> px(size_t(aw) * size_t(ah), 0);
  for (size_t i = 0; i < scene.textures.size(); ++i) {
    const Texture& t = scene.textures[i];
    if (t.w <= 0 || t.h <= 0 || t.index.size() < size_t(t.w) * size_t(t.h)) continue;
    for (int y = 0; y < t.h; ++y)
      for (int x = 0; x < t.w; ++x) {
        const uint8_t idx = t.index[size_t(y) * size_t(t.w) + size_t(x)];
        const uint32_t rgb = scene.palette.rgba[idx] & 0xffffffu;
        const uint32_t a = idx == t.transparent ? 0u : 255u;
        // bytes r, g, b, a (little endian: 0xAABBGGRR)
        px[size_t(pos[i][1] + y) * size_t(aw) + size_t(pos[i][0] + x)] = ((rgb >> 16) & 255u) | (rgb & 0xff00u) | ((rgb & 255u) << 16) | (a << 24);
      }
    rects_[i] = {float(pos[i][0]) / float(aw), float(pos[i][1]) / float(ah), float(t.w) / float(aw), float(t.h) / float(ah)};
  }
  if (atlases_.size() >= 8) { for (auto& kv : atlases_) gl_.DeleteTextures(1, &kv.second.tex); atlases_.clear(); }
  gl_.GenTextures(1, &atlas_);
  gl_.BindTexture(gl::TEXTURE_2D, atlas_);
  gl_.PixelStorei(gl::UNPACK_ALIGNMENT, 4);
  gl_.TexImage2D(gl::TEXTURE_2D, 0, gl::GLint(gl::RGBA8), aw, ah, 0, gl::RGBA, gl::UNSIGNED_BYTE, px.data());
  gl_.TexParameteri(gl::TEXTURE_2D, gl::TEXTURE_MIN_FILTER, gl::GLint(gl::NEAREST));
  gl_.TexParameteri(gl::TEXTURE_2D, gl::TEXTURE_MAG_FILTER, gl::GLint(gl::NEAREST));
  gl_.TexParameteri(gl::TEXTURE_2D, gl::TEXTURE_WRAP_S, gl::GLint(gl::CLAMP_TO_EDGE));
  gl_.TexParameteri(gl::TEXTURE_2D, gl::TEXTURE_WRAP_T, gl::GLint(gl::CLAMP_TO_EDGE));
  atlases_[key] = {atlas_, rects_, atlasW_, atlasH_};
}

gl::GLuint GlRenderer::spriteTexture(const Sprite& spr, const Palette& pal, int transparent) {
  uint64_t sum = 1469598103934665603ull;  // cheap content fingerprint: the texture is rebuilt when a sprite is reused for other pixels
  const size_t n = std::min<size_t>(spr.pixels.size(), 96);
  for (size_t i = 0; i < n; ++i) sum = (sum ^ spr.pixels[i * (spr.pixels.size() / n)]) * 1099511628211ull;
  char key[160];
  std::snprintf(key, sizeof key, "%p:%p:%d:%d:%d:%llx", static_cast<const void*>(spr.pixels.data()), static_cast<const void*>(&pal), spr.w, spr.h, transparent, static_cast<unsigned long long>(sum));
  auto it = sprites_.find(key);
  if (it != sprites_.end()) return it->second;
  std::vector<uint32_t> px(size_t(spr.w) * size_t(spr.h));
  for (size_t i = 0; i < px.size(); ++i) {
    const uint8_t idx = spr.pixels[i];
    const uint32_t rgb = pal.rgba[idx] & 0xffffffu;
    const uint32_t a = int(idx) == transparent ? 0u : 255u;
    px[i] = ((rgb >> 16) & 255u) | (rgb & 0xff00u) | ((rgb & 255u) << 16) | (a << 24);
  }
  gl::GLuint t = 0;
  gl_.GenTextures(1, &t);
  gl_.BindTexture(gl::TEXTURE_2D, t);
  gl_.PixelStorei(gl::UNPACK_ALIGNMENT, 4);
  gl_.TexImage2D(gl::TEXTURE_2D, 0, gl::GLint(gl::RGBA8), spr.w, spr.h, 0, gl::RGBA, gl::UNSIGNED_BYTE, px.data());
  gl_.TexParameteri(gl::TEXTURE_2D, gl::TEXTURE_MIN_FILTER, gl::GLint(gl::NEAREST));
  gl_.TexParameteri(gl::TEXTURE_2D, gl::TEXTURE_MAG_FILTER, gl::GLint(gl::NEAREST));
  gl_.TexParameteri(gl::TEXTURE_2D, gl::TEXTURE_WRAP_S, gl::GLint(gl::CLAMP_TO_EDGE));
  gl_.TexParameteri(gl::TEXTURE_2D, gl::TEXTURE_WRAP_T, gl::GLint(gl::CLAMP_TO_EDGE));
  if (sprites_.size() > 2000) { for (auto& kv : sprites_) gl_.DeleteTextures(1, &kv.second); sprites_.clear(); }
  sprites_[key] = t;
  return t;
}

void GlRenderer::setScissor(int x0, int y0, int x1, int y1) {
  x0 = std::clamp(x0, 0, tw_ - 1); x1 = std::clamp(x1, 0, tw_ - 1);
  y0 = std::clamp(y0, 0, th_ - 1); y1 = std::clamp(y1, 0, th_ - 1);
  gl_.Enable(gl::SCISSOR_TEST);
  gl_.Scissor(x0, th_ - 1 - y1, std::max(0, x1 - x0 + 1), std::max(0, y1 - y0 + 1));
}

float GlRenderer::depthScale(uint8_t layer, int forceIdx) const {
  float s = 1.0f;
  if (curItem_ >= 0) {
    // painter's mode: a later polygon of the same item wins unless it is clearly behind (the software test allows 1 %): later polygons get a slightly nearer depth
    s += 0.0005f * float(std::min<uint32_t>(curPoly_ - itemFirstPoly_, 20));
  } else {
    s = layer == 1 ? 1.0f / 1.3f : layer == 2 ? 0.001f : 1.0f;  // z-buffer mode: scenery must be clearly nearer than road to cover it, backdrop never covers road
    // coplanar polygons (the software test lets the later one win within 0.002 %): later polygons are a hair nearer
    s *= 1.0f + std::min(1e-7f * float(curPoly_), 2e-4f);
  }
  if (forceIdx >= 0) s *= 1.02f;  // lamps, lane colours, shadows: painted over their own polygon
  return s;
}

void GlRenderer::openBatch() {
  const bool changed = batchOpen_ && (curItem_ != batchItem_ || batchScissor_[0] != sx0_ || batchScissor_[1] != sy0_ || batchScissor_[2] != sx1_ || batchScissor_[3] != sy1_);
  if (changed) flush();
  if (batchOpen_) return;
  static const bool noRestore = std::getenv("SLIP_GLNORESTORE") != nullptr;  // debug: the old behaviour (ships see only the last item's depth)
  if (curItem_ < 0 && !globalDepth_ && !noRestore) restoreGlobalDepth();
  if (curItem_ != batchItem_) itemFirstPoly_ = curPoly_;
  batchScissor_[0] = sx0_; batchScissor_[1] = sy0_; batchScissor_[2] = sx1_; batchScissor_[3] = sy1_;
  batchItem_ = curItem_;
  batchOpen_ = true;
  if (curItem_ >= 0 && curItem_ != clearedItem_) {  // a new painter item: nothing it draws is tested against earlier items
    gl_.BindFramebuffer(gl::FRAMEBUFFER, renderFbo());
    setScissor(sx0_, sy0_, sx1_, sy1_);
    gl_.DepthMask(gl::TRUE_);
    gl_.ClearDepth(0.0);
    gl_.Clear(gl::DEPTH_BUFFER_BIT);
    clearedItem_ = curItem_;
    globalDepth_ = false;
  }
}

void GlRenderer::flush() {
  if (!batchOpen_) return;
  batchOpen_ = false;
  if (tris_.empty()) return;
  const gl::GLenum bufs[2] = {gl::COLOR_ATTACHMENT0, gl::COLOR_ATTACHMENT1};
  gl_.BindFramebuffer(gl::FRAMEBUFFER, renderFbo());
  gl_.DrawBuffers(2, bufs);
  gl_.Viewport(0, 0, tw_, th_);
  setScissor(batchScissor_[0], batchScissor_[1], batchScissor_[2], batchScissor_[3]);
  gl_.Enable(gl::DEPTH_TEST);
  gl_.DepthFunc(gl::GEQUAL);
  gl_.DepthMask(gl::TRUE_);
  gl_.Disable(gl::BLEND);
  if ((lightingMode > 0 || fxOn_) && !lightsReady_ && atlasScene_) {
    prepareLighting(*atlasScene_);  // may render the shadow map (changes the framebuffer): everything is re-bound below
    gl_.BindFramebuffer(gl::FRAMEBUFFER, renderFbo());
    gl_.DrawBuffers(2, bufs);
    gl_.Viewport(0, 0, tw_, th_);
    setScissor(batchScissor_[0], batchScissor_[1], batchScissor_[2], batchScissor_[3]);
    gl_.Enable(gl::DEPTH_TEST);
    gl_.DepthFunc(gl::GEQUAL);
    gl_.DepthMask(gl::TRUE_);
    gl_.ColorMask(gl::TRUE_, gl::TRUE_, gl::TRUE_, gl::TRUE_);
  }
  gl_.UseProgram(scene_.id);
  gl_.Uniform4fv(scene_.loc[0], 1, uProj_);
  gl_.Uniform1f(scene_.loc[1], nearD_);
  gl_.Uniform1i(scene_.loc[5], lightingMode > 0 && atlasScene_ ? 1 : 0);
  gl_.Uniform1i(scene_.loc[25], fxOn_ ? 1 : 0);
  if (fxOn_ && atlasScene_) {
    gl_.Uniform1f(scene_.loc[26], float(animTimer) / 16384.0f);
    const float ho[3] = {skyRamp.empty() ? atlasScene_->env.skyAmbient[0] : float((skyRamp.back() >> 16) & 255) / 255.0f, skyRamp.empty() ? atlasScene_->env.skyAmbient[1] : float((skyRamp.back() >> 8) & 255) / 255.0f, skyRamp.empty() ? atlasScene_->env.skyAmbient[2] : float(skyRamp.back() & 255) / 255.0f};
    const float ze[3] = {skyRamp.empty() ? ho[0] : float((skyRamp.front() >> 16) & 255) / 255.0f, skyRamp.empty() ? ho[1] : float((skyRamp.front() >> 8) & 255) / 255.0f, skyRamp.empty() ? ho[2] : float(skyRamp.front() & 255) / 255.0f};
    gl_.Uniform3fv(scene_.loc[27], 1, ho);
    gl_.Uniform3fv(scene_.loc[28], 1, ze);
    gl_.Uniform3fv(scene_.loc[10], 1, sunCam_);
    gl_.Uniform3fv(scene_.loc[11], 1, atlasScene_->env.sunColor);
    gl_.Uniform3fv(scene_.loc[12], 1, upCam_);
    float rc[32], rs[8];
    int rn = 0;
    for (const Ripple& r : ripples) {
      if (rn >= 8) break;
      const double d[3] = {r.pos[0] - cam_.pos[0], r.pos[1] - cam_.pos[1], r.pos[2] - cam_.pos[2]};
      rc[rn * 4] = float(d[0] * right_[0] + d[1] * right_[1] + d[2] * right_[2]);
      rc[rn * 4 + 1] = float(d[0] * up_[0] + d[1] * up_[1] + d[2] * up_[2]);
      rc[rn * 4 + 2] = float(d[0] * fwd_[0] + d[1] * fwd_[1] + d[2] * fwd_[2]);
      rc[rn * 4 + 3] = r.age;
      rs[rn] = r.strength;
      ++rn;
    }
    gl_.Uniform1i(scene_.loc[29], rn);
    if (rn > 0) { gl_.Uniform4fv(scene_.loc[30], rn, rc); gl_.Uniform1fv(scene_.loc[31], rn, rs); }
  }
  if (lightingMode > 0 && atlasScene_) {
    const LightingEnv& e = atlasScene_->env;
    gl_.Uniform3fv(scene_.loc[6], 1, e.skyAmbient);
    gl_.Uniform3fv(scene_.loc[7], 1, e.groundAmbient);
    gl_.Uniform1f(scene_.loc[8], e.exposure);
    gl_.Uniform1f(scene_.loc[9], e.indoorLevel);
    gl_.Uniform3fv(scene_.loc[10], 1, sunCam_);
    gl_.Uniform3fv(scene_.loc[11], 1, e.sunColor);
    gl_.Uniform3fv(scene_.loc[12], 1, upCam_);
    gl_.Uniform1i(scene_.loc[13], lightN_);
    if (lightN_ > 0) {
      gl_.Uniform3fv(scene_.loc[14], lightN_, lightPos_);
      gl_.Uniform3fv(scene_.loc[15], lightN_, lightCol_);
      gl_.Uniform1fv(scene_.loc[16], lightN_, lightRad_);
    }
    gl_.Uniform1i(scene_.loc[17], shadowOn_ ? 1 : 0);
    // both shadow samplers always need a texture bound (an unbound sampler of a different type would invalidate the draw)
    for (int k = 0; k < 2; ++k) {
      gl_.ActiveTexture(gl::TEXTURE0 + 2 + k);
      gl_.BindTexture(gl::TEXTURE_2D, shadowTex_[k] ? shadowTex_[k] : atlas_);
      gl_.Uniform1i(scene_.loc[18 + k], 2 + k);
    }
    if (shadowOn_) {
      gl_.Uniform3fv(scene_.loc[20], 2, shX_);
      gl_.Uniform3fv(scene_.loc[21], 2, shY_);
      gl_.Uniform3fv(scene_.loc[22], 2, shZ_);
      gl_.Uniform3fv(scene_.loc[23], 2, shOff_);
      gl_.Uniform3fv(scene_.loc[24], 2, shParams_);
    }
    gl_.ActiveTexture(gl::TEXTURE0);
  }
  gl_.ActiveTexture(gl::TEXTURE0);
  gl_.BindTexture(gl::TEXTURE_2D, atlas_);
  gl_.Uniform1i(scene_.loc[2], 0);
  gl_.Uniform1i(scene_.loc[3], filterMode_);
  gl_.Uniform2f(scene_.loc[4], atlasW_, atlasH_);
  gl_.BindVertexArray(vao_);
  gl_.BindBuffer(gl::ARRAY_BUFFER, vbo_);
  gl_.BufferData(gl::ARRAY_BUFFER, gl::GLsizeiptr(tris_.size() * sizeof(SceneVertex)), tris_.data(), gl::STREAM_DRAW);
  gl_.DrawArrays(gl::TRIANGLES, 0, gl::GLsizei(tris_.size()));
  tris_.clear();
  depthCopyValid_ = false;
}

void GlRenderer::restoreGlobalDepth() {
  ensureDepthCopy();
  const gl::GLenum bufs[2] = {gl::COLOR_ATTACHMENT0, gl::COLOR_ATTACHMENT1};
  gl_.BindFramebuffer(gl::FRAMEBUFFER, renderFbo());
  gl_.DrawBuffers(2, bufs);
  gl_.Viewport(0, 0, tw_, th_);
  setScissor(vp_.x0, vp_.y0, vp_.x1, vp_.y1);
  gl_.Enable(gl::DEPTH_TEST);
  gl_.DepthFunc(gl::ALWAYS);
  gl_.DepthMask(gl::TRUE_);
  gl_.ColorMask(gl::FALSE_, gl::FALSE_, gl::FALSE_, gl::FALSE_);
  gl_.UseProgram(restore_.id);
  gl_.ActiveTexture(gl::TEXTURE0);
  gl_.BindTexture(gl::TEXTURE_2D, copyTex_);
  gl_.Uniform1i(restore_.loc[0], 0);
  gl_.BindVertexArray(emptyVao_);
  gl_.DrawArrays(gl::TRIANGLES, 0, 3);
  gl_.ColorMask(gl::TRUE_, gl::TRUE_, gl::TRUE_, gl::TRUE_);
  globalDepth_ = true;
}

// ---- real-time lighting -------------------------------------------------------------------------------------------------------------------------------------------------------------
void GlRenderer::prepareLighting(const Scene& sc) {
  lightsReady_ = true;
  const LightingEnv& env = sc.env;
  auto cam = [&](const double d[3], float* o) {
    o[0] = float(d[0] * right_[0] + d[1] * right_[1] + d[2] * right_[2]);
    o[1] = float(d[0] * up_[0] + d[1] * up_[1] + d[2] * up_[2]);
    o[2] = float(d[0] * fwd_[0] + d[1] * fwd_[1] + d[2] * fwd_[2]);
  };
  const double sun[3] = {env.sunDir[0], env.sunDir[1], env.sunDir[2]}, upW[3] = {0, 1, 0};
  cam(sun, sunCam_);
  cam(upW, upCam_);
  // the lights: fixed ones of the track near the camera plus the lights of this frame, the most relevant 24
  struct Cand { float score; double pos[3]; float col[3]; float rad; };
  std::vector<Cand> cands;
  const double camRel[3] = {cam_.pos[0] - sc.origin[0], cam_.pos[1] - sc.origin[1], cam_.pos[2] - sc.origin[2]};
  for (const StaticLight& l : sc.lights) {
    const double d[3] = {double(l.pos[0]) - camRel[0], double(l.pos[1]) - camRel[1], double(l.pos[2]) - camRel[2]};
    const double dist = std::sqrt(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
    if (dist > double(l.radius) + 500000.0) continue;
    cands.push_back({float(dist / double(l.radius)), {d[0], d[1], d[2]}, {l.color[0], l.color[1], l.color[2]}, l.radius});
  }
  for (const PointLight& l : frameLights) {
    const double d[3] = {l.pos[0] - cam_.pos[0], l.pos[1] - cam_.pos[1], l.pos[2] - cam_.pos[2]};
    const double dist = std::sqrt(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
    cands.push_back({float(dist / double(l.radius)) - 1.0f, {d[0], d[1], d[2]}, {l.color[0], l.color[1], l.color[2]}, l.radius});  // dynamic lights first
  }
  std::sort(cands.begin(), cands.end(), [](const Cand& a, const Cand& b) { return a.score < b.score; });
  lightN_ = int(std::min<size_t>(cands.size(), size_t(kMaxLights)));
  for (int i = 0; i < lightN_; ++i) {
    cam(cands[size_t(i)].pos, &lightPos_[i * 3]);
    for (int k = 0; k < 3; ++k) lightCol_[i * 3 + k] = cands[size_t(i)].col[k] * 1.1f;
    lightRad_[i] = cands[size_t(i)].rad;
  }
  shadowOn_ = false;
  if (lightingMode >= 2 && shadowProg_.id && !env.night && env.sunColor[0] + env.sunColor[1] + env.sunColor[2] > 0.05f) renderShadowMap(sc);
}

void GlRenderer::uploadStaticCasters(const Scene& sc) {
  casterScene_ = &sc;
  std::vector<float> v;
  for (const MeshPoly& p : sc.track.polys) {
    if (p.hidden || p.portal || p.count < 3) continue;
    if (p.material >= 0 && size_t(p.material) < sc.materials.size() && sc.materials[size_t(p.material)].invisible) continue;
    for (uint16_t k = 1; k + 1 < p.count; ++k)
      for (uint16_t idx : {uint16_t(0), k, uint16_t(k + 1)}) {
        const Vec3& q = sc.track.verts[p.first + idx];
        v.push_back(q.x); v.push_back(q.y); v.push_back(q.z);
      }
  }
  staticCasterVerts_ = v.size() / 3;
  if (!shadowVao_) { gl_.GenVertexArrays(1, &shadowVao_); gl_.GenBuffers(1, &shadowVbo_); gl_.GenVertexArrays(1, &shadowDynVao_); gl_.GenBuffers(1, &shadowDynVbo_); }
  gl_.BindVertexArray(shadowVao_);
  gl_.BindBuffer(gl::ARRAY_BUFFER, shadowVbo_);
  gl_.BufferData(gl::ARRAY_BUFFER, gl::GLsizeiptr(v.size() * sizeof(float)), v.data(), gl::DYNAMIC_DRAW);
  gl_.EnableVertexAttribArray(0);
  gl_.VertexAttribPointer(0, 3, gl::FLOAT, gl::FALSE_, 3 * sizeof(float), nullptr);
  gl_.BindVertexArray(shadowDynVao_);
  gl_.BindBuffer(gl::ARRAY_BUFFER, shadowDynVbo_);
  gl_.EnableVertexAttribArray(0);
  gl_.VertexAttribPointer(0, 3, gl::FLOAT, gl::FALSE_, 3 * sizeof(float), nullptr);
}

// The sun's shadow maps: two orthographic depth maps from the sun around the camera (a detailed near one and a wide far one; the whole track mesh plus the ships), snapped to their texel
// grids so that they do not swim. Tunnels are lit by their lamps only (indoor receivers ignore the sun), open air by sun and sky.
void GlRenderer::renderShadowMap(const Scene& sc) {
  if (casterScene_ != &sc) uploadStaticCasters(sc);
  static const int kSize[2] = {4096, 2048};
  static const double kRadius[2] = {110000.0, 650000.0};
  const LightingEnv& env = sc.env;
  double lz[3] = {env.sunDir[0], env.sunDir[1], env.sunDir[2]};
  auto norm = [](double* v) { const double l = std::sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]); if (l > 0) for (int i = 0; i < 3; ++i) v[i] /= l; };
  auto cross = [](const double* a, const double* b, double* o) { o[0] = a[1] * b[2] - a[2] * b[1]; o[1] = a[2] * b[0] - a[0] * b[2]; o[2] = a[0] * b[1] - a[1] * b[0]; };
  auto dot = [](const double* a, const double* b) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; };
  norm(lz);
  const double upW[3] = {0, 1, 0};
  double lx[3], ly[3];
  cross(upW, lz, lx); norm(lx);
  cross(lz, lx, ly); norm(ly);
  const double Zr = 2.5e6;
  const double camRel[3] = {cam_.pos[0] - sc.origin[0], cam_.pos[1] - sc.origin[1], cam_.pos[2] - sc.origin[2]};
  // ships (dynamic casters)
  std::vector<float> dyn;
  for (const ShadowCaster& cs : shadowCasters) {
    if (!cs.mesh) continue;
    const Mesh& m = *cs.mesh;
    std::vector<Vec3> w(m.verts.size());
    for (size_t i = 0; i < m.verts.size(); ++i) {
      const Vec3& v = m.verts[i];
      w[i] = {float(cs.xf.pos[0] + cs.xf.R[0] * v.x + cs.xf.R[1] * v.y + cs.xf.R[2] * v.z - sc.origin[0]), float(cs.xf.pos[1] + cs.xf.R[3] * v.x + cs.xf.R[4] * v.y + cs.xf.R[5] * v.z - sc.origin[1]),
              float(cs.xf.pos[2] + cs.xf.R[6] * v.x + cs.xf.R[7] * v.y + cs.xf.R[8] * v.z - sc.origin[2])};
    }
    for (const MeshPoly& p : m.polys)
      for (uint16_t k = 1; k + 1 < p.count; ++k)
        for (uint16_t idx : {uint16_t(0), k, uint16_t(k + 1)}) { const Vec3& q = w[p.first + idx]; dyn.push_back(q.x); dyn.push_back(q.y); dyn.push_back(q.z); }
  }
  if (!dyn.empty()) {
    gl_.BindVertexArray(shadowDynVao_);
    gl_.BindBuffer(gl::ARRAY_BUFFER, shadowDynVbo_);
    gl_.BufferData(gl::ARRAY_BUFFER, gl::GLsizeiptr(dyn.size() * sizeof(float)), dyn.data(), gl::STREAM_DRAW);
  }
  auto cs3 = [&](const double* a, float* o) { o[0] = float(a[0] * right_[0] + a[1] * right_[1] + a[2] * right_[2]); o[1] = float(a[0] * up_[0] + a[1] * up_[1] + a[2] * up_[2]); o[2] = float(a[0] * fwd_[0] + a[1] * fwd_[1] + a[2] * fwd_[2]); };
  for (int c = 0; c < 2; ++c) {
    const int size = kSize[c];
    const double R = kRadius[c], texel = 2.0 * R / double(size);
    if (!shadowFbo_[c] || shadowSize_[c] != gl::GLuint(size)) {
      if (shadowFbo_[c]) { gl_.DeleteFramebuffers(1, &shadowFbo_[c]); gl_.DeleteTextures(1, &shadowTex_[c]); }
      gl_.GenTextures(1, &shadowTex_[c]);
      gl_.BindTexture(gl::TEXTURE_2D, shadowTex_[c]);
      gl_.TexImage2D(gl::TEXTURE_2D, 0, gl::GLint(gl::DEPTH_COMPONENT32F), size, size, 0, 0x1902 /*DEPTH_COMPONENT*/, gl::FLOAT, nullptr);
      gl_.TexParameteri(gl::TEXTURE_2D, gl::TEXTURE_MIN_FILTER, gl::GLint(gl::NEAREST));
      gl_.TexParameteri(gl::TEXTURE_2D, gl::TEXTURE_MAG_FILTER, gl::GLint(gl::NEAREST));
      gl_.TexParameteri(gl::TEXTURE_2D, gl::TEXTURE_WRAP_S, gl::GLint(gl::CLAMP_TO_EDGE));
      gl_.TexParameteri(gl::TEXTURE_2D, gl::TEXTURE_WRAP_T, gl::GLint(gl::CLAMP_TO_EDGE));
      gl_.GenFramebuffers(1, &shadowFbo_[c]);
      gl_.BindFramebuffer(gl::FRAMEBUFFER, shadowFbo_[c]);
      gl_.FramebufferTexture2D(gl::FRAMEBUFFER, gl::DEPTH_ATTACHMENT, gl::TEXTURE_2D, shadowTex_[c], 0);
      const gl::GLenum none = 0;
      gl_.DrawBuffers(1, &none);
      gl_.ReadBuffer(none);
      shadowSize_[c] = gl::GLuint(size);
    }
    const double cxs = std::floor(dot(camRel, lx) / texel) * texel, cys = std::floor(dot(camRel, ly) / texel) * texel;
    double O[3];
    for (int i = 0; i < 3; ++i) O[i] = camRel[i] - (dot(camRel, lx) - cxs) * lx[i] - (dot(camRel, ly) - cys) * ly[i];
    gl_.BindFramebuffer(gl::FRAMEBUFFER, shadowFbo_[c]);
    gl_.Viewport(0, 0, size, size);
    gl_.Disable(gl::SCISSOR_TEST);
    gl_.Enable(gl::DEPTH_TEST);
    gl_.DepthFunc(0x0201 /*LESS*/);
    gl_.DepthMask(gl::TRUE_);
    gl_.ColorMask(gl::FALSE_, gl::FALSE_, gl::FALSE_, gl::FALSE_);
    gl_.ClearDepth(1.0);
    gl_.Clear(gl::DEPTH_BUFFER_BIT);
    gl_.UseProgram(shadowProg_.id);
    const float o3[3] = {float(O[0]), float(O[1]), float(O[2])}, ax[3] = {float(lx[0]), float(lx[1]), float(lx[2])}, ay[3] = {float(ly[0]), float(ly[1]), float(ly[2])}, az[3] = {float(lz[0]), float(lz[1]), float(lz[2])};
    const float par[3] = {float(R), float(Zr), 0};
    gl_.Uniform3fv(shadowProg_.loc[0], 1, o3);
    gl_.Uniform3fv(shadowProg_.loc[1], 1, ax);
    gl_.Uniform3fv(shadowProg_.loc[2], 1, ay);
    gl_.Uniform3fv(shadowProg_.loc[3], 1, az);
    gl_.Uniform3fv(shadowProg_.loc[4], 1, par);
    gl_.BindVertexArray(shadowVao_);
    gl_.DrawArrays(gl::TRIANGLES, 0, gl::GLsizei(staticCasterVerts_));
    if (!dyn.empty()) {
      gl_.BindVertexArray(shadowDynVao_);
      gl_.DrawArrays(gl::TRIANGLES, 0, gl::GLsizei(dyn.size() / 3));
    }
    cs3(lx, &shX_[c * 3]); cs3(ly, &shY_[c * 3]); cs3(lz, &shZ_[c * 3]);
    const double d0[3] = {camRel[0] - O[0], camRel[1] - O[1], camRel[2] - O[2]};
    shOff_[c * 3] = float(dot(d0, lx)); shOff_[c * 3 + 1] = float(dot(d0, ly)); shOff_[c * 3 + 2] = float(dot(d0, lz));
    shParams_[c * 3] = float(R); shParams_[c * 3 + 1] = float(Zr); shParams_[c * 3 + 2] = float(texel);
  }
  gl_.ColorMask(gl::TRUE_, gl::TRUE_, gl::TRUE_, gl::TRUE_);
  shadowOn_ = true;
}

void GlRenderer::ensureDepthCopy() {
  if (depthCopyValid_) return;
  gl_.BindFramebuffer(gl::READ_FRAMEBUFFER, renderFbo());
  gl_.ReadBuffer(gl::COLOR_ATTACHMENT1);
  gl_.BindFramebuffer(gl::DRAW_FRAMEBUFFER, copyFbo_);
  const gl::GLenum b0 = gl::COLOR_ATTACHMENT0;
  gl_.DrawBuffers(1, &b0);
  gl_.Disable(gl::SCISSOR_TEST);
  gl_.BlitFramebuffer(0, 0, tw_, th_, 0, 0, tw_, th_, gl::COLOR_BUFFER_BIT, gl::NEAREST);
  depthCopyValid_ = true;
}

void GlRenderer::onBeginFrame(uint32_t sky, uint32_t ground) {
  if (!ok_) return;
  SDL_GL_MakeCurrent(window_, ctx_);
  if (sceneActive_) finishScene();
  // debug modes that draw into the framebuffer from the CPU (sprite viewer ...) expect the backdrop colour under their pixels
  for (int y = std::max(0, vp_.y0); y <= std::min(h_ - 1, vp_.y1); ++y) std::fill(color_.begin() + size_t(y) * size_t(w_) + size_t(std::max(0, vp_.x0)), color_.begin() + size_t(y) * size_t(w_) + size_t(std::min(w_ - 1, vp_.x1)) + 1, sky);
  if (tw_ != w_ || th_ != h_ || !fbo_) { tw_ = w_; th_ = h_; createTargets(); }
  const float W = float(tw_), H = float(th_);
  uProj_[0] = 2.0f * focal_ / W; uProj_[1] = 2.0f * cx_ / W - 1.0f; uProj_[2] = 2.0f * focal_ / H; uProj_[3] = (H - 2.0f * cy_) / H;
  nearD_ = cam_.nearPlane * 0.5f;
  tris_.clear();
  batchOpen_ = false; batchItem_ = -2; clearedItem_ = -2; shadowMode_ = false; depthCopyValid_ = false; sceneActive_ = true; globalDepth_ = true; lightsReady_ = false;
  // backdrop
  const gl::GLenum b0 = gl::COLOR_ATTACHMENT0, bufs[2] = {gl::COLOR_ATTACHMENT0, gl::COLOR_ATTACHMENT1};
  gl_.BindFramebuffer(gl::FRAMEBUFFER, renderFbo());
  gl_.Viewport(0, 0, tw_, th_);
  setScissor(vp_.x0, vp_.y0, vp_.x1, vp_.y1);
  gl_.Disable(gl::DEPTH_TEST);
  gl_.DepthMask(gl::FALSE_);
  gl_.Disable(gl::BLEND);
  gl_.DrawBuffers(1, &b0);
  gl_.UseProgram(sky_.id);
  auto rgb = [](uint32_t c, float* o) { o[0] = float((c >> 16) & 255) / 255.0f; o[1] = float((c >> 8) & 255) / 255.0f; o[2] = float(c & 255) / 255.0f; };
  float c3[3];
  gl_.Uniform2f(sky_.loc[0], W, H);
  gl_.Uniform2f(sky_.loc[1], cx_, cy_);
  gl_.Uniform1f(sky_.loc[2], focal_);
  gl_.Uniform3fv(sky_.loc[3], 1, right_);
  gl_.Uniform3fv(sky_.loc[4], 1, up_);
  gl_.Uniform3fv(sky_.loc[5], 1, fwd_);
  rgb(sky, c3); gl_.Uniform3fv(sky_.loc[6], 1, c3);
  rgb(ground, c3); gl_.Uniform3fv(sky_.loc[7], 1, c3);
  gl_.Uniform1f(sky_.loc[8], skyBand);
  const int n = int(std::min<size_t>(skyRamp.size(), 40));
  gl_.Uniform1i(sky_.loc[9], n);
  if (n > 0) {
    std::vector<float> ramp(size_t(n) * 3);
    for (int i = 0; i < n; ++i) rgb(skyRamp[size_t(i)], &ramp[size_t(i) * 3]);
    gl_.Uniform3fv(sky_.loc[10], n, ramp.data());
  }
  {
    const bool wg = fxOn_ && atlasScene_ && atlasScene_->env.waterGround;
    gl_.Uniform1i(sky_.loc[11], wg ? 1 : 0);
    if (wg) {
      const float cam3[3] = {float(cam_.pos[0]), float(cam_.pos[1]), float(cam_.pos[2])};
      gl_.Uniform1f(sky_.loc[12], float(animTimer) / 16384.0f);
      gl_.Uniform3fv(sky_.loc[13], 1, cam3);
      gl_.Uniform3fv(sky_.loc[14], 1, atlasScene_->env.sunDir);
      gl_.Uniform3fv(sky_.loc[15], 1, atlasScene_->env.sunColor);
      float rw[32], rs[8];
      int rn = 0;
      for (const Ripple& r : ripples) {
        if (rn >= 8) break;
        rw[rn * 4] = float(r.pos[0] - cam_.pos[0]); rw[rn * 4 + 1] = float(r.pos[1] - cam_.pos[1]); rw[rn * 4 + 2] = float(r.pos[2] - cam_.pos[2]); rw[rn * 4 + 3] = r.age;
        rs[rn] = r.strength;
        ++rn;
      }
      gl_.Uniform1i(sky_.loc[16], rn);
      if (rn > 0) { gl_.Uniform4fv(sky_.loc[17], rn, rw); gl_.Uniform1fv(sky_.loc[18], rn, rs); }
    }
  }
  gl_.BindVertexArray(emptyVao_);
  gl_.DrawArrays(gl::TRIANGLES, 0, 3);
  gl_.DrawBuffers(2, bufs);
  const float zero[4] = {0, 0, 0, 0};
  gl_.ClearBufferfv(gl::COLOR, 1, zero);
  gl_.DepthMask(gl::TRUE_);
  gl_.ClearDepth(0.0);
  gl_.Clear(gl::DEPTH_BUFFER_BIT);
}

void GlRenderer::emitTriangle(const SceneVertex& a, const SceneVertex& b, const SceneVertex& c) {
  tris_.push_back(a);
  tris_.push_back(b);
  tris_.push_back(c);
}

void GlRenderer::rasterTri(const Scene& scene, const VV& a, const VV& b, const VV& c, const SurfaceMaterial* mat, bool useTexture, float light, int upLight, uint8_t layer, int forceIdx) {
  if (!ok_) return;
  if (&scene != atlasScene_) { SDL_GL_MakeCurrent(window_, ctx_); uploadAtlas(scene); }
  stats_.trisRastered++;
  const bool textured = forceIdx < 0 && useTexture && mat && mat->texture >= 0 && size_t(mat->texture) < rects_.size() && rects_[size_t(mat->texture)][2] > 0;
  uint32_t flat;
  if (forceIdx >= 0) flat = scene.palette.rgba[size_t(std::clamp(forceIdx, 0, 255))];
  else if (mat) flat = scene.palette.rgba[size_t(std::clamp(flatIndex(mat, upLight), 0, 255))];  // real-time lighting shades the brightest colour of the ramp itself
  else {
    const uint32_t p = scene.palette.rgba[0];
    auto ch = [&](uint32_t v) { return std::min(255u, uint32_t(float(v) * light)); };
    flat = (ch((p >> 16) & 255) << 16) | (ch((p >> 8) & 255) << 8) | ch(p & 255);
  }
  SceneVertex v[3];
  const VV* src[3] = {&a, &b, &c};
  uint8_t r, g, bl;
  unpack(flat, &r, &g, &bl);
  openBatch();
  if (shadowMode_) {  // ship shadow: clip the triangle to the receiver polygon, then fill it
    std::vector<VV> poly = {a, b, c}, out;
    for (const auto& pl : shadowPlanes_) {
      out.clear();
      for (size_t i = 0; i < poly.size(); ++i) {
        const VV &p0 = poly[i], &p1 = poly[(i + 1) % poly.size()];
        const float d0 = pl[0] * p0.x + pl[1] * p0.y + pl[2] * p0.z, d1 = pl[0] * p1.x + pl[1] * p1.y + pl[2] * p1.z;
        if (d0 >= 0) out.push_back(p0);
        if ((d0 >= 0) != (d1 >= 0)) { const float t = d0 / (d0 - d1); out.push_back({p0.x + (p1.x - p0.x) * t, p0.y + (p1.y - p0.y) * t, p0.z + (p1.z - p0.z) * t, 0, 0}); }
      }
      poly.swap(out);
      if (poly.size() < 3) return;
    }
    const float ds = depthScale(layer, forceIdx) * 1.01f;
    for (size_t k = 1; k + 1 < poly.size(); ++k) {
      const VV* t3[3] = {&poly[0], &poly[k], &poly[k + 1]};
      SceneVertex sv[3];
      for (int i = 0; i < 3; ++i) sv[i] = {t3[i]->x, t3[i]->y, t3[i]->z, 0, 0, r, g, bl, 255, 0, 0, 0, 0, ds, 0, 0, 0, 0, 0};
      emitTriangle(sv[0], sv[1], sv[2]);
    }
    return;
  }
  const float ds = depthScale(layer, forceIdx);
  const std::array<float, 4> rc = textured ? rects_[size_t(mat->texture)] : std::array<float, 4>{0, 0, 0, 0};
  float n[3] = {curN_[0], curN_[1], curN_[2]};
  if (lightingMode > 0 || (fxOn_ && curWater_)) {
    float nl = std::sqrt(n[0] * n[0] + n[1] * n[1] + n[2] * n[2]);
    if (nl < 0.5f) {  // polygons without a stored normal: the triangle's own
      const float e1[3] = {b.x - a.x, b.y - a.y, b.z - a.z}, e2[3] = {c.x - a.x, c.y - a.y, c.z - a.z};
      n[0] = e1[1] * e2[2] - e1[2] * e2[1]; n[1] = e1[2] * e2[0] - e1[0] * e2[2]; n[2] = e1[0] * e2[1] - e1[1] * e2[0];
      nl = std::sqrt(n[0] * n[0] + n[1] * n[1] + n[2] * n[2]);
    }
    if (nl > 1e-6f) { n[0] /= nl; n[1] /= nl; n[2] /= nl; } else { n[0] = n[1] = n[2] = 0; }
  } else { n[0] = n[1] = n[2] = 0; }
  const float flags = (curIndoor_ ? 1.0f : 0.0f) + ((curEmissive_ || (mat && mat->fixedLight != 0)) && lightingMode > 0 ? 2.0f : 0.0f) + (curWater_ && fxOn_ ? 4.0f : 0.0f);
  for (int i = 0; i < 3; ++i) v[i] = {src[i]->x, src[i]->y, src[i]->z, src[i]->u, src[i]->v, r, g, bl, 255, rc[0], rc[1], rc[2], rc[3], ds, n[0], n[1], n[2], flags, curSpec_};
  emitTriangle(v[0], v[1], v[2]);
}

void GlRenderer::shadowBegin(const std::vector<VV>& receiver, int) {
  shadowMode_ = true;
  shadowPlanes_.clear();
  float cx = 0, cy = 0, cz = 0;
  for (const VV& p : receiver) { cx += p.x; cy += p.y; cz += p.z; }
  const float k = receiver.empty() ? 0.0f : 1.0f / float(receiver.size());
  cx *= k; cy *= k; cz *= k;
  // planes through the camera origin and each edge of the receiver: a point inside the receiver polygon is on the positive side of every one
  for (size_t i = 0; i < receiver.size(); ++i) {
    const VV &p = receiver[i], &q = receiver[(i + 1) % receiver.size()];
    float n[3] = {p.y * q.z - p.z * q.y, p.z * q.x - p.x * q.z, p.x * q.y - p.y * q.x};
    if (n[0] * cx + n[1] * cy + n[2] * cz < 0) { n[0] = -n[0]; n[1] = -n[1]; n[2] = -n[2]; }
    shadowPlanes_.push_back({n[0], n[1], n[2]});
  }
}

void GlRenderer::shadowEnd() { shadowMode_ = false; }

void GlRenderer::emitSceneLine(const P3& a, const P3& b, uint32_t color, float ds) {
  const int thick = std::max(1, int(std::lround(float(w_) / 320.0f)));
  const float hw = 0.5f * float(thick);
  const float sx0 = cx_ + a.x / a.z * focal_, sy0 = cy_ - a.y / a.z * focal_, sx1 = cx_ + b.x / b.z * focal_, sy1 = cy_ - b.y / b.z * focal_;
  float dx = sx1 - sx0, dy = sy1 - sy0;
  const float len = std::sqrt(dx * dx + dy * dy);
  if (len > 1e-4f) { dx /= len; dy /= len; } else { dx = 1; dy = 0; }
  const float nx = -dy, ny = dx;  // screen-space normal
  uint8_t r, g, bl;
  unpack(color, &r, &g, &bl);
  // camera-space offsets of the quad corners: a screen offset of p pixels is p * z / focal in the plane of constant z
  auto corner = [&](const P3& p, float along, float side) {
    const float k = p.z / focal_;
    return SceneVertex{p.x + (dx * along + nx * side) * k, p.y - (dy * along + ny * side) * k, p.z, 0, 0, r, g, bl, 255, 0, 0, 0, 0, ds, 0, 0, 0, 0, 0};
  };
  const SceneVertex q0 = corner(a, -hw, hw), q1 = corner(a, -hw, -hw), q2 = corner(b, hw, -hw), q3 = corner(b, hw, hw);
  emitTriangle(q0, q1, q2);
  emitTriangle(q0, q2, q3);
}

void GlRenderer::drawLine3D(P3 a, P3 b, uint32_t col) {
  if (!ok_) return;
  const float n = cam_.nearPlane;
  if (a.z < n && b.z < n) return;
  if (a.z < n) { const float t = (n - a.z) / (b.z - a.z); a = {a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, n}; }
  else if (b.z < n) { const float t = (n - b.z) / (a.z - b.z); b = {b.x + (a.x - b.x) * t, b.y + (a.y - b.y) * t, n}; }
  openBatch();
  emitSceneLine(a, b, col, depthScale(0, 0) * 1.02f);
}

void GlRenderer::drawOverlay(gl::GLenum mode, const std::vector<OverlayVertex>& v, gl::GLuint tex, bool depthTest, bool fullScissor) {
  if (!ok_ || v.empty()) return;
  flush();
  if (depthTest) ensureDepthCopy();
  const gl::GLenum b0 = gl::COLOR_ATTACHMENT0;
  gl_.BindFramebuffer(gl::FRAMEBUFFER, renderFbo());
  gl_.DrawBuffers(1, &b0);
  gl_.Viewport(0, 0, tw_, th_);
  if (fullScissor) setScissor(0, 0, tw_ - 1, th_ - 1); else setScissor(vp_.x0, vp_.y0, vp_.x1, vp_.y1);
  gl_.Disable(gl::DEPTH_TEST);
  gl_.DepthMask(gl::FALSE_);
  gl_.Disable(gl::BLEND);
  gl_.UseProgram(overlay_.id);
  gl_.Uniform2f(overlay_.loc[0], float(tw_), float(th_));
  gl_.ActiveTexture(gl::TEXTURE0);
  gl_.BindTexture(gl::TEXTURE_2D, tex);
  gl_.Uniform1i(overlay_.loc[1], 0);
  gl_.ActiveTexture(gl::TEXTURE1);
  gl_.BindTexture(gl::TEXTURE_2D, copyTex_);
  gl_.Uniform1i(overlay_.loc[2], 1);
  gl_.Uniform1i(overlay_.loc[3], tex ? 1 : 0);
  gl_.Uniform1i(overlay_.loc[4], depthTest ? 1 : 0);
  gl_.BindVertexArray(ovao_);
  gl_.BindBuffer(gl::ARRAY_BUFFER, ovbo_);
  gl_.BufferData(gl::ARRAY_BUFFER, gl::GLsizeiptr(v.size() * sizeof(OverlayVertex)), v.data(), gl::STREAM_DRAW);
  gl_.DrawArrays(mode, 0, gl::GLsizei(v.size()));
  gl_.ActiveTexture(gl::TEXTURE0);
}

void GlRenderer::overlayLine(float x0, float y0, float d0, float x1, float y1, float d1, uint32_t color, bool depthTest) {
  const int thick = std::max(1, int(std::lround(float(w_) / 320.0f)));
  const float hw = 0.5f * float(thick);
  float dx = x1 - x0, dy = y1 - y0;
  const float len = std::sqrt(dx * dx + dy * dy);
  if (len > 1e-4f) { dx /= len; dy /= len; } else { dx = 1; dy = 0; }
  const float nx = -dy, ny = dx;
  uint8_t r, g, b;
  unpack(color, &r, &g, &b);
  auto V = [&](float x, float y, float d) { return OverlayVertex{x, y, d, 0, 0, r, g, b, 255}; };
  const OverlayVertex q0 = V(x0 - dx * hw + nx * hw, y0 - dy * hw + ny * hw, d0), q1 = V(x0 - dx * hw - nx * hw, y0 - dy * hw - ny * hw, d0),
                      q2 = V(x1 + dx * hw - nx * hw, y1 + dy * hw - ny * hw, d1), q3 = V(x1 + dx * hw + nx * hw, y1 + dy * hw + ny * hw, d1);
  drawOverlay(gl::TRIANGLES, {q0, q1, q2, q0, q2, q3}, 0, depthTest);
}

void GlRenderer::drawLineWorld(const double a[3], const double b[3], uint32_t color) {
  if (!ok_) return;
  auto cs = [&](const double* p) {
    const float dx = float(p[0] - cam_.pos[0]), dy = float(p[1] - cam_.pos[1]), dz = float(p[2] - cam_.pos[2]);
    return P3{dx * right_[0] + dy * right_[1] + dz * right_[2], dx * up_[0] + dy * up_[1] + dz * up_[2], dx * fwd_[0] + dy * fwd_[1] + dz * fwd_[2]};
  };
  P3 pa = cs(a), pb = cs(b);
  const float n = cam_.nearPlane;
  if (pa.z < n && pb.z < n) return;
  if (pa.z < n) { const float t = (n - pa.z) / (pb.z - pa.z); pa = {pa.x + (pb.x - pa.x) * t, pa.y + (pb.y - pa.y) * t, n}; }
  else if (pb.z < n) { const float t = (n - pb.z) / (pa.z - pb.z); pb = {pb.x + (pa.x - pb.x) * t, pb.y + (pa.y - pb.y) * t, n}; }
  overlayLine(cx_ + pa.x / pa.z * focal_, cy_ - pa.y / pa.z * focal_, nearD_ / pa.z, cx_ + pb.x / pb.z * focal_, cy_ - pb.y / pb.z * focal_, nearD_ / pb.z, color, true);
}

void GlRenderer::drawStarWorld(const double w[3], double radius, double angle, uint32_t color) {
  if (!ok_) return;
  float sx, sy, z;
  if (!projectToScreen(w, &sx, &sy, &z)) return;
  const double c[3] = {w[0], w[1], w[2]};
  const float dx = float(c[0] - cam_.pos[0]), dy = float(c[1] - cam_.pos[1]), dz = float(c[2] - cam_.pos[2]);
  const P3 cc{dx * right_[0] + dy * right_[1] + dz * right_[2], dx * up_[0] + dy * up_[1] + dz * up_[2], dx * fwd_[0] + dy * fwd_[1] + dz * fwd_[2]};
  auto line = [&](P3 p, P3 q) {
    overlayLine(cx_ + p.x / p.z * focal_, cy_ - p.y / p.z * focal_, nearD_ / p.z, cx_ + q.x / q.z * focal_, cy_ - q.y / q.z * focal_, nearD_ / q.z, color, true);
  };
  if (radius / z * focal_ <= 2.0) {
    line(cc, P3{cc.x + 0.8f * z / focal_, cc.y, cc.z});
  } else {
    const float r = float(radius), ca = float(std::cos(angle * kTau)), sa = float(std::sin(angle * kTau));
    line(P3{cc.x - r * ca, cc.y - r * sa, cc.z}, P3{cc.x + r * ca, cc.y + r * sa, cc.z});
    line(P3{cc.x + r * sa, cc.y - r * ca, cc.z}, P3{cc.x - r * sa, cc.y + r * ca, cc.z});
  }
}

void GlRenderer::drawSpriteWorld(const Sprite& spr, const Palette& pal, const double world[3], double worldWidth, int transparent) {
  if (!ok_) return;
  float sx, sy, z;
  if (spr.w <= 0 || spr.h <= 0 || !projectToScreen(world, &sx, &sy, &z)) return;
  const float pw = float(worldWidth) / z * focal_;
  if (pw < 1.0f) return;
  const float ph = pw * float(spr.h) / float(spr.w);
  const float d = nearD_ / z;
  const gl::GLuint tex = spriteTexture(spr, pal, transparent);
  const float x0 = sx - pw * 0.5f, x1 = sx + pw * 0.5f, y0 = sy - ph * 0.5f, y1 = sy + ph * 0.5f;
  auto V = [&](float x, float y, float u, float v) { return OverlayVertex{x, y, d, u, v, 255, 255, 255, 255}; };
  const OverlayVertex a = V(x0, y0, 0, 0), b = V(x1, y0, 1, 0), c = V(x1, y1, 1, 1), e = V(x0, y1, 0, 1);
  drawOverlay(gl::TRIANGLES, {a, b, c, a, c, e}, tex, true);
}

void GlRenderer::drawRectScreen(int x0, int y0, int x1, int y1, uint32_t color) {
  if (!ok_) return;
  uint8_t r, g, b;
  unpack(color, &r, &g, &b);
  auto V = [&](float x, float y) { return OverlayVertex{x, y, 0, 0, 0, r, g, b, 255}; };
  std::vector<OverlayVertex> v;
  auto quad = [&](float ax, float ay, float bx, float by) {
    const OverlayVertex p = V(ax, ay), q = V(bx, ay), s = V(bx, by), t = V(ax, by);
    v.insert(v.end(), {p, q, s, p, s, t});
  };
  quad(float(x0), float(y0), float(x1 + 1), float(y0 + 1));
  quad(float(x0), float(y1), float(x1 + 1), float(y1 + 1));
  quad(float(x0), float(y0), float(x0 + 1), float(y1 + 1));
  quad(float(x1), float(y0), float(x1 + 1), float(y1 + 1));
  drawOverlay(gl::TRIANGLES, v, 0, false, true);
}

void GlRenderer::drawSky(const Scene& scene, double seconds) {
  if (!ok_ || scene.skySprites.empty()) return;
  const float scale = float(vp_.x1 - vp_.x0 + 1) / 320.0f;
  const double turn = kTau / 65536.0;
  for (int layer = 0; layer < 3; ++layer) {
    const double drift = (layer == 2 ? scene.skySpeed : layer == 0 ? scene.skySpeed / 8.0 : 0.0) * seconds * turn;
    for (const SkySprite& k : scene.skySprites) {
      if (k.layer != layer || k.img.w <= 0) continue;
      const double az = k.az - drift, ce = std::cos(k.el);
      const double d[3] = {std::sin(az) * ce, std::sin(k.el), std::cos(az) * ce};
      const float z = float(d[0] * fwd_[0] + d[1] * fwd_[1] + d[2] * fwd_[2]);
      if (z <= 0.05f) continue;
      const float x = float(d[0] * right_[0] + d[1] * right_[1] + d[2] * right_[2]), y = float(d[0] * up_[0] + d[1] * up_[1] + d[2] * up_[2]);
      const float sx = cx_ + x / z * focal_, sy = cy_ - y / z * focal_;
      const float pw = float(k.img.w) * scale * k.scale, ph = float(k.img.h) * scale * k.scale;
      // the picture stands upright in the world: it turns with the camera's roll
      const float rn = std::sqrt(right_[1] * right_[1] + up_[1] * up_[1]);
      const float ux = rn > 1e-4f ? right_[1] / rn : 0.0f, uy = rn > 1e-4f ? -up_[1] / rn : -1.0f;  // world up on screen (y down)
      const float rx = -uy, ry = ux;                                                                 // world right on screen
      auto P = [&](float lu, float lh, float tu, float tv) { return OverlayVertex{sx + lu * rx + lh * ux, sy + lu * ry + lh * uy, 0, tu, tv, 255, 255, 255, 255}; };
      const OverlayVertex a = P(-pw * 0.5f, 0, 0, 1), b = P(pw * 0.5f, 0, 1, 1), c = P(pw * 0.5f, ph, 1, 0), e = P(-pw * 0.5f, ph, 0, 0);
      const Palette& pal = scene.palette;
      drawOverlay(gl::TRIANGLES, {a, b, c, a, c, e}, spriteTexture(k.img, pal, k.transparent), false);
    }
  }
}

void GlRenderer::fullscreenPass(Program& p, gl::GLuint fbo, int w, int h, int sx0, int sy0, int sx1, int sy1) {
  const gl::GLenum b0 = gl::COLOR_ATTACHMENT0;
  gl_.BindFramebuffer(gl::FRAMEBUFFER, fbo);
  gl_.DrawBuffers(1, &b0);
  gl_.Viewport(0, 0, w, h);
  gl_.Enable(gl::SCISSOR_TEST);
  gl_.Scissor(sx0, sy0, std::max(0, sx1 - sx0 + 1), std::max(0, sy1 - sy0 + 1));
  gl_.Disable(gl::DEPTH_TEST);
  gl_.DepthMask(gl::FALSE_);
  gl_.Disable(gl::BLEND);
  gl_.UseProgram(p.id);
  gl_.BindVertexArray(emptyVao_);
}

// Post passes on the resolved picture: ambient occlusion, bloom + tone mapping (lighting on), then the user's post shader. Rectangle in GL window coordinates (y up).
void GlRenderer::runPost(int x0, int y0, int x1, int y1) {
  const int rw = x1 - x0 + 1, rh = y1 - y0 + 1;
  gl::GLuint srcFbo = fbo_, srcTex = colorTex_, dstFbo = postFbo_, dstTex = postTex_;
  auto swap = [&]() { std::swap(srcFbo, dstFbo); std::swap(srcTex, dstTex); };
  auto bind = [&](int unit, gl::GLuint tex) { gl_.ActiveTexture(gl::TEXTURE0 + unit); gl_.BindTexture(gl::TEXTURE_2D, tex); };
  const int gy0 = th_ - 1 - y1, gy1 = th_ - 1 - y0;   // y flipped: rows of the window in GL coordinates
  (void)rh;
  if (hdr_ && postFbo_) {
    if (aoOn_) {
      depthCopyValid_ = false;
      ensureDepthCopy();
      fullscreenPass(ssao_, dstFbo, tw_, th_, x0, gy0, x1, gy1);
      bind(0, srcTex); gl_.Uniform1i(ssao_.loc[0], 0);
      bind(1, copyTex_); gl_.Uniform1i(ssao_.loc[1], 1);
      gl_.Uniform2f(ssao_.loc[2], float(tw_), float(th_));
      gl_.Uniform1f(ssao_.loc[3], nearD_);
      gl_.Uniform1f(ssao_.loc[4], focal_);
      gl_.Uniform1f(ssao_.loc[5], 26000.0f);
      gl_.Uniform1f(ssao_.loc[6], 0.65f);
      gl_.DrawArrays(gl::TRIANGLES, 0, 3);
      swap();
    }
    const int bw = std::max(1, tw_ / 2), bh = std::max(1, th_ / 2);
    const int hx0 = x0 / 2, hx1 = std::min(bw - 1, x1 / 2), hy0 = gy0 / 2, hy1 = std::min(bh - 1, gy1 / 2);
    if (bloomOn_) {
      fullscreenPass(bloomX_, bloomFbo_[0], bw, bh, hx0, hy0, hx1, hy1);
      bind(0, srcTex); gl_.Uniform1i(bloomX_.loc[0], 0);
      gl_.Uniform2f(bloomX_.loc[1], float(tw_), float(th_));
      gl_.Uniform1f(bloomX_.loc[2], 1.0f);
      gl_.DrawArrays(gl::TRIANGLES, 0, 3);
      for (int it = 0; it < 2; ++it)
        for (int dir = 0; dir < 2; ++dir) {
          fullscreenPass(blur_, bloomFbo_[dir ? 0 : 1], bw, bh, hx0, hy0, hx1, hy1);
          bind(0, bloomTex_[dir ? 1 : 0]); gl_.Uniform1i(blur_.loc[0], 0);
          gl_.Uniform2f(blur_.loc[1], dir ? 0.0f : 1.0f, dir ? 1.0f : 0.0f);
          gl_.Uniform4f(blur_.loc[2], float(hx0), float(hy0), float(hx1), float(hy1));
          gl_.DrawArrays(gl::TRIANGLES, 0, 3);
        }
    }
    fullscreenPass(comp_, dstFbo, tw_, th_, x0, gy0, x1, gy1);
    bind(0, srcTex); gl_.Uniform1i(comp_.loc[0], 0);
    bind(1, bloomTex_[0]); gl_.Uniform1i(comp_.loc[1], 1);
    gl_.Uniform2f(comp_.loc[2], float(tw_), float(th_));
    gl_.Uniform4f(comp_.loc[3], float(x0), float(gy0), float(x1), float(gy1));
    gl_.Uniform1f(comp_.loc[4], bloomOn_ ? 0.55f : 0.0f);
    gl_.DrawArrays(gl::TRIANGLES, 0, 3);
    swap();
  }
  if (havePost_ && postFbo_) {
    fullscreenPass(post_, dstFbo, tw_, th_, x0, gy0, x1, gy1);
    bind(0, srcTex); gl_.Uniform1i(post_.loc[0], 0);
    gl_.Uniform2f(post_.loc[1], float(tw_), float(th_));
    gl_.Uniform4f(post_.loc[2], float(x0), float(th_ - 1 - y1), float(rw), float(rh));
    gl_.DrawArrays(gl::TRIANGLES, 0, 3);
    swap();
  }
  gl_.ActiveTexture(gl::TEXTURE0);
  resultFbo_ = srcFbo;
}

void GlRenderer::drawFx(int mode, gl::GLuint tex, const std::vector<OverlayVertex>& v, bool additive, float life, float seed, float soft) {
  if (!ok_ || v.empty()) return;
  flush();
  if (!lightsReady_ && atlasScene_) prepareLighting(*atlasScene_);
  ensureDepthCopy();
  const gl::GLenum b0 = gl::COLOR_ATTACHMENT0;
  gl_.BindFramebuffer(gl::FRAMEBUFFER, renderFbo());
  gl_.DrawBuffers(1, &b0);
  gl_.Viewport(0, 0, tw_, th_);
  setScissor(vp_.x0, vp_.y0, vp_.x1, vp_.y1);
  gl_.Disable(gl::DEPTH_TEST);
  gl_.DepthMask(gl::FALSE_);
  gl_.Enable(gl::BLEND);
  if (additive) gl_.BlendFunc(gl::ONE, gl::ONE); else gl_.BlendFunc(gl::SRC_ALPHA, gl::ONE_MINUS_SRC_ALPHA);
  gl_.UseProgram(fxProg_.id);
  gl_.Uniform2f(fxProg_.loc[0], float(tw_), float(th_));
  gl_.ActiveTexture(gl::TEXTURE0);
  gl_.BindTexture(gl::TEXTURE_2D, tex ? tex : copyTex_);
  gl_.Uniform1i(fxProg_.loc[1], 0);
  gl_.ActiveTexture(gl::TEXTURE1);
  gl_.BindTexture(gl::TEXTURE_2D, copyTex_);
  gl_.Uniform1i(fxProg_.loc[2], 1);
  gl_.Uniform1i(fxProg_.loc[3], mode);
  gl_.Uniform1f(fxProg_.loc[4], life);
  gl_.Uniform1f(fxProg_.loc[5], seed);
  gl_.Uniform1f(fxProg_.loc[6], nearD_);
  gl_.Uniform1f(fxProg_.loc[7], soft);
  const LightingEnv* e = atlasScene_ ? &atlasScene_->env : nullptr;
  const float amb[3] = {e ? 0.5f * (e->skyAmbient[0] + e->groundAmbient[0]) * e->exposure : 0.7f, e ? 0.5f * (e->skyAmbient[1] + e->groundAmbient[1]) * e->exposure : 0.7f, e ? 0.5f * (e->skyAmbient[2] + e->groundAmbient[2]) * e->exposure : 0.7f};
  const float sunc[3] = {e ? e->sunColor[0] * e->exposure : 0.3f, e ? e->sunColor[1] * e->exposure : 0.3f, e ? e->sunColor[2] * e->exposure : 0.3f};
  gl_.Uniform3fv(fxProg_.loc[8], 1, sunCam_);
  gl_.Uniform3fv(fxProg_.loc[9], 1, sunc);
  gl_.Uniform3fv(fxProg_.loc[10], 1, amb);
  gl_.BindVertexArray(ovao_);
  gl_.BindBuffer(gl::ARRAY_BUFFER, ovbo_);
  gl_.BufferData(gl::ARRAY_BUFFER, gl::GLsizeiptr(v.size() * sizeof(OverlayVertex)), v.data(), gl::STREAM_DRAW);
  gl_.DrawArrays(gl::TRIANGLES, 0, gl::GLsizei(v.size()));
  gl_.Disable(gl::BLEND);
  gl_.ActiveTexture(gl::TEXTURE0);
}

void GlRenderer::drawFxSprite(FxKind kind, const Sprite& spr, const Palette& pal, const double world[3], double worldWidth, int transparent, float life01, float seed) {
  if (!fxOn_) { drawSpriteWorld(spr, pal, world, worldWidth, transparent); return; }
  float sx, sy, z;
  if (spr.w <= 0 || spr.h <= 0 || !projectToScreen(world, &sx, &sy, &z)) return;
  const float pw = float(worldWidth) / z * focal_;
  if (pw < 1.0f) return;
  const float ph = pw * float(spr.h) / float(spr.w);
  const float d = nearD_ / z;
  const gl::GLuint tex = spriteTexture(spr, pal, transparent);
  const float x0 = sx - pw * 0.5f, x1 = sx + pw * 0.5f, y0 = sy - ph * 0.5f, y1 = sy + ph * 0.5f;
  auto V = [&](float x, float y, float u, float v) { return OverlayVertex{x, y, d, u, v, 255, 255, 255, 255}; };
  const OverlayVertex a = V(x0, y0, 0, 0), b = V(x1, y0, 1, 0), c = V(x1, y1, 1, 1), e = V(x0, y1, 0, 1);
  drawFx(kind == FxKind::Smoke ? 0 : kind == FxKind::Mist ? 4 : 1, tex, {a, b, c, a, c, e}, kind == FxKind::Fire || kind == FxKind::Explosion, life01, seed, 0.5f * float(worldWidth));
}

void GlRenderer::drawSparkWorld(const double w[3], const double vel[3], double radius, double angle, uint32_t color, float life01, int kind) {
  if (!fxOn_) { drawStarWorld(w, radius, angle, color); return; }
  float sx, sy, z;
  if (!projectToScreen(w, &sx, &sy, &z)) return;
  const double tail[3] = {w[0] - vel[0] * 0.045, w[1] - vel[1] * 0.045, w[2] - vel[2] * 0.045};
  float tx, ty, tz;
  const bool haveTail = projectToScreen(tail, &tx, &ty, &tz);
  const float scale = float(w_) / 320.0f;
  uint8_t r, g, b;
  unpack(color, &r, &g, &b);
  const float d = nearD_ / z;
  auto V = [&](float x, float y, float u, float v) { return OverlayVertex{x, y, d, u, v, r, g, b, 255}; };
  float dx = haveTail ? tx - sx : 0.0f, dy = haveTail ? ty - sy : 0.0f;
  float len = std::sqrt(dx * dx + dy * dy);
  const float minLen = 1.5f * scale;
  if (len < minLen) { dx = 1.0f; dy = 0.0f; len = minLen; } else { dx /= len; dy /= len; }
  const float hw = std::max(0.9f * scale, float(radius) / z * focal_ * 0.35f);
  const float nx = -dy, ny = dx;
  // streak: head (u = 0) to tail (u = 1), v across
  const OverlayVertex a = V(sx + nx * hw, sy + ny * hw, 0, 1), bq = V(sx - nx * hw, sy - ny * hw, 0, 0), c = V(sx + dx * len - nx * hw, sy + dy * len - ny * hw, 1, 0), e = V(sx + dx * len + nx * hw, sy + dy * len + ny * hw, 1, 1);
  if (kind == 1) {  // a water droplet: a translucent lens with a glint (alpha blended), a little larger than a spark
    const float k = 1.7f;
    const OverlayVertex da = V(sx + nx * hw * k, sy + ny * hw * k, 0, 1), db = V(sx - nx * hw * k, sy - ny * hw * k, 0, 0), dc = V(sx + dx * len - nx * hw * 0.5f, sy + dy * len - ny * hw * 0.5f, 1, 0), dd = V(sx + dx * len + nx * hw * 0.5f, sy + dy * len + ny * hw * 0.5f, 1, 1);
    drawFx(5, 0, {da, db, dc, da, dc, dd}, false, life01, 0, 4000.0f);
    return;
  }
  drawFx(2, 0, {a, bq, c, a, c, e}, true, life01, 0, 4000.0f);
  const float gr = 3.2f * scale;  // small glow at the head
  const OverlayVertex g0 = V(sx - gr, sy - gr, 0, 0), g1 = V(sx + gr, sy - gr, 1, 0), g2 = V(sx + gr, sy + gr, 1, 1), g3 = V(sx - gr, sy + gr, 0, 1);
  drawFx(3, 0, {g0, g1, g2, g0, g2, g3}, true, life01, 0, 4000.0f);
}

void GlRenderer::finishScene() {
  if (!ok_ || !sceneActive_) return;
  sceneActive_ = false;
  SDL_GL_MakeCurrent(window_, ctx_);
  flush();
  int x0 = std::clamp(vp_.x0, 0, tw_ - 1), x1 = std::clamp(vp_.x1, 0, tw_ - 1), y0 = std::clamp(vp_.y0, 0, th_ - 1), y1 = std::clamp(vp_.y1, 0, th_ - 1);
  if (x1 < x0 || y1 < y0) return;
  const int rw = x1 - x0 + 1, rh = y1 - y0 + 1;
  if (msFbo_) {  // resolve the multisampled picture
    const gl::GLenum b0 = gl::COLOR_ATTACHMENT0;
    gl_.Disable(gl::SCISSOR_TEST);
    gl_.BindFramebuffer(gl::READ_FRAMEBUFFER, msFbo_);
    gl_.ReadBuffer(gl::COLOR_ATTACHMENT0);
    gl_.BindFramebuffer(gl::DRAW_FRAMEBUFFER, fbo_);
    gl_.DrawBuffers(1, &b0);
    gl_.BlitFramebuffer(x0, th_ - 1 - y1, x1 + 1, th_ - y0, x0, th_ - 1 - y1, x1 + 1, th_ - y0, gl::COLOR_BUFFER_BIT, gl::NEAREST);
  }
  resultFbo_ = fbo_;
  if ((hdr_ || havePost_) && postFbo_) runPost(x0, y0, x1, y1);
  std::vector<uint32_t> tmp(size_t(rw) * size_t(rh));
  gl_.Disable(gl::SCISSOR_TEST);
  gl_.BindFramebuffer(gl::READ_FRAMEBUFFER, resultFbo_);
  gl_.ReadBuffer(gl::COLOR_ATTACHMENT0);
  gl_.PixelStorei(gl::PACK_ALIGNMENT, 1);
  gl_.ReadPixels(x0, th_ - 1 - y1, rw, rh, gl::BGRA, gl::UNSIGNED_INT_8_8_8_8_REV, tmp.data());
  for (int r = 0; r < rh; ++r) {
    const uint32_t* from = &tmp[size_t(r) * size_t(rw)];
    uint32_t* to = &color_[size_t(y1 - r) * size_t(w_) + size_t(x0)];
    for (int c = 0; c < rw; ++c) to[c] = from[c] | 0xff000000u;
  }
}

}  // namespace slip
