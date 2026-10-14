//	Altirra - Atari 800/800XL/5200 emulator
//	Copyright (C) 2009-2018 Avery Lee
//
//	This program is free software; you can redistribute it and/or modify
//	it under the terms of the GNU General Public License as published by
//	the Free Software Foundation; either version 2 of the License, or
//	(at your option) any later version.
//
//	This program is distributed in the hope that it will be useful,
//	but WITHOUT ANY WARRANTY; without even the implied warranty of
//	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//	GNU General Public License for more details.
//
//	You should have received a copy of the GNU General Public License along
//	with this program. If not, see <http://www.gnu.org/licenses/>.

#if ENGINE_D3D9
	#define SCREENFX_SAMPLE_SRC(uv)			(half4(tex2D(samp0, uv)))
	#define SCREENFX_SAMPLE_BASE(uv)		(half4(tex2D(samp1, uv)))
	#define SCREENFX_SAMPLE_GAMMA(uv)		(half4(tex2D(samp1, uv)))
	#define SCREENFX_SAMPLE_SCANLINE(uv)	(half4(tex2D(samp2, uv)))
	#define SCREENFX_SAMPLE_MASK(uv)		(half4(tex2D(samp3, uv)))
	#define VP_APPLY_VIEWPORT(oPos)

	#define SCREENFX_REG(spc, reg)			: register(spc, reg)

	#define SV_Position POSITION
#else
	#define SCREENFX_SAMPLE_SRC(uv)			SAMPLE2D(srctex, srcsamp, uv)
	#define SCREENFX_SAMPLE_BASE(uv)		SAMPLE2D(basetex, basesamp, uv)
	#define SCREENFX_SAMPLE_GAMMA(uv)		SAMPLE2D(gammatex, gammasamp, uv)
	#define SCREENFX_SAMPLE_SCANLINE(uv)	SAMPLE2D(scanlinetex, scanlinesamp, uv)
	#define SCREENFX_SAMPLE_MASK(uv)		SAMPLE2D(masktex, masksamp, uv)

	#define SCREENFX_REG(spc, reg)

	extern Texture2D scanlinetex : register(t1);
	extern SamplerState scanlinesamp : register(s1);

	extern Texture2D gammatex : register(t2);
	extern SamplerState gammasamp : register(s2);

	extern Texture2D masktex : register(t3);
	extern SamplerState masksamp : register(s3);
#endif

///////////////////////////////////////////////////////////////////////////

cbuffer VS_ScreenFX : register(b0) {
	float4 vsScanlineInfo		SCREENFX_REG(vs, c16);
	float4 vsMaskInfo			SCREENFX_REG(vs, c17);
	float4 vsDistortionInfo		SCREENFX_REG(vs, c18);
	float4 vsDistortionScales	SCREENFX_REG(vs, c19);
	float4 vsSrcUVArea			SCREENFX_REG(vs, c20);
};

cbuffer PS_ScreenFX : register(b0) {
	float4 psSharpnessInfo		SCREENFX_REG(ps, c16);
	float4 psDistortionScales	SCREENFX_REG(ps, c17);
	float4 psImageUVSize		SCREENFX_REG(ps, c18);
	float4 colorCorrectMatrix0	SCREENFX_REG(ps, c19);
	float4 colorCorrectMatrix1	SCREENFX_REG(ps, c20);
	float3 colorCorrectMatrix2	SCREENFX_REG(ps, c21);
};

void VP_PALArtifacting(
	float2 pos : POSITION,
	float2 uv0 : TEXCOORD0,
	float2 uv1 : TEXCOORD1,
	out float4 oPos : SV_Position,
	out float2 oT0 : TEXCOORD0,
	out float2 oT1 : TEXCOORD1)
{
	oPos = float4(pos.xy, 0.5, 1);
	oT0 = uv0;
	oT1 = uv1;
	
	VP_APPLY_VIEWPORT(oPos);
}

half4 FP_PALArtifacting_ExtendedOutput(
	float4 pos : SV_Position,
	float2 uv0 : TEXCOORD0,
	float2 uv1 : TEXCOORD1) : SV_Target0
{
	// Apply PAL chroma blending.
	half3 c = SCREENFX_SAMPLE_SRC(uv0).rgb;
	half3 c2 = SCREENFX_SAMPLE_SRC(uv1).rgb;

	// blend chroma only
	half3 chromaBias = c2 - c;

	chromaBias -= dot(chromaBias, half3(0.30, 0.59, 0.11));

	c += chromaBias * 0.5h;
	return half4(c, 0);
}

half4 FP_PALArtifacting_NormalOutput(
	float4 pos : SV_Position,
	float2 uv0 : TEXCOORD0,
	float2 uv1 : TEXCOORD1) : SV_Target0
{
	half3 c = FP_PALArtifacting_ExtendedOutput(pos, uv0, uv1).rgb;

	// Expand signed palette from [-0.5, 1.5] to [0, 1].
	const float scale = 255.0f / 127.0f;
	const float bias = 64.0f / 255.0f;

	c = c * scale - bias * scale;

	return half4(c, 0);
}

/////////////////////////////////////////////////////////////////////////////

