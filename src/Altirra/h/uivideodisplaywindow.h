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

#ifndef f_AT_UIVIDEODISPLAY_H
#define f_AT_UIVIDEODISPLAY_H

#include <at/atui/uiwidget.h>
#include <at/atui/uicontainer.h>
#include "callback.h"
#include "simeventmanager.h"
#include "uienhancedtext.h"
#include "uikeyboard.h"

class IATUIEnhancedTextEngine;
class ATUILabel;
class ATUIOnScreenKeyboard;
class ATUISettingsWindow;
class ATSimulatorEventManager;
class IATDeviceVideoOutput;
class IATDeviceVideoManager;
class IATVideoWriter;
class IATAutoSuggestEngine;

class ATUIDisplayTool;
class ATUIAutoSuggestPopup;
struct ATUIDisplayAccessibilityScreen;

enum class ATTextCopyMode : uint8;

class ATUIVideoDisplayWindow final : public ATUIContainer, public IATUIEnhancedTextOutput {
public:
	enum {
		kActionOpenOSK = kActionCustom,
		kActionCloseOSK,
		kActionOpenSidePanel,
		kActionCloseAutoSuggest
	};

	ATUIVideoDisplayWindow();
	~ATUIVideoDisplayWindow();

	bool Init(ATSimulatorEventManager& sem, IATDeviceVideoManager& videoMgr);
	void Shutdown();

	void SelectAll();
	void Deselect();
	void Copy(ATTextCopyMode copyMode);
	bool CopyFrameImage(bool trueAspect, VDPixmapBuffer& buf);
	void CopySaveFrame(bool saveFrame, bool trueAspect, const wchar_t *path = nullptr);

	void ToggleHoldKeys();

	void ToggleCaptureMouse();
	void ReleaseMouse();
	void CaptureMouse();

	void RecalibrateLightPen();
	void ActivatePanZoomTool();

	void OpenOSK();
	void CloseOSK();

	void SetAutoSuggestEnabled(bool enabled);
	void ShowSuggestions();
	void CloseAutoSuggest();

	void OpenSidePanel();
	void CloseSidePanel();

	void BeginEnhTextSizeIndicator();
	void EndEnhTextSizeIndicator();

	bool IsTextSelected() const { return !mDragPreviewSpans.empty(); }

	vdrect32 GetOSKSafeArea() const;

	void SetDisplaySourceMapping(vdfunction<bool(vdfloat2&)> dispToSrcFn, vdfunction<bool(vdfloat2&)> srcToDispFn);
	void SetDisplayRect(const vdrect32& r);

	void ClearHighlights();

	void SetEnhancedTextEngine(IATUIEnhancedTextEngine *p);

	void SetOnAllowContextMenu(const vdfunction<void()>& fn) { mpOnAllowContextMenu = fn; }
	void SetOnDisplayContextMenu(const vdfunction<void(const vdpoint32&)>& fn) { mpOnDisplayContextMenu = fn; }
	void SetOnOSKChange(const vdfunction<void()>& fn) { mpOnOSKChange = fn; }

	void AddTool(ATUIDisplayTool& tool);
	void RemoveTool(ATUIDisplayTool& tool);

	void ReadScreen(ATUIDisplayAccessibilityScreen& screenInfo) const;
	vdrect32 GetTextSpanBoundingBox(int ypos, int xc1, int xc2) const;
	vdpoint32 GetNearestBeamPositionForPoint(const vdpoint32& pt) const;

	void SetVideoWriter(IATVideoWriter *writer);

public:		// IATUIEnhancedTextOutput
	void InvalidateTextOutput() override;

private:
	void OnReset();
	void OnFrameTick();

public:
	void OnAddedVideoOutput(uint32 index);
	void OnRemovingVideoOutput(uint32 index);

protected:
	ATUITouchMode GetTouchModeAtPoint(const vdpoint32& pt) const override;
	void OnMouseDown(sint32 x, sint32 y, uint32 vk, bool dblclk) override;
	void OnMouseUp(sint32 x, sint32 y, uint32 vk) override;
	void OnMouseRelativeMove(sint32 dx, sint32 dy) override;
	void OnMouseMove(sint32 x, sint32 y) override;
	bool OnMouseWheel(sint32 x, sint32 y, float delta, bool doPages) override;
	bool OnMouseHWheel(sint32 x, sint32 y, float delta, bool doPages) override;
	void OnMouseLeave() override;
	void OnMouseHover(sint32 x, sint32 y) override;

	bool OnContextMenu(const vdpoint32 *pt) override;

