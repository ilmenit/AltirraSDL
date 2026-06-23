#ifndef ALTIRRA_LIBRETRO_VIDEO_H
#define ALTIRRA_LIBRETRO_VIDEO_H

#include <vd2/system/vdtypes.h>
#include <vd2/Kasumi/pixmaputils.h>

class ATSimulator;
class IVDVideoDisplay;

IVDVideoDisplay *ATLibretroCreateNullVideoDisplay();
bool ATLibretroNullVideoDisplayConsumeFramePosted(IVDVideoDisplay *display);
bool ATLibretroCaptureXrgb(ATSimulator& sim, VDPixmapBuffer& dst);

#endif
