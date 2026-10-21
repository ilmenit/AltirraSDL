#include <stdafx.h>
#include "fujinet_compat.h"
#include <chrono>
#include <cstdio>
#include <fstream>
#include <stdexcept>

int main() {
	namespace fs = std::filesystem;
	const auto root = fs::temp_directory_path() / ("altirra-fujinet-compat-" +
		std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
	fs::create_directory(root);
	try {
		const auto utf8 = [](const fs::path& path) {
			const auto encoded = path.u8string();
			return std::string(encoded.begin(), encoded.end());
		};
		const auto inspect = [&](const char* name, const std::string& body) {
			const auto path = root / ATFujiNetCompat::NativePath(name);
			{ std::ofstream file(path, std::ios::binary); file << body; }
			return ATFujiNetCompat::InspectLegacyDescriptor(utf8(path));
		};
		const auto require = [](bool value) {
			if (!value) throw std::runtime_error("FujiNet compatibility detection failed");
		};
		const std::string signature = "$sio.enable_raw(true); $network.post_message "
			"network_interrupt sync_ack_on_command_frame buffer_to_server";
		auto result = inspect("renamed bus.atdevice", signature);
		require(result.recognizedLegacy && !result.suspectedLegacy &&
			!result.label.empty() && !result.path.empty());
		result = inspect("unicode-\xc5\xbc\xc3\xb3\xc5\x82w.atdevice", signature);
		require(result.recognizedLegacy && result.sourceRead);
		result = inspect("unrelated.atdevice", "$sio.enable_raw $network.post_message network_interrupt");
		require(!result.recognizedLegacy && !result.suspectedLegacy);
		result = inspect("NetSIO.atdevice", "unrelated custom device");
		require(!result.recognizedLegacy && result.suspectedLegacy);
		result = inspect("renamed old version.atdevice", "NetSIO $sio.enable_raw $network");
		require(!result.recognizedLegacy && result.suspectedLegacy);
		result = inspect("large.atdevice", std::string(256 * 1024 + 1, ' ') + signature);
		require(!result.recognizedLegacy && !result.suspectedLegacy);
		result = ATFujiNetCompat::InspectLegacyDescriptor(utf8(root / "netsio.atdevice"));
		require(!result.recognizedLegacy && result.suspectedLegacy);
		result = ATFujiNetCompat::InspectLegacyDescriptor("");
		require(!result.recognizedLegacy && !result.suspectedLegacy);
	} catch (const std::exception& error) {
		fs::remove_all(root);
		std::fprintf(stderr, "%s\n", error.what());
		return 1;
	}
	fs::remove_all(root);
	std::puts("FujiNet compatibility detection passed");
}
