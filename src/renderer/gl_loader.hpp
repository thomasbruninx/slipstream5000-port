// Minimal OpenGL 3.3 core loader: the handful of entry points the OpenGL renderer needs, resolved through SDL_GL_GetProcAddress (no GL headers or loader library required, the
// same code works on macOS, Linux and Windows). Constants and function pointer types are declared here.
#pragma once
#include <cstddef>
#include <cstdint>

namespace slip::gl {

using GLenum = unsigned int;
using GLuint = unsigned int;
using GLint = int;
using GLsizei = int;
using GLboolean = unsigned char;
using GLbitfield = unsigned int;
using GLfloat = float;
using GLchar = char;
using GLintptr = std::ptrdiff_t;
using GLsizeiptr = std::ptrdiff_t;

enum : GLenum {
  FALSE_ = 0, TRUE_ = 1,
  POINTS = 0x0000, LINES = 0x0001, TRIANGLES = 0x0004, TRIANGLE_STRIP = 0x0005,
  DEPTH_BUFFER_BIT = 0x00000100, COLOR_BUFFER_BIT = 0x00004000,
  SRC_ALPHA = 0x0302, ONE_MINUS_SRC_ALPHA = 0x0303,
  CULL_FACE = 0x0B44, DEPTH_TEST = 0x0B71, SCISSOR_TEST = 0x0C11, BLEND = 0x0BE2,
  GREATER = 0x0204, GEQUAL = 0x0206, ALWAYS = 0x0207,
  UNPACK_ALIGNMENT = 0x0CF5, PACK_ALIGNMENT = 0x0D05,
  UNSIGNED_BYTE = 0x1401, FLOAT = 0x1406, UNSIGNED_INT_8_8_8_8_REV = 0x8367,
  RED = 0x1903, RGBA = 0x1908, BGRA = 0x80E1,
  NEAREST = 0x2600, LINEAR = 0x2601, TEXTURE_MAG_FILTER = 0x2800, TEXTURE_MIN_FILTER = 0x2801, TEXTURE_WRAP_S = 0x2802, TEXTURE_WRAP_T = 0x2803,
  REPEAT = 0x2901, CLAMP_TO_EDGE = 0x812F,
  TEXTURE_2D = 0x0DE1, TEXTURE0 = 0x84C0, TEXTURE1 = 0x84C1,
  RGBA8 = 0x8058, RGBA16F = 0x881A, R32F = 0x822E, DEPTH_COMPONENT32F = 0x8CAC, DEPTH_ATTACHMENT = 0x8D00,
  ARRAY_BUFFER = 0x8892, STREAM_DRAW = 0x88E0, DYNAMIC_DRAW = 0x88E8,
  FRAGMENT_SHADER = 0x8B30, VERTEX_SHADER = 0x8B31, COMPILE_STATUS = 0x8B81, LINK_STATUS = 0x8B82, INFO_LOG_LENGTH = 0x8B84,
  FRAMEBUFFER = 0x8D40, READ_FRAMEBUFFER = 0x8CA8, DRAW_FRAMEBUFFER = 0x8CA9, RENDERBUFFER = 0x8D41,
  COLOR_ATTACHMENT0 = 0x8CE0, COLOR_ATTACHMENT1 = 0x8CE1, FRAMEBUFFER_COMPLETE = 0x8CD5,
  MAX_SAMPLES = 0x8D57, VERSION = 0x1F02, RENDERER = 0x1F01, COLOR = 0x1800, DEPTH = 0x1801,
};

#define SLIP_GL_FUNCS(X) \
  X(void, Enable, GLenum) X(void, Disable, GLenum) X(void, Viewport, GLint, GLint, GLsizei, GLsizei) X(void, Scissor, GLint, GLint, GLsizei, GLsizei) \
  X(void, ClearColor, GLfloat, GLfloat, GLfloat, GLfloat) X(void, ClearDepth, double) X(void, Clear, GLbitfield) X(void, DepthFunc, GLenum) X(void, DepthMask, GLboolean) \
  X(void, ColorMask, GLboolean, GLboolean, GLboolean, GLboolean) X(void, BlendFunc, GLenum, GLenum) \
  X(void, PixelStorei, GLenum, GLint) X(void, ReadPixels, GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, void*) X(const unsigned char*, GetString, GLenum) \
  X(void, GenTextures, GLsizei, GLuint*) X(void, DeleteTextures, GLsizei, const GLuint*) X(void, BindTexture, GLenum, GLuint) X(void, TexParameteri, GLenum, GLenum, GLint) \
  X(void, TexImage2D, GLenum, GLint, GLint, GLsizei, GLsizei, GLint, GLenum, GLenum, const void*) X(void, TexSubImage2D, GLenum, GLint, GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, const void*) \
  X(void, ActiveTexture, GLenum) \
  X(void, GenFramebuffers, GLsizei, GLuint*) X(void, DeleteFramebuffers, GLsizei, const GLuint*) X(void, BindFramebuffer, GLenum, GLuint) \
  X(void, FramebufferTexture2D, GLenum, GLenum, GLenum, GLuint, GLint) X(GLenum, CheckFramebufferStatus, GLenum) \
  X(void, GenRenderbuffers, GLsizei, GLuint*) X(void, DeleteRenderbuffers, GLsizei, const GLuint*) X(void, BindRenderbuffer, GLenum, GLuint) \
  X(void, RenderbufferStorage, GLenum, GLenum, GLsizei, GLsizei) X(void, FramebufferRenderbuffer, GLenum, GLenum, GLenum, GLuint) \
  X(void, DrawBuffers, GLsizei, const GLenum*) X(void, ReadBuffer, GLenum) X(void, BlitFramebuffer, GLint, GLint, GLint, GLint, GLint, GLint, GLint, GLint, GLbitfield, GLenum) \
  X(void, ClearBufferfv, GLenum, GLint, const GLfloat*) \
  X(GLuint, CreateShader, GLenum) X(void, ShaderSource, GLuint, GLsizei, const GLchar* const*, const GLint*) X(void, CompileShader, GLuint) \
  X(void, GetShaderiv, GLuint, GLenum, GLint*) X(void, GetShaderInfoLog, GLuint, GLsizei, GLsizei*, GLchar*) X(void, DeleteShader, GLuint) \
  X(GLuint, CreateProgram) X(void, AttachShader, GLuint, GLuint) X(void, LinkProgram, GLuint) X(void, GetProgramiv, GLuint, GLenum, GLint*) \
  X(void, GetProgramInfoLog, GLuint, GLsizei, GLsizei*, GLchar*) X(void, UseProgram, GLuint) X(void, DeleteProgram, GLuint) \
  X(GLint, GetUniformLocation, GLuint, const GLchar*) X(void, Uniform1i, GLint, GLint) X(void, Uniform1f, GLint, GLfloat) X(void, Uniform2f, GLint, GLfloat, GLfloat) \
  X(void, Uniform3f, GLint, GLfloat, GLfloat, GLfloat) X(void, Uniform4f, GLint, GLfloat, GLfloat, GLfloat, GLfloat) X(void, Uniform1fv, GLint, GLsizei, const GLfloat*) \
  X(void, Uniform3fv, GLint, GLsizei, const GLfloat*) X(void, Uniform4fv, GLint, GLsizei, const GLfloat*) \
  X(void, GenVertexArrays, GLsizei, GLuint*) X(void, BindVertexArray, GLuint) X(void, DeleteVertexArrays, GLsizei, const GLuint*) \
  X(void, GenBuffers, GLsizei, GLuint*) X(void, DeleteBuffers, GLsizei, const GLuint*) X(void, BindBuffer, GLenum, GLuint) X(void, BufferData, GLenum, GLsizeiptr, const void*, GLenum) \
  X(void, EnableVertexAttribArray, GLuint) X(void, VertexAttribPointer, GLuint, GLint, GLenum, GLboolean, GLsizei, const void*) \
  X(void, RenderbufferStorageMultisample, GLenum, GLsizei, GLenum, GLsizei, GLsizei) X(void, GetIntegerv, GLenum, GLint*) \
  X(void, DrawArrays, GLenum, GLint, GLsizei) X(void, Finish) X(GLenum, GetError)

struct Api {
#define X(ret, name, ...) ret (*name)(__VA_ARGS__) = nullptr;
  SLIP_GL_FUNCS(X)
#undef X
};

// Resolves every function through SDL_GL_GetProcAddress (a GL context must be current). Returns false and names the first missing function in *missing.
bool loadApi(Api* api, const char** missing);

}  // namespace slip::gl
