#include <stdafx.h>
#include "display_custom_effect.h"
#include <vd2/system/text.h>
#include <vd2/vdjson/jsonreader.h>
#include <vd2/vdjson/jsonvalue.h>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <algorithm>
#include <cstring>
#include <set>

namespace {
std::wstring g_path, g_activePath;
std::string g_error;
uint64_t g_revision = 1;
std::string UTF8(const wchar_t *s) { return VDTextWToU8(VDStringW(s)).c_str(); }
std::string PathUTF8(const std::filesystem::path& path) {
	const auto value = path.u8string();
	return std::string(value.begin(), value.end());
}
std::vector<uint8_t> Read(const std::filesystem::path& path, size_t& budget) {
	std::ifstream input(path, std::ios::binary | std::ios::ate);
	if (!input) throw std::runtime_error("Cannot open effect file: " + PathUTF8(path));
	const auto size = input.tellg();
	if (size <= 0 || size > 1024 * 1024 || (uint64_t)size > budget)
		throw std::runtime_error("Effect files must be nonempty, at most 1 MiB each and 8 MiB in total.");
	budget -= (size_t)size;
	std::vector<uint8_t> data((size_t)size);
	input.seekg(0);
	if (!input.read((char *)data.data(), (std::streamsize)data.size()))
		throw std::runtime_error("Cannot read effect file: " + PathUTF8(path));
	return data;
}
std::string String(const VDJSONValueRef& v, const char *fallback) {
	if (v.IsNull()) return fallback;
	if (!v.IsString() || v.AsStringSpan().size() != wcslen(v.AsString()))
		throw std::runtime_error("Effect text properties must be strings without NUL characters.");
	return UTF8(v.AsString());
}
void Keys(const VDJSONValueRef& value, std::initializer_list<const wchar_t *> allowed) {
	std::set<std::wstring> seen;
	for (auto item : value.AsObject()) {
		const std::wstring name(item.GetName());
		if (!seen.insert(name).second)
			throw std::runtime_error("Duplicate effect property: " + UTF8(name.c_str()));
		if (std::none_of(allowed.begin(), allowed.end(), [&](const wchar_t *s) { return name == s; }))
			throw std::runtime_error("Unsupported effect property: " + UTF8(name.c_str()));
	}
}

}
void ATDisplayCustomEffectSetPath(const wchar_t *path, bool reload) {
	std::wstring next = path ? path : L"";
	if (!reload && next == g_path) return;
	g_path = std::move(next);
	g_error.clear();
	++g_revision;
}
const std::wstring& ATDisplayCustomEffectGetPath() { return g_path; }
const std::wstring& ATDisplayCustomEffectGetActivePath() { return g_activePath; }
const std::string& ATDisplayCustomEffectGetError() { return g_error; }
uint64_t ATDisplayCustomEffectGetRevision() { return g_revision; }
void ATDisplayCustomEffectReportResult(const std::string& error) {
	g_error = error;
	if (error.empty()) g_activePath = g_path;
}
bool ATDisplayCustomEffectLoad(const wchar_t *path,
	ATDisplayCustomEffectPackage& package, std::string& error)
{
	try {
		ATDisplayCustomEffectPackage next;
		if (path && *path) {
			const auto file = std::filesystem::u8path(UTF8(path));
			const auto ext = PathUTF8(file.extension());
			if (ext != ".altfx")
				throw std::runtime_error("Select a portable .altfx package. Native .cgp/.fx effects require porting; see docs/custom-display-effects.md.");
			size_t budget = 8 * 1024 * 1024;
			const auto data = Read(file, budget);
			const auto base = std::filesystem::canonical(file.parent_path().empty()
				? std::filesystem::current_path() : file.parent_path());
			VDJSONDocument document;
			VDJSONReader reader;
			if (!reader.Parse((const char *)data.data(), data.size(), document))
				throw std::runtime_error("Invalid effect JSON.");
			const auto root = document.Root();
			if (!root.IsObject() || !root[L"version"].IsInt() || root[L"version"].AsInt64() != 1)
				throw std::runtime_error("Effect package version must be 1.");
			Keys(root, {L"version", L"passes"});
			const auto passes = root[L"passes"];
			if (!passes.IsArray() || passes.GetArrayLength() < 1 || passes.GetArrayLength() > 8)
				throw std::runtime_error("Effect package must contain 1 to 8 passes.");
			for (auto obj : passes.AsArray()) {
				if (!obj.IsObject()) throw std::runtime_error("Each effect pass must be an object.");
				Keys(obj, {L"glsl", L"spirv", L"dxil", L"msl", L"entrypoint", L"msl_entrypoint", L"filter", L"scale", L"scale_x", L"scale_y"});
				ATDisplayCustomEffectPass pass;
				const auto filter = String(obj[L"filter"], "linear");
				if (filter != "linear" && filter != "nearest") throw std::runtime_error("Unknown effect filter.");
				pass.linear = filter == "linear";
				const auto scale = String(obj[L"scale"], "source");
				if (scale != "source" && scale != "viewport") throw std::runtime_error("Unknown effect scale basis.");
				pass.viewport = scale == "viewport";
				auto factor = [](const VDJSONValueRef& v) {
					if (v.IsNull()) return 1.0f;
					if (!v.IsNumeric() || !std::isfinite(v.AsDouble()) || v.AsDouble() < 0.0625 || v.AsDouble() > 16)
						throw std::runtime_error("Effect scale factors must be between 0.0625 and 16.");
					return (float)v.AsDouble();
				};
				pass.scaleX = factor(obj[L"scale_x"]);
				pass.scaleY = factor(obj[L"scale_y"]);
				pass.entrypoint = String(obj[L"entrypoint"], "main");
				pass.mslEntrypoint = String(obj[L"msl_entrypoint"], "main0");
				if (pass.entrypoint.empty() || pass.mslEntrypoint.empty()) throw std::runtime_error("Shader entrypoint must not be empty.");
				auto asset = [&](const wchar_t *name) {
					const auto value = String(obj[name], "");
					if (value.empty()) return std::vector<uint8_t>();
					const auto relative = std::filesystem::u8path(value);
					if (relative.is_absolute() || relative.has_root_name()) throw std::runtime_error("Effect shader paths must be relative to the package.");
					for (const auto& part : relative) if (part == "..") throw std::runtime_error("Effect shader paths must stay inside the package directory.");
					const auto resolved = std::filesystem::canonical(base / relative);
					if (std::mismatch(base.begin(), base.end(), resolved.begin(), resolved.end()).first != base.end())
						throw std::runtime_error("Effect shader symlinks must stay inside the package directory.");
					return Read(resolved, budget);
				};
				auto glsl = asset(L"glsl");
				pass.glsl.assign(glsl.begin(), glsl.end());
				auto msl = asset(L"msl");
				pass.msl.assign(msl.begin(), msl.end());
				pass.spirv = asset(L"spirv");
				pass.dxil = asset(L"dxil");
				if (pass.glsl.empty() && pass.msl.empty() && pass.spirv.empty() && pass.dxil.empty())
					throw std::runtime_error("Each pass needs at least one shader representation.");
				if ((!pass.glsl.empty() && pass.glsl.find('\0') != std::string::npos)
					|| (!pass.msl.empty() && pass.msl.find('\0') != std::string::npos))
					throw std::runtime_error("Shader source must not contain NUL characters.");
				if (!pass.spirv.empty() && (pass.spirv.size() < 20 || pass.spirv.size() % 4
					|| pass.spirv[0] != 3 || pass.spirv[1] != 2 || pass.spirv[2] != 35 || pass.spirv[3] != 7))
					throw std::runtime_error("Invalid SPIR-V shader header.");
				if (!pass.dxil.empty() && (pass.dxil.size() < 32 || memcmp(pass.dxil.data(), "DXBC", 4)))
					throw std::runtime_error("Invalid DXIL shader header.");
				next.passes.push_back(std::move(pass));
			}
		}
		package = std::move(next);
		error.clear();
		return true;
	} catch (const std::exception& e) {
		error = e.what();
		return false;
	}
}
bool ATDisplayCustomEffectSize(const ATDisplayCustomEffectPass& pass,
	int sourceW, int sourceH, float viewportW, float viewportH, int& w, int& h)
{
	const double x = std::ceil((pass.viewport ? viewportW : sourceW) * (double)pass.scaleX);
	const double y = std::ceil((pass.viewport ? viewportH : sourceH) * (double)pass.scaleY);
	if (!std::isfinite(x) || !std::isfinite(y) || x < 1 || y < 1 || x > 8192 || y > 8192 || x * y > 16777216)
		return false;
	w = (int)x;
	h = (int)y;
	return true;
}
