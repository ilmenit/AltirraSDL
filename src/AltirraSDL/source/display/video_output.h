// AltirraSDL - alternate device video output routing.
#pragma once
#include <vd2/system/vdtypes.h>
#include <vd2/system/vectors.h>

class IATDeviceVideoManager;
class IATDeviceVideoOutput;
class ATSimulatorEventManager;
struct VDPixmap;

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
