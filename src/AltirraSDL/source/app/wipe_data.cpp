//	AltirraSDL - User data wipe implementation
//
//	Walks ATGetConfigDir() depth-first via SDL_EnumerateDirectory,
//	removing files and then their parent directories with
//	SDL_RemovePath.  After the walk, the config dir is left in place
//	(empty) and std::_Exit(0) terminates the process so no atexit /
//	shutdown handler re-creates settings.ini from the in-memory
//	registry.

#include <stdafx.h>
#include <cstdlib>
#include <SDL3/SDL.h>
#include <vd2/system/VDString.h>

#include "wipe_data.h"

extern VDStringA ATGetConfigDir();

namespace {
	struct WipeContext {
		bool ok;
	};

	// Append "fname" to "dir" with a single separator, regardless of
	// whether "dir" already ends in '/'.  SDL_EnumerateDirectory hands
	// us directories without a trailing separator.
	VDStringA Join(const char *dir, const char *fname) {
		VDStringA out;
		if (dir && *dir) {
			out = dir;
			if (out.back() != '/' && out.back() != '\\')
				out += '/';
		}
		if (fname && *fname)
			out.append(fname);
		return out;
	}

	SDL_EnumerationResult SDLCALL WipeCallback(void *userdata,
		const char *dirname, const char *fname)
	{
		// SDL filters "." and ".." for us; defensive guard anyway.
		if (!fname || !*fname) return SDL_ENUM_CONTINUE;
		if (fname[0] == '.' && (fname[1] == 0
			|| (fname[1] == '.' && fname[2] == 0)))
			return SDL_ENUM_CONTINUE;

		auto *ctx = static_cast<WipeContext *>(userdata);
		VDStringA path = Join(dirname, fname);

		SDL_PathInfo info;
		if (!SDL_GetPathInfo(path.c_str(), &info)) {
			// Unreadable — skip but don't fail the whole wipe.
			return SDL_ENUM_CONTINUE;
		}

		if (info.type == SDL_PATHTYPE_DIRECTORY) {
			// Recurse first, then remove the (now-empty) directory.
			SDL_EnumerateDirectory(path.c_str(), WipeCallback, ctx);
			if (!SDL_RemovePath(path.c_str()))
				ctx->ok = false;
		} else {
			if (!SDL_RemovePath(path.c_str()))
				ctx->ok = false;
		}

		return SDL_ENUM_CONTINUE;
	}
}

bool ATWipeAllUserData() {
	VDStringA root = ATGetConfigDir();
	if (root.empty())
		return false;

	WipeContext ctx{ true };
	if (!SDL_EnumerateDirectory(root.c_str(), WipeCallback, &ctx))
		return false;

	// The config dir itself is intentionally left in place — it's a
	// well-known path used by ATRegistryLoadFromDisk and the various
	// cache subsystems on next launch, and removing it would force
	// every consumer to re-create it.
	return ctx.ok;
}

void ATWipeAndExit() {
	ATWipeAllUserData();

	// std::_Exit bypasses C++ destructors and atexit handlers, which
	// is exactly what we want here — the normal shutdown path runs
	// ATRegistryFlushToDisk which would re-create settings.ini from
	// the still-populated in-memory VDRegistryProviderMemory and
	// undo the wipe.  Android: this also tears down the activity
	// process; SDLActivity handles the surface cleanup at the JVM level.
	std::_Exit(0);
}