void VP_ScreenFX(
	float2 pos : POSITION,
	float2 uv0 : TEXCOORD0,
	float2 uv1 : TEXCOORD1,
	out float4 oPos : SV_Position,
	out float2 oTImage : TEXCOORD0,
	out float2 oTScan : TEXCOORD1,
	out float2 oTMask : TEXCOORD2)
{
	oTImage = uv1 * vsSrcUVArea.xy + vsSrcUVArea.wz;

	float2 v = uv1 - float2(0.5f, 0.5f);
	float2 v2 = v * vsDistortionScales.xy;
	float2 uvDistorted = v * sqrt(vsDistortionScales.z / (1.0f + dot(v2, v2))) + float2(0.5f, 0.5f);

	oTScan = uv1 * vsScanlineInfo.xy + vsScanlineInfo.wz;
	oTMask = uvDistorted * vsMaskInfo.xy + vsMaskInfo.wz;

	oPos = float4(pos.xy, 0.5, 1);
	
	VP_APPLY_VIEWPORT(oPos);
}

half4 FP_ScreenFX(
	float4 pos,
	float2 uv,
	float2 uvScan,
	float2 uvMask,
	uniform bool doSharp,
	uniform bool doScanlines,
	uniform bool doGammaCorrection,
	uniform bool doColorCorrection,
	uniform bool doColorCorrectionSRGB,
	uniform bool doDotMask)
{
	half3 c;

	// do stretchblt
	if (doSharp) {
		float2 f = floor(uv + 0.5f);
		float2 d = (f - uv);
	
		uv = (f - saturate(d * psSharpnessInfo.xy + 0.5f) + 0.5f) * psSharpnessInfo.wz;
	}

	c = SCREENFX_SAMPLE_SRC(uv).rgb;
	
	// Apply gamma correction or color correction.
	//
	// If we are doing a gamma correction, this is simply a dependent lookup of RGB
	// through the gamma ramp.
	//
	// If we're doing color correction, this involves conversion to linear space,
	// then a matrix multiplication, then conversion back to gamma space.

	if (doColorCorrection) {
		// Convert to linear space. Note that we are using a straight 2.2 curve here
		// for NTSC, not the 2.4 curve with linear section that would be used with
		// sRGB.

		c = max(0, c * colorCorrectMatrix1.w + colorCorrectMatrix0.w);

		// In most cases, we should be using a 2.2 curve here. However, the exception is when
		// doing linear/HDR rendering with no actual color correction matrix; in that case
		// we enter this code path specifically to do gamma-to-linear conversion and should
		// invert the sRGB EOTF instead.

		if (!doColorCorrectionSRGB) {
			c.r = pow(c.r, 2.2h);
			c.g = pow(c.g, 2.2h);
			c.b = pow(c.b, 2.2h);
		} else {
			c = SrgbToLinear(c);
		}

		// transform color spaces
		c = mul(c, transpose(half3x3(colorCorrectMatrix0.xyz, colorCorrectMatrix1.xyz, colorCorrectMatrix2)));

		if (doDotMask) {
			c *= SCREENFX_SAMPLE_MASK(uvMask).rgb;
		}
	}

	// Apply gamma correction, or linear->gamma for color correction. The lookup
	// texture is 256 texels wide with the endpoints being the first and last
	// texels, so the U coordinates need to be contracted slightly for an accurate
	// lookup.

	if (doGammaCorrection) {
		c.r = SCREENFX_SAMPLE_GAMMA(float2(c.r * (255.0h / 256.0h) + 0.5h / 256.0h, 0)).r;
		c.g = SCREENFX_SAMPLE_GAMMA(float2(c.g * (255.0h / 256.0h) + 0.5h / 256.0h, 0)).r;
		c.b = SCREENFX_SAMPLE_GAMMA(float2(c.b * (255.0h / 256.0h) + 0.5h / 256.0h, 0)).r;
	}

	// Apply scanlines.
	//
	// This mask is applied in linear lighting, but with the gamma conversions optimized out
	// as follows:
	//
	//		c' = (c^gamma * mask) ^ (1/gamma)
	//		   = (c^gamma)^(1/gamma) * mask^(1/gamma)
	//		   = c * mask^(1/gamma)
	//
	// The gamma correction is then baked into the mask texture.

	if (doScanlines) {
		half3 scanMask = SCREENFX_SAMPLE_SCANLINE(uvScan).rgb;

		c *= scanMask;
	}

	return half4(c, 1);
}

struct FPScreenFXInput1 {
	float4 pos : SV_Position;
	float2 uv0 : TEXCOORD0;
};

struct FPScreenFXInput2 {
	float4 pos : SV_Position;
	float2 uv0 : TEXCOORD0;
	float2 uvScan : TEXCOORD1;
	float2 uvMask : TEXCOORD2;
};

