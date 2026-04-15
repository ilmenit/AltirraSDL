//	AltirraSDL - OpenGL function loader implementation

#include <stdafx.h>
#include <SDL3/SDL.h>
#include "gl_funcs.h"
#include <stdio.h>
#include "logging.h"

// Active GL profile set by GLLoadFunctions().  Consulted by shader
// compile helpers and texture upload helpers to choose GLSL version
// strings and pixel transfer formats.
namespace {
	GLProfile g_activeProfile = GLProfile::Desktop33;
}

GLProfile GLGetActiveProfile() { return g_activeProfile; }
void GLSetActiveProfile(GLProfile profile) { g_activeProfile = profile; }

// Define all function pointer globals.
// macOS: framework exports symbols directly (no indirection).
// Android: GLESv3 library exports symbols directly (no indirection).
#if !defined(__APPLE__) && !defined(__ANDROID__)
#define GL_FUNC(ret, name, ...) PFN_##name name = nullptr;
#else
#define GL_FUNC(ret, name, ...)
#endif

GL_FUNC(void, glEnable, GLenum cap)
GL_FUNC(void, glDisable, GLenum cap)
GL_FUNC(void, glViewport, GLint x, GLint y, GLsizei w, GLsizei h)
GL_FUNC(void, glScissor, GLint x, GLint y, GLsizei w, GLsizei h)
GL_FUNC(void, glClearColor, GLfloat r, GLfloat g, GLfloat b, GLfloat a)
GL_FUNC(void, glClear, GLbitfield mask)
GL_FUNC(void, glBlendFunc, GLenum sfactor, GLenum dfactor)
GL_FUNC(void, glBlendEquation, GLenum mode)
GL_FUNC(void, glDrawArrays, GLenum mode, GLint first, GLsizei count)
GL_FUNC(void, glPixelStorei, GLenum pname, GLint param)
GL_FUNC(void, glReadPixels, GLint x, GLint y, GLsizei w, GLsizei h, GLenum format, GLenum type, void *data)
GL_FUNC(GLenum, glGetError, void)
GL_FUNC(const GLubyte*, glGetString, GLenum name)
GL_FUNC(void, glGetIntegerv, GLenum pname, GLint *params)
GL_FUNC(void, glPolygonMode, GLenum face, GLenum mode)
GL_FUNC(void, glColorMask, GLboolean r, GLboolean g, GLboolean b, GLboolean a)
GL_FUNC(void, glGenTextures, GLsizei n, GLuint *textures)
GL_FUNC(void, glDeleteTextures, GLsizei n, const GLuint *textures)
GL_FUNC(void, glBindTexture, GLenum target, GLuint texture)
GL_FUNC(void, glActiveTexture, GLenum texture)
GL_FUNC(void, glTexImage2D, GLenum target, GLint level, GLint internalformat, GLsizei w, GLsizei h, GLint border, GLenum format, GLenum type, const void *data)
GL_FUNC(void, glTexStorage2D, GLenum target, GLsizei levels, GLenum internalformat, GLsizei w, GLsizei h)
GL_FUNC(void, glTexSubImage2D, GLenum target, GLint level, GLint xoff, GLint yoff, GLsizei w, GLsizei h, GLenum format, GLenum type, const void *data)
GL_FUNC(void, glTexParameteri, GLenum target, GLenum pname, GLint param)
GL_FUNC(GLuint, glCreateShader, GLenum type)
GL_FUNC(void, glDeleteShader, GLuint shader)
GL_FUNC(void, glShaderSource, GLuint shader, GLsizei count, const GLchar *const*string, const GLint *length)
GL_FUNC(void, glCompileShader, GLuint shader)
GL_FUNC(void, glGetShaderiv, GLuint shader, GLenum pname, GLint *params)
GL_FUNC(void, glGetShaderInfoLog, GLuint shader, GLsizei maxLen, GLsizei *len, GLchar *log)
GL_FUNC(GLuint, glCreateProgram, void)
GL_FUNC(void, glDeleteProgram, GLuint program)
GL_FUNC(void, glAttachShader, GLuint program, GLuint shader)
GL_FUNC(void, glLinkProgram, GLuint program)
GL_FUNC(void, glUseProgram, GLuint program)
GL_FUNC(void, glGetProgramiv, GLuint program, GLenum pname, GLint *params)
GL_FUNC(void, glGetProgramInfoLog, GLuint program, GLsizei maxLen, GLsizei *len, GLchar *log)
GL_FUNC(GLint, glGetUniformLocation, GLuint program, const GLchar *name)
GL_FUNC(void, glUniform1i, GLint location, GLint v0)
GL_FUNC(void, glUniform1f, GLint location, GLfloat v0)
GL_FUNC(void, glUniform2f, GLint location, GLfloat v0, GLfloat v1)
GL_FUNC(void, glUniform3f, GLint location, GLfloat v0, GLfloat v1, GLfloat v2)
GL_FUNC(void, glUniform4f, GLint location, GLfloat v0, GLfloat v1, GLfloat v2, GLfloat v3)
GL_FUNC(void, glUniform2fv, GLint location, GLsizei count, const GLfloat *value)
GL_FUNC(void, glUniform4fv, GLint location, GLsizei count, const GLfloat *value)
GL_FUNC(void, glUniformMatrix3fv, GLint location, GLsizei count, GLboolean transpose, const GLfloat *value)
GL_FUNC(void, glGenVertexArrays, GLsizei n, GLuint *arrays)
GL_FUNC(void, glDeleteVertexArrays, GLsizei n, const GLuint *arrays)
GL_FUNC(void, glBindVertexArray, GLuint array)
GL_FUNC(void, glGenFramebuffers, GLsizei n, GLuint *fbos)
GL_FUNC(void, glDeleteFramebuffers, GLsizei n, const GLuint *fbos)
GL_FUNC(void, glBindFramebuffer, GLenum target, GLuint fbo)
GL_FUNC(void, glFramebufferTexture2D, GLenum target, GLenum attachment, GLenum textarget, GLuint texture, GLint level)
GL_FUNC(GLenum, glCheckFramebufferStatus, GLenum target)
GL_FUNC(void, glBlitFramebuffer, GLint srcX0, GLint srcY0, GLint srcX1, GLint srcY1, GLint dstX0, GLint dstY0, GLint dstX1, GLint dstY1, GLbitfield mask, GLenum filter)
GL_FUNC(void, glDrawBuffers, GLsizei n, const GLenum *bufs)

