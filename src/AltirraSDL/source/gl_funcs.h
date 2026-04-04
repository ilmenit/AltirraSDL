//	AltirraSDL - OpenGL 3.3 core function declarations
//	Loaded at runtime via SDL_GL_GetProcAddress.

#pragma once

// Include the standard GL type definitions
#if defined(__APPLE__)
	#include <OpenGL/gl3.h>
#else
	// Minimal GL type definitions for platforms without system GL headers
	#ifndef __gl_h_
	#define __gl_h_

	#include <stdint.h>
	#include <stddef.h>

	typedef unsigned int GLenum;
	typedef unsigned char GLboolean;
	typedef unsigned int GLbitfield;
	typedef void GLvoid;
	typedef int8_t GLbyte;
	typedef uint8_t GLubyte;
	typedef int16_t GLshort;
	typedef uint16_t GLushort;
	typedef int GLint;
	typedef unsigned int GLuint;
	typedef int GLsizei;
	typedef float GLfloat;
	typedef double GLdouble;
	typedef char GLchar;
	typedef ptrdiff_t GLsizeiptr;
	typedef ptrdiff_t GLintptr;

	// GL constants
	#define GL_FALSE                          0
	#define GL_TRUE                           1
	#define GL_NONE                           0
	#define GL_NO_ERROR                       0

	// Data types
	#define GL_BYTE                           0x1400
	#define GL_UNSIGNED_BYTE                  0x1401
	#define GL_SHORT                          0x1402
	#define GL_UNSIGNED_SHORT                 0x1403
	#define GL_INT                            0x1404
	#define GL_UNSIGNED_INT                   0x1405
	#define GL_FLOAT                          0x1406
	#define GL_HALF_FLOAT                     0x140B

	// Pixel formats
	#define GL_RED                            0x1903
	#define GL_GREEN                          0x1904
	#define GL_BLUE                           0x1905
	#define GL_ALPHA                          0x1906
	#define GL_RGB                            0x1907
	#define GL_RGBA                           0x1908
	#define GL_BGRA                           0x80E1
	#define GL_UNSIGNED_INT_8_8_8_8_REV       0x8367

	// Internal formats
	#define GL_R8                             0x8229
	#define GL_RG8                            0x822B
	#define GL_RGB8                           0x8051
	#define GL_RGBA8                          0x8058
	#define GL_RGBA16F                        0x881A
	#define GL_SRGB8_ALPHA8                   0x8C43

	// Texture targets and parameters
	#define GL_TEXTURE_2D                     0x0DE1
	#define GL_TEXTURE_MIN_FILTER             0x2801
	#define GL_TEXTURE_MAG_FILTER             0x2800
	#define GL_TEXTURE_WRAP_S                 0x2802
	#define GL_TEXTURE_WRAP_T                 0x2803
	#define GL_NEAREST                        0x2600
	#define GL_LINEAR                         0x2601
	#define GL_NEAREST_MIPMAP_LINEAR          0x2702
	#define GL_LINEAR_MIPMAP_LINEAR           0x2703
	#define GL_CLAMP_TO_EDGE                  0x812F
	#define GL_REPEAT                         0x2901
	#define GL_TEXTURE0                       0x84C0
	#define GL_MAX_TEXTURE_IMAGE_UNITS        0x8872

	// Framebuffer
	#define GL_FRAMEBUFFER                    0x8D40
	#define GL_READ_FRAMEBUFFER               0x8CA8
	#define GL_DRAW_FRAMEBUFFER               0x8CA9
	#define GL_COLOR_ATTACHMENT0              0x8CE0
	#define GL_FRAMEBUFFER_COMPLETE           0x8CD5
	#define GL_DEPTH_ATTACHMENT               0x8D00

	// Buffer objects
	#define GL_ARRAY_BUFFER                   0x8892
	#define GL_ELEMENT_ARRAY_BUFFER           0x8893
	#define GL_STATIC_DRAW                    0x88E4
	#define GL_DYNAMIC_DRAW                   0x88E8

	// Shader types
	#define GL_FRAGMENT_SHADER                0x8B30
	#define GL_VERTEX_SHADER                  0x8B31
	#define GL_COMPILE_STATUS                 0x8B81
	#define GL_LINK_STATUS                    0x8B82
	#define GL_INFO_LOG_LENGTH                0x8B84

	// Draw modes
	#define GL_POINTS                         0x0000
	#define GL_LINES                          0x0001
	#define GL_TRIANGLES                      0x0004
	#define GL_TRIANGLE_STRIP                 0x0005

	// State
	#define GL_BLEND                          0x0BE2
	#define GL_DEPTH_TEST                     0x0B71
	#define GL_CULL_FACE                      0x0B44
	#define GL_SCISSOR_TEST                   0x0C11
	#define GL_STENCIL_TEST                   0x0B90
	#define GL_DITHER                         0x0BD0

	// Blend
	#define GL_ZERO                           0
	#define GL_ONE                            1
	#define GL_SRC_ALPHA                      0x0302
	#define GL_ONE_MINUS_SRC_ALPHA            0x0303
	#define GL_DST_ALPHA                      0x0304
	#define GL_ONE_MINUS_DST_ALPHA            0x0305
	#define GL_FUNC_ADD                       0x8006

	// Clear
	#define GL_COLOR_BUFFER_BIT               0x00004000
	#define GL_DEPTH_BUFFER_BIT               0x00000100
	#define GL_STENCIL_BUFFER_BIT             0x00000400

	// Misc
	#define GL_VIEWPORT                       0x0BA2
	#define GL_FRONT_AND_BACK                 0x0408
	#define GL_FILL                           0x1B02
	#define GL_VERSION                        0x1F02
	#define GL_RENDERER                       0x1F01
	#define GL_VENDOR                         0x1F00

	// Pixel store
	#define GL_UNPACK_ROW_LENGTH              0x0CF2
	#define GL_UNPACK_ALIGNMENT               0x0CF5
	#define GL_PACK_ROW_LENGTH                0x0D02
	#define GL_PACK_ALIGNMENT                 0x0D05

	#endif // __gl_h_