//                                                                                                                                                                 sharp  scan   gamma  cc     ccsrgb dotmask
half4 FP_ScreenFX_PtLinear_NoScanlines_Linear         (FPScreenFXInput1 input) : SV_Target0 { return FP_ScreenFX(input.pos, input.uv0, 0,            0,            false, false, false, false, false, false); }
half4 FP_ScreenFX_PtLinear_NoScanlines_Gamma          (FPScreenFXInput1 input) : SV_Target0 { return FP_ScreenFX(input.pos, input.uv0, 0,            0,            false, false, true,  false, false, false); }
half4 FP_ScreenFX_PtLinear_NoScanlines_CC_SRGB        (FPScreenFXInput1 input) : SV_Target0 { return FP_ScreenFX(input.pos, input.uv0, 0,            0,            false, false, false, true , true , false); }
half4 FP_ScreenFX_PtLinear_NoScanlines_CC_Gamma22     (FPScreenFXInput1 input) : SV_Target0 { return FP_ScreenFX(input.pos, input.uv0, 0,            0,            false, false, false, true , false, false); }
half4 FP_ScreenFX_PtLinear_NoScanlines_GammaCC        (FPScreenFXInput1 input) : SV_Target0 { return FP_ScreenFX(input.pos, input.uv0, 0,            0,            false, false, true,  true , false, false); }
half4 FP_ScreenFX_PtLinear_NoScanlines_Mask_CC_SRGB   (FPScreenFXInput2 input) : SV_Target0 { return FP_ScreenFX(input.pos, input.uv0, input.uvScan, input.uvMask, false, false, false, true , true , true ); }
half4 FP_ScreenFX_PtLinear_NoScanlines_Mask_CC_Gamma22(FPScreenFXInput2 input) : SV_Target0 { return FP_ScreenFX(input.pos, input.uv0, input.uvScan, input.uvMask, false, false, false, true , false, true ); }
half4 FP_ScreenFX_PtLinear_NoScanlines_Mask_GammaCC   (FPScreenFXInput2 input) : SV_Target0 { return FP_ScreenFX(input.pos, input.uv0, input.uvScan, input.uvMask, false, false, true,  true , false, true ); }
half4 FP_ScreenFX_Sharp_NoScanlines_Linear            (FPScreenFXInput1 input) : SV_Target0 { return FP_ScreenFX(input.pos, input.uv0, 0,            0,            true,  false, false, false, false, false); }
half4 FP_ScreenFX_Sharp_NoScanlines_Gamma             (FPScreenFXInput1 input) : SV_Target0 { return FP_ScreenFX(input.pos, input.uv0, 0,            0,            true,  false, true,  false, false, false); }
half4 FP_ScreenFX_Sharp_NoScanlines_CC_SRGB           (FPScreenFXInput1 input) : SV_Target0 { return FP_ScreenFX(input.pos, input.uv0, 0,            0,            true,  false, false, true , true , false); }
half4 FP_ScreenFX_Sharp_NoScanlines_CC_Gamma22        (FPScreenFXInput1 input) : SV_Target0 { return FP_ScreenFX(input.pos, input.uv0, 0,            0,            true,  false, false, true , false, false); }
half4 FP_ScreenFX_Sharp_NoScanlines_GammaCC           (FPScreenFXInput1 input) : SV_Target0 { return FP_ScreenFX(input.pos, input.uv0, 0,            0,            true,  false, true,  true , false, false); }
half4 FP_ScreenFX_Sharp_NoScanlines_Mask_CC_SRGB      (FPScreenFXInput2 input) : SV_Target0 { return FP_ScreenFX(input.pos, input.uv0, input.uvScan, input.uvMask, true,  false, false, true , true , true ); }
half4 FP_ScreenFX_Sharp_NoScanlines_Mask_CC_Gamma22   (FPScreenFXInput2 input) : SV_Target0 { return FP_ScreenFX(input.pos, input.uv0, input.uvScan, input.uvMask, true,  false, false, true , false, true ); }
half4 FP_ScreenFX_Sharp_NoScanlines_Mask_GammaCC      (FPScreenFXInput2 input) : SV_Target0 { return FP_ScreenFX(input.pos, input.uv0, input.uvScan, input.uvMask, true,  false, true,  true , false, true ); }

