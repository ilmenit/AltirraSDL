//	Altirra - Atari 800/800XL/5200 emulator
//	Copyright (C) 2009-2014 Avery Lee
//
//	This program is free software; you can redistribute it and/or modify
//	it under the terms of the GNU General Public License as published by
//	the Free Software Foundation; either version 2 of the License, or
//	(at your option) any later version.
//
//	This program is distributed in the hope that it will be useful,
//	but WITHOUT ANY WARRANTY; without even the implied warranty of
//	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//	GNU General Public License for more details.
//
//	You should have received a copy of the GNU General Public License
//	along with this program; if not, write to the Free Software
//	Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.

#include <stdafx.h>
#include <regex>
#include <vd2/system/math.h>
#include <vd2/system/time.h>
#include <vd2/Dita/services.h>
#include <vd2/Kasumi/pixmapops.h>
#include <vd2/Kasumi/resample.h>
#include <vd2/VDDisplay/font.h>
#include <at/ataudio/pokey.h>
#include <at/atcore/atascii.h>
#include <at/atcore/configvar.h>
#include <at/atcore/device.h>
#include <at/atcore/devicevideo.h>
#include <at/atcore/ksyms.h>
#include <at/atcore/logging.h>
#include <at/atdebugger/symbols.h>
#include <at/atuicontrols/uilabel.h>
#include <at/atui/uianchor.h>
#include <at/atui/uidragdrop.h>
#include <at/atui/uimanager.h>
#include "autosuggest.h"
#include "console.h"
#include "debugger.h"
#include "devicemanager.h"
#include "errordecode.h"
#include "inputdefs.h"
#include "inputmanager.h"
#include "oshelper.h"
#include "simulator.h"
#include "uiaccessors.h"
#include "uiautosuggest.h"
#include "uicaptionupdater.h"
#include "uidisplay.h"
#include "uidisplaytool.h"
#include "uidragdrop.h"
#include "uienhancedtext.h"
#include "uikeyboard.h"
#include "uirender.h"
#include "uivideodisplaywindow.h"
#include "uionscreenkeyboard.h"
#include "uisettingswindow.h"
#include "uitypes.h"
#include "uicalibrationscreen.h"
#include "uidisplayaccessibility.h"
#include "vbxe.h"
#include "videowriter.h"

extern ATSimulator g_sim;

extern IATUIWindowCaptionUpdater *g_winCaptionUpdater;

extern bool g_fullscreen;
extern bool g_mouseClipped;
extern bool g_mouseCaptured;
extern bool g_mouseAutoCapture;
extern bool g_ATUIPointerAutoHide;
extern bool g_ATUITargetPointerVisible;
extern ATUIKeyboardOptions g_kbdOpts;
extern bool g_xepViewAutoswitchingEnabled;
extern ATDisplayStretchMode g_displayStretchMode;
extern ATDisplayFilterMode g_dispFilterMode;
extern int g_dispFilterSharpness;
extern ATUIVideoDisplayWindow *g_pATVideoDisplayWindow;
extern bool g_dispPadIndicators;

void ATCreateUISettingsScreenMain(IATUISettingsScreen **screen);
void OnCommandEditPasteText();
void ATUIPaste(VDStringSpanA);

VDStringA g_ATCurrentAltViewName;
bool g_ATCurrentAltViewIsXEP;
bool g_ATUIDrawPadBounds;
bool g_ATUIDrawPadPointers = true;

ATConfigVarBool g_ATCVUIShowVSyncAdaptiveGraph("ui.show_vsync_adaptive_graph", false);
ATConfigVarFloat g_ATCVUISaveImageScale("ui.save_image.scale", 1.0f);
ATConfigVarBool g_ATCVRecordingVideoShowMotionVectors("recording.video.show_motion_vectors", false);

///////////////////////////////////////////////////////////////////////////

const char *ATUIGetCurrentAltOutputName() {
	return g_ATCurrentAltViewName.c_str();
}

void ATUISetCurrentAltOutputName(const char *name) {
	if (g_ATCurrentAltViewName != name) {
		g_ATCurrentAltViewName = name;

		g_ATCurrentAltViewIsXEP = !strcmp(name, "xep80");

		if (g_pATVideoDisplayWindow)
			g_pATVideoDisplayWindow->UpdateAltDisplay();
	}
}

void ATUIToggleAltOutput(const char *name) {
	if (!strcmp(ATUIGetCurrentAltOutputName(), name))
		ATUISetCurrentAltOutputName("");
	else if (g_pATVideoDisplayWindow && g_pATVideoDisplayWindow->IsAltOutputAvailable(name))
		ATUISetCurrentAltOutputName(name);
}

sint32 ATUIGetCurrentAltViewIndex() {
	return g_pATVideoDisplayWindow ? g_pATVideoDisplayWindow->GetCurrentAltOutputIndex() : -1;
}

void ATUISetAltViewByIndex(sint32 idx) {
	if (g_pATVideoDisplayWindow)
		g_pATVideoDisplayWindow->SelectAltOutputByIndex(idx);
}

void ATUISelectPrevAltOutput() {
	if (g_pATVideoDisplayWindow)
		g_pATVideoDisplayWindow->SelectPrevOutput();
}

void ATUISelectNextAltOutput() {
	if (g_pATVideoDisplayWindow)
		g_pATVideoDisplayWindow->SelectNextOutput();
}

bool ATUIIsAltOutputAvailable() {
	return g_pATVideoDisplayWindow && g_pATVideoDisplayWindow->IsAltOutputAvailable();	
}

bool ATUIIsXEPViewEnabled() {
	return g_ATCurrentAltViewIsXEP;
}

void ATUISetXEPViewEnabled(bool enabled) {
	ATUIToggleAltOutput("xep80");
}

bool ATUIGetAltViewEnabled() {
	return !g_ATCurrentAltViewName.empty();
}

void ATUISetAltViewEnabled(bool enabled) {
	if (enabled == ATUIGetAltViewEnabled())
		return;

	if (enabled)
		ATUISelectNextAltOutput();
	else
		ATUISetCurrentAltOutputName("");
}

bool ATUIGetDrawPadBoundsEnabled() { return g_ATUIDrawPadBounds; }
void ATUISetDrawPadBoundsEnabled(bool enabled) { g_ATUIDrawPadBounds = enabled; }
bool ATUIGetDrawPadPointersEnabled() { return g_ATUIDrawPadPointers; }
void ATUISetDrawPadPointersEnabled(bool enabled) { g_ATUIDrawPadPointers = enabled; }

///////////////////////////////////////////////////////////////////////////

namespace {
	const ATUIDropFilesTarget kDropFileTargets[] = {
		ATUIDropFilesTarget::PasteFilePath,
		ATUIDropFilesTarget::MountCart,
		ATUIDropFilesTarget::MountDisk4,
		ATUIDropFilesTarget::MountDisk3,
		ATUIDropFilesTarget::MountDisk2,
		ATUIDropFilesTarget::MountDisk1,
		ATUIDropFilesTarget::MountImage,
		ATUIDropFilesTarget::BootImage,
	};
}

///////////////////////////////////////////////////////////////////////////

ATUIVideoDisplayWindow::ATUIVideoDisplayWindow()
	: mDisplayRect(0, 0, 0, 0)
	, mbDragActive(false)
	, mbDragInitial(false)
	, mDragAnchorX(0)
	, mDragAnchorY(0)
	, mbMouseHidden(false)
	, mMouseHideX(0)
	, mMouseHideY(0)
	, mbOpenSidePanelDeferred(false)
	, mbCoordIndicatorActive(false)
	, mbCoordIndicatorEnabled(false)
	, mHoverTipArea(0, 0, 0, 0)
	, mbHoverTipActive(false)
	, mpEnhTextEngine(NULL)
	, mpOSK(NULL)
	, mpOSKPanel(nullptr)
	, mpSidePanel(NULL)
	, mpSEM(NULL)
	, mpVideoMgr(nullptr)
	, mAltVOChangeCount(0)
	, mAltVOLayoutChangeCount(0)
	, mpUILabelBadSignal(NULL)
{
	mbFastClip = true;
	SetAlphaFillColor(0);
	SetTouchMode(kATUITouchMode_Dynamic);
	SetDropTarget(true);

	mpOnAddedVideoOutput = [this](uint32 index) { OnAddedVideoOutput(index); };
	mpOnRemovingVideoOutput = [this](uint32 index) { OnRemovingVideoOutput(index); };
}

ATUIVideoDisplayWindow::~ATUIVideoDisplayWindow() {
	Shutdown();
}

bool ATUIVideoDisplayWindow::Init(ATSimulatorEventManager& sem, IATDeviceVideoManager& videoMgr) {
	mpSEM = &sem;
	mEventCallbackIdWarmReset = mpSEM->AddEventCallback(kATSimEvent_WarmReset, [this] { OnReset(); });
	mEventCallbackIdColdReset = mpSEM->AddEventCallback(kATSimEvent_ColdReset, [this] { OnReset(); });
	mEventCallbackIdFrameTick = mpSEM->AddEventCallback(kATSimEvent_FrameTick, [this] { OnFrameTick(); });

	mpVideoMgr = &videoMgr;
	mpVideoMgr->OnAddedOutput().Add(&mpOnAddedVideoOutput);
	mpVideoMgr->OnRemovingOutput().Add(&mpOnRemovingVideoOutput);

	AddTool(*new ATUIDisplayToolPanAndZoom(false));

	return true;
}

void ATUIVideoDisplayWindow::Shutdown() {
	if (mpVideoMgr) {
		mpVideoMgr->OnAddedOutput().Remove(&mpOnAddedVideoOutput);
		mpVideoMgr->OnRemovingOutput().Remove(&mpOnRemovingVideoOutput);
		mpVideoMgr = nullptr;
	}

	if (mpSEM) {
		mpSEM->RemoveEventCallback(mEventCallbackIdWarmReset);
		mpSEM->RemoveEventCallback(mEventCallbackIdColdReset);
		mpSEM->RemoveEventCallback(mEventCallbackIdFrameTick);

		mpSEM = nullptr;
	}

	vdsaferelease <<= mpUILabelEnhTextSize;
}

void ATUIVideoDisplayWindow::ToggleHoldKeys() {
	mbHoldKeys = !mbHoldKeys;

	if (!mbHoldKeys) {
		g_sim.ClearPendingHeldKey();
		g_sim.SetPendingHeldSwitches(0);
	}

	g_sim.GetUIRenderer()->SetPendingHoldMode(mbHoldKeys);
}

void ATUIVideoDisplayWindow::ToggleCaptureMouse() {
	if (g_mouseCaptured)
		ReleaseMouse();
	else
		CaptureMouse();
}

void ATUIVideoDisplayWindow::ReleaseMouse() {
	ReleaseCursor();
	OnCaptureLost();
}

void ATUIVideoDisplayWindow::CaptureMouse() {
	ATInputManager *im = g_sim.GetInputManager();

	if (im->IsMouseMapped() && g_sim.IsRunning()) {
		g_mouseCaptured = true;

		if (im->IsMouseAbsoluteMode()) {
			CaptureCursor(false, true);

			g_mouseClipped = true;
		} else {
			SetCursorImage(kATUICursorImage_Hidden);
			CaptureCursor(true, false);

		}

		g_winCaptionUpdater->SetMouseCaptured(true, !im->IsInputMapped(0, kATInputCode_MouseMMB));
	}
}

void ATUIVideoDisplayWindow::RecalibrateLightPen() {
	AddTool(*new ATUIDisplayToolRecalibrateLightPen);
}

void ATUIVideoDisplayWindow::ActivatePanZoomTool() {
	AddTool(*new ATUIDisplayToolPanAndZoom(true));
}

void ATUIVideoDisplayWindow::OpenOSK() {
	if (!mpOSK) {
		CloseSidePanel();

		mpOSKPanel = new ATUIContainer;
		mpOSKPanel->AddRef();
		AddChild(mpOSKPanel);

		mpOSK = new ATUIOnScreenKeyboard;
		mpOSK->AddRef();
		mpOSKPanel->AddChild(mpOSK);

		mpOSK->Focus();
		OnSize();

		if (mpOnOSKChange)
			mpOnOSKChange();
	}
}

void ATUIVideoDisplayWindow::CloseOSK() {
	if (mpOSK) {
		mpOSKPanel->Destroy();
		vdsaferelease <<= mpOSK;
		vdsaferelease <<= mpOSKPanel;

		if (mpOnOSKChange)
			mpOnOSKChange();
	}
}

void ATUIVideoDisplayWindow::SetAutoSuggestEnabled(bool enabled) {
	mbAutoSuggestEnabled = enabled;
}

void ATUIVideoDisplayWindow::ShowSuggestions() {
	mShowSuggestionsCounter = 1;
	
	mSuggestScanText1.mText.clear();
	mSuggestScanText2.mText.clear();
	mSuggestScanText3.mText.clear();
}

void ATUIVideoDisplayWindow::CloseAutoSuggest() {
	if (mpAutoSuggestPopup) {
		mpAutoSuggestPopup->Destroy();
		mpAutoSuggestPopup = nullptr;

		UnbindAction(kATUIVK_Up, 0);
		UnbindAction(kATUIVK_Down, 0);
		UnbindAction(kATUIVK_Prior, 0);
		UnbindAction(kATUIVK_Next, 0);
		UnbindAction(kATUIVK_Escape, 0);
		UnbindAction(kATUIVK_Return, 0);

		UnbindAction(kATUIVK_UIUp, 0);
		UnbindAction(kATUIVK_UIDown, 0);
		UnbindAction(kATUIVK_UIReject, 0);
		UnbindAction(kATUIVK_UIAccept, 0);

		mbAutoSuggestAcceptMapped = false;
	}
}

void ATUIVideoDisplayWindow::OpenSidePanel() {
	if (mpSidePanel)
		return;

	CloseOSK();

	vdrefptr<IATUISettingsScreen> screen;
	ATCreateUISettingsScreenMain(~screen);

	ATCreateUISettingsWindow(&mpSidePanel);
	mpSidePanel->SetOnDestroy([this]() { vdsaferelease <<= mpSidePanel; });
	mpSidePanel->SetSettingsScreen(screen);
	AddChild(mpSidePanel);
	mpSidePanel->Focus();
}

void ATUIVideoDisplayWindow::CloseSidePanel() {
	if (mpSidePanel) {
		mpSidePanel->Destroy();
		vdsaferelease <<= mpSidePanel;
	}
}

void ATUIVideoDisplayWindow::BeginEnhTextSizeIndicator() {
	mbShowEnhSizeIndicator = true;
}

void ATUIVideoDisplayWindow::EndEnhTextSizeIndicator() {
	mbShowEnhSizeIndicator = false;

	if (mpUILabelEnhTextSize) {
		mpUILabelEnhTextSize->Destroy();
		vdsaferelease <<= mpUILabelEnhTextSize;
	}
}

void ATUIVideoDisplayWindow::Deselect() {
	++mSelectionCommandProcessedCounter;
	ClearDragPreview();
}

void ATUIVideoDisplayWindow::SelectAll() {
	++mSelectionCommandProcessedCounter;
	if (mpAltVideoOutput && !mpAltVideoOutput->GetVideoInfo().mbSignalPassThrough)
		SelectByCaretPosAlt(vdpoint32(0, 0), vdpoint32(65535, 65535));
	else
		SelectByBeamPositionAntic(0, 8, 228, 248);
}

