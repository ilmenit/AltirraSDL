//	AltirraSDL - Bloom V2 GLSL shaders
//	Port of screenfx.fxh Bloom V2 passes.

// Bloom pass 1: sRGB to linear gamma conversion.
static const char kGLSL_BloomGamma_FS[] = R"glsl(
in vec2 vUV;
out vec4 fragColor;
uniform sampler2D uSourceTex;

vec3 SrgbToLinear(vec3 c) {
	vec3 lo = c / 12.92;
	vec3 hi = pow((c + 0.055) / 1.055, vec3(2.4));
	return mix(hi, lo, lessThan(c, vec3(0.04045)));
}

void main() {
	vec3 c = texture(uSourceTex, vUV).rgb;
	fragColor = vec4(SrgbToLinear(c), 0.0);
}
)glsl";

// Bloom downsample: 13-tap bilinear downsample filter.
// From Jiminez, "Next Generation Post Processing in Call of Duty: Advanced Warfare"
// Weights: corners 7/124, edges 16/124, center 32/124.
static const char kGLSL_BloomDown_FS[] = R"glsl(
in vec2 vUV;
out vec4 fragColor;
uniform sampler2D uSourceTex;
uniform vec2 uUVStep;  // half-texel step size

void main() {
	const float w1 = 7.0 / 124.0;   // corners
	const float w2 = 16.0 / 124.0;  // edges
	const float w3 = 32.0 / 124.0;  // center

	vec2 off = uUVStep * 1.75;

	vec3 c = vec3(0.0);

	// 4 corner samples
	c += texture(uSourceTex, vUV + vec2(-off.x, -off.y)).rgb * w1;
	c += texture(uSourceTex, vUV + vec2(+off.x, -off.y)).rgb * w1;
	c += texture(uSourceTex, vUV + vec2(-off.x, +off.y)).rgb * w1;
	c += texture(uSourceTex, vUV + vec2(+off.x, +off.y)).rgb * w1;

	// 4 edge samples
	c += texture(uSourceTex, vUV + vec2(0.0, -off.y)).rgb * w2;
	c += texture(uSourceTex, vUV + vec2(-off.x, 0.0)).rgb * w2;
	c += texture(uSourceTex, vUV + vec2(0.0, +off.y)).rgb * w2;
	c += texture(uSourceTex, vUV + vec2(+off.x, 0.0)).rgb * w2;

	// Center sample
	c += texture(uSourceTex, vUV).rgb * w3;

	fragColor = vec4(c, 0.0);
}
)glsl";

// Bloom upsample: 4-tap asymmetric filter with alpha-blended accumulation.
// Output alpha contains the blend factor for additive blending with the
// previous pyramid level.
static const char kGLSL_BloomUp_FS[] = R"glsl(
in vec2 vUV;
out vec4 fragColor;
uniform sampler2D uSourceTex;
uniform vec2 uUVStep;            // texel step of the SOURCE texture
uniform vec2 uTexSize;           // size of the SOURCE texture (for frac computation)
uniform vec2 uBlendFactors;      // x=bloom scale, y=accumulation blend

void main() {
	// Determine which quadrant of the texel we're in for kernel flip
	vec2 fracPos = fract(vUV * uTexSize);
	vec2 flipSign = mix(vec2(1.0), vec2(-1.0), greaterThanEqual(fracPos, vec2(0.5)));
	vec2 flippedStep = uUVStep * flipSign;

	// 4-tap asymmetric upsample kernel
	vec2 uvA = vUV + flippedStep * vec2(-0.75 - 1.0/6.0, -0.75 - 1.0/6.0);
	vec2 uvB = vUV + flippedStep * vec2(+0.25 + 0.3,     +0.25 + 0.3);

	vec3 c = vec3(0.0);
	c += texture(uSourceTex, vec2(uvA.x, uvA.y)).rgb * (36.0 / 256.0);
	c += texture(uSourceTex, vec2(uvB.x, uvA.y)).rgb * (60.0 / 256.0);
	c += texture(uSourceTex, vec2(uvA.x, uvB.y)).rgb * (60.0 / 256.0);
	c += texture(uSourceTex, vec2(uvB.x, uvB.y)).rgb * (100.0 / 256.0);

	fragColor = vec4(c * uBlendFactors.x, uBlendFactors.y);
}
)glsl";

// Bloom final composition: upsample + base filter + shoulder soft-clip + sRGB.
static const char kGLSL_BloomFinal_FS[] = R"glsl(
in vec2 vUV;
out vec4 fragColor;
uniform sampler2D uSourceTex;     // bloom pyramid (finest level)
uniform sampler2D uBaseTex;       // original emulator frame (linear)

