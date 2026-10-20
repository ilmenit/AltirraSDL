#include <stdafx.h>
#include <cstdio>
#include <cstdlib>
#include <at/atcore/devicevideo.h>
#include <vd2/Kasumi/pixmaputils.h>
#include "videomanager.h"
#include "simeventmanager.h"
#include "uiaccessors.h"
#include "video_output.h"

static void Check(bool condition, const char *message) {
	if (!condition) {
		fprintf(stderr, "%s\n", message);
		exit(1);
	}
}
class Output final : public IATDeviceVideoOutput {
public:
	const char *name;
	const wchar_t *label;
	ATDeviceVideoInfo info {};
	VDPixmapBuffer pixels;
	uint32 activity = 0;
	Output(const char *n, const wchar_t *l) : name(n), label(l) {
		pixels.init(4, 2, nsVDPixmap::kPixFormat_XRGB8888);
		for (int y = 0; y < 2; ++y) {
			auto *row = (uint32 *)((char *)pixels.data + y * pixels.pitch);
			for (int x = 0; x < 4; ++x)
				row[x] = (y + 1) * 16 + x;
		}
		info.mbSignalValid = true;
		info.mDisplayArea = vdrect32(1, 0, 3, 2);
		info.mPixelAspectRatio = 1.25;
	}
	const char *GetName() const override { return name; }
	const wchar_t *GetDisplayName() const override { return label; }
	void Tick(uint32) override {}
	void UpdateFrame() override {}
	const VDPixmap& GetFrameBuffer() override { return pixels; }
	const ATDeviceVideoInfo& GetVideoInfo() override { return info; }
	vdpoint32 PixelToCaretPos(const vdpoint32& p) override { return p; }
	vdrect32 CharToPixelRect(const vdrect32& r) override { return r; }
	int ReadRawText(uint8 *, int, int, int) override { return 0; }
	uint32 GetActivityCounter() override { return activity; }
};
int main() {
	ATVideoManager manager;
	ATSimulatorEventManager events;
	Output xep("xep80", L"XEP80"), bit3("bit3", L"Bit 3");
	manager.AddVideoOutput(&xep);
	manager.AddVideoOutput(&bit3);
	ATSDLVideoOutputInit(manager, events);
	Check(ATUIIsAltOutputAvailable(), "Devices not enumerated");
	ATUISelectNextAltOutput();
	Check(ATUIGetCurrentAltViewIndex() == 0, "Cycle did not select first output");
	ATUISelectPrevAltOutput();
	Check(!ATUIGetAltViewEnabled(), "Previous did not return to computer");
	ATUISelectPrevAltOutput();
	Check(ATUIIsXEPViewEnabled(), "Previous from computer did not wrap");
	const uint64 beforeFirstFrame = ATSDLGetVideoOutputRevision();
	ATSDLVideoOutputUpdate();
	Check(ATSDLGetVideoOutputRevision() > beforeFirstFrame,
		"Fresh output did not request a presentation upload");
	const uint64 stableFrame = ATSDLGetVideoOutputRevision();
	ATSDLVideoOutputUpdate();
	Check(ATSDLGetVideoOutputRevision() == stableFrame,
		"Unchanged output repeatedly requested a presentation upload");
	const auto *frame = ATSDLGetVideoOutputFrame();
	Check(frame && frame->w == 2 && frame->h == 2, "Display crop not applied");
	Check(*(const uint32 *)frame->data == 17, "Wrong cropped source pixel");
	int w, h;
	double par;
	bool exact;
	Check(ATSDLGetVideoOutputGeometry(w, h, par, exact) && par == 1.25,
		"Device aspect ratio lost");
	// XEP80 uses a bit-packed palette. Cropping at x=1 must select
	// the second bit, not round to the next source byte.
	uint32 palette[2] = {0x00112233, 0x00abcdef};
	xep.pixels.init(16, 2, nsVDPixmap::kPixFormat_Pal1);
	xep.pixels.palette = palette;
	*(uint8 *)xep.pixels.data = 0x40;
	xep.info.mFrameBufferLayoutChangeCount++;
	ATSDLVideoOutputUpdate();
	frame = ATSDLGetVideoOutputFrame();
	Check(frame && (*(const uint32 *)frame->data & 0xffffff) == palette[1],
		"Pal1 crop was not bit accurate");
	xep.info.mbSignalValid = false;
	ATSDLVideoOutputUpdate();
	frame = ATSDLGetVideoOutputFrame();
	Check(frame && *(const uint32 *)frame->data == 0,
		"Invalid signal did not blank output");
	xep.info.mbSignalPassThrough = true;
	const uint64 beforePassThrough = ATSDLGetVideoOutputRevision();
	ATSDLVideoOutputUpdate();
	Check(ATSDLGetVideoOutputRevision() > beforePassThrough,
		"Pass-through did not request restoration of computer texture");
	Check(!ATSDLGetVideoOutputFrame(), "Pass-through did not restore computer output");
	xep.info.mbSignalPassThrough = false;
	ATUISetCurrentAltOutputName("");
	++xep.activity;
	events.NotifyEvent(kATSimEvent_FrameTick);
	Check(ATUIIsXEPViewEnabled(), "Activity did not switch to XEP80");
	ATUISetAltViewAutoswitchingEnabled(false);
	++bit3.activity;
	events.NotifyEvent(kATSimEvent_FrameTick);
	Check(ATUIIsXEPViewEnabled(), "Disabled autoswitch changed output");
	manager.RemoveVideoOutput(&xep);
	Check(!ATUIGetAltViewEnabled(), "Removed device left dangling selection");
	ATUISetAltViewAutoswitchingEnabled(true);
	ATUISelectNextAltOutput();
	events.NotifyEvent(kATSimEvent_WarmReset);
	Check(!ATUIGetAltViewEnabled(), "Reset did not restore computer output");
	ATSDLVideoOutputShutdown();
	manager.RemoveVideoOutput(&bit3);
	events.NotifyEvent(kATSimEvent_FrameTick);
	Check(!ATSDLGetVideoOutputManager(), "Shutdown left callbacks registered");
	return 0;
}
