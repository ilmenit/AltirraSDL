//	AltirraSDL - OpenGL shader and FBO helper utilities implementation

#include <stdafx.h>
#include "gl_helpers.h"
#include <stdio.h>
#include <string.h>

GLuint GLCompileShader(GLenum type, const char *source) {
	GLuint shader = glCreateShader(type);
	glShaderSource(shader, 1, &source, nullptr);
	glCompileShader(shader);

	GLint status;
	glGetShaderiv(shader, GL_COMPILE_STATUS, &status);
	if (!status) {
		char log[1024];
		glGetShaderInfoLog(shader, sizeof(log), nullptr, log);
		fprintf(stderr, "[GL] Shader compile error (%s):\n%s\n",
			type == GL_VERTEX_SHADER ? "vertex" : "fragment", log);
		glDeleteShader(shader);
		return 0;
	}
	return shader;
}

GLuint GLCompileShaderMulti(GLenum type, const char *const *sources, int count) {
	GLuint shader = glCreateShader(type);
	glShaderSource(shader, count, sources, nullptr);
	glCompileShader(shader);

	GLint status;
	glGetShaderiv(shader, GL_COMPILE_STATUS, &status);
	if (!status) {
		char log[1024];
		glGetShaderInfoLog(shader, sizeof(log), nullptr, log);
		fprintf(stderr, "[GL] Shader compile error (%s):\n%s\n",
			type == GL_VERTEX_SHADER ? "vertex" : "fragment", log);
		glDeleteShader(shader);
		return 0;
	}
	return shader;
}

GLuint GLLinkProgram(GLuint vs, GLuint fs) {
	GLuint program = glCreateProgram();
	glAttachShader(program, vs);
	glAttachShader(program, fs);
	glLinkProgram(program);

	GLint status;
	glGetProgramiv(program, GL_LINK_STATUS, &status);
	if (!status) {
		char log[1024];
		glGetProgramInfoLog(program, sizeof(log), nullptr, log);
		fprintf(stderr, "[GL] Program link error:\n%s\n", log);
		glDeleteProgram(program);
		program = 0;
	}

	// Delete shaders regardless of success (they're ref-counted by the program)
	glDeleteShader(vs);
	glDeleteShader(fs);

	return program;
}

GLuint GLCreateProgram(const char *vsSrc, const char *fsSrc) {
	GLuint vs = GLCompileShader(GL_VERTEX_SHADER, vsSrc);
	if (!vs) return 0;

	GLuint fs = GLCompileShader(GL_FRAGMENT_SHADER, fsSrc);
	if (!fs) { glDeleteShader(vs); return 0; }

	return GLLinkProgram(vs, fs);
}

GLuint GLCreateProgramMultiFS(const char *vsSrc, const char *const *fsSources, int fsCount) {
	GLuint vs = GLCompileShader(GL_VERTEX_SHADER, vsSrc);
	if (!vs) return 0;

	GLuint fs = GLCompileShaderMulti(GL_FRAGMENT_SHADER, fsSources, fsCount);
	if (!fs) { glDeleteShader(vs); return 0; }

	return GLLinkProgram(vs, fs);
}

GLuint GLCreateTexture2D(int w, int h, GLenum internalFormat, GLenum format,
	GLenum type, const void *data, bool linear)
{
	GLuint tex;
	glGenTextures(1, &tex);
	glBindTexture(GL_TEXTURE_2D, tex);
	glTexImage2D(GL_TEXTURE_2D, 0, internalFormat, w, h, 0, format, type, data);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, linear ? GL_LINEAR : GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, linear ? GL_LINEAR : GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	return tex;
}

GLuint GLCreateFBO(int w, int h, GLenum internalFormat, GLuint *outTex) {
	GLuint tex = GLCreateTexture2D(w, h, internalFormat, GL_RGBA, GL_UNSIGNED_BYTE, nullptr, true);

	GLuint fbo;
	glGenFramebuffers(1, &fbo);
	glBindFramebuffer(GL_FRAMEBUFFER, fbo);
	glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, tex, 0);

	GLenum fbStatus = glCheckFramebufferStatus(GL_FRAMEBUFFER);
	if (fbStatus != GL_FRAMEBUFFER_COMPLETE) {
		fprintf(stderr, "[GL] FBO incomplete: 0x%04X\n", fbStatus);
	}

	glBindFramebuffer(GL_FRAMEBUFFER, 0);

	if (outTex)
		*outTex = tex;
	return fbo;
}

void GLRenderTarget::Create(int w, int h, GLenum internalFormat) {
	Destroy();
	width = w;
	height = h;
	fbo = GLCreateFBO(w, h, internalFormat, &tex);
}

void GLRenderTarget::Destroy() {
	if (fbo) { glDeleteFramebuffers(1, &fbo); fbo = 0; }
	if (tex) { glDeleteTextures(1, &tex); tex = 0; }
	width = 0;
	height = 0;
}

void GLRenderTarget::Bind() const {
	glBindFramebuffer(GL_FRAMEBUFFER, fbo);
	glViewport(0, 0, width, height);
}

void GLDrawFullscreenTriangle() {
	glDrawArrays(GL_TRIANGLES, 0, 3);
}