	bool OnKeyDown(const ATUIKeyEvent& event) override;
	bool OnKeyUp(const ATUIKeyEvent& event) override;
	bool OnChar(const ATUICharEvent& event) override;
	bool OnCharUp(const ATUICharEvent& event) override;

	void OnForceKeysUp() override;

	void OnActionStart(uint32 id) override;
	void OnActionStop(uint32 id) override;

	void OnCreate() override;
	void OnDestroy() override;
	void OnSize() override;

	void OnSetFocus() override;
	void OnKillFocus() override;
	void OnCaptureLost() override;
	
	void OnDeactivate() override;

	ATUIDragEffect OnDragEnter(sint32 x, sint32 y, ATUIDragModifiers modifiers, IATUIDragDropObject *obj) override;
	ATUIDragEffect OnDragOver(sint32 x, sint32 y, ATUIDragModifiers modifiers, IATUIDragDropObject *obj) override;
	void OnDragLeave() override;
	ATUIDragEffect OnDragDrop(sint32 x, sint32 y, ATUIDragModifiers modifiers, IATUIDragDropObject *obj) override;

	void Paint(IVDDisplayRenderer& rdr, sint32 w, sint32 h) override;

public:
	void UpdateEnhTextSize();
	void UpdateAltDisplay();
	void SelectPrevOutput();
	void SelectNextOutput();
	void SelectAltOutputByIndex(sint32 idx);
	sint32 GetCurrentAltOutputIndex() const;
	bool IsAltOutputAvailable() const;
	bool IsAltOutputAvailable(const char *name) const;
	VDStringW GetCurrentOutputName() const;
	vdrect32 GetCurrentOutputArea() const;

private:
	bool ProcessKeyDown(const ATUIKeyEvent& event, bool enableKeyInput);
	bool ProcessKeyUp(const ATUIKeyEvent& event, bool enableKeyInput);
	void ProcessVirtKey(uint32 vkey, uint32 scancode, uint32 keycode, bool repeat);
	void ProcessSpecialKey(uint32 scanCode, bool state);
	void ToggleHeldKey(uint8 keycode);
	void ToggleHeldConsoleButton(uint8 encoding);
	void UpdateCtrlShiftState();
	void ExcludeMappedCtrlShiftState(bool& ctrl, bool& shift) const;

	uint32 ComputeCursorImage(const vdpoint32& pt) const;
	void UpdateMousePosition(int x, int y);
	const vdrect32 GetAltDisplayArea() const;
	bool MapPixelToBeamPosition(int x, int y, float& hcyc, float& vcyc, bool clamp) const;
	bool MapPixelToBeamPosition(int x, int y, int& xc, int& yc, bool clamp) const;
	vdint2 MapBeamPositionToPoint(int xc, int yc) const;
	void MapBeamPositionToPoint(int xc, int yc, int& x, int& y) const;
	vdfloat2 MapBeamPositionToPointF(vdfloat2 pt) const;
	void UpdateDragPreview(int x, int y);
	void SelectByCaretPosAlt(vdpoint32 caretPos1, vdpoint32 caretPos2);
	void UpdateDragPreviewAlt(int x, int y);
	void SelectByBeamPositionAntic(int xc1, int yc1, int xc2, int yc2);
	void UpdateDragPreviewAntic(int x, int y);
	void UpdateDragPreviewRects();
	void ClearDragPreview();
	int GetModeLineYPos(int ys, bool checkValidCopyText) const;
	std::pair<int, int> GetModeLineXYPos(int xcc, int ys, bool checkValidCopyText) const;

	struct ModeLineInfo {
		uint8 mMode = 0;
		uint8 mVPos = 0;
		uint8 mHeight = 0;
		uint8 mHScroll = 0;
		uint8 mDisplayHposStart = 0;
		uint8 mDisplayHposEnd = 0;
		uint8 mFetchHposStart = 0;
		uint8 mFetchHposEnd = 0;
		uint8 mCellWidth = 0;
		uint8 mCellToHRPixelShift = 0;
		uint8 mByteToHRPixelShift = 0;

		bool IsBlank() const {
			return mMode >= 2;
		}

		bool IsTextMode() const {
			return (mMode == 2 || mMode == 3 || mMode == 6 || mMode == 7);
		}
	};

	ModeLineInfo GetModeLineInfo(int vpos) const;

	int ReadText(uint8 *dst, uint8 *raw, int yc, int startChar, int numChars, bool& intl) const;

	struct EditorLine {
		uint8 mCursorCol = 0;
		uint8 mCursorRow = 0;
		vdfastvector<uint8> mText;

		bool operator==(const EditorLine&) const = default;
	};

