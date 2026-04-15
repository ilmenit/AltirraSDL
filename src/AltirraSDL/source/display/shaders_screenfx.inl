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
//	  #define FEAT_VIGNETTE         - Radial edge darkening

static const char kGLSL_ScreenFX_FS[] = R"glsl(
in vec2 vUV;
out vec4 fragColor;

uniform sampler2D uSourceTex;    // Emulator frame

#ifdef FEAT_GAMMA
uniform sampler2D uGammaTex;     // 256x1 gamma ramp LUT
#endif

#ifdef FEAT_SCANLINES
uniform sampler2D uScanlineTex;  // 1xH scanline mask
#endif

// UV transform for scanline/mask lookup — maps viewport UV to window UV.
// Needed whenever scanlines or mask are active.
#if defined(FEAT_SCANLINES) || defined(FEAT_DOT_MASK)
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

#ifdef FEAT_VIGNETTE
uniform float uVignetteIntensity;  // 0 = off, 1 = full darkening at corners
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
	// This is the screen-to-image (inverse) mapping: given a screen
	// position, compute the source image UV.  The forward mapping
	// (image-to-screen) is used in the D3D9 vertex shader; the inverse
	// is needed here because we work per-fragment.
	//
	// See VDDisplayDistortionMapping::MapScreenToImage() for derivation.
	{
		vec2 v = uv - vec2(0.5);
		vec2 v2 = v * uDistortionScales.xy;
		float d = max(1e-5, uDistortionScales.z - dot(v2, v2));
		uv = v / sqrt(d) + vec2(0.5);
	}
	// uvScanMask stays as the original screen UV — the mask/scanline
	// pattern is screen-aligned and must not follow the distortion.

	// Discard pixels outside the distorted image boundary
	if (uv.x < 0.0 || uv.x > 1.0 || uv.y < 0.0 || uv.y > 1.0) {
		fragColor = vec4(0.0, 0.0, 0.0, 1.0);
		return;
	}
#endif

	// Map viewport UV [0,1] to window-space UV for scanline/mask textures.
	// In D3D9 this is always computed in the vertex shader; here it must
	// run whenever scanlines or mask are active.
#if defined(FEAT_SCANLINES) || defined(FEAT_DOT_MASK)
	uvScanMask = uvScanMask * uScanlineInfo.xy + uScanlineInfo.zw;
#endif

#ifdef FEAT_SHARP
	// Sharp bilinear: snap to nearest source pixel center with controlled blend.
	// uv is in normalized [0,1] space; convert to texel space first.
	// uSharpnessInfo.zw = (1/srcW, 1/srcH) for converting back to normalized UV.
	vec2 texUV = uv / uSharpnessInfo.zw;  // texel space [0, srcW] x [0, srcH]
	vec2 f = floor(texUV + 0.5);
	vec2 d = f - texUV;
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
		// Get source texture size (e.g., 320x240) – kept for context, not strictly required
		ivec2 srcSize = textureSize(uSourceTex, 0);

		// Fragment position in window (viewport) pixel coordinates.
		// gl_FragCoord.xy gives pixel coordinates relative to the lower-left corner.
		vec2 fragCoordPx = gl_FragCoord.xy;

		// Size of the mask texture (e.g., 3 for RGB triad, 4 for 2x2 aperture)
		ivec2 maskSize = textureSize(uMaskTex, 0);

		// Compute mask pixel index by wrapping screen pixel coordinates
		// modulo the mask size. This ensures perfect pixel alignment.
		vec2 maskCoord = mod(fragCoordPx, vec2(maskSize));

		// Normalize to [0,1] range for texture sampling
		vec2 maskUV = maskCoord / vec2(maskSize);

		// Sample mask color
		vec3 maskColor = texture(uMaskTex, maskUV).rgb;

		c *= maskColor;
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

#ifdef FEAT_VIGNETTE
	// Radial edge darkening.  vUV is the post-distortion screen UV; we
	// want vignetting aligned to the physical screen so the corners
	// always darken regardless of barrel warp.  dot(v,v)*4 is 1.0 at an
	// edge midpoint and 2.0 at a corner; uVignetteIntensity scales how
	// aggressively the corner is darkened.
	{
		vec2 vc = vUV - vec2(0.5);
		float vd = dot(vc, vc) * 4.0;
		c *= clamp(1.0 - uVignetteIntensity * vd, 0.0, 1.0);
	}
#endif

	fragColor = vec4(c, 1.0);
}
)glsl";

// PAL artifacting fragment shader.
// Samples at two UV offsets and blends chroma (luminance-preserving).
static const char kGLSL_PALArtifacting_FS[] = R"glsl(
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
