// AltirraSDL - alternate device video output routing.
#pragma once
#include <vd2/system/vdtypes.h>
#include <vd2/system/vectors.h>

class IATDeviceVideoManager;
class IATDeviceVideoOutput;
class ATSimulatorEventManager;
struct VDPixmap;
enum ATDisplayStretchMode : uint32;

// Windows GetAltDisplayArea uses half-dot pixels for the square-pixel modes.
// Fit/snapping uses physical pixels; returned dimensions use viewport units.
vdfloat2 ATSDLComputeVideoOutputSize(int width, int height, double par,
	bool exactPixels, float viewportWidth, float viewportHeight,
	ATDisplayStretchMode mode, float pixelsPerUnit = 1.0f);
// Normalized visible-image rectangle, recomputed from current character layout.
bool ATSDLGetVideoOutputTextRect(IATDeviceVideoOutput& output, int column,
	int row, int length, vdrect32f& rect);

void ATSDLVideoOutputInit(IATDeviceVideoManager&, ATSimulatorEventManager&);
void ATSDLVideoOutputShutdown();
void ATSDLVideoOutputUpdate();
uint64 ATSDLGetVideoOutputRevision();
IATDeviceVideoManager *ATSDLGetVideoOutputManager();
// Non-null only when the selected output replaces the computer signal.
IATDeviceVideoOutput *ATSDLGetActiveVideoOutput();
const VDPixmap *ATSDLGetVideoOutputFrame();
bool ATSDLGetVideoOutputGeometry(int& width, int& height, double& par,
	bool& exactPixels);
