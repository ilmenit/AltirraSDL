//	AltirraSDL - Cross-platform Atari emulator
//	Logging macros for the SDL3 frontend
//
//	This program is free software; you can redistribute it and/or modify
//	it under the terms of the GNU General Public License as published by
//	the Free Software Foundation; either version 2 of the License, or
//	(at your option) any later version.

#ifndef ALTIRRASDL_LOGGING_H
#define ALTIRRASDL_LOGGING_H

#include <cstdio>

// Log levels (Python-style):
//
//   LOG_ERROR(tag, fmt, ...)   — always on, unrecoverable or serious problems
//   LOG_WARN(tag, fmt, ...)    — always on, non-fatal issues
//   LOG_INFO(tag, fmt, ...)    — always on, startup/status milestones
//   LOG_DEBUG(tag, fmt, ...)   — Debug builds only, detailed diagnostics
//   LOG_TRACE(tag, fmt, ...)   — off by default, per-frame verbose tracing
//
// tag is a short string identifying the subsystem, e.g. "GL", "Audio",
// "AltirraSDL".  Output format:  [tag] message\n
//
// LOG_DEBUG compiles to nothing in Release builds so there is zero
// runtime cost.  LOG_TRACE is controlled by AT_LOG_TRACE_ENABLED;
// define it to 1 before including this header (or in CMake) to turn
// trace logging on.

#define LOG_ERROR(tag, fmt, ...) \
	fprintf(stderr, "[" tag "] ERROR: " fmt "\n" __VA_OPT__(,) __VA_ARGS__)

#define LOG_WARN(tag, fmt, ...) \
	fprintf(stderr, "[" tag "] Warning: " fmt "\n" __VA_OPT__(,) __VA_ARGS__)

#define LOG_INFO(tag, fmt, ...) \
	fprintf(stderr, "[" tag "] " fmt "\n" __VA_OPT__(,) __VA_ARGS__)

#ifdef _DEBUG
	#define LOG_DEBUG(tag, fmt, ...) \
		fprintf(stderr, "[" tag "] " fmt "\n" __VA_OPT__(,) __VA_ARGS__)
#else
	#define LOG_DEBUG(tag, fmt, ...) ((void)0)
#endif

#ifndef AT_LOG_TRACE_ENABLED
	#define AT_LOG_TRACE_ENABLED 0
#endif

#if AT_LOG_TRACE_ENABLED
	#define LOG_TRACE(tag, fmt, ...) \
		fprintf(stderr, "[" tag "] " fmt "\n" __VA_OPT__(,) __VA_ARGS__)
#else
	#define LOG_TRACE(tag, fmt, ...) ((void)0)
#endif

#endif // ALTIRRASDL_LOGGING_H
