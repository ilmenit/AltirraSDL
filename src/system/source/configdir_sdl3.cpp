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
#include <direct.h>   // _wmkdir
#include <shlobj.h>   // SHGetFolderPathW
#include <vd2/system/text.h>
#endif

#if defined(__ANDROID__) && !defined(ALTIRRA_LIBRETRO_NO_SDL3)
#include <SDL3/SDL_filesystem.h>
#include <SDL3/SDL_stdinc.h>
#endif

static VDStringA s_configDir;

VDStringA ATGetConfigDir() {
	if (s_configDir.empty()) {
		VDStringA dir;

#ifdef _WIN32
		// Use %APPDATA%/altirra on Windows. Callers treat the result as
		// UTF-8 (SDL file APIs, VDTextU8ToW), so query the wide path: the
		// ANSI API returns the legacy code page and mangles non-ASCII user
		// names such as C:\\Users\\Żółw.
		wchar_t appdata[MAX_PATH];
		VDStringW wide;
		if (SUCCEEDED(SHGetFolderPathW(nullptr, CSIDL_APPDATA, nullptr, 0, appdata))) {
			wide = appdata;
		} else {
			const wchar_t *env = _wgetenv(L"APPDATA");
			wide = env && *env ? env : L".";
		}
		wide += L"\\altirra";
		_wmkdir(wide.c_str());
		dir = VDTextWToU8(wide);
#elif defined(__ANDROID__) && !defined(ALTIRRA_LIBRETRO_NO_SDL3)
		// On Android the only reliable writable location is the app's
		// internal storage directory.  SDL_GetPrefPath returns something
		// like /data/user/0/org.altirra.app/files/Altirra/AltirraSDL/
		// — guaranteed writable, survives upgrades, cleaned on uninstall.
		// Do NOT use $HOME on Android: it points inside the APK on some
		// devices and mkdir fails.
		{
			char *p = SDL_GetPrefPath("Altirra", "AltirraSDL");
			if (p && *p) {
				dir = p;
				// SDL_GetPrefPath always appends a trailing slash; strip
				// it so the rest of the code can append "/subdir" safely.
				if (!dir.empty() && dir.back() == '/')
					dir.pop_back();
				SDL_free(p);
			} else {
				if (p) SDL_free(p);
				// Last resort — not normally writable, but at least we
				// don't try to mkdir a garbage path.
				dir = "/data/local/tmp/altirra";
			}
		}
		mkdir(dir.c_str(), 0755);
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
