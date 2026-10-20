// AltirraSDL SDL_GPU display shader.
//
// The SDL GPU renderer supplies the primary texture at t0/s0 (space2) and
// uploads our per-draw constants at b0 (space3). One shader handles the
// persistent multi-pass targets through a pass selector, preserving every
// user-facing effect control without creating shaders in the frame loop.

cbuffer Context : register(b0, space3) {
	float4 sourceDestSize;       // source width/height, destination width/height
	float4 filterParams;         // mode, sharp X, sharp Y, PAL scanline offset
	float4 effectParams1;        // gamma, output gamma, scanlines, vignette
	float4 effectParams2;        // distortion X scale, Y scale, squared radius, signed RGB
	float4 bloomParams;          // enabled, radius, direct intensity, indirect intensity
	float4 maskParams;           // type, scanline source height, openness, destination Y
	float4 colorFlags;           // color correction, mask, bloom threshold, output height
	float4 colorMatrix0;
	float4 colorMatrix1;
	float4 colorMatrix2;
	float4 passParams0;          // pass, UV step X/Y, scale/blend X
	float4 passParams1;          // blend Y, source width/height, base UV step
	float4 bloomShoulder;        // cubic shoulder coefficients
	float4 bloomThresholds;      // mid slope, shoulder X, limit X, reserved
	float4 bloomBaseWeights;     // corner, side, center, reserved
	float4 hdrParams;            // output nits/80 (0=SDR target), sRGB sampler, HDR, linear sampling
};

Texture2D sourceTexture : register(t0, space2);
SamplerState sourceSampler : register(s0, space2);

struct PSInput {
	// SDL's built-in GPU renderer vertex shader supplies color and UV as
	// consecutive generic attributes. D3D12 requires these exact semantics;
	// Vulkan and Metal use the corresponding locations 0 and 1.
	float4 color : TEXCOORD0;
	float2 uv : TEXCOORD1;
};

struct PSOutput {
	float4 color : SV_Target;
};

float3 SRGBToLinear(float3 c) {
	float3 lo = c / 12.92;
	float3 hi = pow((c + 0.055) / 1.055, 2.4);
	return lerp(hi, lo, c <= 0.04045);
}

float3 LinearToSRGB(float3 c) {
	float3 lo = c * 12.92;
	float3 hi = 1.055 * pow(max(c, 0.0), 1.0 / 2.4) - 0.055;
	return lerp(hi, lo, c <= 0.0031308);
}

// SDL's linear renderer installs an sRGB sampler for uploaded 8-bit
// textures. Restore each texel's encoded value BEFORE interpolation: doing
// this after hardware bilinear sampling changes Altirra's existing filters.
float4 SampleEncodedTexel(float2 uv) {
	float4 c = sourceTexture.SampleLevel(sourceSampler, uv, 0.0);
	c.rgb = LinearToSRGB(max(c.rgb, 0.0));
	return c;
}

float4 SampleTexture(float2 uv) {
	[branch]
	if (hdrParams.y <= 0.5)
		return sourceTexture.SampleLevel(sourceSampler, uv, 0.0);
	[branch]
	if (hdrParams.w <= 0.5)
		return SampleEncodedTexel(uv);

	uint width, height;
	sourceTexture.GetDimensions(width, height);
	float2 size = float2(width, height);
	float2 pos = uv * size - 0.5;
	float2 base = floor(pos);
	float2 f = frac(pos);
	// Texel-center samples work with the supplied linear sampler too; the
	// sampler's clamp addressing retains the established edge behavior.
	float4 a = SampleEncodedTexel((base + float2(0.5, 0.5)) / size);
	float4 b = SampleEncodedTexel((base + float2(1.5, 0.5)) / size);
	float4 c = SampleEncodedTexel((base + float2(0.5, 1.5)) / size);
	float4 d = SampleEncodedTexel((base + float2(1.5, 1.5)) / size);
	return lerp(lerp(a, b, f.x), lerp(c, d, f.x), f.y);
}

