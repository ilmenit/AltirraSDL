// AltirraSDL fork: identify existing custom NetSIO transports without
// changing their descriptor, device configuration or external processes.
#pragma once
#include <filesystem>
#include <exception>
#include <fstream>
#include <string>
#include <utility>

namespace ATFujiNetCompat {
inline std::filesystem::path NativePath(const std::string& path) {
#if defined(__cpp_char8_t)
	return std::filesystem::path(std::u8string(path.begin(), path.end()));
#else
	return std::filesystem::u8path(path);
#endif
}
struct LegacyDescriptor {
	bool recognizedLegacy = false;
	bool suspectedLegacy = false;
	bool sourceRead = false;
	std::string label;
	std::string path;
};

inline LegacyDescriptor InspectLegacyDescriptor(const std::string& path) {
	LegacyDescriptor result;
	result.path = path;
	if (path.empty()) return result;
	const auto lower = [](std::string text) {
		for (char& c : text)
			if (c >= 'A' && c <= 'Z') c += 'a' - 'A';
		return text;
	};
	const auto separator = path.find_last_of("/\\");
	const auto filename = lower(path.substr(separator == std::string::npos ? 0 : separator + 1));
	// A missing/unreadable descriptor with the traditional name still
	// deserves an explicit choice. Its filename alone is not proof.
	result.suspectedLegacy = filename == "netsio.atdevice" ||
		filename == "fujinet-emulator-bridge.atdevice";
	try {
		std::ifstream input(NativePath(path), std::ios::binary);
		if (input) {
			constexpr size_t limit = 256 * 1024;
			std::string source(limit + 1, '\0');
			input.read(source.data(), (std::streamsize)source.size());
			const auto size = input.gcount();
			if (!input.bad() && size <= (std::streamsize)limit) {
				result.sourceRead = true;
				source.resize((size_t)size);
				source = lower(std::move(source));
				const auto has = [&](const char* text) {
					return source.find(text) != std::string::npos;
				};
				// Fingerprint behavior, not the path or editable display name.
				// These markers identify the published bridge and renamed or
				// locally configured copies; generic custom network/SIO
				// devices must not be mistaken for a competing FujiNet bus.
				result.recognizedLegacy = has("$sio.enable_raw") &&
					has("$network.post_message") && has("network_interrupt") &&
					has("sync_ack_on_command_frame") && has("buffer_to_server");
				result.suspectedLegacy |= !result.recognizedLegacy &&
					has("netsio") && has("$sio.enable_raw") && has("$network");
			}
		}
	} catch (const std::exception&) {
		// Detection must not make a profile containing a stale descriptor
		// unusable. The normal custom-device loader owns its diagnostics.
	}
	result.suspectedLegacy &= !result.recognizedLegacy;
	if (result.recognizedLegacy) result.label = "NetSIO custom device";
	else if (result.suspectedLegacy) result.label = "Possible NetSIO custom device";
	return result;
}
} // namespace ATFujiNetCompat
