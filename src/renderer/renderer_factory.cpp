#include "renderer/renderer_factory.hpp"

#include <algorithm>
#include <cctype>
#include <cstdio>

#include "renderer/gl_renderer.hpp"
#include "renderer/software_renderer.hpp"

namespace slip {

std::vector<std::string> rendererNames() { return {"software", "opengl"}; }

std::unique_ptr<Renderer> createRenderer(const std::string& nameIn, const RendererOptions& opt, std::string* warning) {
  std::string name = nameIn;
  std::transform(name.begin(), name.end(), name.begin(), [](unsigned char c) { return char(std::tolower(c)); });
  if (name.empty() || name == "software" || name == "sw" || name == "cpu") return std::make_unique<SoftwareRenderer>();
  if (name == "opengl" || name == "gl") {
    auto r = std::make_unique<GlRenderer>();
    std::string err;
    GlOptions go;
    go.postShader = opt.shader;
    go.filter = opt.filter;
    go.msaa = opt.aa;
    go.lighting = opt.lighting;
    go.ao = opt.ao;
    go.fx = opt.fx;
    go.bloom = opt.bloom;
    if (r->init(go, &err)) {
      std::fprintf(stderr, "opengl renderer: %s\n", r->glInfo().c_str());
      return r;
    }
    if (warning) *warning = "OpenGL renderer unavailable (" + err + "), using the software renderer";
    return std::make_unique<SoftwareRenderer>();
  }
  if (warning) *warning = "unknown renderer '" + nameIn + "' (software, opengl), using the software renderer";
  return std::make_unique<SoftwareRenderer>();
}

}  // namespace slip