float3 OutputColor(float3 c) {
	return hdrParams.x > 0.0 ? SRGBToLinear(max(c, 0.0)) * hdrParams.x : c;
}

float3 LimitColor(float3 c) {
	return hdrParams.z > 0.5 ? max(c, 0.0) : saturate(c);
}

float CubicWeight(float x) {
	// Altirra's established bicubic kernel uses A=-0.75. This is slightly
	// sharper than Catmull-Rom (A=-0.5) and matches VDDisplayCreateBicubicTexture.
	x = abs(x);
	if (x <= 1.0)
		return ((1.25 * x - 2.25) * x) * x + 1.0;
	if (x < 2.0)
		return (((-0.75 * x + 3.75) * x - 6.0) * x) + 3.0;
	return 0.0;
}

float4 SampleBicubic(float2 uv) {
	float2 size = sourceDestSize.xy;
	float2 p = uv * size - 0.5;
	float2 base = floor(p);
	float2 f = p - base;
	float4 sum = 0.0;
	float weightSum = 0.0;

	[unroll]
	for (int y = -1; y <= 2; ++y) {
		float wy = CubicWeight((float)y - f.y);
		[unroll]
		for (int x = -1; x <= 2; ++x) {
			float wx = CubicWeight((float)x - f.x);
			float w = wx * wy;
			float2 tapUV = (base + float2(x, y) + 0.5) / size;
			sum += SampleTexture(tapUV) * w;
			weightSum += w;
		}
	}
	return sum / max(weightSum, 0.0001);
}

float4 SampleSource(float2 uv) {
	if (filterParams.x == 2.0)
		return SampleBicubic(uv);

	if (filterParams.x == 4.0) {
		float2 texel = 1.0 / sourceDestSize.xy;
		float2 texUV = uv / texel;
		float2 nearest = floor(texUV + 0.5);
		float2 delta = nearest - texUV;
		uv = (nearest - clamp(delta * filterParams.yz + 0.5, 0.0, 1.0)
			+ 0.5) * texel;
	}

	return SampleTexture(uv);
}

float3 BloomDown(float2 uv) {
	const float w1 = 7.0 / 124.0;
	const float w2 = 16.0 / 124.0;
	const float w3 = 32.0 / 124.0;
	float2 off = passParams0.yz * 1.75;
	float3 c = 0.0;
	c += SampleTexture(uv + float2(-off.x, -off.y)).rgb * w1;
	c += SampleTexture(uv + float2(+off.x, -off.y)).rgb * w1;
	c += SampleTexture(uv + float2(-off.x, +off.y)).rgb * w1;
	c += SampleTexture(uv + float2(+off.x, +off.y)).rgb * w1;
	c += SampleTexture(uv + float2(0.0, -off.y)).rgb * w2;
	c += SampleTexture(uv + float2(-off.x, 0.0)).rgb * w2;
	c += SampleTexture(uv + float2(0.0, +off.y)).rgb * w2;
	c += SampleTexture(uv + float2(+off.x, 0.0)).rgb * w2;
	c += SampleTexture(uv).rgb * w3;
	return c;
}

float3 BloomUpsample(float2 uv) {
	float2 texSize = passParams1.yz;
	float2 fracPos = frac(uv * texSize);
	float2 flipSign = lerp(1.0, -1.0, fracPos >= 0.5);
	float2 stepUV = passParams0.yz * flipSign;
	float2 uvA = uv + stepUV * (-0.75 - 1.0 / 6.0);
	float2 uvB = uv + stepUV * (+0.25 + 0.3);
	float3 c = 0.0;
	c += SampleTexture(float2(uvA.x, uvA.y)).rgb * (36.0 / 256.0);
	c += SampleTexture(float2(uvB.x, uvA.y)).rgb * (60.0 / 256.0);
	c += SampleTexture(float2(uvA.x, uvB.y)).rgb * (60.0 / 256.0);
	c += SampleTexture(float2(uvB.x, uvB.y)).rgb * (100.0 / 256.0);
	return c;
}

