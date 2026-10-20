// AltirraSDL - device video routing matching ATUIVideoDisplayWindow.
#include <stdafx.h>
#include <algorithm>
#include <cstring>
#include <cmath>
#include <vd2/system/VDString.h>
#include <vd2/Kasumi/pixmap.h>
#include <vd2/Kasumi/pixmaputils.h>
#include <vd2/Kasumi/pixmapops.h>
#include <at/atcore/devicevideo.h>
#include "video_output.h"
#include "simeventmanager.h"
#include "uiaccessors.h"
#include "uitypes.h"

vdfloat2 ATSDLComputeVideoOutputSize(int width, int height, double par,
	bool exactPixels, float viewportWidth, float viewportHeight,
	ATDisplayStretchMode mode, float pixelsPerUnit)
{
	if (!std::isfinite(pixelsPerUnit) || pixelsPerUnit <= 0)
		pixelsPerUnit = 1.0f;
	viewportWidth *= pixelsPerUnit;
	viewportHeight *= pixelsPerUnit;
	const float sourceWidth = (float)std::max(1, width);
	const float sourceHeight = (float)std::max(1, height);
	if (exactPixels) {
		const float ratio = std::min(1.0f,
			std::min(viewportWidth / sourceWidth, viewportHeight / sourceHeight));
		return {sourceWidth * ratio / pixelsPerUnit,
			sourceHeight * ratio / pixelsPerUnit};
	}
	if (mode == kATDisplayStretchMode_Unconstrained)
		return {viewportWidth / pixelsPerUnit, viewportHeight / pixelsPerUnit};
	const double effectivePAR = mode == kATDisplayStretchMode_SquarePixels
		|| mode == kATDisplayStretchMode_Integral ? 0.5 : par;
	const float fitWidth = (float)(sourceWidth * effectivePAR);
	float scale = std::min(viewportWidth / fitWidth, viewportHeight / sourceHeight);
	if (scale > 1.0f && (mode == kATDisplayStretchMode_Integral
		|| mode == kATDisplayStretchMode_IntegralPreserveAspectRatio))
		scale = std::floor(scale);
	return {fitWidth * scale / pixelsPerUnit, sourceHeight * scale / pixelsPerUnit};
}

bool ATSDLGetVideoOutputTextRect(IATDeviceVideoOutput& output, int column,
	int row, int length, vdrect32f& rect)
{
	const auto& info = output.GetVideoInfo();
	const auto& area = info.mDisplayArea;
	if (area.empty() || row < 0 || row >= info.mTextRows || column < 0
		|| column >= info.mTextColumns || length <= 0)
		return false;
	length = std::min(length, info.mTextColumns - column);
	const auto pixels = output.CharToPixelRect(
		vdrect32(column, row, column + length, row + 1));
	if (pixels.empty())
		return false;
	rect = vdrect32f(
		(float)(pixels.left - area.left) / area.width(),
		(float)(pixels.top - area.top) / area.height(),
		(float)(pixels.right - area.left) / area.width(),
		(float)(pixels.bottom - area.top) / area.height());
	return true;
}

namespace {
IATDeviceVideoManager *s_manager = nullptr;
ATSimulatorEventManager *s_events = nullptr;
VDStringA s_name;
bool s_autoSwitch = true;
VDPixmapBuffer s_frame;
VDPixmapBuffer s_conversion;
bool s_frameValid = false;
IATDeviceVideoOutput *s_lastOutput = nullptr;
uint32 s_change = 0;
uint32 s_layout = 0;
bool s_signalValid = false;
uint64 s_revision = 0;
vdfunction<void(uint32)> s_removing;
uint32 s_frameCallback = 0, s_coldCallback = 0, s_warmCallback = 0;

IATDeviceVideoOutput *Selected() {
	return s_manager && !s_name.empty()
		? s_manager->GetOutputByName(s_name.c_str()) : nullptr;
}
void Reset() {
	if (s_autoSwitch)
		ATUISetCurrentAltOutputName("");
	s_manager->CheckForNewlyActiveOutputs();
}
}

void ATSDLVideoOutputInit(IATDeviceVideoManager& manager,
	ATSimulatorEventManager& events)
{
	ATSDLVideoOutputShutdown();
	s_manager = &manager;
	s_events = &events;
	s_removing = [](uint32 index) {
		if (s_manager->GetOutput(index) == Selected())
			ATUISetCurrentAltOutputName("");
	};
	manager.OnRemovingOutput().Add(&s_removing);
	s_frameCallback = events.AddEventCallback(kATSimEvent_FrameTick, [] {
		if (s_autoSwitch) {
			const sint32 index = s_manager->CheckForNewlyActiveOutputs();
			if (index >= 0)
				ATUISetAltViewByIndex(index);
		}
	});
	s_coldCallback = events.AddEventCallback(kATSimEvent_ColdReset, Reset);
	s_warmCallback = events.AddEventCallback(kATSimEvent_WarmReset, Reset);
}

void ATSDLVideoOutputShutdown() {
	++s_revision;
	if (s_manager)
		s_manager->OnRemovingOutput().Remove(&s_removing);
	if (s_events) {
		s_events->RemoveEventCallback(s_frameCallback);
		s_events->RemoveEventCallback(s_coldCallback);
		s_events->RemoveEventCallback(s_warmCallback);
	}
	s_manager = nullptr;
	s_events = nullptr;
	s_lastOutput = nullptr;
	s_frameValid = false;
	s_frame.clear();
	s_conversion.clear();
}

IATDeviceVideoManager *ATSDLGetVideoOutputManager() { return s_manager; }
IATDeviceVideoOutput *ATSDLGetActiveVideoOutput() {
	auto *output = Selected();
	return output && !output->GetVideoInfo().mbSignalPassThrough
		? output : nullptr;
}