half4 FP_ScreenFX_PtLinear_Scanlines_Linear         (FPScreenFXInput2 input) : SV_Target0 { return FP_ScreenFX(input.pos, input.uv0, input.uvScan, input.uvMask, false, true,  false, false, false, false); }
half4 FP_ScreenFX_PtLinear_Scanlines_Gamma          (FPScreenFXInput2 input) : SV_Target0 { return FP_ScreenFX(input.pos, input.uv0, input.uvScan, input.uvMask, false, true,  true,  false, false, false); }
half4 FP_ScreenFX_PtLinear_Scanlines_CC_SRGB        (FPScreenFXInput2 input) : SV_Target0 { return FP_ScreenFX(input.pos, input.uv0, input.uvScan, input.uvMask, false, true,  false, true , true , false); }
half4 FP_ScreenFX_PtLinear_Scanlines_CC_Gamma22     (FPScreenFXInput2 input) : SV_Target0 { return FP_ScreenFX(input.pos, input.uv0, input.uvScan, input.uvMask, false, true,  false, true , false, false); }
half4 FP_ScreenFX_PtLinear_Scanlines_GammaCC        (FPScreenFXInput2 input) : SV_Target0 { return FP_ScreenFX(input.pos, input.uv0, input.uvScan, input.uvMask, false, true,  true,  true , false, false); }
half4 FP_ScreenFX_PtLinear_Scanlines_Mask_CC_SRGB   (FPScreenFXInput2 input) : SV_Target0 { return FP_ScreenFX(input.pos, input.uv0, input.uvScan, input.uvMask, false, true,  false, true , true , true); }
half4 FP_ScreenFX_PtLinear_Scanlines_Mask_CC_Gamma22(FPScreenFXInput2 input) : SV_Target0 { return FP_ScreenFX(input.pos, input.uv0, input.uvScan, input.uvMask, false, true,  false, true , false, true); }
half4 FP_ScreenFX_PtLinear_Scanlines_Mask_GammaCC   (FPScreenFXInput2 input) : SV_Target0 { return FP_ScreenFX(input.pos, input.uv0, input.uvScan, input.uvMask, false, true,  true,  true , false, true); }
half4 FP_ScreenFX_Sharp_Scanlines_Linear            (FPScreenFXInput2 input) : SV_Target0 { return FP_ScreenFX(input.pos, input.uv0, input.uvScan, input.uvMask, true,  true,  false, false, false, false); }
half4 FP_ScreenFX_Sharp_Scanlines_Gamma             (FPScreenFXInput2 input) : SV_Target0 { return FP_ScreenFX(input.pos, input.uv0, input.uvScan, input.uvMask, true,  true,  true,  false, false, false); }
half4 FP_ScreenFX_Sharp_Scanlines_CC_SRGB           (FPScreenFXInput2 input) : SV_Target0 { return FP_ScreenFX(input.pos, input.uv0, input.uvScan, input.uvMask, true,  true,  false, true , true , false); }
half4 FP_ScreenFX_Sharp_Scanlines_CC_Gamma22        (FPScreenFXInput2 input) : SV_Target0 { return FP_ScreenFX(input.pos, input.uv0, input.uvScan, input.uvMask, true,  true,  false, true , false, false); }
half4 FP_ScreenFX_Sharp_Scanlines_GammaCC           (FPScreenFXInput2 input) : SV_Target0 { return FP_ScreenFX(input.pos, input.uv0, input.uvScan, input.uvMask, true,  true,  true,  true , false, false); }
half4 FP_ScreenFX_Sharp_Scanlines_Mask_CC_SRGB      (FPScreenFXInput2 input) : SV_Target0 { return FP_ScreenFX(input.pos, input.uv0, input.uvScan, input.uvMask, true,  true,  false, true , true , true); }
half4 FP_ScreenFX_Sharp_Scanlines_Mask_CC_Gamma22   (FPScreenFXInput2 input) : SV_Target0 { return FP_ScreenFX(input.pos, input.uv0, input.uvScan, input.uvMask, true,  true,  false, true , false, true); }
half4 FP_ScreenFX_Sharp_Scanlines_Mask_GammaCC      (FPScreenFXInput2 input) : SV_Target0 { return FP_ScreenFX(input.pos, input.uv0, input.uvScan, input.uvMask, true,  true,  true,  true , false, true); }

///////////////////////////////////////////////////////////////////////////

cbuffer VpBloom : register(b0) {
	float4 vpBloomViewport		SCREENFX_REG(vs, c16);
	float2 vpBloomUVOffset		SCREENFX_REG(vs, c17);
	float2 vpBloomBlurOffsets1	SCREENFX_REG(vs, c18);
	float4 vpBloomBlurOffsets2	SCREENFX_REG(vs, c19);
};

cbuffer FpBloom : register(b0) {
	float4 fpBloomWeights		SCREENFX_REG(ps, c16);
	float2 fpBloomThresholds	SCREENFX_REG(ps, c17);
	float2 fpBloomScales		SCREENFX_REG(ps, c18);
}

void VP_Bloom1(
	float2 pos : POSITION,
	float2 uv0 : TEXCOORD0,
	out float2 oT0 : TEXCOORD0,
	out float2 oT1 : TEXCOORD1,
	out float2 oT2 : TEXCOORD2,
	out float2 oT3 : TEXCOORD3,
	out float4 oPos : SV_Position)
{
	oPos = float4(pos.xy, 0.5, 1);
	oT0 = uv0 + vpBloomUVOffset*float2(-1,-1);
	oT1 = uv0 + vpBloomUVOffset*float2(+1,-1);
	oT2 = uv0 + vpBloomUVOffset*float2(-1,+1);
	oT3 = uv0 + vpBloomUVOffset*float2(+1,+1);
	
	VP_APPLY_VIEWPORT(oPos);
}

half4 FP_Bloom1(
	bool assumeLinearColor,
	float2 uv0 : TEXCOORD0,
	float2 uv1 : TEXCOORD1,
	float2 uv2 : TEXCOORD2,
	float2 uv3 : TEXCOORD3) : SV_Target0
{
	half3 c;

	c = SCREENFX_SAMPLE_SRC(uv0).rgb * 0.25h
	  + SCREENFX_SAMPLE_SRC(uv1).rgb * 0.25h
	  + SCREENFX_SAMPLE_SRC(uv2).rgb * 0.25h
	  + SCREENFX_SAMPLE_SRC(uv3).rgb * 0.25h;

	if (!assumeLinearColor)
		c *= c;

	return half4(c * fpBloomThresholds.x + fpBloomThresholds.y, 0);
}