void ATUIVideoDisplayWindow::Copy(ATTextCopyMode copyMode) {
	if (mDragPreviewSpans.empty())
		return;

	uint8 data[80];
	VDStringW s;

	for(const TextSpan& ts : mDragPreviewSpans) {
		int actual;
		bool intl = false;

		if (mpAltVideoOutput && !mpAltVideoOutput->GetVideoInfo().mbSignalPassThrough) {
			actual = mpAltVideoOutput->ReadRawText(data, ts.mX, ts.mY, 80);
		} else {
			actual = ReadText(data, nullptr, ts.mY, ts.mCharX, ts.mCharWidth, intl);
		}

		if (!actual)
			continue;
	
		const auto& decodeTab = kATATASCIITables.mATASCIIToUnicode[intl];

		if (copyMode == ATTextCopyMode::Escaped) {
			uint8 inv = 0;
			bool started = false;

			for(int i=0; i<actual; ++i) {
				uint8 c = data[i];

				if (!started) {
					if (c == 0x20)
						continue;

					started = true;
				}

				if ((c ^ inv) & 0x80) {
					inv ^= 0x80;

					s.append(L"{inv}");
				}

				c &= 0x7F;

				const uint16 wc = decodeTab[c];

				if (wc < 0x100)
					s += (wchar_t)wc;
				else if (c == 0x00)
					s.append(L"{^},");
				else if (c >= 0x01 && c < 0x1B) {
					s.append(L"{^}");
					s += (char)('a' + (c - 0x01));
				} else if (c == 0x1B) {
					s.append(L"{esc}{esc}");
				} else if (c == 0x1C) {
					if (inv)
						s.append(L"{esc}{+delete}");
					else
						s.append(L"{esc}{up}");
				} else if (c == 0x1D) {
					if (inv)
						s.append(L"{esc}{+insert}");
					else
						s.append(L"{esc}{down}");
				} else if (c == 0x1E) {
					if (inv)
						s.append(L"{esc}{^tab}");
					else
						s.append(L"{esc}{left}");
				} else if (c == 0x1F) {
					if (inv)
						s.append(L"{esc}{+tab}");
					else
						s.append(L"{esc}{right}");
				} else if (c == 0x60) {
					s.append(L"{^}.");
				} else if (c >= 0x61 && c < 0x7B) {
					s += (char)c;
				} else if (c == 0x7B) {
					s.append(L"{^};");
				} else if (c == 0x7D) {
					if (inv)
						s.append(L"{esc}{^}2");
					else
						s.append(L"{esc}{clear}");
				} else if (c == 0x7E) {
					if (inv)
						s.append(L"{esc}{del}");
					else
						s.append(L"{esc}{back}");
				} else if (c == 0x7F) {
					if (inv)
						s.append(L"{esc}{ins}");
					else
						s.append(L"{esc}{tab}");
				}
			}

			while(!s.empty() && s.back() == L' ')
				s.pop_back();

			if (inv)
				s.append(L"{inv}");
		} else if (copyMode == ATTextCopyMode::Hex) {
			for(int i=0; i<actual; ++i)
				s.append_sprintf(L"%02X ", data[i]);

			if (actual)
				s.pop_back();
		} else {
			if (copyMode != ATTextCopyMode::Unicode) {
				for(int i=0; i<actual; ++i) {
					uint16 wc = decodeTab[data[i] & 0x7f];
					data[i] = wc < 0x100 ? (uint8)wc : 0x20;
				}
			}

			int base = 0;
			while(base < actual && data[base] == 0x20)
				++base;

			while(actual > base && data[actual - 1] == 0x20)
				--actual;

			if (copyMode == ATTextCopyMode::Unicode) {
				for(int i = base; i < actual; ++i)
					s += (wchar_t)decodeTab[data[i] & 0x7f];
			} else {
				for(int i = base; i < actual; ++i)
					s += (wchar_t)data[i];
			}
		}

		s += L"\r\n";
	}

	if (s.size() > 2) {
		s.pop_back();
		s.pop_back();
		mpManager->GetClipboard()->CopyText(s.c_str());
	}
}

bool ATUIVideoDisplayWindow::CopyFrameImage(bool trueAspect, VDPixmapBuffer& buf) {
	VDPixmapBuffer frameStorage;
	VDPixmap frameView;
	double par = 1;

	if (mpAltVideoOutput && !mpAltVideoOutput->GetVideoInfo().mbSignalPassThrough) {
		const auto& videoInfo = mpAltVideoOutput->GetVideoInfo();
		const auto& framebuffer = mpAltVideoOutput->GetFrameBuffer();

		par = videoInfo.mPixelAspectRatio;
		frameView = framebuffer;
	} else {
		ATGTIAEmulator& gtia = g_sim.GetGTIA();

		if (!gtia.GetLastFrameBuffer(frameStorage, frameView))
			return false;

		par = gtia.GetPixelAspectRatio();
	}

	// We may get a really evil format like Pal1 from the XEP-80 layer, so first blit
	// it to RGB32.
	buf.init(frameView.w, frameView.h, nsVDPixmap::kPixFormat_XRGB8888);

	VDPixmapBlt(buf, frameView);

	// Perform aspect ratio correction.
	int sw = frameView.w;
	int sh = frameView.h;
	double dw = sw;
	double dh = sh;

	if (trueAspect) {
		if (par < 1.0) {
			dh *= 2;
			par *= 2;
		}

		dw *= par;
	} else {
		if (par < 0.75)
			dh *= 2;
		else if (par > 1.5)
			dw *= 2;
	}

	double scale = std::clamp<double>(g_ATCVUISaveImageScale, 0.25, 4.0);
	dw *= scale;
	dh *= scale;

	int iw = VDRoundToInt(dw);
	int ih = VDRoundToInt(dh);

	if (iw != buf.w || ih != buf.h) {
		VDPixmapBuffer buf2(iw, ih, nsVDPixmap::kPixFormat_XRGB8888);

		if (trueAspect) {
			vdautoptr<IVDPixmapResampler> r(VDCreatePixmapResampler());

			r->SetFilters(IVDPixmapResampler::kFilterSharpLinear, IVDPixmapResampler::kFilterSharpLinear, false);
			r->SetSharpnessFactors(sqrt((float)dw / (float)sw), sqrtf((float)dh / (float)sh));

			const vdrect32f dstRect {
				(float)((iw - dw) * 0.5),
				(float)((ih - dh) * 0.5),
				(float)((iw + dw) * 0.5),
				(float)((ih + dh) * 0.5)
			};

			const vdrect32f srcRect {
				(float)0,
				(float)0,
				(float)frameView.w,
				(float)frameView.h
			};

			r->Init(dstRect, buf2.w, buf2.h, buf2.format, srcRect, buf.w, buf.h, buf.format);
			r->Process(buf2, buf);
		} else
			VDPixmapStretchBltNearest(buf2, buf);

		buf.swap(buf2);
	}

	return true;
}

void ATUIVideoDisplayWindow::CopySaveFrame(bool saveFrame, bool trueAspect, const wchar_t *path) {
	VDPixmapBuffer buf;

	if (!CopyFrameImage(trueAspect, buf))
		return;

	if (saveFrame) {
		VDStringW fn;
		
		if (path)
			fn = path;
		else
			fn = VDGetSaveFileName("scrn"_vdfcctypeid, ATUIGetMainWindow(), L"Save Screenshot", L"Portable Network Graphics (*.png)\0*.png\0", L"png");

		if (!fn.empty())
			ATSaveFrame(buf, fn.c_str());
	} else
		ATCopyFrameToClipboard(buf);

}

vdrect32 ATUIVideoDisplayWindow::GetOSKSafeArea() const {
	vdrect32 r(GetArea());
	r.translate(-r.left, -r.top);

	if (mpOSKPanel) {
		int bottomLimit = mpOSKPanel->GetArea().top;

		if (bottomLimit < r.bottom/2)
			bottomLimit = r.bottom/2;

		if (bottomLimit < r.bottom)
			r.bottom = bottomLimit;
	}

	return r;
}

void ATUIVideoDisplayWindow::SetDisplaySourceMapping(vdfunction<bool(vdfloat2&)> dispToSrcFn, vdfunction<bool(vdfloat2&)> srcToDispFn) {
	mpMapDisplayToSourcePt = std::move(dispToSrcFn);
	mpMapSourceToDisplayPt = std::move(srcToDispFn);
}

void ATUIVideoDisplayWindow::SetDisplayRect(const vdrect32& r) {
	mDisplayRect = r;
	UpdateDragPreviewRects();
}

void ATUIVideoDisplayWindow::ClearHighlights() {
	if (!mHighlightPoints.empty()) {
		mHighlightPoints.clear();
		Invalidate();
	}
}

void ATUIVideoDisplayWindow::SetEnhancedTextEngine(IATUIEnhancedTextEngine *p) {
	if (mpEnhTextEngine == p)
		return;

	mpEnhTextEngine = p;

	if (p) {
		const auto& r = GetClientArea();
		p->OnSize(r.width(), r.height());
	}

	UpdateAltDisplay();
}

void ATUIVideoDisplayWindow::AddTool(ATUIDisplayTool& tool) {
	mTools.erase(std::remove(mTools.begin(), mTools.end(), nullptr), mTools.end());

	if (tool.IsMainTool()) {
		if (!mTools.empty() && mTools.front()->IsMainTool())
			RemoveTool(*mTools.front());

		mTools.insert(mTools.begin(), vdrefptr(&tool));
	} else {
		mTools.emplace_back(&tool);
	}

	tool.InitTool(*mpManager, *this);
}

void ATUIVideoDisplayWindow::RemoveTool(ATUIDisplayTool& tool) {
	if (mpActiveTool == &tool)
		mpActiveTool = nullptr;

	tool.ShutdownTool();

	auto it = std::find(mTools.begin(), mTools.end(), &tool);
	if (it != mTools.end())
		*it = nullptr;
}

void ATUIVideoDisplayWindow::ReadScreen(ATUIDisplayAccessibilityScreen& screenInfo) const {
	screenInfo.mText.clear();
	screenInfo.mLines.clear();
	screenInfo.mFormatSpans.clear();

	const ATGTIAEmulator& gtia = g_sim.GetGTIA();
	const ATGTIAColorTrace& colorTrace = gtia.GetColorTrace();
	uint32 pal[256];
	gtia.GetPalette(pal);

	uint8 buf[48] {};
	uint8 raw[48] {};

	uint32 lastBg = ~0;
	uint32 lastFg = ~0;

	int y = 8;
	while(y < 248) {
		const ModeLineInfo& mli = GetModeLineInfo(y);

		if (!mli.IsTextMode()) {
			++y;
			continue;
		}

		bool intl = false;
		int len = ReadText(buf, raw, y, 0, 48, intl);

		while(len && buf[len - 1] == 0x20)
			--len;

		if (!screenInfo.mLines.empty())
			screenInfo.mText.push_back('\n');

		ATUIDisplayAccessibilityLineInfo& lineInfo = screenInfo.mLines.emplace_back();

		lineInfo.mStartBeamX = mli.mFetchHposStart;
		lineInfo.mStartBeamY = y;
		lineInfo.mHeight = mli.mHeight;
		lineInfo.mBeamToCellShift = mli.mCellToHRPixelShift - 1;
		lineInfo.mTextOffset = (uint32)screenInfo.mText.size();
		lineInfo.mTextLength = 0;

		const auto& lineColorTrace = colorTrace.mColors[y - 8];
		uint32 bg = 0;
		uint32 fg = 0;
		bool mode67 = false;

		if (mli.mMode <= 3) {
			// IR 2, 3: BG = PF2, FG = PF2/PF1
			bg = pal[lineColorTrace[6]];
			fg = pal[(lineColorTrace[6] & 0xF0) + (lineColorTrace[5] & 0x0F)];
		} else if (mli.mMode <= 5) {
			// IR 4, 5: BG = BAK, FG = PF0 (kluge)
			bg = pal[lineColorTrace[8]];
			fg = pal[lineColorTrace[4]];
		} else {
			// IR 6, 7: BG = BAK, FG = varies (must do by char)
			mode67 = true;
			bg = pal[lineColorTrace[8]];
		}

		const auto setColors = [&](uint32 newfg, uint32 newbg) {
			if (newbg != lastBg || newfg != lastFg) {
				auto& formatSpan = screenInfo.mFormatSpans.emplace_back();
				formatSpan.mOffset = lineInfo.mTextOffset;
				formatSpan.mBgColor = newbg;
				formatSpan.mFgColor = newfg;

				lastFg = newfg;
				lastBg = newbg;
			}
		};

		if (!mode67)
			setColors(fg, bg);

		if (len) {
			const auto& decodeTab = kATATASCIITables.mATASCIIToUnicode[intl ? 1 : 0];

			uint8 prev = 0;
			for(int i=0; i<len; ++i) {
				uint8 c = buf[i];

				if (mode67) {
					if ((c & 0x7F) != 0x20) {
						fg = pal[lineColorTrace[4 + (raw[i] >> 6)]];

						setColors(fg, bg);
					}

					screenInfo.mText.push_back(decodeTab[c & 0x7F]);
				} else {
					if ((prev ^ c) & 0x80)
						screenInfo.mText.push_back(c & 0x80 ? L'{' : L'}');

					screenInfo.mText.push_back(decodeTab[c & 0x7F]);
				}

				prev = c;
			}

			if (prev & 0x80)
				screenInfo.mText.push_back(L'}');

			lineInfo.mTextLength = (uint32)screenInfo.mText.size() - lineInfo.mTextOffset;
		}

		y += mli.mHeight;
	}

	auto& lastFormatSpan = screenInfo.mFormatSpans.emplace_back();
	lastFormatSpan.mOffset = (uint32)screenInfo.mText.size();
	lastFormatSpan.mBgColor = 0;
	lastFormatSpan.mFgColor = 0xFFFFFF;

	if (screenInfo.mLines.empty())
		screenInfo.Clear();
}

vdrect32 ATUIVideoDisplayWindow::GetTextSpanBoundingBox(int ypos, int xc1, int xc2) const {
	const ModeLineInfo& mli = GetModeLineInfo(ypos);

	float pixelX1F = (float)mli.mFetchHposStart + (float)(xc1 << mli.mCellToHRPixelShift) * 0.5f;
	float pixelX2F = (float)mli.mFetchHposStart + (float)(xc2 << mli.mCellToHRPixelShift) * 0.5f;
	float pixelY1F = (float)mli.mVPos;
	float pixelY2F = pixelY1F + (float)mli.mHeight;

	vdfloat2 pt11 = MapBeamPositionToPointF(vdfloat2(pixelX1F, pixelY1F));
	vdfloat2 pt21 = MapBeamPositionToPointF(vdfloat2(pixelX2F, pixelY1F));
	vdfloat2 pt12 = MapBeamPositionToPointF(vdfloat2(pixelX1F, pixelY2F));
	vdfloat2 pt22 = MapBeamPositionToPointF(vdfloat2(pixelX2F, pixelY2F));

	vdfloat2 ptmin = nsVDMath::min(nsVDMath::min(pt11, pt12), nsVDMath::min(pt21, pt22));
	vdfloat2 ptmax = nsVDMath::max(nsVDMath::max(pt11, pt12), nsVDMath::max(pt21, pt22));

	return vdrect32(
		VDRoundToInt32(ptmin.x),
		VDRoundToInt32(ptmin.y),
		VDRoundToInt32(ptmax.x),
		VDRoundToInt32(ptmax.y)
	);
}

vdpoint32 ATUIVideoDisplayWindow::GetNearestBeamPositionForPoint(const vdpoint32& pt) const {
	int xc = 0;
	int yc = 0;
	MapPixelToBeamPosition(pt.x, pt.y, xc, yc, true);

	return vdpoint32(xc, yc);
}

void ATUIVideoDisplayWindow::SetVideoWriter(IATVideoWriter *writer) {
	mpVideoWriter = writer;
}

void ATUIVideoDisplayWindow::InvalidateTextOutput() {
	Invalidate();
}

void ATUIVideoDisplayWindow::OnReset() {
	mbHoldKeys = false;
	g_sim.GetUIRenderer()->SetPendingHoldMode(false);

	if (g_xepViewAutoswitchingEnabled)
		ATUISetAltViewEnabled(false);

	mpVideoMgr->CheckForNewlyActiveOutputs();
}

void ATUIVideoDisplayWindow::OnFrameTick() {
	if (g_xepViewAutoswitchingEnabled) {
		sint32 idx = mpVideoMgr->CheckForNewlyActiveOutputs();

		if (idx >= 0)
			ATUISetCurrentAltOutputName(mpVideoMgr->GetOutput(idx)->GetName());
	}

	if (g_sim.GetInputManager()->HasNonBeamPointer()) {
		g_sim.GetUIRenderer()->SetPadInputEnabled(true);
		mPadArea = g_sim.GetUIRenderer()->GetPadArea();
	} else {
		g_sim.GetUIRenderer()->SetPadInputEnabled(false);
	}

	UpdateAutoSuggest();
}

void ATUIVideoDisplayWindow::OnAddedVideoOutput(uint32 index) {
}

void ATUIVideoDisplayWindow::OnRemovingVideoOutput(uint32 index) {
	IATDeviceVideoOutput *output = mpVideoMgr->GetOutput(index);

	if (mpAltVideoOutput == output)
		ATUISetCurrentAltOutputName("");
}

ATUITouchMode ATUIVideoDisplayWindow::GetTouchModeAtPoint(const vdpoint32& pt) const {
	// allow swiping in the bottom quarter to bring up the OSK
	if (pt.y >= mArea.height() * 3 / 4)
		return kATUITouchMode_Default;

	// check for input mapping
	ATInputManager *im = g_sim.GetInputManager();
	
	if (im->IsInputMapped(0, kATInputCode_MouseLMB) && !im->IsInputMapped(0, kATInputCode_MouseRMB))
		return kATUITouchMode_Immediate;

	return kATUITouchMode_Default;
}

