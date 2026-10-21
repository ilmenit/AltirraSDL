// AltirraSDL fork: identify existing custom NetSIO transports without
// changing their descriptor, device configuration or external processes.
#pragma once
#include <filesystem>
#include <exception>
#include <fstream>
#include <string>
#include <string_view>
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

// Inspect immutable source bytes supplied by the actual descriptor loader.
// The file wrapper below is also used by standalone import diagnostics/tests.
inline LegacyDescriptor InspectLegacyDescriptorSource(const std::string& path,
	std::string_view bytes, bool available = true) {
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
	if (available && bytes.size() <= 256 * 1024) {
		result.sourceRead = true;
		const auto source = lower(std::string(bytes));
		const auto has = [&](const char* text) {
			return source.find(text) != std::string::npos;
		};
		// Fingerprint behavior, not the path or editable display name.
		result.recognizedLegacy = has("$sio.enable_raw") &&
			has("$network.post_message") && has("network_interrupt") &&
			has("sync_ack_on_command_frame") && has("buffer_to_server");
		result.suspectedLegacy |= !result.recognizedLegacy &&
			has("netsio") && has("$sio.enable_raw") && has("$network");
	}
	result.suspectedLegacy &= !result.recognizedLegacy;
	if (result.recognizedLegacy) result.label = "NetSIO custom device";
	else if (result.suspectedLegacy) result.label = "Possible NetSIO custom device";
	return result;
}
inline LegacyDescriptor InspectLegacyDescriptor(const std::string& path) {
	try {
		std::ifstream input(NativePath(path), std::ios::binary);
		if (input) {
			std::string source(256 * 1024 + 1, '\0');
			input.read(source.data(), (std::streamsize)source.size());
			if (!input.bad()) {
				source.resize((size_t)input.gcount());
				return InspectLegacyDescriptorSource(path, source);
			}
		}
	} catch (const std::exception&) {
		// Descriptor load diagnostics belong to the custom-device loader.
	}
	return InspectLegacyDescriptorSource(path, {}, false);
}
} // namespace ATFujiNetCompat