half4 FP_Bloom1_Linear(
	float2 uv0 : TEXCOORD0,
	float2 uv1 : TEXCOORD1,
	float2 uv2 : TEXCOORD2,
	float2 uv3 : TEXCOORD3) : SV_Target0
{
	return FP_Bloom1(true, uv0, uv1, uv2, uv3);
}

half4 FP_Bloom1_Gamma(
	float2 uv0 : TEXCOORD0,
	float2 uv1 : TEXCOORD1,
	float2 uv2 : TEXCOORD2,
	float2 uv3 : TEXCOORD3) : SV_Target0
{
	return FP_Bloom1(false, uv0, uv1, uv2, uv3);
}

half4 FP_Bloom1A(
	float2 uv0 : TEXCOORD0,
	float2 uv1 : TEXCOORD1,
	float2 uv2 : TEXCOORD2,
	float2 uv3 : TEXCOORD3) : SV_Target0
{
	half3 c;

	c = SCREENFX_SAMPLE_SRC(uv0).rgb * 0.25h
	  + SCREENFX_SAMPLE_SRC(uv1).rgb * 0.25h
	  + SCREENFX_SAMPLE_SRC(uv2).rgb * 0.25h
	  + SCREENFX_SAMPLE_SRC(uv3).rgb * 0.25h;

	return half4(c, 0);
}

void VP_Bloom2(
	float2 pos : POSITION,
	float2 uv0 : TEXCOORD0,
	float2 uv1 : TEXCOORD1,
	out float2 oT0 : TEXCOORD0,
	out float2 oT1 : TEXCOORD1,
	out float2 oT2 : TEXCOORD2,
	out float2 oT3 : TEXCOORD3,
	out float2 oT4 : TEXCOORD4,
	out float2 oT5 : TEXCOORD5,
	out float2 oT6 : TEXCOORD6,
	out float4 oPos : SV_Position)
{
	oPos = float4(pos.xy, 0.5, 1);
	oT0 = uv0;
	oT1 = uv0 + vpBloomBlurOffsets1;
	oT2 = uv0 - vpBloomBlurOffsets1;
	oT3 = uv0 + vpBloomBlurOffsets2.xy;
	oT4 = uv0 - vpBloomBlurOffsets2.xy;
	oT5 = uv0 + vpBloomBlurOffsets2.zw;
	oT6 = uv0 - vpBloomBlurOffsets2.zw;
	
	VP_APPLY_VIEWPORT(oPos);
}

half4 FP_Bloom2(
	float2 uv0 : TEXCOORD0,
	float2 uv1 : TEXCOORD1,
	float2 uv2 : TEXCOORD2,
	float2 uv3 : TEXCOORD3,
	float2 uv4 : TEXCOORD4,
	float2 uv5 : TEXCOORD5,
	float2 uv6 : TEXCOORD6) : SV_Target0
{
	half4 c;
	half3 s0 = (half3)SCREENFX_SAMPLE_SRC(uv0).rgb;
	half3 s1 = (half3)SCREENFX_SAMPLE_SRC(uv1).rgb;
	half3 s2 = (half3)SCREENFX_SAMPLE_SRC(uv2).rgb;
	half3 s3 = (half3)SCREENFX_SAMPLE_SRC(uv3).rgb;
	half3 s4 = (half3)SCREENFX_SAMPLE_SRC(uv4).rgb;
	half3 s5 = (half3)SCREENFX_SAMPLE_SRC(uv5).rgb;
	half3 s6 = (half3)SCREENFX_SAMPLE_SRC(uv6).rgb;

	c.rgb  =  s0 * fpBloomWeights.x;
	c.rgb += (s1 + s2) * fpBloomWeights.y;
	c.rgb += (s3 + s4) * fpBloomWeights.z;
	c.rgb += (s5 + s6) * fpBloomWeights.w;

	c.a = 0;

	return c;
}

void VP_Bloom3(
	float2 pos : POSITION,
	float2 uv0 : TEXCOORD0,
	float2 uv1 : TEXCOORD1,
	out float2 oT0 : TEXCOORD0,
	out float2 oT1 : TEXCOORD1,
	out float4 oPos : SV_Position)
{
	oPos = float4(pos.xy, 0.5, 1);
	oT0 = uv0;
	oT1 = uv1;
	
	VP_APPLY_VIEWPORT(oPos);
}

half4 FP_Bloom3(
	bool assumeLinearColor,
	float2 uvBase : TEXCOORD0,
	float2 uvBlur : TEXCOORD1,
	Texture2D basetex : register(t1),
	SamplerState basesamp : register(s1)) : SV_Target0
{
	half3 c = SCREENFX_SAMPLE_SRC(uvBlur).rgb;
	half3 d = SCREENFX_SAMPLE_BASE(uvBase).rgb;

	if (!assumeLinearColor)
		d *= d;

	c = d * fpBloomScales.x + c * fpBloomScales.y;

	if (!assumeLinearColor)
		c = sqrt(c);

	return half4(c, 0);
}