float3 BloomBaseFilter(float2 uv) {
	float2 stepUV = float2(passParams1.w, colorFlags.w);
	float3 corners = 0.0;
	float3 sides = 0.0;
	corners += SampleTexture(uv + stepUV * float2(-1.0, -1.0)).rgb;
	corners += SampleTexture(uv + stepUV * float2(+1.0, -1.0)).rgb;
	corners += SampleTexture(uv + stepUV * float2(-1.0, +1.0)).rgb;
	corners += SampleTexture(uv + stepUV * float2(+1.0, +1.0)).rgb;
	sides += SampleTexture(uv + stepUV * float2(-1.0, 0.0)).rgb;
	sides += SampleTexture(uv + stepUV * float2(+1.0, 0.0)).rgb;
	sides += SampleTexture(uv + stepUV * float2(0.0, -1.0)).rgb;
	sides += SampleTexture(uv + stepUV * float2(0.0, +1.0)).rgb;
	float3 center = SampleTexture(uv).rgb;
	return corners * bloomBaseWeights.x + sides * bloomBaseWeights.y
		+ center * bloomBaseWeights.z;
}

// Masks remain in physical pixels. Scanlines follow the distorted source
// raster, with the local sampling rate determining the Nyquist cutoff.
float ScanlineFactor(float2 screenUV) {
	float dvdy = maskParams.y / max(colorFlags.w, 1.0);
	float rasterY = dvdy * (maskParams.w + screenUV.y * sourceDestSize.w);
	if (effectParams2.z > 0.0) {
		float2 v = screenUV - 0.5;
		float2 scaled = v * effectParams2.xy;
		float d = max(0.00001, effectParams2.z - dot(scaled, scaled));
		float2 sourceUV = v / sqrt(d) + 0.5;
		rasterY = sourceUV.y * maskParams.y;
		dvdy = fwidth(rasterY);
	}
	float scan = dvdy <= 0.5
		? 0.5 - 0.5 * cos(frac(0.25 + rasterY) * 6.28318530718) : 0.5;
	float intensity = pow(saturate(effectParams1.z), 2.2);
	return pow(scan * (1.0 - intensity) + intensity, 1.0 / 2.2);
}

