#include <stdafx.h>
#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <limits>
#include <at/atcore/devicevideo.h>
#include <vd2/Kasumi/pixmaputils.h>
#include "videomanager.h"
#include "simeventmanager.h"
#include "uiaccessors.h"
#include "video_output.h"
#include "uitypes.h"

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
	int cellWidth = 8, cellHeight = 8;
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
	vdrect32 CharToPixelRect(const vdrect32& r) override {
		return vdrect32(info.mDisplayArea.left + r.left * cellWidth,
			info.mDisplayArea.top + r.top * cellHeight,
			info.mDisplayArea.left + r.right * cellWidth,
			info.mDisplayArea.top + r.bottom * cellHeight);
	}
	int ReadRawText(uint8 *, int, int, int) override { return 0; }
	uint32 GetActivityCounter() override { return activity; }
};
int main() {
	const auto square = ATSDLComputeVideoOutputSize(640, 192, 6.0 / 14.0,
		false, 1280, 720, kATDisplayStretchMode_SquarePixels);
	Check(square.x == 1200 && square.y == 720,
		"Alternate square-pixel mode lost native half-dot geometry");
	const auto integral = ATSDLComputeVideoOutputSize(640, 192, 6.0 / 14.0,
		false, 1280, 720, kATDisplayStretchMode_Integral);
	Check(integral.x == 960 && integral.y == 576,
		"Alternate integral mode did not snap native half-dot pixels");
	const auto aspect = ATSDLComputeVideoOutputSize(640, 192, 6.0 / 14.0,
		false, 1280, 720, kATDisplayStretchMode_PreserveAspectRatio);
	Check(std::fabs(aspect.x - 1028.5714f) < 0.01f && aspect.y == 720,
		"Alternate aspect mode lost the device pixel aspect ratio");
	const auto exactSize = ATSDLComputeVideoOutputSize(640, 192, 1,
		true, 1280, 720, kATDisplayStretchMode_Unconstrained);
	Check(exactSize.x == 640 && exactSize.y == 192,
		"Exact device pixels stretched beyond their native size");
	const auto reduced = ATSDLComputeVideoOutputSize(640, 192, 1,
		true, 320, 240, kATDisplayStretchMode_Unconstrained);
	Check(reduced.x == 320 && reduced.y == 96,
		"Exact device pixels did not scale down to fit");
	const auto retinaExact = ATSDLComputeVideoOutputSize(512, 480, 1,
		true, 1280, 720, kATDisplayStretchMode_Unconstrained, 2);
	Check(retinaExact.x == 256 && retinaExact.y == 240,
		"Exact pixels were enlarged by the Retina backing scale");
	const auto retinaIntegral = ATSDLComputeVideoOutputSize(640, 192, 6.0 / 14.0,
		false, 1280, 720, kATDisplayStretchMode_Integral, 2);
	Check(retinaIntegral.x == 1120 && retinaIntegral.y == 672,
		"Integral alternate fitting did not snap in physical pixel units");
	const auto retinaAspectIntegral = ATSDLComputeVideoOutputSize(512, 480, 1,
		false, 1280, 720, kATDisplayStretchMode_IntegralPreserveAspectRatio, 2);
	Check(retinaAspectIntegral.x == 768 && retinaAspectIntegral.y == 720,
		"Integral aspect fitting snapped in logical instead of physical units");
	for (const float invalidDensity : {0.0f, -1.0f,
		std::numeric_limits<float>::infinity(),
		std::numeric_limits<float>::quiet_NaN()}) {
		const auto guarded = ATSDLComputeVideoOutputSize(512, 480, 1,
			true, 1280, 720, kATDisplayStretchMode_Unconstrained, invalidDensity);
		Check(guarded.x == 512 && guarded.y == 480,
			"Invalid backing scale was not replaced by a finite positive default");
	}
	Output text("text", L"Text");
	text.info.mDisplayArea = vdrect32(10, 20, 650, 212);
	text.info.mTextColumns = 80;
	text.info.mTextRows = 24;
	vdrect32f selection;
	Check(ATSDLGetVideoOutputTextRect(text, 2, 3, 4, selection)
		&& selection.left == 0.025f && selection.top == 0.125f,
		"Text highlight did not account for the cropped display origin");
	text.cellWidth = 16;
	Check(ATSDLGetVideoOutputTextRect(text, 2, 3, 4, selection)
		&& selection.left == 0.05f && selection.right == 0.15f,
		"Text highlight reused obsolete pixel coordinates after layout change");
	text.info.mDisplayArea = vdrect32(0, 0, 0, 0);
	Check(!ATSDLGetVideoOutputTextRect(text, 2, 3, 4, selection),
		"Empty device raster allowed division by zero in text highlights");
	text.info.mDisplayArea = vdrect32(0, 0, 640, 192);
	text.info.mTextRows = 0;
	Check(!ATSDLGetVideoOutputTextRect(text, 2, 3, 4, selection),
		"Unavailable device text rows retained a highlight");
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
