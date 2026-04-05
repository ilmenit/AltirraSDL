//	Altirra - Atari 800/800XL/5200 emulator
//	System library - config directory for SDL3 builds (all platforms)
//
//	Provides ATGetConfigDir() which returns the path to the user config
//	directory (~/.config/altirra on Linux/macOS, %APPDATA%/altirra on Windows).

#include <stdafx.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <vd2/system/VDString.h>

#ifdef _WIN32
#include <direct.h>   // _mkdir
#include <shlobj.h>   // SHGetFolderPathA
#endif

static VDStringA s_configDir;

VDStringA ATGetConfigDir() {
	if (s_configDir.empty()) {
		VDStringA dir;

#ifdef _WIN32
		// Use %APPDATA%/altirra on Windows
		char appdata[MAX_PATH];
		if (SUCCEEDED(SHGetFolderPathA(nullptr, CSIDL_APPDATA, nullptr, 0, appdata))) {
			dir = appdata;
		} else {
			const char *env = getenv("APPDATA");
			if (env && *env)
				dir = env;
			else
				dir = ".";
		}
		dir += "\\altirra";
		_mkdir(dir.c_str());
#else
		// XDG Base Directory: use $XDG_CONFIG_HOME or ~/.config
		const char *xdgConfig = getenv("XDG_CONFIG_HOME");
		if (xdgConfig && *xdgConfig) {
			dir = xdgConfig;
		} else {
			const char *home = getenv("HOME");
			if (home && *home) {
				dir = home;
				dir += "/.config";
			} else {
				dir = "/tmp";
			}
		}
		dir += "/altirra";
		mkdir(dir.c_str(), 0755);
#endif

		s_configDir = dir;
	}
	return s_configDir;
}
