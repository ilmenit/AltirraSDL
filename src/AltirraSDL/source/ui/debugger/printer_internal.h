//	AltirraSDL - Debugger printer output internal header
//	Forward decls for the per-format save helpers split out of
//	ui_dbg_printer.cpp in Phase 3h.

#pragma once

#include <vd2/system/vdtypes.h>

class ATPrinterGraphicalOutput;

bool SaveFramebufferAsPNG(const uint32 *framebuffer, int w, int h, const char *path);
bool SaveFramebufferAsPDF(const uint32 *framebuffer, int w, int h,
	float docWidthMM, float docHeightMM, const char *path);
bool SavePrinterOutputAsSVG(ATPrinterGraphicalOutput& output, const char *path);