namespace {
	VDStringW GetTipMessage(char *data, sint32 x) {
		// trim and null-terminate the string
		char *s = data;

		while(*s == ' ')
			++s;

		char *t = s + strlen(s);

		while(t != s && t[-1] == ' ')
			--t;

		*t = 0;

		// convert to uppercase
		for(char *s2 = s; *s2; ++s2)
			*s2 = toupper((unsigned char)*s2);

		// look for an error
		VDStringW msg;
		if (t = strstr(s, "ERROR"); t) {
			// skip ERROR string
			t += 5;

			// skip blanks
			while(*t == ' ')
				++t;

			// look for an optional dash or pound
			if (*t == '#' || *t == '-') {
				++t;

				if (t[-1] == '-') {
					while(*t == '-')
						++t;
				}

				// skip more blanks
				while(*t == ' ')
					++t;
			}

			// look for a digit
			if ((unsigned char)(*t - '0') < 10) {
				int errCode = atoi(t);

				if (errCode >= 2 && errCode <= 255) {
					msg.sprintf(L"<b>Error %u</b>", errCode);

					for(const ATDecodedError& de : ATDecodeError((uint8)errCode)) {
						msg += L"\n<b>";
						msg += de.mpClass;
						msg += L":</b> ";
						msg += de.mpMessage;
					}
				}
			}
		}
		
		if (msg.empty()) {
			unsigned address = 0;
			bool isRead = false;

			const std::regex peekRegex(R"--(PEEK\( *([0-9]+) *\))--", std::regex::extended);
			for(auto it = std::cregex_iterator(s, s+strlen(s), peekRegex, std::regex_constants::match_any), itEnd = std::cregex_iterator(); it != itEnd; ++it) {
				const auto& capture = (*it)[0];
				ptrdiff_t offset1 = capture.first - data;
				ptrdiff_t offset2 = capture.second - data;

				if (offset1 <= x && offset2 > x) {
					address = atoi((*it)[1].str().c_str());
					isRead = true;
					break;
				}
			}


			const std::regex pokeRegex(R"--(POKE +([0-9]+) *,)--", std::regex::extended);
			for(auto it = std::cregex_iterator(s, s+strlen(s), pokeRegex, std::regex_constants::match_any), itEnd = std::cregex_iterator(); it != itEnd; ++it) {
				const auto& capture = (*it)[0];
				ptrdiff_t offset1 = capture.first - data;
				ptrdiff_t offset2 = capture.second - data;

				if (offset1 <= x && offset2 > x) {
					address = atoi((*it)[1].str().c_str());
					isRead = false;
					break;
				}
			}

			if (address) {
				ATSymbol sym;

				if (ATGetDebuggerSymbolLookup()->LookupSymbol(address, isRead ? kATSymbol_Read : kATSymbol_Write, sym)) {
					msg.sprintf(L"<b>Address %u ($%0*X):</b> %hs", address, address >= 256 ? 4 : 2, address, sym.mpName);
				}
			}
		}

		if (msg.empty() && *s)
			msg = L"There is no help for this message.\nHover over an SIO, CIO, BASIC, or DOS error message for help.";

		return msg;
	}
}

void ATUIVideoDisplayWindow::OnMouseDown(sint32 x, sint32 y, uint32 vk, bool dblclk) {
	ATInputManager *im = g_sim.GetInputManager();

	Focus();

	if (mpOSK) {
		CloseOSK();
		return;
	}

	if (mpActiveTool) {
		if (vdrefptr(mpActiveTool)->OnMouseDown(x, y, vk))
			return;
	}

	for(const auto& tool : mTools) {
		if (tool) {
			if (tool->HasPriorityOverInputManager()) {
				if (vdrefptr(tool)->OnMouseDown(x, y, vk) && tool) {
					mpActiveTool = tool;
					CaptureCursor();
					return;
				}
			}

			if (tool->IsMainTool())
				break;
		}
	}

	// If the mouse is mapped, it gets first crack at inputs unless Alt is down.
	// If it is mapped in pad mode, we only handle inputs within the pad area.
	const bool alt = mpManager->IsKeyDown(kATUIVK_Alt);
	if (im->IsMouseMapped() && !alt && (!im->HasNonBeamPointer() || mPadArea.contains(vdpoint32(x, y)))) {
		if (g_sim.IsRunning()) {
			// Check if auto-capture is on and we haven't captured the mouse yet. If so, we
			// should capture the mouse but otherwise eat the click
			if (g_mouseAutoCapture && !g_mouseCaptured) {
				if (vk == kATUIVK_LButton) {
					CaptureMouse();
					return;
				}
			} else {
				const bool absMode = im->IsMouseAbsoluteMode();

				// Check if the mouse is captured or we are in absolute mode. If we are in
				// relative mode and haven't captured the mouse we should not route this
				// shunt to the input manager.
				if (g_mouseCaptured || absMode) {
					if (absMode)
						UpdateMousePosition(x, y);

					switch(vk) {
						case kATUIVK_LButton:
							im->OnButtonDown(0, kATInputCode_MouseLMB);
							break;

						case kATUIVK_MButton:
							if (im->IsInputMapped(0, kATInputCode_MouseMMB))
								im->OnButtonDown(0, kATInputCode_MouseMMB);
							else if (g_mouseCaptured)
								ReleaseMouse();
							break;

						case kATUIVK_RButton:
							im->OnButtonDown(0, kATInputCode_MouseRMB);
							break;

						case kATUIVK_XButton1:
							im->OnButtonDown(0, kATInputCode_MouseX1B);
							break;

						case kATUIVK_XButton2:
							im->OnButtonDown(0, kATInputCode_MouseX2B);
							break;
					}

					return;
				}
			}
		} else {
			// Check if the mouse is captured or we are in absolute mode. If we are in
			// relative mode and haven't captured the mouse we should not route this
			// shunt to the input manager.
			if (g_mouseCaptured) {
				if (vk == kATUIVK_MButton) {
					if (!im->IsInputMapped(0, kATInputCode_MouseMMB))
						ReleaseMouse();
				}

				return;
			}
		}
	}
	
	// We aren't routing this mouse event to the input manager, so do selection if it's the
	// LMB.

	for(const auto& tool : mTools) {
		if (tool) {
			if (!tool->HasPriorityOverInputManager() && vdrefptr(tool)->OnMouseDown(x, y, vk) && tool) {
				mpActiveTool = tool;
				CaptureCursor();
				return;
			}

			if (tool->IsMainTool())
				break;
		}
	}

	if (vk == kATUIVK_LButton) {
		if (alt) {
			// tooltip request -- let's try to grab text
			int xc;
			int yc;

			bool valid = false;

			if (mpAltVideoOutput && !mpAltVideoOutput->GetVideoInfo().mbSignalPassThrough) {
				const auto& videoInfo = mpAltVideoOutput->GetVideoInfo();
				const vdrect32& rBlit = GetAltDisplayArea();
				const vdrect32& rDisp = videoInfo.mDisplayArea;

				if (rBlit.contains(vdpoint32(x, y)) && !rDisp.empty()) {
					int dx = VDRoundToInt((float)x * (float)rDisp.width() / (float)rBlit.width());
					int dy = VDRoundToInt((float)y * (float)rDisp.height() / (float)rBlit.height());

					const vdpoint32 caretPos = mpAltVideoOutput->PixelToCaretPos(vdpoint32(dx, dy));

					uint8 buf[81];
					int actual = mpAltVideoOutput->ReadRawText(buf, 0, caretPos.y, 80);

					char text[81];
					for(int i=0; i<actual; ++i) {
						uint8 c = buf[i] & 0x7f;

						if ((uint8)(c - 0x20) > 0x5f)
							c = 0x20;

						text[i] = (char)c;
					}

					text[actual] = 0;

					const VDStringW& msg = GetTipMessage(text, caretPos.x);

					if (!msg.empty()) {
						const vdrect32& lineRect = mpAltVideoOutput->CharToPixelRect(vdrect32(0, caretPos.y, videoInfo.mTextColumns, caretPos.y + 1));

						float scaleX = (float)rBlit.width() / (float)rDisp.width();
						float scaleY = (float)rBlit.height() / (float)rDisp.height();

						mHoverTipArea.set(
							VDRoundToInt(lineRect.left * scaleX + rBlit.left),
							VDRoundToInt(lineRect.top * scaleY + rBlit.top),
							VDRoundToInt(lineRect.right * scaleX + rBlit.left),
							VDRoundToInt(lineRect.bottom * scaleY + rBlit.top));

						g_sim.GetUIRenderer()->SetHoverTip(x, y, msg.c_str());
						mbHoverTipActive = true;
						valid = true;
					}
				}
			} else if (!g_sim.IsRunning() && ATIsDebugConsoleActive()) {
				if (mpManager->IsKeyDown(kATUIVK_Shift)) {
					int hcyc, vcyc;

					if (MapPixelToBeamPosition(x, y, hcyc, vcyc, false)) {
						const auto& antic = g_sim.GetAntic();
						uint32 frame = antic.GetRawFrameCounter();

						// switch from color cycles to machine cycles
						hcyc >>= 1;

						if (vcyc >= (int)antic.GetBeamY() || (vcyc == (int)antic.GetBeamY() && hcyc >= (int)antic.GetBeamX()))
							--frame;

						ATConsolePingBeamPosition(frame, vcyc < 0 ? 0 : vcyc, hcyc < 0 ? 0 : hcyc);
					}

				} else {
					mbCoordIndicatorEnabled = true;
					CaptureCursor();
					SetCoordinateIndicator(x, y);
					SetCursorImage(kATUICursorImage_Cross);
				}
			} else if (MapPixelToBeamPosition(x, y, xc, yc, false)) {
				// attempt to copy out text
				auto [xmode, ymode] = GetModeLineXYPos(xc, yc, true);

				if (ymode >= 0) {
					uint8 data[49];
					bool intl;

					int actual = ReadText(data, nullptr, ymode, 0, 48, intl);
					data[actual] = 0;

					char cdata[49];
					std::transform(std::begin(data), std::end(data), std::begin(cdata), [](uint8 c) { return (char)(c & 0x7F); });

					const VDStringW& msg = GetTipMessage(cdata, xmode);

					if (!msg.empty()) {
						int xp1, xp2, yp1, yp2;
						MapBeamPositionToPoint(0, ymode, xp1, yp1);

						ATAnticEmulator& antic = g_sim.GetAntic();
						const ATAnticEmulator::DLHistoryEntry *dlhist = antic.GetDLHistory();
						while(++ymode < 248 && !dlhist[ymode].mbValid)
							;

						MapBeamPositionToPoint(228, ymode, xp2, yp2);

						mHoverTipArea.set(xp1, yp1, xp2, yp2);

						g_sim.GetUIRenderer()->SetHoverTip(x, y, msg.c_str());
						mbHoverTipActive = true;
						valid = true;
					}
				}
			}

			if (!valid)
				ClearHoverTip();
		} else {
			// double-click on the left 10% of the screen opens the side panel
			if (dblclk) {
				if (x < GetArea().width() / 10) {
					mbOpenSidePanelDeferred = true;
					return;
				}
			}

			if (mpAltVideoOutput && !mpAltVideoOutput->GetVideoInfo().mbSignalPassThrough) {
				const auto& vi = mpAltVideoOutput->GetVideoInfo();
				mbDragActive = vi.mTextColumns > 0 && vi.mTextRows > 0 && !GetAltDisplayArea().empty();

				if (mbDragActive) {
					mDragAnchorX = x;
					mDragAnchorY = y;
				}
			} else {
				mbDragActive = MapPixelToBeamPosition(x, y, mDragAnchorX, mDragAnchorY, true)
					&& GetModeLineYPos(mDragAnchorY, true) >= 0;
			}

			mDragStartTime = VDGetCurrentTick();
			
			if (mbDragActive) {
				// We specifically don't clear the drag preview here as that would make it
				// impossible to use Copy from the context menu with touch.
				mbDragInitial = true;
				mbMouseHidden = false;
				CaptureCursor();
			}
		}
	}
}

void ATUIVideoDisplayWindow::OnMouseUp(sint32 x, sint32 y, uint32 vk) {
	if (mpActiveTool) {
		vdrefptr tool(mpActiveTool);
		mpActiveTool = nullptr;

		ReleaseCursor();
		tool->OnMouseUp(x, y, vk);
	}

	if (vk == kATUIVK_LButton) {
		ClearCoordinateIndicator();
		ClearHoverTip();

		if (mbCoordIndicatorEnabled) {
			mbCoordIndicatorEnabled = false;
			ReleaseCursor();
		}

		if (mbDragActive) {
			mbDragActive = false;

			if (VDGetCurrentTick() - mDragStartTime < 250)
				mbDragInitial = false;

			ReleaseCursor();
			UpdateDragPreview(x, y);
			return;
		} else if (!mDragPreviewSpans.empty()) {
			if (VDGetCurrentTick() - mDragStartTime < 250)
				ClearDragPreview();
		}

		if (mbOpenSidePanelDeferred) {
			mbOpenSidePanelDeferred = false;
			OpenSidePanel();
			return;
		}
	}

	ATInputManager *im = g_sim.GetInputManager();

	if (g_mouseCaptured || im->IsMouseAbsoluteMode()) {
		if (im->IsMouseMapped()) {
			switch(vk) {
				case kATUIVK_LButton:
					im->OnButtonUp(0, kATInputCode_MouseLMB);
					break;
				case kATUIVK_RButton:
					im->OnButtonUp(0, kATInputCode_MouseRMB);
					break;
				case kATUIVK_MButton:
					im->OnButtonUp(0, kATInputCode_MouseMMB);
					break;
				case kATUIVK_XButton1:
					im->OnButtonUp(0, kATInputCode_MouseX1B);
					break;
				case kATUIVK_XButton2:
					im->OnButtonUp(0, kATInputCode_MouseX2B);
					break;
			}

			// Eat the message to prevent a context menu.
			return;
		}
	}

	if (vk == kATUIVK_RButton) {
		if (mpOnAllowContextMenu)
			mpOnAllowContextMenu();
	}
}

void ATUIVideoDisplayWindow::OnMouseRelativeMove(sint32 dx, sint32 dy) {
	ATInputManager *im = g_sim.GetInputManager();

	im->OnMouseMove(0, dx, dy);
	SetCursorImage(kATUICursorImage_Hidden);
}

void ATUIVideoDisplayWindow::OnMouseMove(sint32 x, sint32 y) {
	// MPC-HC sometimes injects mouse moves in order to prevent the screen from
	// going to sleep. We need to filter out these moves to prevent the cursor
	// from blinking.
	if (mbMouseHidden) {
		if (mMouseHideX == x && mMouseHideY == y)
			return;

		mbMouseHidden = false;
	}

	if (mpActiveTool) {
		vdrefptr(mpActiveTool)->OnMouseMove(x, y);
		SetCursorImage(ComputeCursorImage(vdpoint32(x, y)));
		return;
	}

	// If we have already entered a selection drag, it has highest priority.
	if (mbDragActive) {
		SetCursorImage(kATUICursorImage_IBeam);
		UpdateDragPreview(x, y);
		return;
	}

	// Check if we're stopped and should do debug queries.
	if (mbCoordIndicatorEnabled) {
		SetCoordinateIndicator(x, y);
		SetCursorImage(kATUICursorImage_Cross);
		return;
	}

	SetCursorImage(ComputeCursorImage(vdpoint32(x, y)));

	auto *pIM = g_sim.GetInputManager();
	if ((g_mouseCaptured || !g_mouseAutoCapture) && pIM->IsMouseAbsoluteMode()) {
		UpdateMousePosition(x, y);
	} else if (mbHoverTipActive) {
		if (!mHoverTipArea.contains(vdpoint32(x, y))) {
			ClearHoverTip();
		}
	}
}

bool ATUIVideoDisplayWindow::OnMouseWheel(sint32 x, sint32 y, float delta, bool doPages) {
	if (delta == 0)
		return false;

	if (mpActiveTool) {
		if (vdrefptr(mpActiveTool)->OnMouseWheel(x, y, delta, doPages))
			return true;
	}

	for(int pass = 0; pass < 2; ++pass) {
		const bool doPriority = (pass == 0);
		for(const auto& tool : mTools) {
			if (tool && tool->HasPriorityOverInputManager() == doPriority) {
				if (vdrefptr(tool)->OnMouseWheel(x, y, delta, doPages))
					return true;

				if (tool->IsMainTool())
					break;
			}
		}

		if (pass == 0) {
			if (g_sim.GetInputManager()->OnMouseWheel(0, delta))
				return true;
		}
	}

	return false;
}

bool ATUIVideoDisplayWindow::OnMouseHWheel(sint32 x, sint32 y, float delta, bool doPages) {
	if (delta == 0)
		return false;

	return g_sim.GetInputManager()->OnMouseHWheel(0, delta);
}

void ATUIVideoDisplayWindow::OnMouseLeave() {
	ClearCoordinateIndicator();
	mbCoordIndicatorEnabled = false;

	ClearHoverTip();

	mbOpenSidePanelDeferred = false;

	ATInputManager *im = g_sim.GetInputManager();
	im->SetMouseVirtualStickPos(0, 0);
}

void ATUIVideoDisplayWindow::OnMouseHover(sint32 x, sint32 y) {
	if (g_mouseCaptured)
		return;

	if (g_ATUIPointerAutoHide && !mpManager->IsKeyDown(kATUIVK_Alt)) {
		SetCursorImage(kATUICursorImage_Hidden);
		mbMouseHidden = true;
		mMouseHideX = x;
		mMouseHideY = y;
	}
}

bool ATUIVideoDisplayWindow::OnContextMenu(const vdpoint32 *pt) {
	// For now we do a bit of a hack and let the top-level native display code handle this,
	// as it is too hard currently to display the menu here.
	if (mpOnDisplayContextMenu) {
		if (pt)
			mpOnDisplayContextMenu(*pt);
		else
			mpOnDisplayContextMenu(TranslateClientPtToScreenPt(vdpoint32(mClientArea.width() >> 1, mClientArea.height() >> 1)));
	}

	return true;
}

