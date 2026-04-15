//	AltirraSDL - OpenGL shader and FBO helper utilities implementation

#include <stdafx.h>
#include "gl_helpers.h"
#include <stdio.h>
#include <string.h>
#include <vector>
#include "logging.h"

// ---------------------------------------------------------------------------
// Shader preamble
// ---------------------------------------------------------------------------
// Fragment shaders on GLES 3.0 MUST declare a default precision for
// float/int/sampler2D; Desktop GL tolerates precision qualifiers but
// doesn't require them.  We always emit them on GLES and keep Desktop
// free of them to avoid any "precision qualifier in #version 330"
// compatibility concerns.  The string is returned as a single constant
// pointer per (profile, shader-type) combination so callers can splice
// it directly in front of the shader body.

static const char kPreamble_Desktop33[] =
	"#version 330 core\n";

static const char kPreamble_ES30_VS[] =
	"#version 300 es\n"
	"precision highp float;\n"
	"precision highp int;\n";

static const char kPreamble_ES30_FS[] =
	"#version 300 es\n"
	"precision highp float;\n"
	"precision highp int;\n"
	"precision highp sampler2D;\n";

const char *GLGetShaderPreamble(GLenum shaderType) {
	if (GLGetActiveProfile() == GLProfile::ES30)
		return (shaderType == GL_FRAGMENT_SHADER) ? kPreamble_ES30_FS
		                                          : kPreamble_ES30_VS;
	return kPreamble_Desktop33;
}

GLuint GLCompileShader(GLenum type, const char *source) {
	const char *preamble = GLGetShaderPreamble(type);
	const char *sources[2] = { preamble, source };
	GLuint shader = glCreateShader(type);
	glShaderSource(shader, 2, sources, nullptr);
	glCompileShader(shader);

	GLint status;
	glGetShaderiv(shader, GL_COMPILE_STATUS, &status);
	if (!status) {
		char log[1024];
		glGetShaderInfoLog(shader, sizeof(log), nullptr, log);
		LOG_ERROR("GL", "Shader compile error (%s):\n%s", type == GL_VERTEX_SHADER ? "vertex" : "fragment", log);
		glDeleteShader(shader);
		return 0;
	}
	return shader;
}

GLuint GLCompileShaderMulti(GLenum type, const char *const *sources, int count) {
	const char *preamble = GLGetShaderPreamble(type);

	// Build [preamble, sources[0], sources[1], ...] in a local array so we
	// make exactly one glShaderSource call (matches pre-refactor behaviour).
	std::vector<const char *> all;
	all.reserve(count + 1);
	all.push_back(preamble);
	for (int i = 0; i < count; ++i)
		all.push_back(sources[i]);

	GLuint shader = glCreateShader(type);
	glShaderSource(shader, (GLsizei)all.size(), all.data(), nullptr);
	glCompileShader(shader);

	GLint status;
	glGetShaderiv(shader, GL_COMPILE_STATUS, &status);
	if (!status) {
		char log[1024];
		glGetShaderInfoLog(shader, sizeof(log), nullptr, log);
		LOG_ERROR("GL", "Shader compile error (%s):\n%s", type == GL_VERTEX_SHADER ? "vertex" : "fragment", log);
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
		LOG_ERROR("GL", "Program link error:\n%s", log);
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
	// Use immutable storage (glTexStorage2D) — required by librashader which
	// attaches textures to its own internal FBOs.  Mutable textures created
	// with glTexImage2D cause framebuffer incompleteness in librashader's
	// GL3.3 path on some drivers.
	glTexStorage2D(GL_TEXTURE_2D, 1, internalFormat, w, h);
	if (data)
		glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, w, h, format, type, data);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, linear ? GL_LINEAR : GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, linear ? GL_LINEAR : GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	return tex;
}

// ---------------------------------------------------------------------------
// XRGB8888 pixel upload
// ---------------------------------------------------------------------------
// Per-profile selection of the pixel transfer format.  On Desktop we use
// the native BGRA/REV path; on GLES we pick RGBA/UNSIGNED_BYTE and the
// texture has R↔B swizzle applied so the shader sees correct channels.