PSOutput main(PSInput input) {
	PSOutput output;
	float2 uv = input.uv;
	float2 screenUV = uv;
	int passMode = (int)(passParams0.x + 0.5);

	if (passMode == 11) {
		output.color = float4(SampleTexture(uv).rgb, 1.0);
		return output;
	}
	if (passMode == 1) {
		float3 c = SampleTexture(uv).rgb;
		float3 c2 = SampleTexture(uv + float2(0.0, -filterParams.w / max(sourceDestSize.y, 1.0))).rgb;
		float3 chroma = c2 - c;
		chroma -= dot(chroma, float3(0.30, 0.59, 0.11));
		c += chroma * 0.5;
		if (effectParams2.w > 0.5) {
			const float scale = 255.0 / 127.0;
			c = c * scale - (64.0 / 255.0) * scale;
		}
		output.color = float4(c, 1.0);
		return output;
	}
	if (passMode == 2) {
		float3 c = SampleTexture(uv).rgb;
		if (effectParams2.w > 0.5) {
			const float scale = 255.0 / 127.0;
			c = c * scale - (64.0 / 255.0) * scale;
		}
		output.color = float4(SRGBToLinear(max(c, 0.0)), 0.0);
		return output;
	}
	if (passMode == 3) {
		output.color = float4(BloomDown(uv), 0.0);
		return output;
	}
	if (passMode == 4) {
		output.color = float4(BloomUpsample(uv) * passParams0.w,
			passParams1.x);
		return output;
	}
	if (passMode == 5) {
		output.color = float4(BloomBaseFilter(uv), 1.0);
		return output;
	}
	if (passMode == 6) {
		output.color = float4(BloomUpsample(uv) * passParams0.w, 1.0);
		return output;
	}
	if (passMode == 7) {
		float3 x = min(SampleTexture(uv).rgb,
			bloomThresholds.z);
		float3 mid = bloomThresholds.x * x;
		float3 shoulder = ((bloomShoulder.x * x + bloomShoulder.y) * x
			+ bloomShoulder.z) * x + bloomShoulder.w;
		float3 c = lerp(mid, shoulder, x > bloomThresholds.y);
		output.color = float4(LinearToSRGB(max(c, 0.0)), 1.0);
		return output;
	}
	if (passMode == 8) {
		output.color = float4(SampleSource(uv).rgb, 1.0);
		return output;
	}
	if (passMode == 10) {
		float3 c = SampleTexture(uv).rgb;
		if (effectParams1.y <= 0.0)
			c = LinearToSRGB(max(c, 0.0));
		else
			c = pow(max(c, 0.0), 1.0 / max(effectParams1.y, 0.0001));
		c = pow(LimitColor(c), 1.0 / max(effectParams1.x, 0.0001));
		if (effectParams1.z > 0.0) {
			c *= ScanlineFactor(screenUV);
		}
		if (effectParams1.w > 0.0) {
			float2 vc = screenUV - 0.5;
			float vd = dot(vc, vc) * 4.0;
			c *= saturate(1.0 - effectParams1.w * vd);
		}
		output.color = float4(OutputColor(c), 1.0) * input.color;
		return output;
	}

	if (effectParams2.z > 0.0) {
		float2 v = uv - 0.5;
		float2 scaled = v * effectParams2.xy;
		float d = max(0.00001, effectParams2.z - dot(scaled, scaled));
		uv = v / sqrt(d) + 0.5;
		if (any(uv < 0.0) || any(uv > 1.0)) {
			output.color = float4(0.0, 0.0, 0.0, input.color.a);
			return output;
		}
	}

	float3 c = SampleSource(uv).rgb;

	if (filterParams.w != 0.0) {
		float2 palUV = uv + float2(0.0,
			-filterParams.w / max(sourceDestSize.y, 1.0));
		float3 c2 = SampleSource(palUV).rgb;
		float3 chroma = c2 - c;
		chroma -= dot(chroma, float3(0.30, 0.59, 0.11));
		c += chroma * 0.5;
	}

	if (effectParams2.w > 0.5) {
		const float scale = 255.0 / 127.0;
		c = c * scale - (64.0 / 255.0) * scale;
	}

	if (colorFlags.x > 0.5 || colorFlags.y > 0.5) {
		c = (effectParams1.y <= 0.0)
			? SRGBToLinear(LimitColor(c))
			: pow(LimitColor(c), effectParams1.y);
		float3 corrected;
		corrected.r = dot(c, colorMatrix0.xyz);
		corrected.g = dot(c, colorMatrix1.xyz);
		corrected.b = dot(c, colorMatrix2.xyz);
		c = corrected;
	}

	if (passMode == 9) {
		output.color = float4(c, 1.0);
		return output;
	}

	float gamma = max(effectParams1.x, 0.0001);
	if (colorFlags.x > 0.5 || colorFlags.y > 0.5) {
		if (effectParams1.y <= 0.0)
			c = LinearToSRGB(max(c, 0.0));
		else
			c = pow(max(c, 0.0), 1.0 / max(effectParams1.y, 0.0001));
	}
	c = pow(LimitColor(c), 1.0 / gamma);

	if (effectParams1.z > 0.0) {
		c *= ScanlineFactor(screenUV);
	}

	if (effectParams1.w > 0.0) {
		float2 vc = screenUV - 0.5;
		float vd = dot(vc, vc) * 4.0;
		c *= saturate(1.0 - effectParams1.w * vd);
	}

	output.color = float4(OutputColor(c), 1.0) * input.color;
	return output;
}