bool ATUIVideoDisplayWindow::OnKeyDown(const ATUIKeyEvent& event) {
	if (ATUIWidget::OnKeyDown(event))
		return true;

	if (!mTools.empty() && mTools.front() && mTools.front()->IsMainTool()) {
		if (event.mVirtKey == kATUIVK_Escape || event.mVirtKey == kATUIVK_UIReject) {
			RemoveTool(*mTools.front());
			return true;
		}
	}

	// Right-Alt kills capture.
	if (event.mExtendedVirtKey == kATUIVK_RAlt && g_mouseCaptured) {
		ReleaseMouse();
		return true;
	}

	// fall through so the simulator still receives the alt key, in case a key is typed
	const auto selCmdCounter = mSelectionCommandProcessedCounter;
	if (ProcessKeyDown(event, !mpEnhTextEngine || mpEnhTextEngine->IsRawInputEnabled())) {
		CloseAutoSuggest();

		// slight hack, don't clear the selection if a selection command was
		// processed
		if (selCmdCounter == mSelectionCommandProcessedCounter)
			ClearDragPreview();
		return true;
	}

	if (mpEnhTextEngine) {
		if (!mpEnhTextEngine->IsRawInputEnabled() && event.mVirtKey == kATUIVK_A + ('V'-'A') && mpManager->IsKeyDown(kATUIVK_Control) && !mpManager->IsKeyDown(kATUIVK_Shift) && !mpManager->IsKeyDown(kATUIVK_Alt)) {
			OnCommandEditPasteText();
			ClearDragPreview();
			CloseAutoSuggest();
			return true;
		}

		if (mpEnhTextEngine->OnKeyDown(event.mVirtKey)) {
			CloseAutoSuggest();
			ClearDragPreview();
			return true;
		}
	}

	return false;
}

bool ATUIVideoDisplayWindow::OnKeyUp(const ATUIKeyEvent& event) {
	if (ATUIWidget::OnKeyUp(event))
		return true;

	if (ProcessKeyUp(event, !mpEnhTextEngine || mpEnhTextEngine->IsRawInputEnabled()) || (mpEnhTextEngine && mpEnhTextEngine->OnKeyUp(event.mVirtKey)))
		return true;

	return false;
}

bool ATUIVideoDisplayWindow::OnChar(const ATUICharEvent& event) {
	ClearDragPreview();

	if (mpEnhTextEngine && !mpEnhTextEngine->IsRawInputEnabled()) {
		if (mpEnhTextEngine)
			mpEnhTextEngine->OnChar(event.mCh);

		return true;
	}

	int code = event.mCh;
	if (code <= 0)
		return false;

	if (g_kbdOpts.mbRawKeys) {
		uint32 ch;

		if (!event.mbIsRepeat && ATUIGetScanCodeForCharacter32(code, ch)) {
			CloseAutoSuggest();

			if (ch >= 0x100)
				ProcessVirtKey(0, event.mScanCode, ch, false);
			else if (mbHoldKeys)
				ToggleHeldKey((uint8)ch);
			else {
				mbShiftToggledPostKeyDown = false;

				auto it = std::find_if(mActiveKeys.begin(), mActiveKeys.end(), [=](const ActiveKey& ak) { return ak.mVkey == 0 && ak.mNativeScanCode == event.mScanCode; });
				if (it != mActiveKeys.end())
					it->mScanCode = ch;
				else
					mActiveKeys.push_back(ActiveKey { 0, event.mScanCode, (uint8)ch });

				UpdateCtrlShiftState();
				g_sim.GetPokey().PushRawKey((uint8)ch, !g_kbdOpts.mbFullRawKeys);
			}
		}
	} else {
		uint32 ch;

		if (ATUIGetScanCodeForCharacter32(code, ch)) {
			CloseAutoSuggest();

			if (ch >= 0x100)
				ProcessVirtKey(0, event.mScanCode, ch, false);
			else
				g_sim.GetPokey().PushKey(ch, event.mbIsRepeat, false, true, true);
		}
	}

	return true;
}

bool ATUIVideoDisplayWindow::OnCharUp(const ATUICharEvent& event) {
	auto it = std::find_if(mActiveKeys.begin(), mActiveKeys.end(), [=](const ActiveKey& ak) { return ak.mVkey == 0 && ak.mNativeScanCode == event.mScanCode; });
	if (it != mActiveKeys.end()) {
		g_sim.GetPokey().ReleaseRawKey(it->mScanCode, !g_kbdOpts.mbFullRawKeys);
		*it = mActiveKeys.back();
		mActiveKeys.pop_back();
		UpdateCtrlShiftState();
	}

	for(uint32 i=0; i<(uint32)vdcountof(mActiveSpecialVKeys); ++i) {
		if (mActiveSpecialVKeys[i] == 0 && mActiveSpecialScanCodes[i] == event.mScanCode) {
			mActiveSpecialVKeys[i] = 0;
			mActiveSpecialScanCodes[i] = 0;

			ProcessSpecialKey(kATUIKeyScanCodeFirst + i, false);
			break;
		}
	}

	return true;
}

void ATUIVideoDisplayWindow::OnForceKeysUp() {
	auto& pokey = g_sim.GetPokey();
	pokey.SetShiftKeyState(false, !g_kbdOpts.mbFullRawKeys);
	pokey.SetControlKeyState(false);
	pokey.ReleaseAllRawKeys(!g_kbdOpts.mbFullRawKeys);

	mbShiftDepressed = false;
	mbShiftToggledPostKeyDown = false;
	mActiveKeys.clear();
	UpdateCtrlShiftState();

	g_sim.GetGTIA().SetConsoleSwitch(0x07, false);
}

void ATUIVideoDisplayWindow::OnActionStart(uint32 id) {
	switch(id) {
		case kActionOpenSidePanel:
			OpenSidePanel();
			break;

		case kActionOpenOSK:
			OpenOSK();
			break;

		case kActionCloseAutoSuggest:
			CloseAutoSuggest();
			break;

		default:
			return ATUIContainer::OnActionStart(id);
	}
}

void ATUIVideoDisplayWindow::OnActionStop(uint32 id) {
	switch(id) {
		case kActionCloseOSK:
			CloseOSK();
			break;
	}
}

void ATUIVideoDisplayWindow::OnCreate() {
	ATUIContainer::OnCreate();

	BindAction(kATUIVK_UIMenu, kActionOpenSidePanel);
	BindAction(kATUIVK_UIOption, kActionOpenOSK);
	BindAction(kATUIVK_UIReject, kActionCloseOSK);

	mpUILabelBadSignal = new ATUILabel;
	mpUILabelBadSignal->AddRef();
	mpUILabelBadSignal->SetVisible(true);
	mpUILabelBadSignal->SetFont(mpManager->GetThemeFont(kATUIThemeFont_Default));
	mpUILabelBadSignal->SetBorderColor(0xFFFFFFFF);
	mpUILabelBadSignal->SetFillColor(0xFF204050);
	mpUILabelBadSignal->SetTextColor(0xFFFFFFFF);
	mpUILabelBadSignal->SetTextOffset(8, 8);
	mpUILabelBadSignal->SetPlacement(vdrect32f(0.5f, 0.5f, 0.5f, 0.5f), vdpoint32(0, 0), vdfloat2{0.5f, 0.5f});

	AddChild(mpUILabelBadSignal);
}

void ATUIVideoDisplayWindow::OnDestroy() {
	UnbindAllActions();

	vdsaferelease <<= mpUILabelBadSignal;
	vdsaferelease <<= mpOSK;
	vdsaferelease <<= mpOSKPanel;
	vdsaferelease <<= mpSidePanel;

	ATUIContainer::OnDestroy();
}

void ATUIVideoDisplayWindow::OnSize() {
	const vdsize32& csz = mClientArea.size();

	if (mpOSK) {
		mpOSK->AutoSize();

		const vdsize32& osksz = mpOSK->GetSizeOffset();
		mpOSK->SetPosition(vdpoint32((csz.w - osksz.w) >> 1, 0));

		mpOSKPanel->SetArea(vdrect32(0, csz.h - osksz.h, csz.w, csz.h));
	}

	UpdateEnhTextSize();

	ATUIContainer::OnSize();
}

void ATUIVideoDisplayWindow::OnSetFocus() {
	g_sim.GetInputManager()->SetRestrictedMode(false);

	CloseSidePanel();
}

void ATUIVideoDisplayWindow::OnKillFocus() {
	g_sim.GetInputManager()->SetRestrictedMode(true);

	if (mbDragActive) {
		mbDragActive = false;

		ReleaseCursor();
	}

	OnForceKeysUp();
}

void ATUIVideoDisplayWindow::OnDeactivate() {
	ATInputManager *im = g_sim.GetInputManager();
	im->ReleaseButtons(0, kATInputCode_JoyClass-1);

	OnForceKeysUp();
}

void ATUIVideoDisplayWindow::OnCaptureLost() {
	mbDragActive = false;
	g_mouseCaptured = false;
	g_mouseClipped = false;
	g_winCaptionUpdater->SetMouseCaptured(false, false);

	if (mpActiveTool) {
		vdrefptr tool(mpActiveTool);
		mpActiveTool = nullptr;

		tool->OnCancelMode();
	}
}

ATUIDragEffect ATUIVideoDisplayWindow::OnDragEnter(sint32 x, sint32 y, ATUIDragModifiers modifiers, IATUIDragDropObject *obj) {
	if (!ATUICanDropFiles(obj))
		return ATUIDragEffect::None;

	CreateDropTargetOverlays();
	return OnDragOver(x, y, modifiers, obj);
}

ATUIDragEffect ATUIVideoDisplayWindow::OnDragOver(sint32 x, sint32 y, ATUIDragModifiers modifiers, IATUIDragDropObject *obj) {
	int idx = FindDropTargetOverlay(x, y);

	if (idx >= 0) {
		switch(kDropFileTargets[idx]) {
			case ATUIDropFilesTarget::BootImage:
				obj->SetDropDescription(ATUIDropIconType::Copy, L"Boot image", nullptr);
				break;
			case ATUIDropFilesTarget::MountImage:
				obj->SetDropDescription(ATUIDropIconType::Copy, L"Mount image", nullptr);
				break;
			case ATUIDropFilesTarget::MountCart:
				obj->SetDropDescription(ATUIDropIconType::Copy, L"Mount as cartridge", nullptr);
				break;
			case ATUIDropFilesTarget::MountDisk1:
				obj->SetDropDescription(ATUIDropIconType::Copy, L"Mount as disk %1", L"D1:");
				break;
			case ATUIDropFilesTarget::MountDisk2:
				obj->SetDropDescription(ATUIDropIconType::Copy, L"Mount as disk %1", L"D2:");
				break;
			case ATUIDropFilesTarget::MountDisk3:
				obj->SetDropDescription(ATUIDropIconType::Copy, L"Mount as disk %1", L"D3:");
				break;
			case ATUIDropFilesTarget::MountDisk4:
				obj->SetDropDescription(ATUIDropIconType::Copy, L"Mount as disk %1", L"D4:");
				break;
			case ATUIDropFilesTarget::PasteFilePath:
				// Check if the data object has only one file path.
				if (obj->GetFilePathCount() == 1) {
					if (VDStringW path; obj->GetFilePath(path)) {
						VDStringA cioPath;
						for(IATDeviceNativePathMapping *iface : g_sim.GetDeviceManager()->GetInterfaces<IATDeviceNativePathMapping>(false, false, false)) {
							if (iface->MapNativePathToCIOPath(cioPath, path.c_str())) {
								obj->SetDropDescription(ATUIDropIconType::Copy, L"Paste file path", nullptr);
								break;
							}
						}
						break;
					}
				}

				obj->ClearDropDescription();
				HighlightDropTargetOverlay(-1);
				return ATUIDragEffect::None;
		}
	} else {
		obj->ClearDropDescription();
	}

	HighlightDropTargetOverlay(idx);

	return idx >= 0 ? ATUIDragEffect::Copy : ATUIDragEffect::None;
}

void ATUIVideoDisplayWindow::OnDragLeave() {
	DestroyDropTargetOverlays();
}

ATUIDragEffect ATUIVideoDisplayWindow::OnDragDrop(sint32 x, sint32 y, ATUIDragModifiers modifiers, IATUIDragDropObject *obj) {
	int idx = FindDropTargetOverlay(x, y);

	HighlightDropTargetOverlay(idx);

	if (idx < 0)
		return ATUIDragEffect::None;

	ATUIDropFilesTarget target = kDropFileTargets[idx];

	ATUIDropFiles(TranslateClientPtToScreenPt(vdpoint32(x, y)), target, obj);

	return ATUIDragEffect::Copy;
}