half4 FP_Bloom3_Linear(float2 uvBase : TEXCOORD0, float2 uvBlur : TEXCOORD1, Texture2D basetex : register(t1), SamplerState basesamp : register(s1)) : SV_Target0
{
	return FP_Bloom3(true, uvBase, uvBlur, basetex, basesamp);
}

half4 FP_Bloom3_Gamma(float2 uvBase : TEXCOORD0, float2 uvBlur : TEXCOORD1, Texture2D basetex : register(t1), SamplerState basesamp : register(s1)) : SV_Target0
{
	return FP_Bloom3(false, uvBase, uvBlur, basetex, basesamp);
}

////////////////////////////////////////////////////////////////////////////////

cbuffer FpBloomV2 : register(b0) {
	float4 fpBloomV2UVStep			SCREENFX_REG(ps, c16);
	float4 fpBloomV2BlendFactors	SCREENFX_REG(ps, c17);
};

void VP_BloomGamma(
	float2 pos : POSITION,
	float2 uv0 : TEXCOORD0,
	out float2 oT0 : TEXCOORD0,
	out float4 oPos : SV_Position)
{
	oPos = float4(pos.xy, 0.5, 1);
	oT0 = uv0;
	
	VP_APPLY_VIEWPORT(oPos);
}

half4 FP_BloomGamma(
	float2 uv0 : TEXCOORD0
) : SV_Target0
{
	half3 c, d;

	c = SCREENFX_SAMPLE_SRC(uv0).rgb;

	return half4(SrgbToLinear(c), 0);
}

void VP_BloomDown(
	float2 pos : POSITION,
	float2 uv0 : TEXCOORD0,
	out float2 oT0 : TEXCOORD0,
	out float4 oPos : SV_Position)
{
	oPos = float4(pos.xy, 0.5, 1);
	oT0 = uv0;
	
	VP_APPLY_VIEWPORT(oPos);
}

half4 FP_BloomDown(
	float2 uv0 : TEXCOORD0
) : SV_Target0
{
	half3 c, d;

	// The downsampling filter is the 13 bilinear sample filter from:
	//
	// Jiminez, Jorge, Next Generation Post Processing in Call of Duty: Advanced Warfare.
	// Retrieved from https://www.iryoku.com/next-generation-post-processing-in-call-of-duty-advanced-warfare/.
	//
	// The filter is a concatenation of two separate filters, which combined cover a
	// 6x6 texel area (with each sample being a bilinear sample at a texel corner):
	//
	// 1   1    1   1
	//            4
	// 1   1    1   1
	//  (/4)     (/8)
	//
	// This combines to the following bilinear samples:
	//
	// 1   2   1
	//   4   4
	// 2   4   2  (/32)
	//   4   4
	// 1   2   1
	//
	// Or, the following raw texel weights:
	//
	// 1 1 2 2 1 1
	// 1 5 6 6 5 1
	// 2 6 8 8 6 2 (/128)
	// 2 6 8 8 6 2
	// 1 5 6 6 5 1
	// 1 1 2 2 1 1
	//
	// We can almost do this with 9 bilinear samples, except for the corners
	// which aren't separable. We cheat a bit and reduce the corner weights to
	// 0.

	const half w1 =  7.0h / 124.0h;
	const half w2 = 16.0h / 124.0h;
	const half w3 = 32.0h / 124.0h;

	float2 uv0_a = uv0 - fpBloomV2UVStep.xy*float2( 1.75f, 1.75f);
	float2 uv0_c = uv0 + fpBloomV2UVStep.xy*float2( 1.75f, 1.75f);

	c  = SCREENFX_SAMPLE_SRC(float2(uv0_a.x, uv0_a.y)).rgb * w1;
	c += SCREENFX_SAMPLE_SRC(float2(uv0_c.x, uv0_a.y)).rgb * w1;
	c += SCREENFX_SAMPLE_SRC(float2(uv0_a.x, uv0_c.y)).rgb * w1;
	c += SCREENFX_SAMPLE_SRC(float2(uv0_c.x, uv0_c.y)).rgb * w1;

	c += SCREENFX_SAMPLE_SRC(float2(uv0  .x, uv0_a.y)).rgb * w2;
	c += SCREENFX_SAMPLE_SRC(float2(uv0_a.x, uv0  .y)).rgb * w2;
	c += SCREENFX_SAMPLE_SRC(float2(uv0  .x, uv0_c.y)).rgb * w2;
	c += SCREENFX_SAMPLE_SRC(float2(uv0_c.x, uv0  .y)).rgb * w2;

	c += SCREENFX_SAMPLE_SRC(float2(uv0  .x, uv0  .y)).rgb * w3;

	return half4(c, 0);
}

void VP_BloomUp(
	float2 pos : POSITION,
	float2 uv0 : TEXCOORD0,
	out float2 oT0 : TEXCOORD0,
	out float4 oPos : SV_Position)
{
	oPos = float4(pos.xy, 0.5, 1);
	oT0 = uv0;
	
	VP_APPLY_VIEWPORT(oPos);
}

