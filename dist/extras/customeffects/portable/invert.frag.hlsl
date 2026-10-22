cbuffer Context : register(b0, space3) {
	float4 sourceSize;
	float4 outputSize;
	float4 frame;
};
Texture2D sourceTexture : register(t0, space2);
SamplerState sourceSampler : register(s0, space2);
struct Input { float4 color : TEXCOORD0; float2 uv : TEXCOORD1; };
float3 EncodeSRGB(float3 c) {
	return lerp(12.92 * c, 1.055 * pow(max(c, 0.0), 1.0 / 2.4) - 0.055, c > 0.0031308);
}
float4 main(Input input) : SV_Target {
	float4 c = sourceTexture.Sample(sourceSampler, input.uv);
	if (frame.z > 0.5) c.rgb = EncodeSRGB(c.rgb);
	if (frame.w > 0.5) c.rgb = c.rgb * (255.0 / 127.0) - 64.0 / 127.0;
	c.rgb = 1.0 - c.rgb;
	if (frame.w > 0.5) c.rgb = (c.rgb * 127.0 + 64.0) / 255.0;
	return c;
}
