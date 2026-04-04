//	AltirraSDL - Screen effects GLSL fragment shader
//	Port of screenfx.fxh FP_ScreenFX with compile-time #define toggles.
//
//	Feature toggles (prepended before this source):
//	  #define FEAT_SHARP            - Sharp bilinear sampling
//	  #define FEAT_SCANLINES        - Scanline mask
//	  #define FEAT_GAMMA            - Gamma correction via lookup texture
//	  #define FEAT_COLOR_CORRECT    - Color correction matrix
//	  #define FEAT_CC_SRGB          - sRGB linearization (else gamma 2.2)
//	  #define FEAT_DOT_MASK         - Screen dot/aperture/slot mask
//	  #define FEAT_DISTORTION       - CRT barrel distortion (UV warp)

static const char kGLSL_ScreenFX_FS[] = R"glsl(
#version 330 core

in vec2 vUV;
out vec4 fragColor;

uniform sampler2D uSourceTex;    // Emulator frame

#ifdef FEAT_GAMMA
uniform sampler2D uGammaTex;     // 256x1 gamma ramp LUT
#endif

#ifdef FEAT_SCANLINES
uniform sampler2D uScanlineTex;  // 1xH scanline mask
uniform vec4 uScanlineInfo;      // xy = UV scale, zw = UV offset
#endif

#ifdef FEAT_DOT_MASK
uniform sampler2D uMaskTex;      // Screen mask texture
#endif

#ifdef FEAT_SHARP
uniform vec4 uSharpnessInfo;     // xy = snap scale, zw = UV prescale
#endif

#ifdef FEAT_COLOR_CORRECT
uniform vec4 uColorCorrectM0;    // row 0 of 3x3 matrix + w=pre-bias
uniform vec4 uColorCorrectM1;    // row 1 of 3x3 matrix + w=pre-bias
uniform vec3 uColorCorrectM2;    // row 2 of 3x3 matrix
#endif

#ifdef FEAT_DISTORTION
uniform vec3 uDistortionScales;  // x=scaleX, y=scaleY, z=sqRadius
uniform vec4 uImageUVSize;       // xy=image UV size, zw=image UV offset
#endif

// sRGB utilities (only needed for color correction)
#if defined(FEAT_COLOR_CORRECT) && defined(FEAT_CC_SRGB)
vec3 SrgbToLinear(vec3 c) {
	vec3 lo = c / 12.92;
	vec3 hi = pow((c + 0.055) / 1.055, vec3(2.4));
	return mix(hi, lo, lessThan(c, vec3(0.04045)));
}
#endif

void main() {
	vec2 uv = vUV;
	vec2 uvScanMask = vUV;

#ifdef FEAT_DISTORTION
	// CRT barrel distortion — fragment shader UV warp
	// Map from screen [0,1] to centered [-0.5,+0.5]
	vec2 v = uv - vec2(0.5);
	vec2 v2 = v * uDistortionScales.xy;
	float r2 = dot(v2, v2);
	float scale = sqrt(uDistortionScales.z / (1.0 + r2));
	uv = v * scale + vec2(0.5);
	uvScanMask = uv;

	// Discard pixels outside the distorted image boundary
	if (uv.x < 0.0 || uv.x > 1.0 || uv.y < 0.0 || uv.y > 1.0) {
		fragColor = vec4(0.0, 0.0, 0.0, 1.0);
		return;
	}
#endif

#ifdef FEAT_SCANLINES
	uvScanMask = uvScanMask * uScanlineInfo.xy + uScanlineInfo.zw;
#endif

#ifdef FEAT_SHARP
	// Sharp bilinear: snap to nearest source pixel center with controlled blend
	vec2 f = floor(uv + 0.5);
	vec2 d = f - uv;
	uv = (f - clamp(d * uSharpnessInfo.xy + 0.5, 0.0, 1.0) + 0.5) * uSharpnessInfo.zw;
#endif

	vec3 c = texture(uSourceTex, uv).rgb;

#ifdef FEAT_COLOR_CORRECT
	// Apply pre-bias
	c = max(vec3(0.0), c * uColorCorrectM1.w + uColorCorrectM0.w);

	// Linearize
	#ifdef FEAT_CC_SRGB
		c = SrgbToLinear(c);
	#else
		c = pow(c, vec3(2.2));
	#endif

	// Color matrix transform (columns packed into uniform rows)
	vec3 corrected;
	corrected.r = dot(c, uColorCorrectM0.rgb);
	corrected.g = dot(c, uColorCorrectM1.rgb);
	corrected.b = dot(c, uColorCorrectM2);
	c = corrected;

	#ifdef FEAT_DOT_MASK
		c *= texture(uMaskTex, uvScanMask).rgb;
	#endif
#endif

#ifdef FEAT_GAMMA
	// Per-channel gamma lookup — texture is 256 texels wide
	float gammaScale = 255.0 / 256.0;
	float gammaBias = 0.5 / 256.0;
	c.r = texture(uGammaTex, vec2(c.r * gammaScale + gammaBias, 0.0)).r;
	c.g = texture(uGammaTex, vec2(c.g * gammaScale + gammaBias, 0.0)).r;
	c.b = texture(uGammaTex, vec2(c.b * gammaScale + gammaBias, 0.0)).r;
#endif

#ifdef FEAT_SCANLINES
	// Scanline mask multiplication (gamma-corrected mask already baked)
	vec3 scanMask = texture(uScanlineTex, uvScanMask).rgb;
	c *= scanMask;
#endif

	fragColor = vec4(c, 1.0);
}
)glsl";

// PAL artifacting fragment shader.
// Samples at two UV offsets and blends chroma (luminance-preserving).
static const char kGLSL_PALArtifacting_FS[] = R"glsl(
#version 330 core

in vec2 vUV;
out vec4 fragColor;

uniform sampler2D uSourceTex;
uniform vec2 uChromaOffset;  // UV offset for chroma-shifted sample
uniform bool uSignedRGB;     // true if palette uses signed encoding

void main() {
	vec3 c = texture(uSourceTex, vUV).rgb;
	vec3 c2 = texture(uSourceTex, vUV + uChromaOffset).rgb;

	// Blend chroma only (preserve luminance)
	vec3 chromaBias = c2 - c;
	chromaBias -= dot(chromaBias, vec3(0.30, 0.59, 0.11));
	c += chromaBias * 0.5;

	if (uSignedRGB) {
		// Expand signed palette from [-0.5, 1.5] to [0, 1]
		const float scale = 255.0 / 127.0;
		const float bias = 64.0 / 255.0;
		c = c * scale - bias * scale;
	}

	fragColor = vec4(c, 1.0);
}
)glsl";
