#include <stdafx.h>

#include "libretro_log.h"

#include <cstdarg>
#include <cstdio>

namespace {
	retro_log_printf_t g_logCallback = nullptr;
}

void ATLibretroSetLogCallback(retro_log_printf_t cb) {
	g_logCallback = cb;
}

void ATLibretroLog(enum retro_log_level level, const char *fmt, ...) {
	va_list args;
	va_start(args, fmt);

	if (g_logCallback) {
		char buf[2048];
		std::vsnprintf(buf, sizeof buf, fmt, args);
		g_logCallback(level, "%s", buf);
	} else {
		std::fputs("[AltirraLibretro] ", stderr);
		std::vfprintf(stderr, fmt, args);
	}

	va_end(args);
}
