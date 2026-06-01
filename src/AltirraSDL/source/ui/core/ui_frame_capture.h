//	AltirraSDL - clean emulator frame capture helpers.

#pragma once

#include <vd2/Kasumi/pixmaputils.h>

class ATSimulator;

enum class ATUIFrameCaptureMode {
	Display,
	TrueAspect,
	Raw,
};

bool ATUICaptureEmulatorFrame(ATSimulator& sim, ATUIFrameCaptureMode mode,
	VDPixmapBuffer& dst);

