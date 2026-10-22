#include <stdafx.h>
#include "../../src/AltirraSDL/source/display/display_custom_effect.h"
#include <vd2/system/text.h>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <limits>
#include <chrono>

static void Check(bool ok, const char *message) {
	if (!ok) { std::fprintf(stderr, "%s\n", message); std::exit(1); }
}
int main() {
	namespace fs = std::filesystem;
	const auto dir = fs::temp_directory_path() / ("altirra-effects-" +
		std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
	fs::create_directory(dir);
	struct Cleanup { fs::path p; ~Cleanup() { std::error_code ec; fs::remove_all(p, ec); } } cleanup {dir};
	const auto path = dir / "effect.altfx";
	const auto utf8 = path.u8string();
	const auto wide = VDTextU8ToW(VDStringA(std::string(utf8.begin(), utf8.end()).c_str()));
	auto write = [&](const char *json) { std::ofstream(path) << json; };
	std::ofstream(dir / "shader.glsl") << "void main() {}";
	ATDisplayCustomEffectPackage package;
	std::string error;
	write(R"({"version":1,"passes":[{"glsl":"shader.glsl","scale":"viewport","scale_x":0.5,"filter":"nearest"},{"glsl":"shader.glsl"}]})");
	Check(ATDisplayCustomEffectLoad(wide.c_str(), package, error), "Valid multipass effect must load");
	Check(package.passes.size() == 2 && !package.passes[0].linear, "Pass filtering must be preserved");
	int w = 0, h = 0;
	Check(ATDisplayCustomEffectSize(package.passes[0], 320, 240, 1280, 720, w, h)
		&& w == 640 && h == 720, "Viewport scales must use requested geometry");
	Check(!ATDisplayCustomEffectSize(package.passes[0], 320, 240,
		std::numeric_limits<float>::infinity(), 720, w, h), "Nonfinite dimensions must be rejected");
	Check(!ATDisplayCustomEffectSize(package.passes[1], 8192, 8192, 0, 0, w, h), "Total pixel limit must be enforced");
	const char *bad[] = {
		R"({"version":2,"passes":[{"glsl":"shader.glsl"}]})",
		R"({"version":1,"passes":[]})",
		R"({"version":1,"passes":[{"glsl":"shader.glsl","scale_x":0}]})",
		R"({"version":1,"passes":[{"glsl":"shader.glsl","scale_x":"2"}]})",
		R"({"version":1,"passes":[{"glsl":"../shader.glsl"}]})",
		R"({"version":1,"passes":[{"glsl":"missing.glsl"}]})",
		R"({"version":1,"passes":[{"filter":"linear"}]})",
		R"({"version":1,"passes":[{"glsl":"shader.glsl","filter":"cubic"}]})",
		R"({"version":1,"passes":[{"glsl":"shader.glsl","feedback":true}]})",
		R"({"version":1,"passes":[{"spirv":"shader.glsl"}]})",
		R"({"version":1,"passes":[{"glsl":"shader\u0000.glsl"}]})",
	};
	for (const char *json : bad) {
		write(json);
		Check(!ATDisplayCustomEffectLoad(wide.c_str(), package, error) && !error.empty(), "Malformed effect must fail with a diagnostic");
		Check(package.passes.size() == 2, "Failed replacement must retain the last loaded package");
	}
	// On platforms which permit unprivileged symlink creation, a lexical
	// relative path must not bypass package containment through a symlink.
	const auto outside = dir.parent_path() / (dir.filename().string() + "-outside.glsl");
	std::ofstream(outside) << "void main() {}";
	Cleanup cleanupOutside {outside};
	std::error_code symlinkError;
	fs::create_symlink(outside, dir / "linked.glsl", symlinkError);
	if (!symlinkError) {
		write(R"({"version":1,"passes":[{"glsl":"linked.glsl"}]})");
		Check(!ATDisplayCustomEffectLoad(wide.c_str(), package, error), "Shader symlink must not escape the package");
	}
	std::ofstream(dir / "large.glsl") << std::string(1024 * 1024 + 1, 'x');
	write(R"({"version":1,"passes":[{"glsl":"large.glsl"}]})");
	Check(!ATDisplayCustomEffectLoad(wide.c_str(), package, error), "Oversized shader must be rejected");
	Check(!ATDisplayCustomEffectLoad(L"native.cgp", package, error)
		&& error.find("porting") != std::string::npos, "Native effects require an explicit conversion diagnostic");
	ATDisplayCustomEffectSetPath(wide.c_str(), false);
	const auto revision = ATDisplayCustomEffectGetRevision();
	ATDisplayCustomEffectReportResult("");
	ATDisplayCustomEffectSetPath(L"broken.altfx", false);
	ATDisplayCustomEffectReportResult("Invalid effect");
	Check(ATDisplayCustomEffectGetActivePath() == wide.c_str(), "Failed effect must retain the active path");
	ATDisplayCustomEffectSetPath(L"broken.altfx", true);
	Check(ATDisplayCustomEffectGetRevision() == revision + 2, "Forced reload must change revision");
	Check(ATDisplayCustomEffectLoad(L"", package, error) && package.passes.empty(), "Empty path must clear all passes");
	std::puts("Portable custom-effect validation passed");
}