void ATUIVideoDisplayWindow::Paint(IVDDisplayRenderer& rdr, sint32 w, sint32 h) {
	if (mpAltVideoOutput) {
		mpAltVideoOutput->UpdateFrame();

		const auto& vi = mpAltVideoOutput->GetVideoInfo();
		uint32 lcc = vi.mFrameBufferLayoutChangeCount;
		if (mAltVOLayoutChangeCount != lcc) {
			mAltVOLayoutChangeCount = lcc;
			mAltVOImageView.SetImage(mpAltVideoOutput->GetFrameBuffer(), true);
		}

		uint32 cc = vi.mFrameBufferChangeCount;
		if (cc != mAltVOChangeCount) {
			mAltVOChangeCount = cc;
			mAltVOImageView.Invalidate();
		}

		if (!vi.mbSignalValid) {
			rdr.SetColorRGB(0);
			rdr.FillRect(0, 0, w, h);

			mpUILabelBadSignal->SetVisible(true);
			mpUILabelBadSignal->SetTextAlign(ATUILabel::kAlignCenter);

			if (vi.mHorizScanRate > 0 && vi.mVertScanRate > 0)
				mpUILabelBadSignal->SetTextF(L"Unsupported video mode\n%.3fKHz, %.1fHz", vi.mHorizScanRate / 1000.0f, vi.mVertScanRate);
			else
				mpUILabelBadSignal->SetText(L"No signal");
		} else if (vi.mbSignalPassThrough) {
			mpUILabelBadSignal->SetVisible(false);
		} else {
			const vdrect32& dst = GetAltDisplayArea();

			if (dst.left > 0 || dst.top > 0 || dst.right < w || dst.bottom < h) {
				rdr.SetColorRGB(0);
				rdr.FillRect(0, 0, w, h);
			}

			mpUILabelBadSignal->SetVisible(false);

			VDDisplayBltOptions opts = {};

			switch(g_dispFilterMode) {
				case kATDisplayFilterMode_Point:
					opts.mFilterMode = VDDisplayBltOptions::kFilterMode_Point;
					break;

				case kATDisplayFilterMode_AnySuitable:
				case kATDisplayFilterMode_Bilinear:
				case kATDisplayFilterMode_Bicubic:
					opts.mFilterMode = VDDisplayBltOptions::kFilterMode_Bilinear;
					break;

				case kATDisplayFilterMode_SharpBilinear:
					opts.mFilterMode = VDDisplayBltOptions::kFilterMode_Bilinear;

					{
						static const float kFactors[5] = { 1.259f, 1.587f, 2.0f, 2.520f, 3.175f };

						const float factor = kFactors[std::max(0, std::min(4, g_dispFilterSharpness + 2))];

						opts.mSharpnessX = std::max(1.0f, factor / 2.0f);
						opts.mSharpnessY = std::max(1.0f, factor);
					}
					break;
			}

			const vdrect32& src = vi.mDisplayArea;

			if (vi.mBorderColor) {
				rdr.SetColorRGB(vi.mBorderColor);

				if (dst.top > 0)
					rdr.FillRect(0, 0, w, dst.top);

				if (dst.left > 0)
					rdr.FillRect(0, dst.top, dst.left, dst.height());

				if (dst.right < w)
					rdr.FillRect(dst.right, dst.top, w - dst.right, dst.height());

				if (dst.bottom < h)
					rdr.FillRect(0, 0, w, h);
			}

			rdr.StretchBlt(dst.left, dst.top, dst.width(), dst.height(), mAltVOImageView, src.left, src.top, src.width(), src.height(), opts);
		}
	} else
		mpUILabelBadSignal->SetVisible(false);

	if (rdr.GetCaps().mbSupportsAlphaBlending) {
		vdfastvector<vdfloat2> pts;

		for (const HighlightPoint& xr : mHighlightPoints) {
			pts.push_back(vdfloat2{(float)xr.mX, (float)xr.mY});

			if (xr.mbClose) {
				rdr.AlphaTriStrip(pts.data(), pts.size(), 0x8000A0FF);
				pts.clear();
			}
		}
	} else {
		rdr.SetColorRGB(0x00A0FF);

		// Convert the tristrips to polygon outlines. For each tristrip, we need to separate the
		// even and odd points and place the odd points reversed after the even points.
		vdfastvector<vdpoint32> pts[2];
		bool odd = false;

		for (const HighlightPoint& xr : mHighlightPoints) {
			const vdpoint32 pt(xr.mX, xr.mY);

			pts[odd].push_back(pt);
			odd = !odd;

			if (xr.mbClose) {
				for(auto it = pts[1].rbegin(), itEnd = pts[1].rend(); it != itEnd; ++it)
					pts[0].push_back(*it);

				pts[0].push_back(pts[0].front());

				rdr.PolyLine(pts[0].data(), pts[0].size() - 1);

				pts[0].clear();
				pts[1].clear();
			}
		}
	}

	auto& im = *g_sim.GetInputManager();
	if (g_ATUIDrawPadBounds && im.HasNonBeamPointer()) {
		const auto& r = mPadArea;

		rdr.SetColorRGB(0xE0E4FF);
		rdr.FillRect(r.left, r.top, r.width(), 1);
		rdr.FillRect(r.left, r.top, 1, r.height());
		rdr.FillRect(r.right - 1, r.top, 1, r.height());
		rdr.FillRect(r.left, r.bottom - 1, r.width(), 1);
	}

	if (g_ATUIDrawPadPointers) {
		vdfastvector<ATInputPointerInfo> pointers;
		im.GetPointers(pointers);

		if (!pointers.empty()) {
			const vdsize32 size = mPadArea.size();
			const bool plf = rdr.GetCaps().mbSupportsPolyLineF;

			for(const ATInputPointerInfo& pointer : pointers) {
				// If this is the primary pointer for a controller and it's bound to the mouse in
				// absolute mode, don't show it if we're showing the target mouse pointer cursor.
				if (pointer.mbPrimary && im.HasAbsMousePointer() && im.IsMouseActiveTarget() && g_ATUITargetPointerVisible)
					continue;

				sint32 x = 0;
				sint32 y = 0;

				switch(pointer.mCoordSpace) {
					case ATInputPointerCoordinateSpace::None:
						break;

					case ATInputPointerCoordinateSpace::Normalized:
						x = mPadArea.left + VDRoundToInt((float)(mPadArea.width() - 1) * (pointer.mPos.x * 0.5f + 0.5f));
						y = mPadArea.top + VDRoundToInt((float)(mPadArea.height() - 1) * (pointer.mPos.y * 0.5f + 0.5f));
						break;

					case ATInputPointerCoordinateSpace::Beam:
						{
							const auto& pt = MapBeamPositionToPointF(vdfloat2 { pointer.mPos.x, pointer.mPos.y });

							x = VDRoundToInt(pt.x);
							y = VDRoundToInt(pt.y);
						}
						break;
				}

				if (pointer.mRadius < 0) {
					rdr.SetColorRGB(0xE0E4FF);
					rdr.FillRect(x - 20, y, 41, 1);
					rdr.FillRect(x, y - 20, 1, 41);
					rdr.SetColorRGB(0x101230);
					rdr.FillRect(x - 10, y, 21, 1);
					rdr.FillRect(x, y - 10, 1, 21);
				} else {
					float rx = pointer.mRadius * (float)mPadArea.width()  * 0.5f;
					float ry = pointer.mRadius * (float)mPadArea.height() * 0.5f;

					rdr.SetColorRGB(0xE0E4FF);
					if (plf && rx > 2 && ry > 2) {
						vdfloat2 fpts[33];
						vdfloat2 cen { (float)x, (float)y };
						vdfloat2 del { 1.0f, 0.0f };
						vdfloat2 r { rx, ry };

						const float cs = 0.98078528040323044912618223613424f;
						const float sn = 0.19509032201612826784828486847702f;

						for(int i=0; i<32; ++i) {
							fpts[i] = cen + del * r;

							del = vdfloat2 { del.x*cs + del.y*sn, del.y*cs - del.x*sn };
						}

						fpts[32] = fpts[0];
						rdr.PolyLineF(fpts, 32, true);
					} else {
						sint32 irx = VDRoundToInt(rx);
						sint32 iry = VDRoundToInt(ry);
 
						vdpoint32 pts[5];
						pts[0] = vdpoint32(x - irx, y - iry);
						pts[1] = vdpoint32(x + irx, y - iry);
						pts[2] = vdpoint32(x + irx, y + iry);
						pts[3] = vdpoint32(x - irx, y + iry);
						pts[4] = vdpoint32(x - irx, y - iry);
						rdr.PolyLine(pts, 4);
					}
				}
			}
		}
	}

	if (g_ATCVUIShowVSyncAdaptiveGraph) {
		extern float g_frameRefreshPeriod;
		extern float g_frameCorrectionFactor;

		if (rdr.PushViewport(vdrect32(10, 500, 510, 800), 10, 500)) {
			rdr.SetColorRGB(0x40404040);
			rdr.AlphaFillRect(0, 0, 500, 1, 0x40404040);
			rdr.AlphaFillRect(0, 299, 500, 1, 0x40404040);
			rdr.AlphaFillRect(1, 150, 498, 1, 0x80404040);
			rdr.AlphaFillRect(0, 1, 1, 298, 0x40404040);
			rdr.AlphaFillRect(499, 1, 1, 298, 0x40404040);

			static uint64 sLastTick = 0;

			const uint64 t = VDGetPreciseTick();

			float frameTime = (float)(sint64)(t - sLastTick) * (float)VDGetPreciseSecondsPerTick();
			sLastTick = t;

			struct VSyncHistory {
				vdfloat2 mHistory[3][500] {};
			};
		
			static vdautoptr<VSyncHistory> sHistoryStorage(new VSyncHistory);

			static vdfloat2 (&sHistory)[3][500] = sHistoryStorage->mHistory;
			for(int i=0; i<3; ++i) {
				for(int j=0; j<499; ++j) {
					sHistory[i][j].x = (float)j;
					sHistory[i][j].y = sHistory[i][j+1].y;
				}

				sHistory[i][499].x = 499.0f;

				if (i == 2)
					sHistory[i][499].y = 150.0f - frameTime / 0.033f * 150.0f;
				else if (i)
					sHistory[i][499].y = 150.0f - g_frameCorrectionFactor / 0.002f * 150.0f;
				else
					sHistory[i][499].y = 150.0f - g_frameRefreshPeriod / 0.033f * 150.0f;

				rdr.SetColorRGB(i == 2 ? 0x4080FF : i ? 0x00FF00 : 0xFF0000);
				rdr.PolyLineF(sHistory[i], 499, true);
			}
			rdr.PopViewport();
		}
	}

	if (mpVideoWriter && g_ATCVRecordingVideoShowMotionVectors) {
		ATVideoRecordingDebugInfo debugInfo;

		if (mpVideoWriter->GetDebugInfo(debugInfo)
			&& debugInfo.mImageWidth > 0
			&& debugInfo.mImageHeight > 0
			&& !debugInfo.mVideoDestRect.empty()
			&& !mDisplayRect.empty()
		) {
			rdr.AlphaFillRect(
				mDisplayRect.left,
				mDisplayRect.top,
				mDisplayRect.width(),
				mDisplayRect.height(),
				0xC0000000);
			
			rdr.SetColorRGB(0xFFFFFF);

			// Blocks are given in output video coordinates, but our display rect maps to the
			// video destination rect used by the video recorder. The transform from output
			// coordinates to viewport coordinates is as follows:
			//
			// - Subtract the destination rect origin
			// - Divide by destination rect width
			// - Multiply by display rect width
			// - Add display rect origin

			const float mvScaleX = mDisplayRect.width() / debugInfo.mVideoDestRect.width();
			const float mvScaleY = mDisplayRect.height() / debugInfo.mVideoDestRect.height();
			const float mvBlockStepX = (float)debugInfo.mBlockWidth * mvScaleX;
			const float mvBlockStepY = (float)debugInfo.mBlockHeight * mvScaleY;

			const auto *mv = debugInfo.mMotionVectors.data();

			float y = (float)mDisplayRect.top + 0.5f * mvBlockStepY - debugInfo.mVideoDestRect.top * mvScaleY;

			for(uint32 by = 0; by < debugInfo.mNumBlocksY; ++by) {

				float x = (float)mDisplayRect.left + 0.5f * mvBlockStepX - debugInfo.mVideoDestRect.left * mvScaleX;
				for(uint32 bx = 0; bx < debugInfo.mNumBlocksX; ++bx) {
					vdfloat2 pts[2] {
						vdfloat2 { x, y },
						vdfloat2 { x + mv->mX * mvScaleX, y + mv->mY * mvScaleY }
					};

					rdr.PolyLineF(pts, 1, true);
					rdr.FillRect((sint32)x, (sint32)y, 1, 1);

					++mv;
					x += mvBlockStepX;
				}

				y += mvBlockStepY;
			}
		}
	}

	ATUIContainer::Paint(rdr, w, h);
}

void ATUIVideoDisplayWindow::UpdateAltDisplay() {
	IATDeviceVideoOutput *prev = mpAltVideoOutput;

	if (mpEnhTextEngine)
		mpAltVideoOutput = mpEnhTextEngine->GetVideoOutput();
	else if (!g_ATCurrentAltViewName.empty())
		mpAltVideoOutput = mpVideoMgr->GetOutputByName(g_ATCurrentAltViewName.c_str());
	else
		mpAltVideoOutput = nullptr;

	if (prev != mpAltVideoOutput) {
		if (mpAltVideoOutput) {
			const auto& vi = mpAltVideoOutput->GetVideoInfo();

			// do force a change event next frame
			mAltVOChangeCount = vi.mFrameBufferChangeCount - 1;
			mAltVOLayoutChangeCount = vi.mFrameBufferLayoutChangeCount - 1;

			Invalidate();
		}

		IATUIRenderer *uir = g_sim.GetUIRenderer();
		if (uir)
			uir->SetStatusMessage(GetCurrentOutputName().c_str());
	}

	mpVideoMgr->CheckForNewlyActiveOutputs();
}

void ATUIVideoDisplayWindow::SelectPrevOutput() {
	const sint32 idx = mpVideoMgr->IndexOfOutput(mpAltVideoOutput);

	SelectAltOutputByIndex(idx < 0 ? (sint32)mpVideoMgr->GetOutputCount() - 1 : idx - 1);
}

void ATUIVideoDisplayWindow::SelectNextOutput() {
	SelectAltOutputByIndex(mpVideoMgr->IndexOfOutput(mpAltVideoOutput) + 1);
}

void ATUIVideoDisplayWindow::SelectAltOutputByIndex(sint32 idx) {
	IATDeviceVideoOutput *next = idx >= 0 ? mpVideoMgr->GetOutput((uint32)idx) : nullptr;

	if (next)
		ATUISetCurrentAltOutputName(next->GetName());
	else
		ATUISetCurrentAltOutputName("");
}

sint32 ATUIVideoDisplayWindow::GetCurrentAltOutputIndex() const {
	return mpVideoMgr->IndexOfOutput(mpAltVideoOutput);
}

bool ATUIVideoDisplayWindow::IsAltOutputAvailable() const {
	return mpVideoMgr->GetOutputCount() > 0;
}

bool ATUIVideoDisplayWindow::IsAltOutputAvailable(const char *name) const {
	return mpVideoMgr->GetOutputByName(name) != nullptr;
}

VDStringW ATUIVideoDisplayWindow::GetCurrentOutputName() const {
	if (mpAltVideoOutput)
		return VDStringW(mpAltVideoOutput->GetDisplayName()) + L" Output";
	else
		return VDStringW(L"Normal Output");
}

vdrect32 ATUIVideoDisplayWindow::GetCurrentOutputArea() const {
	if (mpAltVideoOutput)
		return GetAltDisplayArea();
	else
		return mDisplayRect;
}

bool ATUIVideoDisplayWindow::ProcessKeyDown(const ATUIKeyEvent& event, bool enableKeyInput) {
	ATInputManager *im = g_sim.GetInputManager();

	const int key = event.mVirtKey;
	const bool alt = mpManager->IsKeyDown(kATUIVK_Alt);

	if (!alt) {
		const int inputCode = event.mExtendedVirtKey;

		if (im->IsInputMapped(0, inputCode)) {
			im->OnButtonDown(0, inputCode);

			if (!g_kbdOpts.mbAllowInputMapOverlap)
				return true;
		}
	}

	const bool isRepeat = event.mbIsRepeat;
	bool shift = mpManager->IsKeyDown(kATUIVK_Shift);
	bool ctrl = mpManager->IsKeyDown(kATUIVK_Control);
	const bool ext = event.mbIsExtendedKey;

	if (ATUIActivateVirtKeyMapping(key, alt, ctrl, shift, ext, false, kATUIAccelContext_Display)) {
		return true;
	} else {
		// Check if L/R Ctrl/Shift are bound in input maps and mask out the Ctrl/Shift flags for
		// the keyboard mapping if so. This is to prevent those keys from triggering Ctrl/Shift
		// keys. Note that we only do this AFTER checking the accelerators above, as we do not
		// want to block these for keyboard shortcuts, ESPECIALLY if AltGr is involved.
		ExcludeMappedCtrlShiftState(ctrl, shift);

		uint32 scanCode;
		if (ATUIGetScanCodeForVirtualKey(key, alt, ctrl, shift, ext, scanCode)) {
			if (!enableKeyInput)
				return false;

			ProcessVirtKey(key, 0, scanCode, isRepeat);
			return true;
		}

		switch(key) {
			case kATUIVK_Shift:
				if (!mbShiftDepressed) {
					mbShiftDepressed = true;
					mbShiftToggledPostKeyDown = true;
					UpdateCtrlShiftState();
				}
				break;
		}
	}

	return false;
}

bool ATUIVideoDisplayWindow::ProcessKeyUp(const ATUIKeyEvent& event, bool enableKeyInput) {
	ATInputManager *im = g_sim.GetInputManager();

	const int key = event.mVirtKey;
	const bool alt = mpManager->IsKeyDown(kATUIVK_Alt);

	// Check if we need to release an active key. Do this before the Alt-check
	// is done for the shunt to the input manager -- this is necessary if Alt+key
	// was originally pressed and Alt is released first, but un-Alted key is
	// bound in the input manager.
	auto it = std::find_if(mActiveKeys.begin(), mActiveKeys.end(), [=](const ActiveKey& ak) { return ak.mVkey == key && ak.mNativeScanCode == 0; });
	if (it != mActiveKeys.end()) {
		g_sim.GetPokey().ReleaseRawKey(it->mScanCode, !g_kbdOpts.mbFullRawKeys);
		*it = mActiveKeys.back();
		mActiveKeys.pop_back();
		UpdateCtrlShiftState();
	}

	// Pass the key up to the input manager. This is done even if Alt is pressed, to
	// ensure that an up event occurs for any keys that were pressed down prior to
	// Alt being pressed.
	const int inputCode = event.mExtendedVirtKey;
	if (im->IsInputMapped(0, inputCode)) {
		im->OnButtonUp(0, inputCode);

		if (alt && !g_kbdOpts.mbAllowInputMapOverlap)
			return true;
	}

	for(uint32 i=0; i<(uint32)vdcountof(mActiveSpecialVKeys); ++i) {
		if (mActiveSpecialVKeys[i] == key) {
			mActiveSpecialVKeys[i] = 0;
			mActiveSpecialScanCodes[i] = 0;

			ProcessSpecialKey(kATUIKeyScanCodeFirst + i, false);
			return true;
		}
	}

	const bool shift = mpManager->IsKeyDown(kATUIVK_Shift);
	const bool ctrl = mpManager->IsKeyDown(kATUIVK_Control);
	const bool ext = event.mbIsExtendedKey;

	if (!ATUIActivateVirtKeyMapping(key, alt, ctrl, shift, ext, true, kATUIAccelContext_Display)) {
		switch(key) {
			case kATUIVK_Shift:
				if (mbShiftDepressed) {
					mbShiftDepressed = false;
					mbShiftToggledPostKeyDown = true;
					UpdateCtrlShiftState();
				}
				break;
		}
	}

	return false;
}

void ATUIVideoDisplayWindow::ProcessVirtKey(uint32 vkey, uint32 scancode, uint32 keycode, bool repeat) {
	if (keycode >= kATUIKeyScanCodeFirst && keycode <= kATUIKeyScanCodeLast) {
		if (!repeat) {
			ProcessSpecialKey(keycode, true);

			mActiveSpecialVKeys[keycode - kATUIKeyScanCodeFirst] = vkey;
			mActiveSpecialScanCodes[keycode - kATUIKeyScanCodeFirst] = scancode;
		}
	} else if (mbHoldKeys) {
		if (!repeat)
			ToggleHeldKey((uint8)keycode);
	} else {
		if (g_kbdOpts.mbRawKeys) {
			if (!repeat) {
				mbShiftToggledPostKeyDown = false;

				auto it = std::find_if(mActiveKeys.begin(), mActiveKeys.end(), [=](const ActiveKey& ak) { return ak.mVkey == vkey && ak.mNativeScanCode == scancode; });
				if (it != mActiveKeys.end())
					it->mScanCode = keycode;
				else
					mActiveKeys.push_back(ActiveKey { vkey, scancode, (uint8)keycode });

				UpdateCtrlShiftState();
				g_sim.GetPokey().PushRawKey(keycode, !g_kbdOpts.mbFullRawKeys);
			}
		} else
			g_sim.GetPokey().PushKey(keycode, repeat);
	}
}

