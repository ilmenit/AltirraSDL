// AltirraSDL portable custom display effects. All entry points are main-thread only.
#pragma once
#include <cstdint>
#include <string>
#include <vector>

struct ATDisplayCustomEffectPass {
	std::string glsl, msl;
	std::vector<uint8_t> spirv, dxil;
	std::string entrypoint = "main", mslEntrypoint = "main0";
	bool linear = true;
	bool viewport = false;
	float scaleX = 1, scaleY = 1;
};
struct ATDisplayCustomEffectPackage {
	std::vector<ATDisplayCustomEffectPass> passes;
};
// One 48-byte uniform block, same layout on all GPU backends. Source size,
// output size, frame counter and time are available in three vec4/float4s.
struct ATDisplayCustomEffectUniforms {
	float sourceSize[4]; // width, height, reciprocal width, reciprocal height
	float outputSize[4];
	float frame[4]; // frame counter, seconds, sRGB sampler decode, packed extended RGB
};
void ATDisplayCustomEffectSetPath(const wchar_t *path, bool reload);
const std::wstring& ATDisplayCustomEffectGetPath();
const std::wstring& ATDisplayCustomEffectGetActivePath();
const std::string& ATDisplayCustomEffectGetError();
uint64_t ATDisplayCustomEffectGetRevision();
void ATDisplayCustomEffectReportResult(const std::string& error);
// Empty path returns an empty package. On failure the output is unchanged.
bool ATDisplayCustomEffectLoad(const wchar_t *path,
	ATDisplayCustomEffectPackage& package, std::string& error);
// Bound both individual dimensions and total allocation. Returns false instead
// of changing requested shader geometry or allowing an unbounded GPU allocation.
bool ATDisplayCustomEffectSize(const ATDisplayCustomEffectPass& pass,
	int sourceW, int sourceH, float viewportW, float viewportH, int& w, int& h);