uint64 ATSDLGetVideoOutputRevision() { return s_revision; }

void ATSDLVideoOutputUpdate() {
	auto *output = ATSDLGetActiveVideoOutput();
	if (!output) {
		if (s_frameValid)
			++s_revision;
		s_frameValid = false;
		s_lastOutput = nullptr;
		return;
	}
	output->UpdateFrame();
	const auto& info = output->GetVideoInfo();
	const auto& px = output->GetFrameBuffer();
	const vdrect32 area = info.mDisplayArea;
	if (area.width() <= 0 || area.height() <= 0
		|| area.left < 0 || area.top < 0 || area.right > px.w
		|| area.bottom > px.h || !px.data) {
		// A connected but unprogrammed device still replaces the signal.
		// Keep a black pixel available instead of exposing the GTIA frame.
		if (!s_frameValid || s_frame.w != 1 || s_frame.h != 1)
			++s_revision;
		s_frame.init(1, 1, nsVDPixmap::kPixFormat_XRGB8888);
		*(uint32 *)s_frame.data = 0;
		s_frameValid = true;
		s_lastOutput = nullptr;
		return;
	}
	if (s_frameValid && output == s_lastOutput
		&& info.mFrameBufferChangeCount == s_change
		&& info.mFrameBufferLayoutChangeCount == s_layout
		&& info.mbSignalValid == s_signalValid)
		return;

	++s_revision;
	s_frame.init(area.width(), area.height(), nsVDPixmap::kPixFormat_XRGB8888);
	if (info.mbSignalValid) {
		// Convert before cropping: XEP80 can use bit-packed Pal1, whose
		// visible area need not start on a byte boundary.
		s_conversion.init(px.w, px.h, nsVDPixmap::kPixFormat_XRGB8888);
		s_frameValid = VDPixmapBlt(s_conversion, px);
		if (s_frameValid)
			s_frameValid = VDPixmapBlt(s_frame, 0, 0, s_conversion,
				area.left, area.top, area.width(), area.height());
	} else {
		// Invalid scanning parameters produce a black display, not stale pixels.
		for (int y = 0; y < s_frame.h; ++y)
			std::fill_n((uint32 *)((char *)s_frame.data + y * s_frame.pitch),
				s_frame.w, 0u);
		s_frameValid = true;
	}
	s_lastOutput = output;
	s_change = info.mFrameBufferChangeCount;
	s_layout = info.mFrameBufferLayoutChangeCount;
	s_signalValid = info.mbSignalValid;
}

const VDPixmap *ATSDLGetVideoOutputFrame() {
	return s_frameValid && ATSDLGetActiveVideoOutput() ? &s_frame : nullptr;
}
bool ATSDLGetVideoOutputGeometry(int& width, int& height, double& par,
	bool& exactPixels)
{
	auto *output = ATSDLGetActiveVideoOutput();
	if (!output)
		return false;
	const auto& vi = output->GetVideoInfo();
	width = std::max(1, vi.mDisplayArea.width());
	height = std::max(1, vi.mDisplayArea.height());
	par = vi.mPixelAspectRatio > 0 ? vi.mPixelAspectRatio : 1.0;
	exactPixels = vi.mbForceExactPixels;
	return true;
}

const char *ATUIGetCurrentAltOutputName() { return s_name.c_str(); }
void ATUISetCurrentAltOutputName(const char *name) {
	if (s_name == (name ? name : ""))
		return;
	s_name = name ? name : "";
	++s_revision;
	s_frameValid = false;
	s_lastOutput = nullptr;
	if (s_manager)
		s_manager->CheckForNewlyActiveOutputs();
}
void ATUIToggleAltOutput(const char *name) {
	if (!strcmp(ATUIGetCurrentAltOutputName(), name))
		ATUISetCurrentAltOutputName("");
	else if (s_manager && s_manager->GetOutputByName(name))
		ATUISetCurrentAltOutputName(name);
}
bool ATUIIsAltOutputAvailable() {
	return s_manager && s_manager->GetOutputCount();
}
bool ATUIIsXEPViewEnabled() { return s_name == "xep80"; }
void ATUISetXEPViewEnabled(bool enabled) {
	if (enabled)
		ATUISetCurrentAltOutputName("xep80");
	else if (ATUIIsXEPViewEnabled())
		ATUISetCurrentAltOutputName("");
}
bool ATUIGetAltViewEnabled() { return !s_name.empty(); }
void ATUISetAltViewEnabled(bool enabled) {
	if (enabled == ATUIGetAltViewEnabled())
		return;
	if (enabled)
		ATUISelectNextAltOutput();
	else
		ATUISetCurrentAltOutputName("");
}
sint32 ATUIGetCurrentAltViewIndex() {
	return s_manager ? s_manager->IndexOfOutput(Selected()) : -1;
}
void ATUISetAltViewByIndex(sint32 index) {
	auto *output = s_manager && index >= 0
		? s_manager->GetOutput((uint32)index) : nullptr;
	ATUISetCurrentAltOutputName(output ? output->GetName() : "");
}
void ATUISelectPrevAltOutput() {
	const sint32 index = ATUIGetCurrentAltViewIndex();
	ATUISetAltViewByIndex(index < 0 && s_manager
		? (sint32)s_manager->GetOutputCount() - 1 : index - 1);
}
void ATUISelectNextAltOutput() {
	ATUISetAltViewByIndex(ATUIGetCurrentAltViewIndex() + 1);
}
bool ATUIGetAltViewAutoswitchingEnabled() { return s_autoSwitch; }
void ATUISetAltViewAutoswitchingEnabled(bool enabled) {
	s_autoSwitch = enabled;
	if (s_manager)
		s_manager->CheckForNewlyActiveOutputs();
}
