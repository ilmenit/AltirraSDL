// Full-screen triangle for the production fragment shader regression.
struct VSOutput {
	float4 color : COLOR0;
	float2 uv : TEXCOORD0;
	float4 position : SV_Position;
};
VSOutput main(uint index : SV_VertexID) {
	VSOutput output;
	float2 uv = float2((index << 1) & 2, index & 2);
	output.color = 1.0;
	output.uv = uv;
	output.position = float4(uv * float2(2.0, -2.0) + float2(-1.0, 1.0), 0.0, 1.0);
	return output;
}