void ATUIVideoDisplayWindow::ProcessSpecialKey(uint32 scanCode, bool state) {
	switch(scanCode) {
		case kATUIKeyScanCode_Start:
			if (mbHoldKeys) {
				if (state)
					ToggleHeldConsoleButton(0x01);
			} else
				g_sim.GetGTIA().SetConsoleSwitch(0x01, state);
			break;
		case kATUIKeyScanCode_Select:
			if (mbHoldKeys) {
				if (state)
					ToggleHeldConsoleButton(0x02);
			} else
				g_sim.GetGTIA().SetConsoleSwitch(0x02, state);
			break;
		case kATUIKeyScanCode_Option:
			if (mbHoldKeys) {
				if (state)
					ToggleHeldConsoleButton(0x04);
			} else
				g_sim.GetGTIA().SetConsoleSwitch(0x04, state);
			break;
		case kATUIKeyScanCode_Break:
			g_sim.GetPokey().SetBreakKeyState(state, !g_kbdOpts.mbFullRawKeys);
			break;
	}
}

void ATUIVideoDisplayWindow::ToggleHeldKey(uint8 keycode) {
	if (g_sim.GetPendingHeldKey() == keycode)
		g_sim.ClearPendingHeldKey();
	else
		g_sim.SetPendingHeldKey(keycode);
}

void ATUIVideoDisplayWindow::ToggleHeldConsoleButton(uint8 encoding) {
	uint8 buttons = g_sim.GetPendingHeldSwitches();

	g_sim.SetPendingHeldSwitches(buttons ^ encoding);
}

void ATUIVideoDisplayWindow::UpdateCtrlShiftState() {
	// if we're in 5200 mode, do not update ctrl/shift as there is no keyboard
	ATInputManager *im = g_sim.GetInputManager();
	if (im->Is5200Mode())
		return;

	uint8 c = 0;
	
	// It is possible for us to have a conflict where a key mapping has been
	// created with the Shift or Ctrl key to a key that doesn't require Shift or Ctrl
	// on the Atari keyboard. Therefore, we override the Shift key state on the host
	// with whatever is required by the scan code, unless the Shift key has been
	// toggled after the initial key down (see below).
	if (mActiveKeys.empty()) {
		if (mbShiftDepressed)
			c = 0x40;
	} else {
		for(const auto& ak : mActiveKeys)
			c |= ak.mScanCode;
	}

	auto& pokey = g_sim.GetPokey();
	pokey.SetControlKeyState((c & 0x80) != 0);

	// If the Shift key is toggled after a key down, force the Shift key state to
	// the host Shift key. This will be consistent except in the case where the
	// key down has a Shift key mismatch between the host keys, which is an ambiguous
	// case anyway. When there is no such mismatch, this makes Shift key up/down after
	// non-Shift key up/down work as expected.
	if (g_kbdOpts.mbRawKeys && !mbShiftToggledPostKeyDown) {
		pokey.SetShiftKeyState((c & 0x40) != 0, !g_kbdOpts.mbFullRawKeys);
	} else
		pokey.SetShiftKeyState(mbShiftDepressed, true);
}

void ATUIVideoDisplayWindow::ExcludeMappedCtrlShiftState(bool& ctrl, bool& shift) const {
	if (g_kbdOpts.mbAllowInputMapModifierOverlap)
		return;

	ATInputManager *im = g_sim.GetInputManager();

	if (shift) {
		const bool lshiftBound = im->IsInputMapped(0, kATInputCode_KeyLShift);
		const bool rshiftBound = im->IsInputMapped(0, kATInputCode_KeyRShift);

		if (lshiftBound || rshiftBound) {
			shift = !lshiftBound && mpManager->IsKeyDown(kATUIVK_LShift);

			if (!shift)
				shift = !rshiftBound && mpManager->IsKeyDown(kATUIVK_RShift);
		}
	}

	if (ctrl) {
		const bool lctrlBound = im->IsInputMapped(0, kATInputCode_KeyLControl);
		const bool rctrlBound = im->IsInputMapped(0, kATInputCode_KeyRControl);

		if (lctrlBound || rctrlBound) {
			ctrl = !lctrlBound && mpManager->IsKeyDown(kATUIVK_LControl);

			if (!shift)
				ctrl = !rctrlBound && mpManager->IsKeyDown(kATUIVK_RControl);
		}
	}
}

uint32 ATUIVideoDisplayWindow::ComputeCursorImage(const vdpoint32& pt) const {
	bool validBeamPosition;
	
	if (mpAltVideoOutput && !mpAltVideoOutput->GetVideoInfo().mbSignalPassThrough) {
		const auto& vi = mpAltVideoOutput->GetVideoInfo();
		validBeamPosition = vi.mTextRows > 0 && vi.mTextColumns > 0 && GetAltDisplayArea().contains(pt);
	} else {
		int xs, ys;
		validBeamPosition = (MapPixelToBeamPosition(pt.x, pt.y, xs, ys, false) && GetModeLineYPos(ys, true) >= 0);
	}

	ATUICursorImage cursorImage = kATUICursorImage_None;
	if (mpActiveTool) {
		cursorImage = mpActiveTool->GetCursorImage(pt.x, pt.y);

		if (cursorImage != kATUICursorImage_None)
			return cursorImage;
	}

	for(const auto& tool : mTools) {
		if (tool && tool->HasPriorityOverInputManager()) {
			cursorImage = tool->GetCursorImage(pt.x, pt.y);

			if (cursorImage != kATUICursorImage_None)
				return cursorImage;

			if (tool->IsMainTool())
				break;
		}
	}

	auto& im = *g_sim.GetInputManager();
	if (!g_mouseCaptured && validBeamPosition && mpManager->IsKeyDown(kATUIVK_Alt))
		return kATUICursorImage_Query;

	if (im.IsMouseMapped()) {
		if (g_mouseAutoCapture && !g_mouseCaptured) {

			// Auto-capture is on but we have not captured the cursor. In this case we show the
			// arrow. We don't show the I-Beam because left-click selection functionality is
			// overridden by the mouse auto-capture.

			return kATUICursorImage_Arrow;

		} else if (im.IsMouseAbsoluteMode()) {

			// We're in absolute mode, and either the mouse is captured or auto-capture is off.
			// In this case we will be passing absolute mouse inputs to the input manager. Show
			// the target or off-target depending on whether the target is active (light pen
			// aimed at screen or otherwise not a light pen/gun).

			if (!im.HasNonBeamPointer() || mPadArea.contains(pt)) {
				if (im.IsMouseActiveTarget())
					return g_ATUITargetPointerVisible ? kATUICursorImage_Target : kATUICursorImage_Hidden;
				else
					return kATUICursorImage_TargetOff;
			}
		} else if (IsCursorCaptured()) {

			// We're in relative mode and the cursor is captured. We need to hide the mouse
			// cursor so you don't see the jitters.

			return kATUICursorImage_Hidden;

		}

		// The mouse is bound in relative mode but not captured, so we should fall through.
	}

	for(const auto& tool : mTools) {
		if (tool && !tool->HasPriorityOverInputManager()) {
			cursorImage = tool->GetCursorImage(pt.x, pt.y);

			if (cursorImage != kATUICursorImage_None)
				return cursorImage;

			if (tool->IsMainTool())
				break;
		}
	}

	if (validBeamPosition)
		return kATUICursorImage_IBeam;

	return kATUICursorImage_Arrow;
}

void ATUIVideoDisplayWindow::UpdateMousePosition(int x, int y) {
	int padX = 0;
	int padY = 0;

	const vdsize32 size = mPadArea.size();

	if (size.w)
		padX = VDRoundToInt((x - mPadArea.left) * 131072.0f / ((float)mPadArea.width()  - 1) - 0x10000);

	if (size.h)
		padY = VDRoundToInt((y - mPadArea.top ) * 131072.0f / ((float)mPadArea.height() - 1) - 0x10000);

	ATInputManager *im = g_sim.GetInputManager();
	im->SetMousePadPos(padX, padY);

	float xc, yc;
	if (MapPixelToBeamPosition(x, y, xc, yc, false)) {
		// map cycles to normalized position (note that this must match the light pen
		// computations)
		float xn = (xc - 128.0f) * (65536.0f / 94.0f);
		float yn = (yc - 128.0f) * (65536.0f / 188.0f);

		im->SetMouseBeamPos(VDRoundToInt(xn), VDRoundToInt(yn));
	}

	// compute 50% rect within inscribed rect of window
	im->SetMouseVirtualStickPos(0, 0);

	if (size.w > 1 && size.h > 1) {
		float sizeX = (float)size.w - 1.0f;
		float sizeY = (float)size.h - 1.0f;
		float normX = (float)x / sizeX * 2.0f - 1.0f;
		float normY = (float)y / sizeY * 2.0f - 1.0f;

		if (sizeX > sizeY)
			normX *= sizeX / sizeY;
		else if (sizeY > sizeX)
			normY *= sizeY / sizeX;

		im->SetMouseVirtualStickPos(VDRoundToInt32(normX * 0x1.0p+17), VDRoundToInt32(normY * 0x1.0p+17));
	}
}

const vdrect32 ATUIVideoDisplayWindow::GetAltDisplayArea() const {
	if (!mpAltVideoOutput)
		return vdrect32(0, 0, 0, 0);

	const auto& vi = mpAltVideoOutput->GetVideoInfo();
	const vdrect32& r = vi.mDisplayArea;
	float viewportWidth = mArea.width();
	float viewportHeight = mArea.height();

	if (g_dispPadIndicators)
		viewportHeight -= g_sim.GetUIRenderer()->GetIndicatorSafeHeight();

	vdrect32 rsafe = g_pATVideoDisplayWindow->GetOSKSafeArea();

	viewportHeight = std::min<float>(viewportHeight, rsafe.height());

	float w = viewportWidth;
	float h = viewportHeight;

	if (vi.mbForceExactPixels) {
		float ratio = std::min<float>(1, std::min<float>((float)w / (float)r.width(), (float)h / (float)r.height()));

		w = (float)r.width() * ratio;
		h = (float)r.height() * ratio;
	} else if (g_displayStretchMode != kATDisplayStretchMode_Unconstrained) {
		float par = 0.5;
		
		switch(g_displayStretchMode) {
			case kATDisplayStretchMode_SquarePixels:
			case kATDisplayStretchMode_Integral:
				break;

			default:
				par = (float)vi.mPixelAspectRatio;
				break;
		}

		const float fitw = (float)r.width() * par;
		const float fith = (float)r.height();
		float scale = std::min<float>(w / fitw, h / fith);

		switch(g_displayStretchMode) {
			case kATDisplayStretchMode_Integral:
			case kATDisplayStretchMode_IntegralPreserveAspectRatio:
				if (scale > 1.0)
					scale = floor(scale);
				break;
		}

		w = fitw * scale;
		h = fith * scale;
	}

	if (!vi.mbForceExactPixels) {
		const float zoom = ATUIGetDisplayZoom();
		w *= zoom;
		h *= zoom;
	}

	vdfloat2 relativeSourceOrigin = vdfloat2(0.5f, 0.5f) - ATUIGetDisplayPanOffset();

	vdrect32f rd(
		w * (relativeSourceOrigin.x - 1.0f),
		h * (relativeSourceOrigin.y - 1.0f),
		w * relativeSourceOrigin.x,
		h * relativeSourceOrigin.y
	);

	rd.translate(viewportWidth * 0.5f, viewportHeight * 0.5f);

	// try to pixel snap the rectangle if possible
	vdrect32f error(
		rd.left   - roundf(rd.left  ),
		rd.top    - roundf(rd.top   ),
		rd.right  - roundf(rd.right ),
		rd.bottom - roundf(rd.bottom)
	);

	rd.translate(-0.5f*(error.left + error.right), -0.5f*(error.top + error.bottom));

	vdrect32 ri;

	ri.left = VDRoundToInt(rd.left);
	ri.top = VDRoundToInt(rd.top);
	ri.right = ri.left + VDRoundToInt(rd.width());
	ri.bottom = ri.top + VDRoundToInt(rd.height());

	return ri;
}

bool ATUIVideoDisplayWindow::MapPixelToBeamPosition(int x, int y, float& hcyc, float& vcyc, bool clamp) const {
	if (!mDisplayRect.contains(vdpoint32(x, y))) {
		if (!clamp)
			return false;

		if (mDisplayRect.empty())
			return false;
	}

	x -= mDisplayRect.left;
	y -= mDisplayRect.top;

	if (clamp) {
		x = std::clamp(x, 0, mDisplayRect.width());
		y = std::clamp(y, 0, mDisplayRect.height());
	}

	const vdfloat2 displayRectSize {(float)mDisplayRect.width(), (float)mDisplayRect.height()};
	vdfloat2 pt = (vdfloat2{(float)x, (float)y} + vdfloat2{0.5f, 0.5f}) / displayRectSize;

	if (mpMapDisplayToSourcePt && !mpMapDisplayToSourcePt(pt) && !clamp)
		return false;

	pt *= displayRectSize;
	x = VDFloorToInt(pt.x);
	y = VDFloorToInt(pt.y);

	ATGTIAEmulator& gtia = g_sim.GetGTIA();
	const vdrect32 scanArea(gtia.GetFrameScanArea());

	// map position to cycles
	hcyc = (float)scanArea.left + ((float)x + 0.5f) * (float)scanArea.width()  / (float)mDisplayRect.width()  - 0.5f;
	vcyc = (float)scanArea.top  + ((float)y + 0.5f) * (float)scanArea.height() / (float)mDisplayRect.height() - 0.5f;
	return true;
}

bool ATUIVideoDisplayWindow::MapPixelToBeamPosition(int x, int y, int& xc, int& yc, bool clamp) const {
	float xf, yf;

	if (!MapPixelToBeamPosition(x, y, xf, yf, clamp))
		return false;

	xc = (int)floorf(xf + 0.5f);
	yc = (int)floorf(yf + 0.5f);
	return true;
}

// Map a beam position in (half cycles, scan) to a screen point. This maps points at the corners of
// pixels instead of the centers since it is used for rectangle/polygon mapping.
vdint2 ATUIVideoDisplayWindow::MapBeamPositionToPoint(int xc, int yc) const {
	const vdfloat2 pt = MapBeamPositionToPointF(vdfloat2 {(float)xc, (float)yc});

	return vdint2 {
		VDRoundToInt(pt.x),
		VDRoundToInt(pt.y)
	};
}

void ATUIVideoDisplayWindow::MapBeamPositionToPoint(int xc, int yc, int& x, int& y) const {
	const auto& pt = MapBeamPositionToPoint(xc, yc);

	x = pt.x;
	y = pt.y;
}

vdfloat2 ATUIVideoDisplayWindow::MapBeamPositionToPointF(vdfloat2 pt) const {
	ATGTIAEmulator& gtia = g_sim.GetGTIA();
	const vdrect32 scanArea(gtia.GetFrameScanArea());

	// map position to cycles
	pt = pt - vdfloat2{(float)scanArea.left, (float)scanArea.top};

	pt /= vdfloat2 { (float)scanArea.width(), (float)scanArea.height() };

	if (mpMapSourceToDisplayPt)
		mpMapSourceToDisplayPt(pt);

	const vdfloat2 drPos { (float)mDisplayRect.left, (float)mDisplayRect.top };
	const vdfloat2 drSize { (float)mDisplayRect.width(), (float)mDisplayRect.height() };
	return pt * drSize + drPos;
}

void ATUIVideoDisplayWindow::UpdateDragPreview(int x, int y) {
	if (mpAltVideoOutput && !mpAltVideoOutput->GetVideoInfo().mbSignalPassThrough)
		UpdateDragPreviewAlt(x, y);
	else
		UpdateDragPreviewAntic(x, y);
}

void ATUIVideoDisplayWindow::UpdateDragPreviewAlt(int x, int y) {
	const auto& vi = mpAltVideoOutput->GetVideoInfo();
	if (!vi.mTextRows || !vi.mTextColumns)
		return;

	const vdrect32& drawArea = GetAltDisplayArea();
	if (drawArea.empty())
		return;

	const vdrect32& dispArea = vi.mDisplayArea;
	if (dispArea.empty())
		return;

	const int xepx1 = VDFloorToInt(((float)(mDragAnchorX - drawArea.left) + 0.5f) * (float)dispArea.width() / (float)drawArea.width());
	const int xepy1 = VDFloorToInt(((float)(mDragAnchorY - drawArea.top) + 0.5f) * (float)dispArea.height() / (float)drawArea.height());
	const int xepx2 = VDFloorToInt(((float)(x - drawArea.left) + 0.5f) * (float)dispArea.width() / (float)drawArea.width());
	const int xepy2 = VDFloorToInt(((float)(y - drawArea.top) + 0.5f) * (float)dispArea.height() / (float)drawArea.height());

	if (mbDragInitial) {
		if (xepx1 == xepx2 && xepy1 == xepy2)
			return;

		mbDragInitial = false;
	}

	vdpoint32 caretPos1 = mpAltVideoOutput->PixelToCaretPos(vdpoint32(xepx1, xepy1));
	vdpoint32 caretPos2 = mpAltVideoOutput->PixelToCaretPos(vdpoint32(xepx2, xepy2));

	SelectByCaretPosAlt(caretPos1, caretPos2);
}

