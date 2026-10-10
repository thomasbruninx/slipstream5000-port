#include "renderer/gl_loader.hpp"

#include <SDL3/SDL.h>

namespace slip::gl {

bool loadApi(Api* api, const char** missing) {
#define X(ret, name, ...)                                                                                      \
  api->name = reinterpret_cast<ret (*)(__VA_ARGS__)>(reinterpret_cast<void*>(SDL_GL_GetProcAddress("gl" #name))); \
  if (!api->name) { if (missing) *missing = "gl" #name; return false; }
  SLIP_GL_FUNCS(X)
#undef X
  return true;
}

}  // namespace slip::gl