half4 FP_BloomUp(
	float2 uv0 : TEXCOORD0
) : SV_Target0
{
	half3 c;

	// The original upsampling filter is a 3x3 loop filter:
	//
	//   1  2  1
	//   2  4  2
	//   1  2  1
	//
	// However, this is being applied while also expanding the image by 2x, so
	// the sampling positions are 1/4 from the center:
	//
	//  +-----------+
	//  |           |
	//  |  x     x  |
	//  |           |
	//  |     o     |
	//  |           |
	//  |  x     x  |
	//  |           |
	//  +-----------+
	//
	// For that reason, we can't use the standard trick of four overlapping
	// bilinear samples. However, we can use four non-overlapping samples, with
	// very carefully arranged offsets:
	//
	// Filter kernel for top-left case (UV point at -0.25, -0.25 from [center])
	//
	//   1    5    7    3
	//   5   25   35   15
	//   7   35  [49]  21
	//   3   15   21    9
	//
	// This kernel needs to be flipped according to which of the four sample
	// positions in the texel are being used.

	float2 flipSign = (frac(uv0 * fpBloomV2UVStep.wz) < 0.5f ? float2(1.0f, 1.0f) : float2(-1.0f, -1.0f));
	float2 flippedStep = fpBloomV2UVStep.xy * flipSign;

	float2 uv0_a = uv0 + flippedStep * float2(-0.75f - 1.0f/6.0f, -0.75f - 1.0f/6.0f);
	float2 uv0_b = uv0 + flippedStep * float2(+0.25f + 0.3f, +0.25f + 0.3f);

	c  = SCREENFX_SAMPLE_SRC(float2(uv0_a.x, uv0_a.y)).rgb * ( 36.0h/256.0h);
	c += SCREENFX_SAMPLE_SRC(float2(uv0_b.x, uv0_a.y)).rgb * ( 60.0h/256.0h);
	c += SCREENFX_SAMPLE_SRC(float2(uv0_a.x, uv0_b.y)).rgb * ( 60.0h/256.0h);
	c += SCREENFX_SAMPLE_SRC(float2(uv0_b.x, uv0_b.y)).rgb * (100.0h/256.0h);

	return half4(c * fpBloomV2BlendFactors.x, fpBloomV2BlendFactors.y);
}

////////////////////////////////////////////////////////////////////////////////

#if ENGINE_D3D9

void VP_BloomUpNoBlend(
	float2 pos : POSITION,
	float2 uv0 : TEXCOORD0,
	float2 uv1 : TEXCOORD1,
	out float2 oT0 : TEXCOORD0,
	out float2 oTBase : TEXCOORD1,
	out float4 oPos : SV_Position)
{
	oPos = float4(pos.xy, 0.5, 1);
	oT0 = uv0;
	oTBase = uv1;
	
	VP_APPLY_VIEWPORT(oPos);
}

half4 FP_BloomUpNoBlend(
	float2 uv0 : TEXCOORD0,
	float2 uv1 : TEXCOORD1
) : SV_Target0
{
	half3 c;

	float2 flipSign = (frac(uv0 * fpBloomV2UVStep.wz) < 0.5f ? float2(1.0f, 1.0f) : float2(-1.0f, -1.0f));
	float2 flippedStep = fpBloomV2UVStep.xy * flipSign;

	float2 uv0_a = uv0 + flippedStep * float2(-0.75f - 1.0f/6.0f, -0.75f - 1.0f/6.0f);
	float2 uv0_b = uv0 + flippedStep * float2(+0.25f + 0.3f, +0.25f + 0.3f);

	c  = SCREENFX_SAMPLE_SRC(float2(uv0_a.x, uv0_a.y)).rgb * ( 36.0h/256.0h);
	c += SCREENFX_SAMPLE_SRC(float2(uv0_b.x, uv0_a.y)).rgb * ( 60.0h/256.0h);
	c += SCREENFX_SAMPLE_SRC(float2(uv0_a.x, uv0_b.y)).rgb * ( 60.0h/256.0h);
	c += SCREENFX_SAMPLE_SRC(float2(uv0_b.x, uv0_b.y)).rgb * (100.0h/256.0h);

	half3 d = SCREENFX_SAMPLE_BASE(uv1).rgb;

	return half4(c * fpBloomV2BlendFactors.x + d * fpBloomV2BlendFactors.y, 0);
}

#endif

////////////////////////////////////////////////////////////////////////////////

cbuffer FpBloomV2Final : register(b0) {
	float4 fpBloomV2FUVStep			SCREENFX_REG(ps, c16);
	float4 fpBloomV2FBlendFactors	SCREENFX_REG(ps, c17);
	float4 fpBloomV2FShoulderCurve	SCREENFX_REG(ps, c18);
	float4 fpBloomV2FThresholds		SCREENFX_REG(ps, c19);
	float4 fpBloomV2FBaseUVStep		SCREENFX_REG(ps, c20);
	float4 fpBloomV2FBaseWeights	SCREENFX_REG(ps, c21);

#if !ENGINE_D3D9
	float3x3 fpBloomV2FToDisplayRGB;
	float3x3 fpBloomV2FFromDisplayRGB;
#endif
};