void ATUIVideoDisplayWindow::SelectByCaretPosAlt(vdpoint32 caretPos1, vdpoint32 caretPos2) {
	const auto& vi = mpAltVideoOutput->GetVideoInfo();
	if (!vi.mTextRows || !vi.mTextColumns)
		return;

	caretPos1.x = std::clamp<sint32>(caretPos1.x, 0, vi.mTextColumns);
	caretPos1.y = std::clamp<sint32>(caretPos1.y, 0, vi.mTextRows);
	caretPos2.x = std::clamp<sint32>(caretPos2.x, 0, vi.mTextColumns);
	caretPos2.y = std::clamp<sint32>(caretPos2.y, 0, vi.mTextRows);

	mDragPreviewSpans.clear();

	if (caretPos1.y == caretPos2.y) {
		if (caretPos1.x == caretPos2.x) {
			UpdateDragPreviewRects();
			return;
		}

		if (caretPos1.x > caretPos2.x)
			std::swap(caretPos1, caretPos2);
	} else if (caretPos1.y > caretPos2.y)
		std::swap(caretPos1, caretPos2);

	for(int cy = caretPos1.y; cy <= caretPos2.y; ++cy) {
		TextSpan& ts = mDragPreviewSpans.push_back();

		ts.mX = (cy == caretPos1.y) ? caretPos1.x : 0;
		ts.mY = cy;
		ts.mWidth = ((cy == caretPos2.y) ? caretPos2.x : vi.mTextColumns) - ts.mX;
		ts.mHeight = 1;
		ts.mCharX = ts.mX;
		ts.mCharWidth = ts.mWidth;
	}

	UpdateDragPreviewRects();
}

void ATUIVideoDisplayWindow::UpdateDragPreviewAntic(int x, int y) {
	int xc2, yc2;

	if (!MapPixelToBeamPosition(x, y, xc2, yc2, true)) {
		ClearDragPreview();
		return;
	}

	int xc1 = mDragAnchorX;
	int yc1 = mDragAnchorY;

	if (mbDragInitial) {
		if (xc1 == xc2 && yc1 == yc2)
			return;

		mbDragInitial = false;
	}

	yc1 = GetModeLineYPos(yc1, false);
	yc2 = GetModeLineYPos(yc2, false);

	if ((yc1 | yc2) < 0) {
		ClearDragPreview();
		return;
	}

	SelectByBeamPositionAntic(xc1, yc1, xc2, yc2);
}

void ATUIVideoDisplayWindow::SelectByBeamPositionAntic(int xc1, int yc1, int xc2, int yc2) {
	xc1 = std::clamp<int>(xc1, 0, 228);
	xc2 = std::clamp<int>(xc2, 0, 228);

	yc1 = std::clamp<int>(yc1, 8, 248);
	yc2 = std::clamp<int>(yc2, 8, 248);

	if (yc1 > yc2 || (yc1 == yc2 && xc1 > xc2)) {
		std::swap(xc1, xc2);
		std::swap(yc1, yc2);
	}

	ATAnticEmulator& antic = g_sim.GetAntic();
	const ATAnticEmulator::DLHistoryEntry *dlhist = antic.GetDLHistory();

	ATGTIAEmulator& gtia = g_sim.GetGTIA();
	const vdrect32 scanArea(gtia.GetFrameScanArea());

	mDragPreviewSpans.clear();

	for(int yc = yc1; yc <= yc2; ++yc) {
		if (!dlhist[yc].mbValid)
			continue;

		bool textModeLine = false;
		switch(dlhist[yc].mControl & 15) {
			case 2:
			case 3:
			case 6:
			case 7:
				textModeLine = true;
				break;
		}

		if (!textModeLine)
			continue;

		int pfwidth = dlhist[yc].mDMACTL & 3;
		if (!pfwidth)
			continue;

		if (pfwidth < 3 && (dlhist[yc].mControl & 0x10))
			++pfwidth;

		int left = (yc == yc1) ? xc1 : scanArea.left;
		int right = (yc == yc2) ? xc2 : scanArea.right;

		const int leftborder = 0x50 - 0x10*pfwidth;
		left = std::max<int>(left, leftborder);
		right = std::min<int>(right, 0xB0 + 0x10*pfwidth);

		bool dblwide = false;
		switch(dlhist[yc].mControl & 15) {
			case 2:
			case 3:
				left = (left + 2) & ~3;
				right = (right + 2) & ~3;
				break;

			case 6:
			case 7:
				left  = (left + 4) & ~7;
				right = (right + 4) & ~7;
				dblwide = true;
				break;
		}

		if (left >= right)
			continue;

		TextSpan& ts = mDragPreviewSpans.push_back();
		ts.mX = left;
		ts.mWidth = right - left;
		ts.mY = yc;
		ts.mHeight = 0;

		if (dblwide) {
			ts.mCharX = (left - leftborder) >> 3;
			ts.mCharWidth = (right - left) >> 3;
		} else {
			ts.mCharX = (left - leftborder) >> 2;
			ts.mCharWidth = (right - left) >> 2;
		}

		for(int i=0; i<16; ++i) {
			++ts.mHeight;

			if (yc + ts.mHeight >= 248 || dlhist[yc + ts.mHeight].mbValid)
				break;
		}
	}

	UpdateDragPreviewRects();
}

void ATUIVideoDisplayWindow::UpdateDragPreviewRects() {
	ClearHighlights();

	if (mDragPreviewSpans.empty())
		return;

	if (mpAltVideoOutput && !mpAltVideoOutput->GetVideoInfo().mbSignalPassThrough) {
		const vdrect32& drawArea = GetAltDisplayArea();
		const auto& vi = mpAltVideoOutput->GetVideoInfo();
		const vdrect32& dispArea = vi.mDisplayArea;
		
		if (!dispArea.empty()) {
			const float scaleX = (float)drawArea.width() / (float)dispArea.width();
			const float scaleY = (float)drawArea.height() / (float)dispArea.height();

			TextSpans::const_iterator it(mDragPreviewSpans.begin()), itEnd(mDragPreviewSpans.end());	
			for(; it != itEnd; ++it) {
				const TextSpan& ts = *it;
				const vdrect32& spanArea = mpAltVideoOutput->CharToPixelRect(vdrect32(ts.mX, ts.mY, ts.mX + ts.mWidth, ts.mY + 1));

				const int x1 = VDRoundToInt(spanArea.left   * scaleX) + drawArea.left;
				const int y1 = VDRoundToInt(spanArea.top    * scaleY) + drawArea.top;
				const int x2 = VDRoundToInt(spanArea.right  * scaleX) + drawArea.left;
				const int y2 = VDRoundToInt(spanArea.bottom * scaleY) + drawArea.top;

				mHighlightPoints.push_back(HighlightPoint { x1, y1, false });
				mHighlightPoints.push_back(HighlightPoint { x1, y2, false });
				mHighlightPoints.push_back(HighlightPoint { x2, y1, false });
				mHighlightPoints.push_back(HighlightPoint { x2, y2, true });
			}
		}
	} else {
		// Due to distortion effects our rect may not stay as one when mapped from beam position to screen.
		// Worse yet, the resulting polygon may not even be convex. We can, however, expect that a horizontal
		// strip will mostly stay as such, so we can make do by checking for excessive curvature on the top and
		// bottom edges and subdivide the strip as necessary, relying on the rendering code using a horizontal
		// tri-strip.
		vdfastvector<float> ustack;

		for(const TextSpan& ts : mDragPreviewSpans) {
			// discard empty spans just in case, as these will cause tesselation problems
			if (!ts.mWidth || !ts.mHeight)
				continue;

			int u1 = (float)ts.mX;
			int u2 = u1 + (float)ts.mWidth;
			const int v1 = (float)ts.mY;
			const int v2 = v1 + (float)ts.mHeight;

			for (int u = u1; u <= u2; ) {
				int x1, y1, x2, y2;
				MapBeamPositionToPoint(u, v1, x1, y1);
				MapBeamPositionToPoint(u, v2, x2, y2);

				mHighlightPoints.push_back(HighlightPoint { x1, y1, false });
				mHighlightPoints.push_back(HighlightPoint { x2, y2, false });

				if (u == u2)
					break;

				if (mpMapSourceToDisplayPt) {
					// While adaptive tesselation would be more precise and efficient, it runs into
					// problems with inconsistent tesselation between rows causing gaps or overlaps.
					// To avoid this, we uniformly tesselate on a common grid used for all highlight
					// rects.

					u = (u + 16) & ~15;
					if (u > u2)
						u = u2;
				} else {
					u = u2;
				}
			}

			mHighlightPoints.back().mbClose = true;
		}
	}

	Invalidate();
}

void ATUIVideoDisplayWindow::ClearDragPreview() {
	mDragPreviewSpans.clear();

	ClearHighlights();
}

int ATUIVideoDisplayWindow::GetModeLineYPos(int ys, bool checkValidCopyText) const {
	return GetModeLineXYPos(0, ys, checkValidCopyText).second;
}

std::pair<int, int> ATUIVideoDisplayWindow::GetModeLineXYPos(int xcc, int ys, bool checkValidCopyText) const {
	const ModeLineInfo& mli = GetModeLineInfo(ys);

	if (!checkValidCopyText || mli.IsTextMode()) {
		return {
			(((xcc - mli.mFetchHposStart)*2) >> mli.mCellToHRPixelShift),
			mli.mVPos
		};
	}

	return {-1, -1};
}

ATUIVideoDisplayWindow::ModeLineInfo ATUIVideoDisplayWindow::GetModeLineInfo(int vpos) const {
	ATAnticEmulator& antic = g_sim.GetAntic();
	const ATAnticEmulator::DLHistoryEntry *dlhist = antic.GetDLHistory();

	ModeLineInfo modeInfo {};

	if (vpos >= 248)
		vpos = 247;

	for(; vpos >= 8; --vpos) {
		const ATAnticEmulator::DLHistoryEntry& modeLine = dlhist[vpos];

		if (modeLine.mbValid) {
			const uint8 pfWidth = modeLine.mDMACTL & 3;
			const uint8 mode = modeLine.mControl & 15;

			static constexpr uint8 kModeHeight[16] {
				1, 1, 8, 10, 8, 16, 8, 16, 8, 4, 4, 2, 1, 2, 1, 1
			};

			const uint8 pfFetchWidth = (modeLine.mControl & 0x10) && pfWidth < 3 ? pfWidth + 1 : pfWidth;
			static constexpr uint8 kPFStart[4] = { 0x40, 0x40, 0x30, 0x20 };
			static constexpr uint8 kPFEnd  [4] = { 0x40, 0xC0, 0xD0, 0xE0 };

			int hscroll = modeLine.mHVScroll & 15;

			static constexpr uint8 kCellToHRPixelShift[16] {
				0, 0, 3, 3, 3, 3, 4, 4, 3, 2, 2, 1, 1, 1, 1, 0
			};

			static constexpr uint8 kByteToHRPixelShift[16] {
				0, 0, 3, 3, 3, 3, 4, 4, 5, 5, 4, 4, 4, 3, 3, 3
			};

			modeInfo.mMode = mode;
			modeInfo.mVPos = vpos;
			modeInfo.mHeight = kModeHeight[mode];
			modeInfo.mHScroll = hscroll;
			modeInfo.mDisplayHposStart = kPFStart[pfWidth];
			modeInfo.mDisplayHposEnd = kPFEnd[pfWidth];
			modeInfo.mFetchHposStart = kPFStart[pfFetchWidth] + hscroll;
			modeInfo.mFetchHposEnd = kPFEnd[pfFetchWidth] + hscroll;
			modeInfo.mCellToHRPixelShift = kCellToHRPixelShift[mode];
			modeInfo.mCellWidth = mode >= 2 ? ((modeInfo.mFetchHposEnd - modeInfo.mFetchHposStart)*2) >> modeInfo.mCellToHRPixelShift : 0;
			modeInfo.mByteToHRPixelShift = kByteToHRPixelShift[mode];

			break;
		}
	}

	return modeInfo;
}

/// Read characters from a text mode line into a buffer; returns the number
/// of characters read, or zero if an error occurs (range out of bounds, no
/// mode line, not a supported text mode line). The returned buffer is _not_
/// null terminated.
///
int ATUIVideoDisplayWindow::ReadText(uint8 *dst, uint8 *raw, int yc, int startChar, int numChars, bool& intl) const {
	ATAnticEmulator& antic = g_sim.GetAntic();
	const ATAnticEmulator::DLHistoryEntry *dlhist = antic.GetDLHistory();
	const ATAnticEmulator::DLHistoryEntry& dle = dlhist[yc];

	intl = false;

	// check that mode line is valid
	if (!dle.mbValid)
		return 0;

	// check mode
	switch(dle.mControl & 15) {
		case 2:
		case 3:
		case 6:
		case 7:
			break;

		default:
			return 0;
	}

	// compute width
	static const int kWidthLookup[2][4] = {
		{ 0, 16, 20, 24 },	// no horizontal scrolling
		{ 0, 20, 24, 24 },	// horizontal scrolling
	};

	int len = (dle.mControl & 4 ? 1 : 2) * kWidthLookup[(dle.mControl & 0x10) != 0][dle.mDMACTL & 3];

	// clip
	if (numChars <= 0 || startChar >= len)
		return 0;

	if (startChar < 0) {
		numChars += startChar;
		startChar = 0;
	}

	if (numChars > len - startChar)
		numChars = len - startChar;

	if (numChars <= 0)
		return 0;

	// read out raw bytes
	uint8 data[48];
	g_sim.GetMemoryManager()->DebugAnticReadMemory(data, dle.mPFAddress + startChar, len);

	static const uint8 kInternalToATASCIIXorTab[4]={
		0x20, 0x60, 0x40, 0x00
	};

	uint8 mask = (dle.mControl & 4) ? 0x3f : 0xff;
	uint8 xorval = (dle.mControl & 4) && (dle.mCHBASE & 1) ? 0x40 : 0x00;

	if (!(dle.mControl & 4) && dle.mCHBASE == (0xCC >> 1))
		intl = true;

	for(int i=0; i<numChars; ++i) {
		uint8 c = data[i];

		// convert INTERNAL char to ATASCII
		c &= mask;
		c ^= xorval;

		c ^= kInternalToATASCIIXorTab[(c & 0x60) >> 5];

		*dst++ = c;
	}

	return numChars;
}

void ATUIVideoDisplayWindow::ReadEditorLine(EditorLine& line) const {
	const uint8 mode = g_sim.DebugReadByte(ATKernelSymbols::DINDEX);
	const bool textWindowActive = g_sim.DebugReadByte(ATKernelSymbols::SWPFLG);

	line.mText.clear();

	uint8 row = 0;
	uint8 col = 0;

	if (!mode || textWindowActive) {
		const bool textIsCurrent = !mode || textWindowActive;

		row = g_sim.DebugReadByte(textIsCurrent ? (uint32)ATKernelSymbols::ROWCRS : (uint32)ATKernelSymbols::TXTROW);
		col = g_sim.DebugReadByte(textIsCurrent ? (uint32)ATKernelSymbols::COLCRS : (uint32)ATKernelSymbols::TXTCOL);
		const uint8 lmar = g_sim.DebugReadByte(ATKernelSymbols::LMARGN);
		const uint8 rmar = g_sim.DebugReadByte(ATKernelSymbols::RMARGN);
		const uint16 addr = g_sim.DebugReadWord(textIsCurrent ? (uint32)ATKernelSymbols::SAVMSC : (uint32)ATKernelSymbols::TXTMSC);

		if (lmar <= rmar && rmar <= 39 && col >= lmar && col <= rmar && row < (mode ? 4 : 24)) {
			// read previous line, if there is one
			if (row >= 1) {
				const size_t prevLineLen = rmar + 1 - lmar;
				const uint16 prevAddr = addr + (row - 1) * 40 + lmar;

				line.mText.resize(prevLineLen);

				for(size_t i = 0; i < prevLineLen; ++i)
					line.mText[i] = g_sim.DebugReadByte((uint16)(prevAddr + lmar + i));
			}

			// read current line up to cursor
			const size_t currentLineLen = col - lmar;
			if (currentLineLen) {
				const uint16 curAddr = addr + row * 40;

				line.mText.resize(line.mText.size() + currentLineLen);

				uint8 *dst = &*(line.mText.end() - currentLineLen);

				for(size_t i = lmar; i < col; ++i)
					dst[i - lmar] = g_sim.DebugReadByte((uint16)(curAddr + i));
			}
		}
	}

	line.mCursorCol = col;
	line.mCursorRow = row;
}

void ATUIVideoDisplayWindow::ClearCoordinateIndicator() {
	if (mbCoordIndicatorActive) {
		mbCoordIndicatorActive = false;

		IATUIRenderer *uir = g_sim.GetUIRenderer();
		if (uir)
			uir->SetHoverTip(0, 0, NULL);
	}
}