uniform vec2 uUVStep;             // texel step for bloom source
uniform vec2 uTexSize;            // size of bloom source
uniform vec2 uBlendFactors;       // x=bloom scale, y=base mix (unused here)

uniform vec4 uShoulderCurve;      // cubic coefficients: a*x³ + b*x² + c*x + d
uniform vec4 uThresholds;         // x=midSlope, y=shoulderX, z=limitX
uniform vec2 uBaseUVStep;         // texel step for base 9-tap filter
uniform vec4 uBaseWeights;        // x=corners, y=sides, z=center

vec3 LinearToSrgb(vec3 c) {
	vec3 lo = c * 12.92;
	vec3 hi = 1.055 * pow(max(c, vec3(0.0)), vec3(1.0 / 2.4)) - 0.055;
	return mix(hi, lo, lessThan(c, vec3(0.0031308)));
}

void main() {
	// The bloom pipeline renders through multiple FBO passes using the
	// same Y-flipping fullscreen triangle VS.  Each FBO pass flips the
	// image orientation, so after an even number of passes a texture is
	// back in "source convention" (v=0 = frame top) and after an odd
	// number it is in "GL convention" (v=0 = frame bottom).
	//
	// In this final pass:
	//   uSourceTex = bloom pyramid[0] — 2 FBO passes from source → source convention
	//   uBaseTex   = bloomLinearFBO   — 1 FBO pass from source  → GL convention
	//
	// They have different Y orientations.  We flip the bloom pyramid UV
	// so both textures agree, producing a correctly oriented output.
	vec2 bloomUV = vec2(vUV.x, 1.0 - vUV.y);

	// Upsample from bloom pyramid (same kernel as BloomUp)
	vec2 fracPos = fract(bloomUV * uTexSize);
	vec2 flipSign = mix(vec2(1.0), vec2(-1.0), greaterThanEqual(fracPos, vec2(0.5)));
	vec2 flippedStep = uUVStep * flipSign;

	vec2 uvA = bloomUV + flippedStep * vec2(-0.75 - 1.0/6.0, -0.75 - 1.0/6.0);
	vec2 uvB = bloomUV + flippedStep * vec2(+0.25 + 0.3,     +0.25 + 0.3);

	vec3 c = vec3(0.0);
	c += texture(uSourceTex, vec2(uvA.x, uvA.y)).rgb * (36.0 / 256.0);
	c += texture(uSourceTex, vec2(uvB.x, uvA.y)).rgb * (60.0 / 256.0);
	c += texture(uSourceTex, vec2(uvA.x, uvB.y)).rgb * (60.0 / 256.0);
	c += texture(uSourceTex, vec2(uvB.x, uvB.y)).rgb * (100.0 / 256.0);

	// 9-tap base image filter (corner/side/center weighted)
	vec3 d1 = vec3(0.0);  // corners
	vec3 d2 = vec3(0.0);  // sides
	vec3 d3 = vec3(0.0);  // center

	d1 += texture(uBaseTex, vUV + uBaseUVStep * vec2(-1.0, -1.0)).rgb;
	d1 += texture(uBaseTex, vUV + uBaseUVStep * vec2(+1.0, -1.0)).rgb;
	d1 += texture(uBaseTex, vUV + uBaseUVStep * vec2(-1.0, +1.0)).rgb;
	d1 += texture(uBaseTex, vUV + uBaseUVStep * vec2(+1.0, +1.0)).rgb;
	d2 += texture(uBaseTex, vUV + uBaseUVStep * vec2(-1.0,  0.0)).rgb;
	d2 += texture(uBaseTex, vUV + uBaseUVStep * vec2(+1.0,  0.0)).rgb;
	d2 += texture(uBaseTex, vUV + uBaseUVStep * vec2( 0.0, -1.0)).rgb;
	d2 += texture(uBaseTex, vUV + uBaseUVStep * vec2( 0.0, +1.0)).rgb;
	d3 += texture(uBaseTex, vUV).rgb;

	vec3 d = d1 * uBaseWeights.x + d2 * uBaseWeights.y + d3 * uBaseWeights.z;

	// Combine bloom with base
	vec3 x = min(c * uBlendFactors.x + d, vec3(uThresholds.z));

	// Soft-clip shoulder curve
	vec3 mid = uThresholds.x * x;
	vec3 shoulder = ((uShoulderCurve.x * x + uShoulderCurve.y) * x
		+ uShoulderCurve.z) * x + uShoulderCurve.w;
	vec3 r = mix(mid, shoulder, greaterThan(x, vec3(uThresholds.y)));

	// Convert to sRGB
	fragColor = vec4(LinearToSrgb(max(r, vec3(0.0))), 1.0);
}
)glsl";