#endif // __APPLE__

// Function pointer type declarations
#define GL_FUNC(ret, name, ...) typedef ret (GLAPIENTRY *PFN_##name)(__VA_ARGS__);
#ifndef GLAPIENTRY
	#ifdef _WIN32
		#define GLAPIENTRY __stdcall
	#else
		#define GLAPIENTRY
	#endif
#endif

// Core GL 1.x-2.x functions (available on all GL 3.3 core contexts)
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

// Texture functions
GL_FUNC(void, glGenTextures, GLsizei n, GLuint *textures)
GL_FUNC(void, glDeleteTextures, GLsizei n, const GLuint *textures)
GL_FUNC(void, glBindTexture, GLenum target, GLuint texture)
GL_FUNC(void, glActiveTexture, GLenum texture)
GL_FUNC(void, glTexImage2D, GLenum target, GLint level, GLint internalformat, GLsizei w, GLsizei h, GLint border, GLenum format, GLenum type, const void *data)
GL_FUNC(void, glTexSubImage2D, GLenum target, GLint level, GLint xoff, GLint yoff, GLsizei w, GLsizei h, GLenum format, GLenum type, const void *data)
GL_FUNC(void, glTexParameteri, GLenum target, GLenum pname, GLint param)

// Shader functions
GL_FUNC(GLuint, glCreateShader, GLenum type)
GL_FUNC(void, glDeleteShader, GLuint shader)
GL_FUNC(void, glShaderSource, GLuint shader, GLsizei count, const GLchar *const*string, const GLint *length)
GL_FUNC(void, glCompileShader, GLuint shader)
GL_FUNC(void, glGetShaderiv, GLuint shader, GLenum pname, GLint *params)
GL_FUNC(void, glGetShaderInfoLog, GLuint shader, GLsizei maxLen, GLsizei *len, GLchar *log)

// Program functions
GL_FUNC(GLuint, glCreateProgram, void)
GL_FUNC(void, glDeleteProgram, GLuint program)
GL_FUNC(void, glAttachShader, GLuint program, GLuint shader)
GL_FUNC(void, glLinkProgram, GLuint program)
GL_FUNC(void, glUseProgram, GLuint program)
GL_FUNC(void, glGetProgramiv, GLuint program, GLenum pname, GLint *params)
GL_FUNC(void, glGetProgramInfoLog, GLuint program, GLsizei maxLen, GLsizei *len, GLchar *log)

// Uniform functions
GL_FUNC(GLint, glGetUniformLocation, GLuint program, const GLchar *name)
GL_FUNC(void, glUniform1i, GLint location, GLint v0)
GL_FUNC(void, glUniform1f, GLint location, GLfloat v0)
GL_FUNC(void, glUniform2f, GLint location, GLfloat v0, GLfloat v1)
GL_FUNC(void, glUniform3f, GLint location, GLfloat v0, GLfloat v1, GLfloat v2)
GL_FUNC(void, glUniform4f, GLint location, GLfloat v0, GLfloat v1, GLfloat v2, GLfloat v3)
GL_FUNC(void, glUniform2fv, GLint location, GLsizei count, const GLfloat *value)
GL_FUNC(void, glUniform4fv, GLint location, GLsizei count, const GLfloat *value)
GL_FUNC(void, glUniformMatrix3fv, GLint location, GLsizei count, GLboolean transpose, const GLfloat *value)

// VAO functions
GL_FUNC(void, glGenVertexArrays, GLsizei n, GLuint *arrays)
GL_FUNC(void, glDeleteVertexArrays, GLsizei n, const GLuint *arrays)
GL_FUNC(void, glBindVertexArray, GLuint array)

// FBO functions
GL_FUNC(void, glGenFramebuffers, GLsizei n, GLuint *fbos)
GL_FUNC(void, glDeleteFramebuffers, GLsizei n, const GLuint *fbos)
GL_FUNC(void, glBindFramebuffer, GLenum target, GLuint fbo)
GL_FUNC(void, glFramebufferTexture2D, GLenum target, GLenum attachment, GLenum textarget, GLuint texture, GLint level)
GL_FUNC(GLenum, glCheckFramebufferStatus, GLenum target)
GL_FUNC(void, glDrawBuffers, GLsizei n, const GLenum *bufs)

#undef GL_FUNC

// Global function pointers — loaded by GLLoadFunctions()
#define GL_FUNC(ret, name, ...) extern PFN_##name name;

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
GL_FUNC(void, glGenTextures, GLsizei n, GLuint *textures)
GL_FUNC(void, glDeleteTextures, GLsizei n, const GLuint *textures)
GL_FUNC(void, glBindTexture, GLenum target, GLuint texture)
GL_FUNC(void, glActiveTexture, GLenum texture)
GL_FUNC(void, glTexImage2D, GLenum target, GLint level, GLint internalformat, GLsizei w, GLsizei h, GLint border, GLenum format, GLenum type, const void *data)
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
GL_FUNC(void, glDrawBuffers, GLsizei n, const GLenum *bufs)

#undef GL_FUNC

// Load all GL function pointers via SDL_GL_GetProcAddress.
// Must be called after SDL_GL_CreateContext. Returns true on success.
bool GLLoadFunctions();
