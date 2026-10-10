// Renderer selection (--renderer NAME). Every back end implements slip::Renderer (renderer.hpp); the game only talks to that interface.
#pragma once
#include <memory>
#include <string>
#include <vector>

#include "renderer/renderer.hpp"

namespace slip {

struct RendererOptions {
  std::string shader;  // --shader: post-processing shader of the OpenGL renderer ("" = none)
  std::string filter = "nearest";  // --filter: texture filtering of the OpenGL renderer (nearest | bilinear | smooth)
  std::string lighting = "off";  // --lighting: real-time lighting of the OpenGL renderer (off | lights | shadows)
  bool fx = false;     // --fx: effects shaders of the OpenGL renderer (water, soft particles, spark streaks)
  bool ao = true, bloom = true;  // --no-ao / --no-bloom: with lighting on, ambient occlusion and bloom
  int aa = 0;          // --aa: multisample anti-aliasing of the OpenGL renderer (0 = off, 2, 4, 8)
};

// Names of the back ends compiled in ("software", "opengl", ...).
std::vector<std::string> rendererNames();
// Creates the renderer called `name` (case-insensitive; "" = software). An unknown or unusable back end (for example no OpenGL 3.3) falls back to the software renderer and says why in *warning.
std::unique_ptr<Renderer> createRenderer(const std::string& name, const RendererOptions& opt, std::string* warning);

}  // namespace slip
