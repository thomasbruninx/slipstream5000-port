// Renderer selection and the back end interface: names, fallbacks, the software back end's framebuffer. (The OpenGL back end needs a display; it is only checked for a clean fallback.)
#include <cstdio>

#include "renderer/renderer_factory.hpp"

using namespace slip;
static int failures = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); ++failures; } } while (0)

int main() {
  const auto names = rendererNames();
  CHECK(names.size() >= 2 && names[0] == "software" && names[1] == "opengl");
  std::string warn;
  auto sw = createRenderer("", {}, &warn);
  CHECK(sw && std::string(sw->name()) == "software" && warn.empty());
  sw->resize(64, 32);
  CHECK(sw->width() == 64 && sw->height() == 32 && sw->framebuffer() && sw->pixels() == sw->framebuffer());
  Camera cam;
  sw->beginFrame(cam, 0xff112233u, 0xff445566u);
  sw->finishScene();
  CHECK(sw->pixels()[0] == 0xff112233u || sw->pixels()[0] == 0xff445566u);  // backdrop
  auto bogus = createRenderer("no-such-renderer", {}, &warn);
  CHECK(bogus && std::string(bogus->name()) == "software" && !warn.empty());
  warn.clear();
  auto gl = createRenderer("OpenGL", {}, &warn);  // a working context or the software fallback with a reason
  CHECK(gl && (std::string(gl->name()) == "opengl" || (std::string(gl->name()) == "software" && !warn.empty())));
  if (std::string(gl->name()) == "opengl") {
    gl->resize(64, 32);
    gl->beginFrame(cam, 0xff112233u, 0xff445566u);
    gl->finishScene();
    CHECK(gl->pixels()[0] == 0xff112233u || gl->pixels()[0] == 0xff445566u);  // the same backdrop through the GPU
  }
  {  // filtering / anti-aliasing options never break the start: unknown values or unsupported sample counts fall back
    RendererOptions o;
    o.filter = "bilinear"; o.aa = 4;
    auto g2 = createRenderer("opengl", o, &warn);
    CHECK(g2);
    if (std::string(g2->name()) == "opengl") {
      g2->resize(64, 32);
      g2->beginFrame(cam, 0xff112233u, 0xff445566u);
      g2->finishScene();
      CHECK(g2->pixels()[0] == 0xff112233u || g2->pixels()[0] == 0xff445566u);
    }
    o.fx = true; o.lighting = "shadows";
    CHECK(createRenderer("opengl", o, &warn));
    o.filter = "bogus"; o.aa = 3;
    CHECK(createRenderer("opengl", o, &warn));
  }
  std::printf("renderer: %d failure(s)\n", failures);
  return failures ? 1 : 0;
}