	void ReadEditorLine(EditorLine& line) const;

	void ClearCoordinateIndicator();
	void SetCoordinateIndicator(int x, int y);

	void ClearHoverTip();

	sint32 FindDropTargetOverlay(sint32 x, sint32 y) const;
	void HighlightDropTargetOverlay(int index);
	void CreateDropTargetOverlays();
	void DestroyDropTargetOverlays();

	void UpdateAutoSuggest();

	bool mbShiftDepressed = false;
	bool mbShiftToggledPostKeyDown = false;
	bool mbHoldKeys = false;
	uint32 mSelectionCommandProcessedCounter = 0;

	vdrect32 mDisplayRect = { 0, 0, 0, 0 };
	vdrect32 mPadArea { 0, 0, 0, 0 };

	vdfunction<bool(vdfloat2&)> mpMapDisplayToSourcePt;
	vdfunction<bool(vdfloat2&)> mpMapSourceToDisplayPt;

	bool	mbDragActive = false;
	bool	mbDragInitial = false;
	uint32	mDragStartTime = 0;
	int		mDragAnchorX = 0;
	int		mDragAnchorY = 0;

	bool	mbMouseHidden = false;
	int		mMouseHideX = 0;
	int		mMouseHideY = 0;

	bool	mbOpenSidePanelDeferred = false;

	struct HighlightPoint {
		int mX;
		int mY;
		bool mbClose;
	};

	typedef vdfastvector<HighlightPoint> HighlightPoints;
	HighlightPoints mHighlightPoints;

	bool	mbShowEnhSizeIndicator = false;
	bool	mbCoordIndicatorActive = false;
	bool	mbCoordIndicatorEnabled = false;
	vdrect32	mHoverTipArea = { 0, 0, 0, 0 };
	bool		mbHoverTipActive = false;

	struct TextSpan {
		int mX;
		int mY;
		int mWidth;
		int mHeight;
		int mCharX;
		int mCharWidth;
	};

	typedef vdfastvector<TextSpan> TextSpans;
	TextSpans mDragPreviewSpans;

	IATUIEnhancedTextEngine *mpEnhTextEngine = nullptr;
	ATUIOnScreenKeyboard *mpOSK = nullptr;
	ATUIContainer *mpOSKPanel = nullptr;
	ATUISettingsWindow *mpSidePanel = nullptr;

	ATSimulatorEventManager *mpSEM = nullptr;
	uint32 mEventCallbackIdWarmReset = 0;
	uint32 mEventCallbackIdColdReset = 0;
	uint32 mEventCallbackIdFrameTick = 0;

	IATDeviceVideoManager *mpVideoMgr = nullptr;

	IATDeviceVideoOutput *mpAltVideoOutput = nullptr;
	VDDisplayImageView mAltVOImageView;
	uint32 mAltVOChangeCount = 0;
	uint32 mAltVOLayoutChangeCount = 0;

	ATUILabel *mpUILabelBadSignal = nullptr;
	ATUILabel *mpUILabelEnhTextSize = nullptr;

	vdvector<vdrefptr<ATUIDisplayTool>> mTools;
	ATUIDisplayTool *mpActiveTool = nullptr;

	vdrefptr<IATAutoSuggestEngine> mpAutoSuggestEngine;
	vdrefptr<ATUIAutoSuggestPopup> mpAutoSuggestPopup;
	bool mbAutoSuggestEnabled = false;
	bool mbAutoSuggestAcceptMapped = false;
	uint32 mShowSuggestionsCounter = 0;

	vdfunction<void()> mpOnAllowContextMenu;
	vdfunction<void(const vdpoint32&)> mpOnDisplayContextMenu;
	vdfunction<void()> mpOnOSKChange;

	vdrefptr<ATUIWidget> mpDropTargetOverlays[8];

	vdfunction<void(uint32)> mpOnAddedVideoOutput;
	vdfunction<void(uint32)> mpOnRemovingVideoOutput;

	EditorLine mSuggestScanText1;
	EditorLine mSuggestScanText2;
	EditorLine mSuggestScanText3;

	IATVideoWriter *mpVideoWriter = nullptr;

	struct ActiveKey {
		uint32 mVkey;
		uint32 mNativeScanCode;
		uint8 mScanCode;
	};

	vdfastvector<ActiveKey> mActiveKeys;
	uint32 mActiveSpecialVKeys[kATUIKeyScanCodeLast + 1 - kATUIKeyScanCodeFirst] = {};
	uint32 mActiveSpecialScanCodes[kATUIKeyScanCodeLast + 1 - kATUIKeyScanCodeFirst] = {};
};

#endif