void VP_BloomFinal(
	float2 pos : POSITION,
	float2 uv0 : TEXCOORD0,
	float2 uv1 : TEXCOORD1,
	out float2 oT0 : TEXCOORD0,
	out float2 oTBase : TEXCOORD1,
	out float4 oPos : SV_Position)
{
	oPos = float4(pos.xy, 0.5, 1);
	oT0 = uv0;
	oTBase = uv1;
	
	VP_APPLY_VIEWPORT(oPos);
}

half4 FP_BloomFinal(
	float2 uv0,
	float2 uvBase,
	uniform Texture2D basetex,
	uniform SamplerState basesamp,
	uniform bool useRGBBasis
) : SV_Target0
{
	// Filter kernel for top-left case (-0.25, -0.25 from [center])
	//
	//   1    5    7    3
	//   5   25   35   15
	//   7   35  [49]  21
	//   3   15   21    9

	float2 flipSign = (frac(uv0 * fpBloomV2FUVStep.wz) < 0.5f ? float2(1.0f, 1.0f) : float2(-1.0f, -1.0f));
	float2 flippedStep = fpBloomV2FUVStep.xy * flipSign;

	float2 uv0_a = uv0 + flippedStep * float2(-0.75f - 1.0f/6.0f, -0.75f - 1.0f/6.0f);
	float2 uv0_b = uv0 + flippedStep * float2(+0.25f + 0.3f, +0.25f + 0.3f);

	half3 c;
	c  = SCREENFX_SAMPLE_SRC(float2(uv0_a.x, uv0_a.y)).rgb * ( 36.0h/256.0h);
	c += SCREENFX_SAMPLE_SRC(float2(uv0_b.x, uv0_a.y)).rgb * ( 60.0h/256.0h);
	c += SCREENFX_SAMPLE_SRC(float2(uv0_a.x, uv0_b.y)).rgb * ( 60.0h/256.0h);
	c += SCREENFX_SAMPLE_SRC(float2(uv0_b.x, uv0_b.y)).rgb * (100.0h/256.0h);
		
	half3 d1 = 0;
	half3 d2 = 0;
	half3 d3 = 0;
	d1 += SCREENFX_SAMPLE_BASE(uvBase + fpBloomV2FBaseUVStep.xy*float2(-1, -1)).rgb;
	d1 += SCREENFX_SAMPLE_BASE(uvBase + fpBloomV2FBaseUVStep.xy*float2(+1, -1)).rgb;
	d1 += SCREENFX_SAMPLE_BASE(uvBase + fpBloomV2FBaseUVStep.xy*float2(-1, +1)).rgb;
	d1 += SCREENFX_SAMPLE_BASE(uvBase + fpBloomV2FBaseUVStep.xy*float2(+1, +1)).rgb;
	d2 += SCREENFX_SAMPLE_BASE(uvBase + fpBloomV2FBaseUVStep.xy*float2(-1,  0)).rgb;
	d2 += SCREENFX_SAMPLE_BASE(uvBase + fpBloomV2FBaseUVStep.xy*float2(+1,  0)).rgb;
	d2 += SCREENFX_SAMPLE_BASE(uvBase + fpBloomV2FBaseUVStep.xy*float2( 0, -1)).rgb;
	d2 += SCREENFX_SAMPLE_BASE(uvBase + fpBloomV2FBaseUVStep.xy*float2( 0, +1)).rgb;
	d3 += SCREENFX_SAMPLE_BASE(uvBase).rgb;

	half3 d = d1 * (half)fpBloomV2FBaseWeights.x + d2 * (half)fpBloomV2FBaseWeights.y + d3 * (half)fpBloomV2FBaseWeights.z;

	float3 x0 = min(c * fpBloomV2FBlendFactors.x + d, fpBloomV2FThresholds.z);
	float3 x = x0;

#if !ENGINE_D3D9
	if (useRGBBasis)
		x = mul(x, fpBloomV2FToDisplayRGB);
#endif

	float3 mid = fpBloomV2FThresholds.x * x;
	float3 shoulder = ((fpBloomV2FShoulderCurve.x*x + fpBloomV2FShoulderCurve.y)*x + fpBloomV2FShoulderCurve.z)*x + fpBloomV2FShoulderCurve.w;

	float3 r = x > fpBloomV2FThresholds.y ? shoulder : mid;

#if !ENGINE_D3D9
	if (useRGBBasis)
		r = mul(r, fpBloomV2FFromDisplayRGB);
#endif

	return half4(r, 0);
}

half4 FP_BloomFinalSRGB(
	float2 uv0 : TEXCOORD0,
	float2 uvBase : TEXCOORD1,
	Texture2D basetex : register(t1),
	SamplerState basesamp : register(s1)
) : SV_Target0
{
	return FP_BloomFinal(uv0, uvBase, basetex, basesamp, false);
}

#if !ENGINE_D3D9
half4 FP_BloomFinalWCG(
	float2 uv0 : TEXCOORD0,
	float2 uvBase : TEXCOORD1,
	Texture2D basetex : register(t1),
	SamplerState basesamp : register(s1)
) : SV_Target0
{
	return FP_BloomFinal(uv0, uvBase, basetex, basesamp, true);
}
#endif