static void GLGetXRGB8888TransferParams(GLenum *outFormat, GLenum *outType) {
	if (GLGetActiveProfile() == GLProfile::ES30) {
		*outFormat = GL_RGBA;
		*outType   = GL_UNSIGNED_BYTE;
	} else {
		*outFormat = GL_BGRA;
		*outType   = GL_UNSIGNED_INT_8_8_8_8_REV;
	}
}

GLuint GLCreateXRGB8888Texture(int w, int h, bool linear, const void *data) {
	GLuint tex;
	glGenTextures(1, &tex);
	glBindTexture(GL_TEXTURE_2D, tex);
	glTexStorage2D(GL_TEXTURE_2D, 1, GL_RGBA8, w, h);

	if (GLGetActiveProfile() == GLProfile::ES30) {
		// GLES samples RGBA-ordered memory as (R,G,B,A).  The source is
		// BGRX, so we remap the sampler's R←B and B←R to restore RGB
		// order.  A and G are identity.  Swizzle state is per-texture
		// and persists for this texture's lifetime.
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_SWIZZLE_R, GL_BLUE);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_SWIZZLE_G, GL_GREEN);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_SWIZZLE_B, GL_RED);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_SWIZZLE_A, GL_ALPHA);
	}

	if (data)
		GLUploadXRGB8888(w, h, data, 0);

	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, linear ? GL_LINEAR : GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, linear ? GL_LINEAR : GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	return tex;
}

void GLUploadXRGB8888(int w, int h, const void *data, int pitch) {
	GLenum format, type;
	GLGetXRGB8888TransferParams(&format, &type);

	if (pitch > 0 && pitch != w * 4)
		glPixelStorei(GL_UNPACK_ROW_LENGTH, pitch / 4);
	glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, w, h, format, type, data);
	if (pitch > 0 && pitch != w * 4)
		glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
}

void GLSetFramebufferSRGB(bool enable) {
	// GLES 3.0 has no framebuffer-wide sRGB toggle — sRGB encoding is
	// controlled per-attachment by the texture's internal format.  Since
	// all our bloom/screen-FX textures are GL_RGBA8 (linear), nothing
	// needs to be disabled on GLES.  Desktop GL does expose the toggle
	// and some librashader presets flip it on; we must restore a known
	// state before ImGui rendering.
	if (GLGetActiveProfile() == GLProfile::ES30)
		return;
#ifdef GL_FRAMEBUFFER_SRGB
	if (enable)
		glEnable(GL_FRAMEBUFFER_SRGB);
	else
		glDisable(GL_FRAMEBUFFER_SRGB);
#else
	(void)enable;
#endif
}

GLuint GLCreateFBO(int w, int h, GLenum internalFormat, GLuint *outTex) {
	// Choose appropriate format/type pair for the internal format.
	// Data pointer is null so these only matter for format compatibility.
	GLenum format = GL_RGBA;
	GLenum type = GL_UNSIGNED_BYTE;
	if (internalFormat == GL_RGBA16F
#ifdef GL_RGBA32F
		|| internalFormat == GL_RGBA32F
#endif
	) {
		type = GL_FLOAT;
	}

	GLuint tex = GLCreateTexture2D(w, h, internalFormat, format, type, nullptr, true);

	GLuint fbo;
	glGenFramebuffers(1, &fbo);
	glBindFramebuffer(GL_FRAMEBUFFER, fbo);
	glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, tex, 0);

	GLenum fbStatus = glCheckFramebufferStatus(GL_FRAMEBUFFER);
	if (fbStatus != GL_FRAMEBUFFER_COMPLETE) {
		LOG_INFO("GL", "FBO incomplete: 0x%04X (internalFormat=0x%04X, %dx%d)", fbStatus, internalFormat, w, h);
		glBindFramebuffer(GL_FRAMEBUFFER, 0);
		glDeleteFramebuffers(1, &fbo);
		glDeleteTextures(1, &tex);
		if (outTex)
			*outTex = 0;
		return 0;
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