#undef GL_FUNC

#ifdef __APPLE__
// glTexStorage2D is GL 4.2, not available in Apple's gl3.h — proc-load it.
PFN_glTexStorage2D glTexStorage2D = nullptr;
#endif

bool GLLoadFunctions(GLProfile profile) {
	bool ok = true;
	g_activeProfile = profile;

#if !defined(__APPLE__) && !defined(__ANDROID__)
#define GL_LOAD(name) do { \
	name = (PFN_##name)SDL_GL_GetProcAddress(#name); \
	if (!name) { LOG_ERROR("GL", "Failed to load: %s", #name); ok = false; } \
} while(0)
#else
#define GL_LOAD(name) /* system headers/linker provide 'name' directly */
#endif

	GL_LOAD(glEnable);
	GL_LOAD(glDisable);
	GL_LOAD(glViewport);
	GL_LOAD(glScissor);
	GL_LOAD(glClearColor);
	GL_LOAD(glClear);
	GL_LOAD(glBlendFunc);
	GL_LOAD(glBlendEquation);
	GL_LOAD(glDrawArrays);
	GL_LOAD(glPixelStorei);
	GL_LOAD(glReadPixels);
	GL_LOAD(glGetError);
	GL_LOAD(glGetString);
	GL_LOAD(glGetIntegerv);
	// glPolygonMode is optional — absent on macOS core profile AND on
	// GLES 3.0.  ImGui uses it for wireframe debug mode only; silently
	// skip if missing.  Nothing in AltirraSDL's own rendering depends on
	// it, so a null pointer is safe.
#if !defined(__APPLE__) && !defined(__ANDROID__)
	glPolygonMode = (PFN_glPolygonMode)SDL_GL_GetProcAddress("glPolygonMode");
#endif
	GL_LOAD(glColorMask);
	GL_LOAD(glGenTextures);
	GL_LOAD(glDeleteTextures);
	GL_LOAD(glBindTexture);
	GL_LOAD(glActiveTexture);
	GL_LOAD(glTexImage2D);
	GL_LOAD(glTexStorage2D);
	GL_LOAD(glTexSubImage2D);
	GL_LOAD(glTexParameteri);
	GL_LOAD(glCreateShader);
	GL_LOAD(glDeleteShader);
	GL_LOAD(glShaderSource);
	GL_LOAD(glCompileShader);
	GL_LOAD(glGetShaderiv);
	GL_LOAD(glGetShaderInfoLog);
	GL_LOAD(glCreateProgram);
	GL_LOAD(glDeleteProgram);
	GL_LOAD(glAttachShader);
	GL_LOAD(glLinkProgram);
	GL_LOAD(glUseProgram);
	GL_LOAD(glGetProgramiv);
	GL_LOAD(glGetProgramInfoLog);
	GL_LOAD(glGetUniformLocation);
	GL_LOAD(glUniform1i);
	GL_LOAD(glUniform1f);
	GL_LOAD(glUniform2f);
	GL_LOAD(glUniform3f);
	GL_LOAD(glUniform4f);
	GL_LOAD(glUniform2fv);
	GL_LOAD(glUniform4fv);
	GL_LOAD(glUniformMatrix3fv);
	GL_LOAD(glGenVertexArrays);
	GL_LOAD(glDeleteVertexArrays);
	GL_LOAD(glBindVertexArray);
	GL_LOAD(glGenFramebuffers);
	GL_LOAD(glDeleteFramebuffers);
	GL_LOAD(glBindFramebuffer);
	GL_LOAD(glFramebufferTexture2D);
	GL_LOAD(glCheckFramebufferStatus);
	GL_LOAD(glBlitFramebuffer);
	GL_LOAD(glDrawBuffers);

#undef GL_LOAD

#ifdef __APPLE__
	// glTexStorage2D is GL 4.2 — not in macOS gl3.h, must proc-load explicitly.
	glTexStorage2D = (PFN_glTexStorage2D)SDL_GL_GetProcAddress("glTexStorage2D");
	if (!glTexStorage2D) { LOG_ERROR("GL", "Failed to load: glTexStorage2D"); ok = false; }
#endif

	return ok;
}
