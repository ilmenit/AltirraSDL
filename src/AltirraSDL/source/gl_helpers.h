//	AltirraSDL - OpenGL shader and FBO helper utilities

#pragma once

#include "gl_funcs.h"
#include <vector>

// Compile a shader from source. Returns 0 on failure (logs errors to stderr).
GLuint GLCompileShader(GLenum type, const char *source);

// Compile a shader from multiple source strings concatenated together.
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
