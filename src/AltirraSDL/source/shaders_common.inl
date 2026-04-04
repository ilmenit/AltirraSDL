//	AltirraSDL - Common GLSL shader sources
//	Fullscreen triangle vertex shader and sRGB conversion utilities.

// Fullscreen triangle vertex shader.
// Generates a triangle that covers [-1,+1] clip space from gl_VertexID.
// UV output covers [0,1] with Y=0 at top (matching SDL/ImGui convention).
static const char kGLSL_FullscreenTriangleVS[] = R"glsl(
#version 330 core
out vec2 vUV;
void main() {
	// Generate oversized triangle from vertex ID (0,1,2)
	float x = float((gl_VertexID & 1) << 2) - 1.0;
	float y = float((gl_VertexID & 2) << 1) - 1.0;
	gl_Position = vec4(x, y, 0.0, 1.0);
	// UV: [0,1] with Y flipped (top=0) to match texture convention
	vUV = vec2(x * 0.5 + 0.5, 1.0 - (y * 0.5 + 0.5));
}
)glsl";

// sRGB conversion functions (shared by multiple shaders).
static const char kGLSL_SRGBUtils[] = R"glsl(
vec3 SrgbToLinear(vec3 c) {
	vec3 lo = c / 12.92;
	vec3 hi = pow((c + 0.055) / 1.055, vec3(2.4));
	return mix(hi, lo, lessThan(c, vec3(0.04045)));
}

vec3 LinearToSrgb(vec3 c) {
	vec3 lo = c * 12.92;
	vec3 hi = 1.055 * pow(c, vec3(1.0 / 2.4)) - 0.055;
	return mix(hi, lo, lessThan(c, vec3(0.0031308)));
}
)glsl";

// Simple passthrough fragment shader — texture sampling only.
static const char kGLSL_PassthroughFS[] = R"glsl(
#version 330 core
in vec2 vUV;
out vec4 fragColor;
uniform sampler2D uSourceTex;
void main() {
	fragColor = vec4(texture(uSourceTex, vUV).rgb, 1.0);
}
)glsl";
