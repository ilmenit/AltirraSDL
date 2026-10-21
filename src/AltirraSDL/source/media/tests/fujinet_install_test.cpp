// Driver for fujinet_install_test.py: one installer operation per process.
#include <stdafx.h>
#include "fujinet_install.h"
#include "fujinet_pc.h"
#include <vd2/system/error.h>
#include <cstdio>
#include <stdexcept>
#include <string>

int main(int argc, char** argv) {
	if (argc < 4) return 2;
	const std::string mode = argv[1];
	ATFujiNetInstall::Progress progress;
	if (mode == "extract") {
		// extract <archive> <directory>
		try {
			ATFujiNetInstall::Extract(argv[2], argv[3], progress);
			std::printf("ERROR \n");
		} catch (const MyError& error) {
			std::printf("ERROR %s\n", error.c_str());
		} catch (const std::exception& error) {
			std::printf("ERROR %s\n", error.what());
		}
		return 0;
	}
	if (mode != "install" || argc < 5) return 2;
	// install <releases-url> <install-root> <suffix>[,<suffix>...]
	ATFujiNetPCPackage package;
	package.system = "test system";
	for (std::string list = argv[4]; !list.empty();) {
		const auto comma = list.find(',');
		package.assetSuffixes.push_back(list.substr(0, comma));
		list = comma == std::string::npos ? std::string() : list.substr(comma + 1);
	}
	const auto result = ATFujiNetInstall::Install(package, argv[3], progress, argv[2]);
	// The panel retires older versions once the new one is selected.
	if (result.error.empty()) ATFujiNetInstall::RemoveOtherVersions(argv[3], result.executable);
	std::printf("EXECUTABLE %s\nVERSION %s\nERROR %s\n", result.executable.c_str(),
		result.version.c_str(), result.error.c_str());
	for (const auto& library : result.missing)
		std::printf("MISSING %s %s\n", library.formula.c_str(), library.path.c_str());
	std::printf("COMMAND %s\n", ATFujiNetInstall::HomebrewCommand(result.missing).c_str());
	return 0;
}