void ATUIVideoDisplayWindow::SetCoordinateIndicator(int x, int y) {
	int hcyc, vcyc;

	if (!MapPixelToBeamPosition(x, y, hcyc, vcyc, false)) {
		ClearCoordinateIndicator();
		return;
	}

	mbCoordIndicatorActive = true;

	IATUIRenderer *uir = g_sim.GetUIRenderer();
	if (uir) {
		VDStringW s;

		s.sprintf(L"<b>Pos:</b> (%u,%u) [frame %u]\n", hcyc, vcyc, g_sim.GetAntic().GetRawFrameCounter());

		ATAnticEmulator& antic = g_sim.GetAntic();
		const ATAnticEmulator::DLHistoryEntry *dlhist = antic.GetDLHistory();

		bool dlvalid = false;
		if (vcyc >= 8 && vcyc < 248) {
			int y = vcyc;

			while(y > 8 && !dlhist[y].mbValid)
				--y;

			const ATAnticEmulator::DLHistoryEntry& dlent = dlhist[y];

			if (dlent.mbValid) {
				uint8 mode = dlent.mControl & 15;
				uint8 special = dlent.mControl & 0xf0;

				dlvalid = true;
				s.append_sprintf(L"<b>DL[$%04X]:</b> ", dlent.mDLAddress);

				if (mode >= 2) {
					const wchar_t *const kPFWidths[]={ L"Disabled", L"Narrow", L"Normal", L"Wide" };

					s.append_sprintf(L"Mode %X %ls @ $%04X", mode, kPFWidths[dlent.mDMACTL & 3], dlent.mPFAddress);
				} else if (mode == 1) {
					if (dlent.mControl & 0x40)
						s.append(L"JVB");
					else
						s.append(L"Jump");

					special &= 0xb0;
				} else {
					s.append_sprintf(L"Blank x%u", ((dlent.mControl >> 4) & 7) + 1);
					special &= 0x80;
				}

				if (special) {
					s.append(L" (");

					if (special & 0x80)
						s.append(L"DLI, ");

					if (special & 0x40)
						s.append(L"LMS, ");

					if (special & 0x20)
						s.append_sprintf(L"VSCR=%u, ", dlent.mHVScroll >> 4);

					if (special & 0x10)
						s.append_sprintf(L"HSCR=%u, ", dlent.mHVScroll & 15);

					s.pop_back();
					s.pop_back();

					s.append(L")");
				}

				const auto& modeLineInfo = GetModeLineInfo(y);

				if (hcyc >= modeLineInfo.mDisplayHposStart && hcyc < modeLineInfo.mDisplayHposEnd) {
					const uint32 hrPixel = ((hcyc - modeLineInfo.mFetchHposStart)*2);
					const uint32 cellOffset = hrPixel >> modeLineInfo.mCellToHRPixelShift;
					const uint32 byteOffset = hrPixel >> modeLineInfo.mByteToHRPixelShift;
					const uint32 addr = (dlent.mPFAddress & 0xF000) + ((dlent.mPFAddress + byteOffset) & 0xFFF);
					
					s.append_sprintf(L"\n<b>%ls</b>: X=%u @ ($%04X) = $%02X"
						, modeLineInfo.mMode < 8 ? L"Cell" : L"Pixel"
						, cellOffset
						, addr
						, g_sim.DebugAnticReadByte(addr)
					);
				}
			}
		}

		if (!dlvalid)
			s.append(L"<b>DL:</b> None");

		// process XDL if VBXE is enabled
		ATVBXEEmulator *vbxe = g_sim.GetGTIA().GetVBXE();
		if (vbxe) {
			const ATVBXEXDLHistoryEntry *xdlhe = vbxe->GetXDLHistory(vcyc);

			if (xdlhe && xdlhe->mbXdlActive) {
				s.append_sprintf(L"\n\n<b>VBXE XDL:</b> $%05X", xdlhe->mXdlAddr);

				if (xdlhe->mOverlayMode != (uint8)ATVBXEOverlayMode::Disabled) {
					const uint8 ovWidth = xdlhe->mAttrByte & 3;

					s.append_sprintf(L"\n<b>VBXE Overlay:</b> %ls %ls @ $%05X.+$%03X"
						, ovWidth == 1 ? L"Normal" : ovWidth == 2 ? L"Wide" : L"Narrow"
						,	  xdlhe->mOverlayMode == (uint8)ATVBXEOverlayMode::LR ? L"LR"
							: xdlhe->mOverlayMode == (uint8)ATVBXEOverlayMode::SR ? L"SR"
							: xdlhe->mOverlayMode == (uint8)ATVBXEOverlayMode::HR ? L"HR"
							: L"Text"
						, xdlhe->mOverlayAddr
						, xdlhe->mOverlayStep
					);

					if (xdlhe->mOverlayMode == (uint8)ATVBXEOverlayMode::Text) {
						s.append_sprintf(L" [font @ $%05X]", (uint32)xdlhe->mChBase << 11);
					}
				}

				if (xdlhe->mbMapEnabled) {
					s.append_sprintf(L"\n<b>VBXE Attribute Map:</b> %ux%u fields @ $%05X.+$%03X"
						, xdlhe->mMapFieldWidth + 1
						, xdlhe->mMapFieldHeight + 1
						, xdlhe->mMapAddr
						, xdlhe->mMapStep
					);

					if (xdlhe->mMapHScroll || xdlhe->mMapVScroll)
						s.append_sprintf(L" [scroll %u,%u]", xdlhe->mMapHScroll, xdlhe->mMapVScroll);
				}
			}
		}

		uir->SetHoverTip(x, y, s.c_str());
	}
}

void ATUIVideoDisplayWindow::ClearHoverTip() {
	if (mbHoverTipActive) {
		mbHoverTipActive = false;
		g_sim.GetUIRenderer()->SetHoverTip(0, 0, NULL);
	}
}

sint32 ATUIVideoDisplayWindow::FindDropTargetOverlay(sint32 x, sint32 y) const {
	for (int i=0; i<(int)vdcountof(mpDropTargetOverlays); ++i) {
		ATUIWidget *w = mpDropTargetOverlays[i];

		if (w && w->GetArea().contains(vdpoint32(x, y)))
			return i;
	}

	return -1;
}

void ATUIVideoDisplayWindow::HighlightDropTargetOverlay(int index) {
	for (int i=0; i<(int)vdcountof(mpDropTargetOverlays); ++i) {
		ATUILabel *label = static_cast<ATUILabel *>(mpDropTargetOverlays[i].get());

		if (label) {
			const uint32 c = (i == index) ? 0xF0A0A0A0 : 0xF0606060;

			label->SetAlphaFillColor(c);
		}
	}
}

void ATUIVideoDisplayWindow::CreateDropTargetOverlays() {
	if (mpDropTargetOverlays[0])
		return;

	static_assert(vdcountof(kDropFileTargets) == vdcountof(mpDropTargetOverlays));

	for(int i=0; i<(int)vdcountof(kDropFileTargets); ++i) {
		const ATUIDropFilesTarget target = kDropFileTargets[i];

		if (target == ATUIDropFilesTarget::PasteFilePath) {
			// check if there are any devices that support this
			bool haveMapping = false;

			for(IATDeviceNativePathMapping *iface : g_sim.GetDeviceManager()->GetInterfaces<IATDeviceNativePathMapping>(false, false, false)) {
				if (iface->CanSupportPathMapping()) {
					haveMapping = true;
					break;
				}
			}

			if (!haveMapping)
				continue;
		}

		vdrefptr<ATUILabel> label { new ATUILabel };
		label->SetTextColor(0xFF000000);
		label->SetTextAlign(ATUILabel::kAlignCenter);
		label->SetTextVAlign(ATUILabel::kVAlignMiddle);

		switch(target) {
			case ATUIDropFilesTarget::MountCart:
				label->SetText(L"Mount cartridge");
				break;

			case ATUIDropFilesTarget::MountDisk1:
			case ATUIDropFilesTarget::MountDisk2:
			case ATUIDropFilesTarget::MountDisk3:
			case ATUIDropFilesTarget::MountDisk4:
				label->SetTextF(L"Mount disk D%u:", (unsigned)target - (unsigned)ATUIDropFilesTarget::MountDisk1 + 1);
				break;

			case ATUIDropFilesTarget::MountImage:
				label->SetText(L"Mount image");
				break;

			case ATUIDropFilesTarget::BootImage:
				label->SetText(L"Boot image");
				break;

			case ATUIDropFilesTarget::PasteFilePath:
				label->SetText(L"Paste file path");
				break;
		}

		vdrect32f r;

		constexpr int kNumSideOptions = 7;

		if (i == kNumSideOptions) {
			r.left = 0.0f;
			r.right = 0.74f;
			r.top = 0.0f;
			r.bottom = 1.0f;
		} else {
			r.left = 0.75f;
			r.right = 1.0f;

			const float proportionalSpacing = 0.01f;
			const float proportionalHeight = (1.0f - proportionalSpacing * (float)(kNumSideOptions - 1)) / (float)kNumSideOptions;
			r.bottom = 1.0f - (proportionalHeight + proportionalSpacing) * i;
			r.top = r.bottom - proportionalHeight;
		}

		vdrefptr<IATUIAnchor> anchor; 
		ATUICreateProportionAnchor(r, ~anchor);
		label->SetAnchor(anchor);
		
		GetManager()->GetMainWindow()->AddChild(label);
		label->SetFont(GetManager()->GetThemeFont(kATUIThemeFont_Header));

		mpDropTargetOverlays[i] = label;
	}

	// force layout update since we'll be directly rect testing
	GetManager()->GetMainWindow()->UpdateLayout();

	HighlightDropTargetOverlay(-1);
}

void ATUIVideoDisplayWindow::DestroyDropTargetOverlays() {
	for(vdrefptr<ATUIWidget>& w : mpDropTargetOverlays) {
		if (w) {
			w->GetParent()->RemoveChild(w);
			w = nullptr;
		}
	}
}

void ATUIVideoDisplayWindow::UpdateEnhTextSize() {
	if (mpEnhTextEngine) {
		auto *vo = mpEnhTextEngine->GetVideoOutput();

		const auto videoInfoPrev = vo->GetVideoInfo();

		const auto csz = GetClientArea().size();
		mpEnhTextEngine->OnSize(csz.w, csz.h);

		const auto videoInfoNext = vo->GetVideoInfo();

		if (videoInfoPrev.mTextRows != videoInfoNext.mTextRows || videoInfoPrev.mTextColumns != videoInfoNext.mTextColumns) {
			if (mbShowEnhSizeIndicator) {
				if (!mpUILabelEnhTextSize) {
					mpUILabelEnhTextSize = new ATUILabel;
					mpUILabelEnhTextSize->AddRef();

					mpUILabelEnhTextSize->SetFont(mpManager->GetThemeFont(kATUIThemeFont_Default));
					mpUILabelEnhTextSize->SetTextOffset(8, 8);
					mpUILabelEnhTextSize->SetTextColor(0xFFFFFF);
					mpUILabelEnhTextSize->SetBorderColor(0xFFFFFF);
					mpUILabelEnhTextSize->SetFillColor(0x404040);
					mpUILabelEnhTextSize->SetPlacement(vdrect32f(0.5f, 0.5f, 0.5f, 0.5f), vdpoint32(0, 0), vdfloat2{0.5f, 0.5f});

					AddChild(mpUILabelEnhTextSize);
				}

				mpUILabelEnhTextSize->SetTextF(L"%ux%u", videoInfoNext.mTextColumns, videoInfoNext.mTextRows);
			}
		}
	}
}

void ATUIVideoDisplayWindow::UpdateAutoSuggest() {
	if (mShowSuggestionsCounter && ++mShowSuggestionsCounter >= 100) {
		mShowSuggestionsCounter = 0;
	}

	if (!mbAutoSuggestEnabled && !mShowSuggestionsCounter)
		return;

	if (!mpAutoSuggestEngine)
		mpAutoSuggestEngine = ATCreateAutoSuggestEngine();

	if (mShowSuggestionsCounter) {
		std::swap(mSuggestScanText1, mSuggestScanText2);
	}

	std::swap(mSuggestScanText2, mSuggestScanText3);

	ReadEditorLine(mSuggestScanText3);

	if (mSuggestScanText2 == mSuggestScanText3 && (mShowSuggestionsCounter ? mSuggestScanText1 == mSuggestScanText2 : mSuggestScanText1 != mSuggestScanText2)) {
		mShowSuggestionsCounter = 0;

		std::swap(mSuggestScanText1, mSuggestScanText2);

		VDStringA text;

		for(uint8 c : mSuggestScanText1.mText) {
			c = ATConvertInternalToATASCII(c);

			if (c >= 0x20 && c < 0x7F)
				text.push_back((char)c);
			else
				text.push_back(' ');
		}

		mpAutoSuggestEngine->Update(text);

		const size_t n = mpAutoSuggestEngine->GetNumResults();
		if (n) {
			if (!mpAutoSuggestPopup) {
				mpAutoSuggestPopup = new ATUIAutoSuggestPopup;
				AddChild(mpAutoSuggestPopup);

				mpAutoSuggestPopup->SetOnEntrySelected(
					[this](sint32 index) {
						if (index >= 0 && mpAutoSuggestEngine && (uint32)index < mpAutoSuggestEngine->GetNumResults()) {
							const auto& result = mpAutoSuggestEngine->GetResult(index);

							if (!result.mInsertionText.empty()) {
								ATUIPaste(result.mInsertionText);
								CloseAutoSuggest();
							}
						}
					}
				);

				mpAutoSuggestPopup->SetOnSelectionChanged(
					[this](sint32 index) {
						if (index >= 0) {
							if (!mbAutoSuggestAcceptMapped) {
								mbAutoSuggestAcceptMapped = true;

								BindAction(kATUIVK_Return, ATUIAutoSuggestPopup::kActionAccept, 0, mpAutoSuggestPopup->GetInstanceId());
							}
						} else {
							if (mbAutoSuggestAcceptMapped) {
								mbAutoSuggestAcceptMapped = false;

								UnbindAction(kATUIVK_Return, 0);
							}
						}
					}
				);

				BindAction(kATUIVK_Up, ATUIAutoSuggestPopup::kActionMoveUp, 0, mpAutoSuggestPopup->GetInstanceId());
				BindAction(kATUIVK_Down, ATUIAutoSuggestPopup::kActionMoveDown, 0, mpAutoSuggestPopup->GetInstanceId());
				BindAction(kATUIVK_UIUp, ATUIAutoSuggestPopup::kActionMoveUp, 0, mpAutoSuggestPopup->GetInstanceId());
				BindAction(kATUIVK_UIDown, ATUIAutoSuggestPopup::kActionMoveDown, 0, mpAutoSuggestPopup->GetInstanceId());
				BindAction(kATUIVK_Prior, ATUIAutoSuggestPopup::kActionMovePageUp, 0, mpAutoSuggestPopup->GetInstanceId());
				BindAction(kATUIVK_Next, ATUIAutoSuggestPopup::kActionMovePageDown, 0, mpAutoSuggestPopup->GetInstanceId());
				BindAction(kATUIVK_Escape, kActionCloseAutoSuggest);
			}

			mpAutoSuggestPopup->UpdateSuggestions(*mpAutoSuggestEngine);
			
			// By default, we want the autosuggest widget to align left/top against bottom/left of the cursor. But if there isn't
			// enough room on the bottom and more room on the top, flip this around so it appears above.
			const sint32 cursorBeamX1 = 48 + 4 * mSuggestScanText3.mCursorCol;
			const sint32 cursorBeamY1 = 32 + 8 * mSuggestScanText3.mCursorRow;
			const sint32 cursorBeamY2 = cursorBeamY1 + 8;

			vdint2 posCursorBL = MapBeamPositionToPoint(cursorBeamX1, cursorBeamY2);
			vdint2 posCursorTL = MapBeamPositionToPoint(cursorBeamX1, cursorBeamY1);
			vdrect32 viewportArea = GetClientArea();

			posCursorBL.y = std::clamp<sint32>(posCursorBL.y, 0, viewportArea.bottom);
			posCursorTL.y = std::clamp<sint32>(posCursorTL.y, 0, viewportArea.bottom);
			
			const vdsize32 idealSize = mpAutoSuggestPopup->GetIdealSize();

			const sint32 spaceAbove = posCursorTL.y;
			const sint32 spaceBelow = viewportArea.bottom - posCursorBL.y;

			const sint32 desiredWidth = std::min<sint32>(idealSize.w, viewportArea.right);
			const sint32 desiredHeight = std::min<sint32>(idealSize.h, viewportArea.bottom);

			vdrect32 r;
			r.left = std::min<sint32>(viewportArea.right - desiredWidth, posCursorBL.x);
			r.right = r.left + desiredWidth;

			if (spaceBelow < desiredHeight && spaceAbove > spaceBelow) {
				r.top = std::max<sint32>(0, posCursorTL.y - desiredHeight);
				r.bottom = posCursorTL.y;
			} else {
				r.top = posCursorBL.y;
				r.bottom = std::min<sint32>(posCursorBL.y + desiredHeight, viewportArea.bottom);
			}

			mpAutoSuggestPopup->SetArea(r);
		} else {
			CloseAutoSuggest();
		}
	}
}
