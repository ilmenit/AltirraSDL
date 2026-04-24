//	AltirraSDL - OpenGL shader and FBO helper utilities

#pragma once

#ifdef ALTIRRA_WASM
// -----------------------------------------------------------------------
// WASM stubs: the GL display backend is not compiled for WebAssembly.
// All GL helper entry points and GL constants/functions used by UI code
// are provided as inline no-ops so callers link without change.
// -----------------------------------------------------------------------
#include <stdint.h>
typedef unsigned int GLuint;
typedef unsigned int GLenum;

// GL constants used by UI files that include gl_helpers.h
#define GL_TEXTURE_2D 0x0DE1u
#define GL_RGBA       0x1908u
#define GL_RGBA8      0x8058u
#define GL_UNSIGNED_BYTE 0x1401u

// Raw GL function stubs
inline void glDeleteTextures(int, const GLuint*) {}
inline void glBindTexture(GLenum, GLuint) {}

// gl_helpers.h public API stubs
inline GLuint GLCreateTexture2D(int, int, GLenum, GLenum, GLenum,
	const void *, bool = true) { return 0; }
inline GLuint GLCreateXRGB8888Texture(int, int, bool, const void *) { return 0; }
inline void   GLUploadXRGB8888(int, int, const void *, int = 0) {}

#else  // !ALTIRRA_WASM

// ---------------------------------------------------------------------------
// Shader preamble helpers
// ---------------------------------------------------------------------------
// The shaders_*.inl source fragments contain only the shader BODY (in/out
// declarations + main).  GLCompileShader / GLCreateProgram prepend the
// GLSL version + precision line appropriate to the active GL profile:
//   Desktop 3.3 Core  →  "#version 330 core\n"
//   GLES 3.0          →  "#version 300 es\nprecision highp float;\n
//                         precision highp int;\nprecision highp sampler2D;\n"
// This keeps a single shader source usable on both profiles without
// duplicating the body.

// Returns the version/precision preamble string for the active GL
// profile.  The returned pointer is valid for the process lifetime.
const char *GLGetShaderPreamble(GLenum shaderType);

// Compile a shader from source. Returns 0 on failure (logs errors to stderr).
// The profile-appropriate preamble (#version + precision) is automatically
// prepended; callers pass only the shader body.
GLuint GLCompileShader(GLenum type, const char *source);

// Compile a shader from multiple source strings concatenated together.
// The profile preamble is automatically prepended; do not include #version
// in any of the input strings.
GLuint GLCompileShaderMulti(GLenum type, const char *const *sources, int count);

// Link a vertex + fragment shader into a program. Returns 0 on failure.
// Detaches and deletes the shaders after linking.
GLuint GLLinkProgram(GLuint vs, GLuint fs);

// Compile and link a program from vertex and fragment source strings.
// Returns 0 on failure.
GLuint GLCreateProgram(const char *vsSrc, const char *fsSrc);

// Create a program from a vertex shader source and multiple fragment source
// strings (concatenated). Returns 0 on failure.
GLuint GLCreateProgramMultiFS(const char *vsSrc, const char *const *fsSources, int fsCount);

// Create an RGBA8 texture with the given dimensions and data (may be null).
GLuint GLCreateTexture2D(int w, int h, GLenum internalFormat, GLenum format,
	GLenum type, const void *data, bool linear = true);

// ---------------------------------------------------------------------------
// XRGB8888 pixel upload — profile-independent
// ---------------------------------------------------------------------------
// Altirra's emulator frame (and the screen-mask / debug / gallery textures)
// are produced in Windows DIB XRGB8888 layout: 32-bit little-endian
// DWORDs, so memory byte order is B,G,R,X.  Desktop GL can ingest this
// directly with {GL_BGRA, GL_UNSIGNED_INT_8_8_8_8_REV} — the native path
// on virtually every desktop GPU.  GLES 3.0 has neither GL_BGRA nor
// GL_UNSIGNED_INT_8_8_8_8_REV in core; the portable equivalent is
// {GL_RGBA, GL_UNSIGNED_BYTE} with a per-texture R↔B swizzle so shader
// samples still see correct colour channels.
//
// These helpers hide the per-profile choice behind one entry point so
// every call site — emulator frame, screen-mask LUT, gallery thumbnails,
// debugger bitmap/memory views, rewind thumbnails — stays identical on
// every target.

// Create an immutable-storage RGBA8 texture suitable for receiving
// XRGB8888-layout pixel data.  On GLES also sets the R↔B swizzle so
// sampler output matches desktop behaviour.  `data` may be null (content
// uploaded later via GLUploadXRGB8888).
GLuint GLCreateXRGB8888Texture(int w, int h, bool linear, const void *data);

// Upload a full or partial XRGB8888 image into a currently-bound
// GL_TEXTURE_2D target.  Caller has bound the target texture.  `pitch`
// is in bytes; pass 0 to use `w*4` (tight packing).
void GLUploadXRGB8888(int w, int h, const void *data, int pitch = 0);

// Enable or disable framebuffer-wide sRGB encoding.  On Desktop this
// toggles GL_FRAMEBUFFER_SRGB; on GLES 3.0 this is a no-op because the
// equivalent is controlled per-framebuffer-attachment by the attached
// texture's internal format (e.g. GL_SRGB8_ALPHA8).  Callers use this
// to undo librashader side-effects before ImGui rendering.
void GLSetFramebufferSRGB(bool enable);

// Create an FBO with a single color attachment texture.
// Returns the FBO id; sets *outTex to the color attachment texture.
GLuint GLCreateFBO(int w, int h, GLenum internalFormat, GLuint *outTex);

// FBO render target for the bloom pyramid.
struct GLRenderTarget {
	GLuint fbo = 0;
	GLuint tex = 0;
	int width = 0;
	int height = 0;

	void Create(int w, int h, GLenum internalFormat);
	void Destroy();
	void Bind() const;
};

// Draw a fullscreen triangle using the empty VAO.
// Assumes the VAO is already bound.
void GLDrawFullscreenTriangle();

#endif // ALTIRRA_WASM
