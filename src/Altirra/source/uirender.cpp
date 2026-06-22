//	Altirra - Atari 800/800XL emulator
//	Copyright (C) 2008-2010 Avery Lee
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
#include <vd2/system/bitmath.h>
#include <vd2/system/math.h>
#include <vd2/system/memory.h>
#include <vd2/system/registry.h>
#include <vd2/system/time.h>
#include <vd2/system/VDString.h>
#include <vd2/Kasumi/pixmap.h>
#include <vd2/Kasumi/pixmaputils.h>
#include <vd2/Kasumi/resample.h>
#include <vd2/VDDisplay/compositor.h>
#include <vd2/VDDisplay/renderer.h>
#include <vd2/VDDisplay/renderersoft.h>
#include <vd2/VDDisplay/textrenderer.h>
#include <at/ataudio/audiooutput.h>
#include <at/atcore/notifylist.h>
#include "uirender.h"
#include "audiomonitor.h"
#include "slightsid.h"
#include <at/atui/uianchor.h>
#include <at/atui/uicontainer.h>
#include <at/atui/uimanager.h>
#include <at/atui/uiwidget.h>
#include <at/atui/uiwidgetanimator.h>
#include <at/atuicontrols/uibutton.h>
#include <at/atuicontrols/uiimage.h>
#include <at/atuicontrols/uilabel.h>
#include "uicommandicons.h"
#include "uikeyboard.h"
#include "uiquickbar.h"
#include "settings.h"

namespace {
	void Shade(IVDDisplayRenderer& rdr, int x1, int y1, int dx, int dy) {
		if (rdr.GetCaps().mbSupportsAlphaBlending) {
			rdr.AlphaFillRect(x1, y1, dx, dy, 0x80000000);
		} else {
			rdr.SetColorRGB(0);
			rdr.FillRect(x1, y1, dx, dy);
		}
	}
}

///////////////////////////////////////////////////////////////////////////
class ATUIOverlayCustomization final : public ATUIContainer {
public:
	void Init();
	void Shutdown();

	void AddCustomizableWidget(const char *tag, ATUIWidget *w, const wchar_t *mpLabel);
	void BindCustomizableWidget(const char *tag, ATUIWidget *w);
	void RemoveCustomizableWidget(ATUIWidget *w);

	void SetDefaultPlacement(const char *tag, const vdrect32f& anchors, const vdpoint32& offset, const vdfloat2& pivot, const vdsize32& sizeOffset, bool autoSize);

private:
	void OnCreate() override;
	void Paint(IVDDisplayRenderer& rdr, sint32 w, sint32 h) override;
	void OnSetFocus() override;
	bool OnKeyDown(const ATUIKeyEvent& event) override;
	void OnMouseMove(sint32 x, sint32 y) override;
	void OnMouseDownL(sint32 x, sint32 y) override;
	void OnMouseUpL(sint32 x, sint32 y) override;
	void OnMouseLeave() override;

	void SelectPrev();
	void SelectNext();
	void SetSelectedIndex(sint32 idx);
	void UpdateSelectionLabel();
	void SetHoveredIndex(sint32 idx);
	void SetSelectionAnchor(sint32 x, sint32 y);
	void UpdateSelectedAnchor();
	void RepositionAnchorPanel();
	static vdint2 AnchorsToAlignment(const vdrect32f& anchors);
	static std::pair<vdrect32f, vdfloat2> AlignmentToAnchorsAndPivot(const vdint2& alignment);

	void LoadSettings(ATSettingsCategory categoryMask, VDRegistryKey& key);
	void SaveSettings(ATSettingsCategory categoryMask, VDRegistryKey& key);

	struct CWidgetInfo {
		ATUIWidget *mpWidget;
		const char *mpTag;
		const wchar_t *mpLabel;
		vdrect32f mAnchors;
		vdpoint32 mOffset;
		vdfloat2 mPivot;
		vdsize32 mSizeOffset;
		bool mbAutoSize;
		vdrect32f mDefaultAnchors;
		vdpoint32 mDefaultOffset;
		vdfloat2 mDefaultPivot;
		vdsize32 mDefaultSizeOffset;
		bool mbDefaultAutoSize;
		bool mbUsingDefault;
	};

	vdfastvector<CWidgetInfo> mCustomizableWidgets;
	sint32 mSelectedIndex = -1;
	sint32 mHoveredIndex = -1;

	bool mbSelectionLockedX = false;
	bool mbSelectionLockedY = false;
	ATUIWidgetMetrics mSelectionConstraints;
	bool mbDragging = false;
	bool mbDragMoving = false;

	struct DragFactor {
		sint32 mAxisScale;
		sint32 mAxisOffset;
	};

	DragFactor mDragOffsetOX;
	DragFactor mDragOffsetOY;
	DragFactor mDragOffsetSX;
	DragFactor mDragOffsetSY;

	vdrefptr<ATUIContainer> mpAnchorPanel;
	vdrefptr<ATUIButton> mpAnchorButtons[4][4];
	vdrefptr<ATUILabel> mpSelectionLabel;

	ATSettingsLoadSaveCallback mLoadCallback;
	ATSettingsLoadSaveCallback mSaveCallback;
};

void ATUIOverlayCustomization::Init() {
	mLoadCallback = [this](uint32 profileId, ATSettingsCategory categoryMask, VDRegistryKey& key) {
		LoadSettings(categoryMask, key);
	};

	mSaveCallback = [this](uint32 profileId, ATSettingsCategory categoryMask, VDRegistryKey& key) {
		SaveSettings(categoryMask, key);
	};

	ATSettingsRegisterLoadCallback(&mLoadCallback);
	ATSettingsRegisterSaveCallback(&mSaveCallback);
}

void ATUIOverlayCustomization::Shutdown() {
	for(auto& cwi : mCustomizableWidgets) {
		vdsaferelease <<= cwi.mpWidget;
	}

	ATSettingsUnregisterLoadCallback(&mLoadCallback);
	ATSettingsUnregisterSaveCallback(&mSaveCallback);
}

void ATUIOverlayCustomization::AddCustomizableWidget(const char *tag, ATUIWidget *w, const wchar_t *label) {
	if (w)
		w->AddRef();

	CWidgetInfo& cwi = mCustomizableWidgets.push_back();
	cwi.mpWidget = w;
	cwi.mpTag = tag;
	cwi.mpLabel = label;

	if (w) {
		cwi.mDefaultAnchors = w->GetAnchors();
		cwi.mDefaultOffset = w->GetOffset();
		cwi.mDefaultPivot = w->GetPivot();
		cwi.mDefaultSizeOffset = w->GetSizeOffset();
		cwi.mbDefaultAutoSize = w->IsAutoSize();
	} else {
		cwi.mDefaultAnchors = vdrect32f(0, 0, 0, 0);
		cwi.mDefaultOffset = vdpoint32(0, 0);
		cwi.mDefaultPivot = vdfloat2{0, 0};
		cwi.mDefaultSizeOffset = vdsize32(0, 0);
		cwi.mbDefaultAutoSize = true;
	}

	cwi.mAnchors = cwi.mDefaultAnchors;
	cwi.mOffset = cwi.mDefaultOffset;
	cwi.mPivot = cwi.mDefaultPivot;
	cwi.mSizeOffset = cwi.mDefaultSizeOffset;
	cwi.mbAutoSize = cwi.mbDefaultAutoSize;
	cwi.mbUsingDefault = true;

	if (w)
		Invalidate();
}

void ATUIOverlayCustomization::BindCustomizableWidget(const char *tag, ATUIWidget *w) {
	sint32 index = 0;

	for(auto& cwi : mCustomizableWidgets) {
		if (!strcmp(cwi.mpTag, tag)) {
			if (cwi.mpWidget != w) {
				if (!w && mSelectedIndex == index)
					SetSelectedIndex(-1);

				if (cwi.mpWidget)
					cwi.mpWidget->Release();

				if (w)
					w->AddRef();

				cwi.mpWidget = w;

				if (w) {
					w->SetPlacement(cwi.mAnchors, cwi.mOffset, cwi.mPivot);
					if (cwi.mbAutoSize)
						w->SetAutoSize();
					else
						w->SetSizeOffset(cwi.mSizeOffset);
				}
			}
			break;
		}

		++index;
	}
}

void ATUIOverlayCustomization::RemoveCustomizableWidget(ATUIWidget *w) {
	if (!w)
		return;

	auto it = std::find_if(mCustomizableWidgets.begin(), mCustomizableWidgets.end(),
		[=](const CWidgetInfo& ci) {
			return ci.mpWidget == w;
		}
	);

	if (it == mCustomizableWidgets.end())
		return;

	const sint32 pos = (sint32)(it - mCustomizableWidgets.begin());

	if (mSelectedIndex == pos)
		SetSelectedIndex(-1);
	else if (mSelectedIndex > pos)
		--mSelectedIndex;

	if (mHoveredIndex == pos)
		mHoveredIndex = -1;
	else if (mHoveredIndex > pos)
		--mHoveredIndex;

	mCustomizableWidgets.erase(it);

	w->Release();
	Invalidate();
}

void ATUIOverlayCustomization::SetDefaultPlacement(const char *tag, const vdrect32f& anchors, const vdpoint32& offset, const vdfloat2& pivot, const vdsize32& sizeOffset, bool autoSize) {
	for (auto& cwi : mCustomizableWidgets) {
		if (!strcmp(cwi.mpTag, tag)) {
			cwi.mDefaultAnchors = anchors;
			cwi.mDefaultOffset = offset;
			cwi.mDefaultPivot = pivot;
			cwi.mDefaultSizeOffset = sizeOffset;
			cwi.mbDefaultAutoSize = autoSize;

			if (cwi.mbUsingDefault) {
				cwi.mAnchors = cwi.mDefaultAnchors;
				cwi.mOffset = cwi.mDefaultOffset;
				cwi.mPivot = cwi.mDefaultPivot;
				cwi.mSizeOffset = cwi.mDefaultSizeOffset;
				cwi.mbAutoSize = cwi.mbDefaultAutoSize;

				if (cwi.mpWidget) {
					cwi.mpWidget->SetPlacement(anchors, offset, pivot);

					if (cwi.mbAutoSize)
						cwi.mpWidget->SetAutoSize();
					else
						cwi.mpWidget->SetSizeOffset(cwi.mSizeOffset);
				}
			}
			break;
		}
	}
}

void ATUIOverlayCustomization::OnCreate() {
	mpAnchorPanel = new ATUIContainer;
	mpAnchorPanel->SetVisible(false);
	AddChild(mpAnchorPanel);

	static constexpr const wchar_t *kLabels[4][4] = {
		{ L"TL", L"TC", L"TR", L"T*" },
		{ L"ML", L"MC", L"MR", L"M*" },
		{ L"BL", L"BC", L"BR", L"B*" },
		{ L"*L", L"*C", L"*R", L"**" },
	};

	vdsize32 maxSize(0, 0);

	for(int i=0; i<4; ++i) {
		for(int j=0; j<4; ++j) {
			vdrefptr<ATUIButton> button(new ATUIButton);

			button->SetText(kLabels[i][j]);
			mpAnchorPanel->AddChild(button);

			maxSize.include(button->Measure().mMinSize);

			button->OnPressedEvent() = [=,this] {
				SetSelectionAnchor(j, i);
			};

			button->SetToggleMode(true);

			mpAnchorButtons[i][j] = std::move(button);
		}
	}

	for(int i=0; i<4; ++i) {
		for(int j=0; j<4; ++j) {
			vdrect32 r;

			r.left = j * maxSize.w;
			r.top = i * maxSize.h;
			r.right = r.left + maxSize.w;
			r.bottom = r.top + maxSize.h;
			mpAnchorButtons[i][j]->SetArea(r);
		}
	}

	mpAnchorPanel->SetSize(vdsize32(maxSize.w * 4, maxSize.h * 4));

	vdrefptr<ATUIContainer> infoContainer{new ATUIContainer};
	AddChild(infoContainer);
	infoContainer->SetHitTransparent(true);
	infoContainer->SetAlphaFillColor(0xE0000000);
	infoContainer->SetAnchors(vdrect32f(0.05f, 0.05f, 0.05f, 0.05f));

	vdrefptr<ATUILabel> label;
	label = new ATUILabel;
	infoContainer->AddChild(label);
	label->SetHitTransparent(true);
	label->SetDockMode(kATUIDockMode_Top);
	label->SetFont(GetManager()->GetThemeFont(kATUIThemeFont_Header));
	label->SetText(
		L"Press Esc to exit customization mode.\n"
		L"\n"
		L"[Shift]+Tab - cycle selected item\n"
		L"R - reset layout for selected item"
	);
	label->SetAlphaFillColor(0);
	label->SetTextColor(0xFFFFFF);

	mpSelectionLabel = new ATUILabel;
	infoContainer->AddChild(mpSelectionLabel);
	mpSelectionLabel->SetHitTransparent(true);
	mpSelectionLabel->SetFont(GetManager()->GetThemeFont(kATUIThemeFont_Header));
	mpSelectionLabel->SetAlphaFillColor(0);
	mpSelectionLabel->SetTextColor(0xFFFFFF);
	mpSelectionLabel->SetDockMode(kATUIDockMode_Top);

	UpdateSelectionLabel();
}

void ATUIOverlayCustomization::Paint(IVDDisplayRenderer& rdr, sint32 w, sint32 h) {
	sint32 index = 0;

	for(const auto& cwi : mCustomizableWidgets) {
		ATUIWidget *widget = cwi.mpWidget;
		if (!widget)
			continue;

		vdrect32 r = widget->GetArea();

		if (r.width() <= 0) {
			r.right = r.left + 1;
			--r.left;
		}

		if (r.height() <= 0) {
			r.bottom = r.top + 1;
			--r.top;
		}

		const uint32 c = (index == mSelectedIndex) ? 0xFF0000 : (index == mHoveredIndex) ? 0x4080FF : 0x808080;
		++index;

		rdr.SetColorRGB(c);

		const sint32 rw = r.width();
		const sint32 rh = r.height();

		if (rw > 2 && rh > 2) {
			rdr.FillRect(r.left, r.top, rw, 1);
			rdr.FillRect(r.left, r.top+1, 1, rh-2);
			rdr.FillRect(r.right-1, r.top+1, 1, rh-2);
			rdr.FillRect(r.left, r.bottom-1, rw, 1);
		} else {
			rdr.FillRect(r.left, r.top, rw, rh);
		}
	}

	ATUIContainer::Paint(rdr, w, h);
}

void ATUIOverlayCustomization::OnSetFocus() {
	if (mSelectedIndex < 0)
		SelectNext();
}

bool ATUIOverlayCustomization::OnKeyDown(const ATUIKeyEvent& event) {
	if (event.mVirtKey == kATUIVK_Escape) {
		GetParent()->Focus();

		SetSelectedIndex(-1);
		SetVisible(false);
		return true;
	} else if (event.mVirtKey == kATUIVK_Tab) {
		if (GetManager()->IsKeyDown(kATUIVK_Shift))
			SelectPrev();
		else
			SelectNext();
		return true;
	} else if (event.mVirtKey == kATUIVK_A + ('R' - 'A')) {
		if ((size_t)mSelectedIndex < mCustomizableWidgets.size()) {
			auto& cwi = mCustomizableWidgets[mSelectedIndex];

			if (!cwi.mbUsingDefault) {
				cwi.mbUsingDefault = true;

				cwi.mAnchors = cwi.mDefaultAnchors;
				cwi.mOffset = cwi.mDefaultOffset;
				cwi.mPivot = cwi.mDefaultPivot;
				cwi.mSizeOffset = cwi.mDefaultSizeOffset;
				cwi.mbAutoSize = cwi.mbDefaultAutoSize;

				cwi.mpWidget->SetPlacement(cwi.mDefaultAnchors, cwi.mDefaultOffset, cwi.mDefaultPivot);

				if (cwi.mbAutoSize)
					cwi.mpWidget->SetAutoSize();
				else
					cwi.mpWidget->SetSizeOffset(cwi.mSizeOffset);

				cwi.mpWidget->GetParent()->UpdateLayout();
				RepositionAnchorPanel();
			}
		}

		return true;
	}

	return false;
}

void ATUIOverlayCustomization::OnMouseMove(sint32 x, sint32 y) {
	if (mbDragging) {
		auto& cwi = mCustomizableWidgets[mSelectedIndex];
		ATUIWidget *widget = cwi.mpWidget;

		vdpoint32 offset;
		offset.x = (x * mDragOffsetOX.mAxisScale)/2 + mDragOffsetOX.mAxisOffset;
		offset.y = (y * mDragOffsetOY.mAxisScale)/2 + mDragOffsetOY.mAxisOffset;
		widget->SetOffset(offset);

		cwi.mOffset = offset;

		vdsize32 sz;
		sz.w = (x * mDragOffsetSX.mAxisScale)/2 + mDragOffsetSX.mAxisOffset;
		sz.h = (y * mDragOffsetSY.mAxisScale)/2 + mDragOffsetSY.mAxisOffset;
		widget->SetSizeOffset(sz);

		cwi.mSizeOffset = sz;
		cwi.mbAutoSize = false;

		widget->GetParent()->UpdateLayout();

		RepositionAnchorPanel();

		mCustomizableWidgets[mSelectedIndex].mbUsingDefault = false;
	} else {
		const vdpoint32 pt{x, y};

		sint32 n = (sint32)mCustomizableWidgets.size();
		sint32 offset = mSelectedIndex >= 0 ? mSelectedIndex + 1 : 0;

		const sint32 edgeWidth = std::max<sint32>(1, (sint32)(0.5f + 4.0f * mpManager->GetThemeScaleFactor()));

		sint32 foundIndex = -1;
		for(sint32 i = 0; i < n; ++i) {
			sint32 j = (i + offset) % n;
			ATUIWidget *widget = mCustomizableWidgets[j].mpWidget;
			if (!widget)
				continue;

			vdrect32 r = widget->GetArea();
			vdrect32 r2 = r;

			r2.left -= edgeWidth;
			r2.top -= edgeWidth;
			r2.right += edgeWidth;
			r2.bottom += edgeWidth;

			if (r2.contains(pt)) {
				foundIndex = j;

				sint32 axisx1 = 0;
				sint32 axisx2 = 0;
				sint32 axisy1 = 0;
				sint32 axisy2 = 0;

				if (x < r.right - edgeWidth)
					axisx1 = -1;

				if (x > r.left + edgeWidth)
					axisx2 = -1;

				if (y < r.bottom - edgeWidth)
					axisy1 = -1;

				if (y > r.top + edgeWidth)
					axisy2 = -1;

				// kill move axes unless both are moving
				sint32 xMove = axisx1 & axisx2;
				sint32 yMove = axisy1 & axisy2;

				mbDragMoving = xMove && yMove;

				if (xMove != yMove) {
					axisx1 &= ~xMove;
					axisx2 &= ~xMove;
					axisy1 &= ~yMove;
					axisy2 &= ~yMove;
				}

				// kill resizing for locked axes
				if (mbSelectionLockedX && !xMove) {
					axisx1 = 0;
					axisx2 = 0;
				}

				if (mbSelectionLockedY && !yMove) {
					axisy1 = 0;
					axisy2 = 0;
				}

				vdpoint32 startingOffset = widget->GetOffset();
				vdsize32 startingSizeOffset = widget->GetSizeOffset();
				vdfloat2 pivot = widget->GetPivot();

				if (!widget->IsForcedSize()) {
					const vdrect32f& anchors = widget->GetAnchors();
					const vdsize32& csize = GetArea().size();
					const vdsize32& wsize = r.size();

					startingSizeOffset.w = wsize.w - anchors.width() * (float)csize.w;
					startingSizeOffset.h = wsize.h - anchors.height() * (float)csize.h;
				}

				const sint32 posAxisX = 1 + axisx1 - axisx2;
				const sint32 posAxisY = 1 + axisy1 - axisy2;

				if (mbDragMoving) {
					mDragOffsetOX.mAxisScale = 2;
					mDragOffsetOY.mAxisScale = 2;
					mDragOffsetSX.mAxisScale = 0;
					mDragOffsetSY.mAxisScale = 0;
				} else {
					// alignment				min-drag		max-drag
					// ---------------------------------------------------
					// align left/top			off,-size		size
					// align middle/center		off/2,-size		off/2,size
					// align right/bottom		-size			off,size

					const sint32 anchorX = VDRoundToInt32(pivot.x * 2.0f);
					const sint32 anchorY = VDRoundToInt32(pivot.y * 2.0f);

					mDragOffsetOX.mAxisScale = posAxisX > 1 ? anchorX : posAxisX < 1 ? 2 - anchorX : 0;
					mDragOffsetOY.mAxisScale = posAxisY > 1 ? anchorY : posAxisY < 1 ? 2 - anchorY : 0;
					mDragOffsetSX.mAxisScale = posAxisX > 1 ? 2 : posAxisX < 1 ? -2 : 0;
					mDragOffsetSY.mAxisScale = posAxisY > 1 ? 2 : posAxisY < 1 ? -2 : 0;

				}

				mDragOffsetOX.mAxisOffset = startingOffset.x - (x * mDragOffsetOX.mAxisScale)/2;
				mDragOffsetOY.mAxisOffset = startingOffset.y - (y * mDragOffsetOY.mAxisScale)/2;
				mDragOffsetSX.mAxisOffset = startingSizeOffset.w - (x * mDragOffsetSX.mAxisScale)/2;
				mDragOffsetSY.mAxisOffset = startingSizeOffset.h - (y * mDragOffsetSY.mAxisScale)/2;

				static constexpr ATUICursorImage kCursorLookup[3][3] = {
					{
						kATUICursorImage_SizeDiagRev,
						kATUICursorImage_SizeVert,
						kATUICursorImage_SizeDiagFwd,
					},
					{
						kATUICursorImage_SizeHoriz,
						kATUICursorImage_Move,
						kATUICursorImage_SizeHoriz,
					},
					{
						kATUICursorImage_SizeDiagFwd,
						kATUICursorImage_SizeVert,
						kATUICursorImage_SizeDiagRev,
					},
				};

				SetCursorImage(kCursorLookup[1 + axisy1 - axisy2][1 + axisx1 - axisx2]);
				break;
			}
		}

		if (foundIndex < 0)
			SetCursorImage(kATUICursorImage_Arrow);

		SetHoveredIndex(foundIndex);
	}
}

void ATUIOverlayCustomization::OnMouseDownL(sint32 x, sint32 y) {
	mbDragging = false;
	OnMouseMove(x, y);

	SetSelectedIndex(mHoveredIndex);
	Focus();

	if (mSelectedIndex >= 0) {
		mbDragging = true;
		CaptureCursor();
	}
}

void ATUIOverlayCustomization::OnMouseUpL(sint32 x, sint32 y) {
	mbDragging = false;
	ReleaseCursor();
}

void ATUIOverlayCustomization::OnMouseLeave() {
	SetHoveredIndex(-1);
}

void ATUIOverlayCustomization::SelectPrev() {
	const sint32 n = (sint32)mCustomizableWidgets.size();
	sint32 i = mSelectedIndex;

	for(sint32 j = 0; j < n; ++j) {
		if (--i < 0)
			i = n - 1;

		if (mCustomizableWidgets[i].mpWidget) {
			SetSelectedIndex(i);
			break;
		}
	}
}

void ATUIOverlayCustomization::SelectNext() {
	const sint32 n = (sint32)mCustomizableWidgets.size();
	sint32 i = mSelectedIndex;

	if (i < 0)
		i = -1;

	for(sint32 j = 0; j < n; ++j) {
		if (++i >= n)
			i = 0;

		if (mCustomizableWidgets[i].mpWidget) {
			SetSelectedIndex(i);
			break;
		}
	}
}

void ATUIOverlayCustomization::SetSelectedIndex(sint32 idx) {
	if (mSelectedIndex == idx)
		return;

	mSelectedIndex = idx;

	if (mSelectedIndex >= 0) {
		ATUIWidget *w = mCustomizableWidgets[mSelectedIndex].mpWidget;

		const auto& m = w->Measure();
		mSelectionConstraints = m;
		mbSelectionLockedX = (m.mMinSize.w == m.mMaxSize.w);
		mbSelectionLockedY = (m.mMinSize.h == m.mMaxSize.h);

		for(int i=0; i<3; ++i)
			mpAnchorButtons[i][3]->SetEnabled(!mbSelectionLockedX);

		for(int i=0; i<3; ++i)
			mpAnchorButtons[3][i]->SetEnabled(!mbSelectionLockedY);

		mpAnchorButtons[3][3]->SetEnabled(!mbSelectionLockedX && !mbSelectionLockedY);
	}

	mbDragging = false;
	Invalidate();
	RepositionAnchorPanel();
	UpdateSelectionLabel();
	UpdateSelectedAnchor();
}

void ATUIOverlayCustomization::UpdateSelectionLabel() {
	mpSelectionLabel->SetTextF(L"Selected item: %ls", mSelectedIndex >= 0 ? mCustomizableWidgets[mSelectedIndex].mpLabel : L"none");
}

void ATUIOverlayCustomization::SetHoveredIndex(sint32 idx) {
	if (mHoveredIndex == idx)
		return;

	mHoveredIndex = idx;
	Invalidate();
}

void ATUIOverlayCustomization::SetSelectionAnchor(sint32 x, sint32 y) {
	if (mSelectedIndex < 0)
		return;

	auto& cwi = mCustomizableWidgets[mSelectedIndex];
	ATUIWidget *w = cwi.mpWidget;
	const vdrect32 origArea = w->GetArea();

	// set the anchors and pivot
	auto [newAnchors, newPivot] = AlignmentToAnchorsAndPivot(vdint2{x, y});

	cwi.mAnchors = newAnchors;
	w->SetAnchors(newAnchors);

	cwi.mPivot = newPivot;
	w->SetPivot(newPivot);

	// compute new anchored area
	const vdrect32 rc = GetClientArea();
	vdrect32 r;
	r.left = rc.left + VDRoundToInt32((float)rc.width() * newAnchors.left);
	r.top = rc.top + VDRoundToInt32((float)rc.height() * newAnchors.top);
	r.right = rc.left + VDRoundToInt32((float)rc.width() * newAnchors.right);
	r.bottom = rc.top + VDRoundToInt32((float)rc.height() * newAnchors.bottom);

	// apply offset and size corrections
	float dx1 = (float)(origArea.left   - r.left);
	float dy1 = (float)(origArea.top    - r.top);
	float dx2 = (float)(origArea.right  - r.right);
	float dy2 = (float)(origArea.bottom - r.bottom);

	const vdpoint32 offset(
		VDRoundToInt32(dx1 + (dx2 - dx1)*newPivot.x),
		VDRoundToInt32(dy1 + (dy2 - dy1)*newPivot.y)
	);

	cwi.mOffset = offset;
	w->SetOffset(offset);

	if (!w->IsForcedSize()) {
		const vdsize32 sizeOffset(dx2 - dx1, dy2 - dy1);

		cwi.mSizeOffset = sizeOffset;

		w->SetSizeOffset(sizeOffset);
	}

	UpdateSelectedAnchor();
}

void ATUIOverlayCustomization::UpdateSelectedAnchor() {
	if (mSelectedIndex < 0)
		return;

	ATUIWidget *w = mCustomizableWidgets[mSelectedIndex].mpWidget;
	const vdrect32f& anchors = w->GetAnchors();

	auto [anchorX, anchorY] = AnchorsToAlignment(anchors);

	for(int i=0; i<4; ++i) {
		for(int j=0; j<4; ++j) {
			mpAnchorButtons[i][j]->SetDepressed(i == anchorY && j == anchorX);
		}
	}
}

void ATUIOverlayCustomization::RepositionAnchorPanel() {
	if (mSelectedIndex < 0) {
		mpAnchorPanel->SetVisible(false);
		return;
	}

	// position panel on sides where there is the most space
	const sint32 margin = 8;
	ATUIWidget *w = mCustomizableWidgets[mSelectedIndex].mpWidget;
	vdrect32 r = w->GetArea();
	vdrect32 rc = GetClientArea();
	vdrect32 rp = mpAnchorPanel->GetArea();

	if (r.left - rc.left > rc.right - r.right)
		rp.translate((r.left - margin) - rp.right, 0);
	else
		rp.translate((r.right + margin) - rp.left, 0);

	if (r.top - rc.top > rc.bottom - r.bottom)
		rp.translate(0, (r.top - margin) - rp.bottom);
	else
		rp.translate(0, (r.bottom + margin) - rp.top);

	mpAnchorPanel->SetArea(rp);
	mpAnchorPanel->SetVisible(true);
}

vdint2 ATUIOverlayCustomization::AnchorsToAlignment(const vdrect32f& anchors) {
	sint32 anchorX = 3;
	sint32 anchorY = 3;

	if (anchors.width() < 0.5f)
		anchorX = VDRoundToInt32(anchors.left * 2.0f);

	if (anchors.width() < 0.5f)
		anchorY = VDRoundToInt32(anchors.top * 2.0f);

	return vdint2{anchorX, anchorY};
}

std::pair<vdrect32f, vdfloat2> ATUIOverlayCustomization::AlignmentToAnchorsAndPivot(const vdint2& alignment) {
	static constexpr float kAnchorLookup1[4] = { 0.0f, 0.5f, 1.0f, 0.0f };
	static constexpr float kAnchorLookup2[4] = { 0.0f, 0.5f, 1.0f, 1.0f };

	const vdrect32f newAnchors(
		kAnchorLookup1[alignment.x & 3],
		kAnchorLookup1[alignment.y & 3],
		kAnchorLookup2[alignment.x & 3],
		kAnchorLookup2[alignment.y & 3]
	);
	static constexpr float kPivotLookup[4] = { 0.0f, 0.5f, 1.0f, 0.5f };
	const vdfloat2 newPivot{kPivotLookup[alignment.x & 3], kPivotLookup[alignment.y & 3]};

	return { newAnchors, newPivot };
}

void ATUIOverlayCustomization::LoadSettings(ATSettingsCategory categoryMask, VDRegistryKey& key) {
	VDRegistryKey subKey(key, "HUD", false);

	for (CWidgetInfo& cwi : mCustomizableWidgets) {
		cwi.mbUsingDefault = true;
		cwi.mAnchors = cwi.mDefaultAnchors;
		cwi.mOffset = cwi.mDefaultOffset;
		cwi.mPivot = cwi.mDefaultPivot;
		cwi.mSizeOffset = cwi.mDefaultSizeOffset;
		cwi.mbAutoSize = cwi.mbDefaultAutoSize;

		VDStringA s;
		if (subKey.getString(cwi.mpTag, s)) {
			int alignmentX, alignmentY, offsetX, offsetY, sizeOffsetX, sizeOffsetY, autoSize;

			if (7 == sscanf(s.c_str(), "%d,%d:%d,%d:%d,%d:%d", &alignmentX, &alignmentY, &offsetX, &offsetY, &sizeOffsetX, &sizeOffsetY, &autoSize)) {
				cwi.mbUsingDefault = false;

				auto [anchors, pivot] = AlignmentToAnchorsAndPivot(vdint2{alignmentX, alignmentY});

				cwi.mAnchors = anchors;
				cwi.mPivot = pivot;
				cwi.mOffset = vdpoint32{offsetX, offsetY};
				cwi.mSizeOffset = vdsize32{sizeOffsetX, sizeOffsetY};
				cwi.mbAutoSize = autoSize != 0;
			}
		}

		if (cwi.mpWidget) {
			cwi.mpWidget->SetPlacement(cwi.mAnchors, cwi.mOffset, cwi.mPivot);

			if (cwi.mbAutoSize)
				cwi.mpWidget->SetAutoSize();
			else
				cwi.mpWidget->SetSizeOffset(cwi.mSizeOffset);
		}
	}
}

void ATUIOverlayCustomization::SaveSettings(ATSettingsCategory categoryMask, VDRegistryKey& key) {
	VDRegistryKey subKey(key, "HUD", true);

	VDStringA s;
	for (const CWidgetInfo& cwi : mCustomizableWidgets) {
		if (cwi.mbUsingDefault) {
			subKey.removeValue(cwi.mpTag);
		} else {
			const vdint2& alignment = AnchorsToAlignment(cwi.mAnchors);

			s.sprintf("%d,%d:%d,%d:%d,%d:%d"
				, alignment.x
				, alignment.y
				, cwi.mOffset.x
				, cwi.mOffset.y
				, cwi.mSizeOffset.w
				, cwi.mSizeOffset.h
				, cwi.mbAutoSize ? 1 : 0);

			subKey.setString(cwi.mpTag, s.c_str());
		}
	}
}

///////////////////////////////////////////////////////////////////////////
class ATUIAudioStatusDisplay final : public ATUIWidget {
public:
	void SetFont(IVDDisplayFont *font);
	void Update(const ATUIAudioStatus& status);
	void AutoSize();

protected:
	void Paint(IVDDisplayRenderer& rdr, sint32 w, sint32 h);

	void FormatLine(int idx, const ATUIAudioStatus& status);

	vdrefptr<IVDDisplayFont> mpFont;
	VDStringW mText;
	ATUIAudioStatus mAudioStatus;
};

void ATUIAudioStatusDisplay::SetFont(IVDDisplayFont *font) {
	mpFont = font;
}

void ATUIAudioStatusDisplay::Update(const ATUIAudioStatus& status) {
	mAudioStatus = status;

	Invalidate();
}

void ATUIAudioStatusDisplay::Paint(IVDDisplayRenderer& rdr, sint32 w, sint32 h) {
	VDDisplayFontMetrics metrics;
	mpFont->GetMetrics(metrics);

	const int fonth = metrics.mAscent + metrics.mDescent;
	int y2 = 2;

	VDDisplayTextRenderer& tr = *rdr.GetTextRenderer();
	tr.SetFont(mpFont);
	tr.SetAlignment(VDDisplayTextRenderer::kAlignLeft, VDDisplayTextRenderer::kVertAlignTop);
	tr.SetColorRGB(0xffffff);

	for(int i=0; i<8; ++i) {
		FormatLine(i, mAudioStatus);
		tr.DrawTextLine(2, y2, mText.c_str());
		y2 += fonth;
	}
}

void ATUIAudioStatusDisplay::AutoSize() {
	if (!mpFont)
		return;

	// create a test structure with some big values in it
	ATUIAudioStatus testStatus = {};

	testStatus.mUnderflowCount = 9999;
	testStatus.mOverflowCount = 9999;
	testStatus.mDropCount = 9999;
	testStatus.mMeasuredMin = 999999;
	testStatus.mMeasuredMax = 999999;
	testStatus.mTargetMin = 999999;
	testStatus.mTargetMax = 999999;
	testStatus.mIncomingRate = 99999;
	testStatus.mExpectedRate = 99999;
	testStatus.mbStereoMixing = true;

	// loop over all the strings and compute max size
	sint32 w = 0;
	for(int i=0; i<8; ++i) {
		FormatLine(i, testStatus);

		w = std::max<sint32>(w, mpFont->MeasureString(mText.c_str(), mText.size(), false).w);
	}

	// resize me
	VDDisplayFontMetrics metrics;
	mpFont->GetMetrics(metrics);

	const int fonth = metrics.mAscent + metrics.mDescent;

	SetSize(vdsize32(4 + w, 4 + fonth * 8));
}

void ATUIAudioStatusDisplay::FormatLine(int idx, const ATUIAudioStatus& status) {
	switch(idx) {
	case 0:
		mText.sprintf(L"Underflow count: %d", status.mUnderflowCount);
		break;

	case 1:
		mText.sprintf(L"Overflow count: %d", status.mOverflowCount);
		break;

	case 2:
		mText.sprintf(L"Drop count: %d", status.mDropCount);
		break;

	case 3:
		mText.sprintf(L"Measured range: %5d-%5d (%.1f ms)"
			, status.mMeasuredMin
			, status.mMeasuredMax
			, (float)status.mMeasuredMin * 1000.0f / (status.mSamplingRate * 4.0f)
		);
		break;

	case 4:
		mText.sprintf(L"Target range: %5d-%5d", status.mTargetMin, status.mTargetMax);
		break;

	case 5:
		mText.sprintf(L"Incoming data rate: %.2f samples/sec", status.mIncomingRate);
		break;

	case 6:
		mText.sprintf(L"Expected data rate: %.2f samples/sec", status.mExpectedRate);
		break;

	case 7:
		mText.sprintf(L"Mixing mode: %ls", status.mbStereoMixing ? L"stereo" : L"mono");
		break;
	}
}

///////////////////////////////////////////////////////////////////////////
class ATUIAudioDisplay : public ATUIWidget {
public:
	ATUIAudioDisplay();

	void SetCyclesPerSecond(double rate) { mCyclesPerSecond = rate; }

	void SetAudioMonitor(ATAudioMonitor *mon);
	void SetSlightSID(ATSlightSIDEmulator *ss);

	void SetBigFont(IVDDisplayFont *font);
	void SetSmallFont(IVDDisplayFont *font);

	void Update() { Invalidate(); }

protected:
	ATUIWidgetMetrics OnMeasure() override;
	void Paint(IVDDisplayRenderer& rdr, sint32 w, sint32 h) override;
	void PaintSID(IVDDisplayRenderer& rdr, VDDisplayTextRenderer& tr, sint32 w, sint32 h);
	void PaintPOKEY(IVDDisplayRenderer& rdr, VDDisplayTextRenderer& tr, sint32 w, sint32 h);

	double mCyclesPerSecond = 1;

	vdrefptr<IVDDisplayFont> mpBigFont;
	vdrefptr<IVDDisplayFont> mpSmallFont;

	int mBigFontW;
	int mBigFontH;
	int mSmallFontW;
	int mSmallFontH;

	ATAudioMonitor *mpAudioMonitor;
	ATSlightSIDEmulator *mpSlightSID;
};

ATUIAudioDisplay::ATUIAudioDisplay()
	: mBigFontW(0)
	, mBigFontH(0)
	, mSmallFontW(0)
	, mSmallFontH(0)
	, mpAudioMonitor(NULL)
	, mpSlightSID(NULL)
{
}

void ATUIAudioDisplay::SetAudioMonitor(ATAudioMonitor *mon) {
	mpAudioMonitor = mon;
}

void ATUIAudioDisplay::SetSlightSID(ATSlightSIDEmulator *ss) {
	mpSlightSID = ss;
}

void ATUIAudioDisplay::SetBigFont(IVDDisplayFont *font) {
	if (mpBigFont == font)
		return;

	mpBigFont = font;

	const vdsize32& size = font->MeasureString(L"0123456789", 10, false);

	mBigFontW = size.w / 10;
	mBigFontH = size.h;
}

void ATUIAudioDisplay::SetSmallFont(IVDDisplayFont *font) {
	if (mpSmallFont == font)
		return;

	mpSmallFont = font;

	const vdsize32& size = font->MeasureString(L"0123456789", 10, false);

	mSmallFontW = size.w / 10;
	mSmallFontH = size.h;
}

ATUIWidgetMetrics ATUIAudioDisplay::OnMeasure() {
	const int chanht = 5 + mBigFontH + mSmallFontH;
	const int chanw = (std::max<int>(11*mSmallFontW, 8*mBigFontW) + 4) * 4;

	ATUIWidgetMetrics m;
	m.mMinSize = { chanw, chanht * 4 };
	m.mMaxSize.h = m.mMinSize.h;
	m.mDesiredSize = m.mMinSize;
	return m;
}

void ATUIAudioDisplay::Paint(IVDDisplayRenderer& rdr, sint32 w, sint32 h) {
	VDDisplayTextRenderer& tr = *rdr.GetTextRenderer();

	if (mpSlightSID)
		PaintSID(rdr, tr, w, h);
	else
		PaintPOKEY(rdr, tr, w, h);
}

void ATUIAudioDisplay::PaintSID(IVDDisplayRenderer& rdr, VDDisplayTextRenderer& tr, sint32 w, sint32 h) {
	const int fontw = mBigFontW;
	const int fonth = mBigFontH;
	const int fontsmw = mSmallFontW;
	const int fontsmh = mSmallFontH;

	const int chanht = 5 + fonth + fontsmh;

	const int x = 0;
	const int y = 0;

	const int x_freq = x + fontw * 8;
	const int x_note1 = x;
	const int x_note2 = x + fontsmw * 5;
	const int x_modes = x_freq + 4;
	const int x_duty = x_freq + 4;
	const int x_volbar = x_modes + 9*fontsmw;
	const int x_adsr = x_volbar + 5;

	wchar_t buf[128];

	const uint8 *regbase = mpSlightSID->GetRegisters();
	for(int ch=0; ch<3; ++ch) {
		const uint8 *chreg = regbase + 7*ch;
		const int chy = y + chanht*ch;
		const int chy_freq = chy;
		const int chy_modes = chy;
		const int chy_duty = chy + fontsmh;
		const int chy_note = chy + fonth;
		const int chy_adsr = chy + 1;
		const int chy_envelope = chy_adsr + fontsmh;
		const uint32 color = (ch != 2 || !(regbase[0x18] & 0x80)) ? 0xFFFFFF : 0x006e6e6e;

		const uint32 freq = chreg[0] + chreg[1]*256;
		//const float hz = (float)freq * (17897725.0f / 18.0f / 16777216.0f);
		const float hz = (float)freq * (985248.0f / 16777216.0f);
		swprintf(buf, 128, L"%.1f", hz);

		tr.SetFont(mpBigFont);
		tr.SetColorRGB(color);
		tr.SetAlignment(VDDisplayTextRenderer::kAlignRight, VDDisplayTextRenderer::kVertAlignTop);
		tr.DrawTextLine(x_freq, chy_freq, buf);
		tr.SetFont(mpSmallFont);

		buf[0] = chreg[4] & 0x80 ? 'N' : ' ';
		buf[1] = chreg[4] & 0x40 ? 'P' : ' ';
		buf[2] = chreg[4] & 0x20 ? 'S' : ' ';
		buf[3] = chreg[4] & 0x10 ? 'T' : ' ';
		buf[4] = chreg[4] & 0x08 ? 'E' : ' ';
		buf[5] = chreg[4] & 0x04 ? 'R' : ' ';
		buf[6] = chreg[4] & 0x02 ? 'S' : ' ';
		buf[7] = chreg[4] & 0x01 ? 'G' : ' ';
		buf[8] = 0;
		tr.SetAlignment(VDDisplayTextRenderer::kAlignLeft, VDDisplayTextRenderer::kVertAlignTop);
		tr.DrawTextLine(x_modes, chy_modes, buf);

		swprintf(buf, 128, L"%3.0f%%", (chreg[2] + (chreg[3] & 15)*256) * 100.0f / 4096.0f);
		tr.DrawTextLine(x_duty, chy_duty, buf);

		float midiNote = 69.0f + logf(hz + 0.0001f) * 17.312340490667560888319096172023f - 105.37631656229591524883618971458f;

		if (midiNote < 0)
			midiNote = 0;
		else if (midiNote > 140)
			midiNote = 140;

		int midiNoteInt = (int)(0.5f + midiNote);
		swprintf(buf, 128, L"%04X", freq);
		tr.DrawTextLine(x_note1, chy_note, buf);

		swprintf(buf, 128, L"%3u%+1.0f", midiNoteInt, (midiNote - midiNoteInt) * 10.0f);
		tr.DrawTextLine(x_note2, chy_note, buf);

		const int maxbarht = chanht - 2;

		int env = mpSlightSID->GetEnvelopeValue(ch);
		int ht = (env >> 4) * maxbarht / 15;

		int sustain = (chreg[6] >> 4);
		int sustainht = (sustain * maxbarht)/15;
		rdr.SetColorRGB(0x003b3b3b);
		rdr.FillRect(x_volbar, chy+chanht-1-sustainht, 2, sustainht);

		rdr.SetColorRGB(color);
		rdr.FillRect(x_volbar, chy+chanht-1-ht, 2, ht);

		int envmode = mpSlightSID->GetEnvelopeMode(ch);
		bool sustainMode = env <= sustain*17;

		int x2 = x_adsr;
		tr.SetColorRGB(envmode == 0 ? 0xFFFFFF : 0x007a4500);
		tr.DrawTextLine(x2, chy_adsr, L"A");
		x2 += mpSmallFont->MeasureString(L"A", 1, false).w;

		tr.SetColorRGB(envmode == 1 && !sustainMode ? 0xFFFFFF : 0x007a4500);
		tr.DrawTextLine(x2, chy_adsr, L"D");
		x2 += mpSmallFont->MeasureString(L"D", 1, false).w;

		tr.SetColorRGB(envmode == 1 && sustainMode ? 0xFFFFFF : 0x007a4500);
		tr.DrawTextLine(x2, chy_adsr, L"S");
		x2 += mpSmallFont->MeasureString(L"S", 1, false).w;

		tr.SetColorRGB(envmode == 2 ? 0xFFFFFF : 0x007a4500);
		tr.DrawTextLine(x2, chy_adsr, L"R");

		swprintf(buf, 128, L"%02X%02X", chreg[5], chreg[6]);
		tr.SetColorRGB(color);
		tr.DrawTextLine(x_adsr, chy_envelope, buf);
	}

	swprintf(buf, 128, L"%ls %ls %ls @ $%04X [%X] -> CH%lc%lc%lc"
		, regbase[24] & 0x10 ? L"LP" : L"  "
		, regbase[24] & 0x20 ? L"BP" : L"  "
		, regbase[24] & 0x40 ? L"HP" : L"  "
		, (regbase[21] & 7) + 8*regbase[22]
		, regbase[23] >> 4
		, regbase[23] & 0x01 ? L'1' : L' '
		, regbase[23] & 0x02 ? L'2' : L' '
		, regbase[23] & 0x04 ? L'3' : L' '
	);

	tr.SetColorRGB(0xFFFFFF);
	tr.DrawTextLine(x, y + chanht*3 + 6, buf);
}

void ATUIAudioDisplay::PaintPOKEY(IVDDisplayRenderer& rdr, VDDisplayTextRenderer& tr, sint32 w, sint32 h) {
	if (!mpAudioMonitor)
		return;

	const int fontw = mBigFontW;
	const int fonth = mBigFontH;
	const int fontsmw = mSmallFontW;
	const int fontsmh = mSmallFontH;

	ATPokeyAudioLog *log;
	ATPokeyRegisterState *rstate;

	const uint8 chanMask = mpAudioMonitor->Update(&log, &rstate);

	const uint8 audctl = rstate->mReg[8];
	const uint8 skctl = rstate->mReg[15];

	int slowRate = audctl & 0x01 ? 114 : 28;
	int divisors[4];

	const int borrowOffset12 = (skctl & 8) ? 6 : 4;

	divisors[0] = (audctl & 0x40) ? (int)rstate->mReg[0] + borrowOffset12 : ((int)rstate->mReg[0] + 1) * slowRate;

	divisors[1] = (audctl & 0x10)
		? (audctl & 0x40) ? rstate->mReg[0] + ((int)rstate->mReg[2] << 8) + borrowOffset12 + 3 : (rstate->mReg[0] + ((int)rstate->mReg[2] << 8) + 1) * slowRate
		: ((int)rstate->mReg[2] + 1) * slowRate;

	divisors[2] = (audctl & 0x20) ? (int)rstate->mReg[4] + borrowOffset12 : ((int)rstate->mReg[4] + 1) * slowRate;

	divisors[3] = (audctl & 0x08)
		? (audctl & 0x20) ? rstate->mReg[4] + ((int)rstate->mReg[6] << 8) + 7 : (rstate->mReg[4] + ((int)rstate->mReg[6] << 8) + 1) * slowRate
		: ((int)rstate->mReg[6] + 1) * slowRate;

	// layout
	const int chanht = 5 + fonth + fontsmh;

	const int x = 0;
	const int y = 0;

	const int x_link = x;
	const int x_clock = x + 1*fontsmw;
	const int x_highpass = x + 5*fontsmw + (fontsmw >> 1);
	const int x_mode = x + 7*fontsmw;
	const int x_waveform = x + std::max<int>(11*fontsmw, 8*fontw) + 4;

	const int chanw = w;

	const float hstepf = log->mLastFrameSampleCount ? (float)(chanw - x_waveform - 4) / (float)log->mLastFrameSampleCount : 0;
	const sint32 hstep = (sint32)(0.5f + hstepf * 0x10000);

	Shade(rdr, x, y, chanw, chanht * 4);

	wchar_t buf[128];
	vdfastvector<vdfloat2> fpts;
	vdfastvector<vdpoint32> pts;

	for(int ch=0; ch<4; ++ch) {
		const int chy = y + chanht*ch;
		const int chanfreqy = chy + 4;
		const int chandetaily = chy + 4 + fonth + 1;

		// draw frequency
		swprintf(buf, 128, L"%.1f", (mCyclesPerSecond * 0.5) / divisors[ch]);

		tr.SetColorRGB(chanMask & (1 << ch) ? 0xFFFFFF : 0x808080);
		tr.SetFont(mpBigFont);
		tr.SetAlignment(VDDisplayTextRenderer::kAlignRight, VDDisplayTextRenderer::kVertAlignTop);
		tr.DrawTextLine(x_waveform - 4, chanfreqy, buf);

		tr.SetAlignment(VDDisplayTextRenderer::kAlignLeft, VDDisplayTextRenderer::kVertAlignTop);
		tr.SetFont(mpSmallFont);

		// draw link/clock indicator
		if ((ch == 1 && (audctl & 0x10)) || (ch == 3 && (audctl & 0x08)))
			tr.DrawTextLine(x_link, chandetaily, L"16");
		else if ((ch == 0 && (audctl & 0x40)) || (ch == 2 && (audctl & 0x20)))
			tr.DrawTextLine(x_clock, chandetaily, L"1.79");
		else
			tr.DrawTextLine(x_clock, chandetaily, audctl & 1 ? L" 15K" : L" 64K");

		// draw high-pass indicator
		if ((ch == 0 && (audctl & 4)) || (ch == 1 && (audctl & 2)))
			tr.DrawTextLine(x_highpass, chandetaily, L"H");

		// draw mode indicator
		const uint8 ctl = rstate->mReg[ch*2 + 1];
		if (ctl & 0x10)
			tr.DrawTextLine(x_mode, chandetaily, L"V");
		else {
			buf[0] = (ctl & 0x80) ? L'L' : L'5';

			if (ch < 2)
				buf[1] = (skctl & 0x08) ? L'2' : L' ';
			else
				buf[1] = (skctl & 0x10) ? L'A' : L' ';

			buf[3] = L' ';
			buf[4] = 0;

			if (ctl & 0x20)
				buf[2] = L'T';
			else if (ctl & 0x40)
				buf[2] = L'4';
			else if (audctl & 0x80)
				buf[2] = L'9';
			else {
				buf[2] = L'1';
				buf[3] = L'7';
			}

			tr.DrawTextLine(x_mode, chandetaily, buf);
		}

		// draw volume indicator
		int vol = (ctl & 15) * (chanht - 3) / 15;

		rdr.FillRect(x_waveform, chy + chanht - 1 - vol, 1, vol);

		// draw waveform -- note that the log starts at scan 248, so we must rotate it around
		const uint32 n = log->mLastFrameSampleCount;

		if (n > 248) {
			const int waveformHeight = chanht - 3;
			const int pybase = chy + chanht - 1;
			const float waveformScale = -(float)waveformHeight / (float)log->mFullScaleValue;
			const float waveformOffset = (float)pybase + 0.5f;

			if (rdr.GetCaps().mbSupportsPolyLineF) {
				float px = (float)x_waveform + 2.0f + 0.5f;

				fpts.resize(n);
				for(uint32 pos = 0; pos < n; ++pos) {
					float py = log->mpStates[pos].mChannelOutputs[ch] * waveformScale + waveformOffset;
					fpts[pos] = vdfloat2{px, py};

					px += hstepf;
				}

				rdr.PolyLineF(fpts.data(), n - 1, true);
			} else {
				uint32 hpos = 0x8000 + ((x_waveform + 2) << 16);
				pts.resize(n);
				for(uint32 pos = 0; pos < n; ++pos) {
					int px = hpos >> 16;
					hpos += hstep;

					int py = (int)(log->mpStates[pos].mChannelOutputs[ch] * waveformScale + waveformOffset);

					pts[pos] = vdpoint32(px, py);
				}

				rdr.PolyLine(pts.data(), n - 1);
			}
		}
	}
}

///////////////////////////////////////////////////////////////////////////
class ATUIAudioScope final : public ATUIContainer {
public:
	ATUIAudioScope();

	void SetAudioMonitor(bool secondary, ATAudioMonitor *mon);

	void Update();

protected:
	void OnCreate() override;
	void OnSize() override;
	ATUIWidgetMetrics OnMeasure() override;
	void Paint(IVDDisplayRenderer& rdr, sint32 w, sint32 h) override;
	void AdjustRate(int delta);
	void UpdateRateLabel();
	void UpdateSampleCounts(int i);

	ATAudioMonitor *mpAudioMonitors[2] {};

	vdfastvector<float> mWaveforms[2];
	vdfastvector<vdpoint32> mLineData;
	vdfastvector<vdfloat2> mLineDataF;
	vdrefptr<ATUILabel> mpRateLabel;

	static constexpr float kUsPerDiv[] = {
		100.0f,
		200.0f,
		500.0f,
		1000.0f,
		2000.0f,
		5000.0f,
		10000.0f,
		20000.0f,
		50000.0f,
		100000.0f,
		200000.0f,
	};

	uint32 mRateIndex = 3;
	uint32 mSamplesRequested = 0;
	uint32 mSampleScale = 1;
};

ATUIAudioScope::ATUIAudioScope() {
	SetHitTransparent(true);
}

void ATUIAudioScope::SetAudioMonitor(bool secondary, ATAudioMonitor *mon) {
	const int i = secondary ? 1 : 0;
	if (mpAudioMonitors[i] == mon)
		return;

	if (mpAudioMonitors[i])
		mpAudioMonitors[i]->SetMixedSampleCount(0);

	mpAudioMonitors[i] = mon;

	UpdateSampleCounts(i);
}

namespace {
	template<size_t T_Step>
	static void Downsample(float * VDRESTRICT dst, const float * VDRESTRICT src, sint32 n) {
		constexpr float scale = 1.0f / (float)T_Step;

		for (sint32 j = 0; j < n; ++j) {
			float v = 0;

			// need to use a loop here to autovectorize -- fold expression won't work
			for(size_t k = 0; k < T_Step; ++k)
				v += src[k];

			*dst++ = v * scale;

			src += T_Step;
		}
	}
}

void ATUIAudioScope::Update() {
	ATPokeyAudioLog *logs[2] {};

	for(int i=0; i<2; ++i) {
		ATAudioMonitor *mon = mpAudioMonitors[i];
		if (!mon)
			continue;

		ATPokeyRegisterState *rstate;
		mon->Update(&logs[i], &rstate);

		if (logs[i]->mNumMixedSamples < logs[i]->mMaxMixedSamples)
			return;
	}

	sint32 w = GetArea().width();
	sint32 n = (sint32)mSamplesRequested;

	mSampleScale = 1;

	while(mSampleScale < 64 && n > w*2) {
		mSampleScale += mSampleScale;
		n >>= 1;
	}

	for(int i=0; i<2; ++i) {
		ATPokeyAudioLog *log = logs[i];
		if (!log)
			continue;

		auto& wf = mWaveforms[i];
		wf.resize(n);

		const float *src = log->mpMixedSamples;
		float *dst = wf.data();

		     if (mSampleScale <=  1)	Downsample< 1>(dst, src, n);
		else if (mSampleScale <=  2)	Downsample< 2>(dst, src, n);
		else if (mSampleScale <=  4)	Downsample< 4>(dst, src, n);
		else if (mSampleScale <=  8)	Downsample< 8>(dst, src, n);
		else if (mSampleScale <= 16)	Downsample<16>(dst, src, n);
		else if (mSampleScale <= 32)	Downsample<32>(dst, src, n);
		else							Downsample<64>(dst, src, n);

		log->mNumMixedSamples = 0;

		Invalidate();
	}
}

void ATUIAudioScope::OnCreate() {
	vdrefptr<ATUIContainer> tray{new ATUIContainer};
	AddChild(tray);
	tray->SetPlacement(vdrect32f(1,1,1,1), vdpoint32(0, 0), vdfloat2{1,1});

	vdrefptr<ATUIButton> button;

	button = new ATUIButton;
	tray->AddChild(button);
	button->SetText(L" < ");
	button->SetDockMode(kATUIDockMode_Right);

	button->OnPressedEvent() = [this] {
		AdjustRate(-1);
	};

	mpRateLabel = new ATUILabel;
	tray->AddChild(mpRateLabel);
	mpRateLabel->SetDockMode(kATUIDockMode_Right);
	mpRateLabel->SetFillColor(0);
	mpRateLabel->SetTextColor(0xE0E0E0);
	mpRateLabel->SetTextAlign(ATUILabel::kAlignCenter);
	mpRateLabel->SetTextVAlign(ATUILabel::kVAlignMiddle);
	mpRateLabel->SetMinSizeText(L"99999 ms/div");

	UpdateRateLabel();

	button = new ATUIButton;
	tray->AddChild(button);
	button->SetText(L" > ");
	button->SetDockMode(kATUIDockMode_Right);

	button->OnPressedEvent() = [this] {
		AdjustRate(+1);
	};
}

void ATUIAudioScope::OnSize() {
	ATUIContainer::OnSize();

	UpdateSampleCounts(0);
	UpdateSampleCounts(1);
}

ATUIWidgetMetrics ATUIAudioScope::OnMeasure() {
	ATUIWidgetMetrics m;
	m.mDesiredSize = { 512, 128 };
	return m;
}

void ATUIAudioScope::Paint(IVDDisplayRenderer& rdr, sint32 w, sint32 h) {
	if (h <= 0 || w <= 0)
		return;

	float divsPerView = 10.0f;
	float usPerDiv = kUsPerDiv[mRateIndex];
	float usPerView = usPerDiv * divsPerView;
	float secsPerView = usPerView / 1000000.0f;
	float samplesPerSec = 63920.8f;
	float samplesPerViewF = samplesPerSec * secsPerView;

	sint32 ymid = (h - 1) >> 1;
	float yscale = -(float)ymid;
	float yoffset = -yscale;
	float xscale = w / samplesPerViewF * (float)mSampleScale;
	float xoffset = xscale * 0.5f - 0.5f;

	if (rdr.GetCaps().mbSupportsAlphaBlending) {
		for(int i=1; i<10; ++i) {
			sint32 x = (w * i + 5) / 10;

			rdr.AlphaFillRect(x, 0, 1, h, 0x80808080);
		}

		rdr.AlphaFillRect(0, ymid, w, 1, 0x80808080);
	} else {
		rdr.SetColorRGB(0x80808080);
		for(int i=1; i<10; ++i) {
			sint32 x = (w * i + 5) / 10;

			rdr.FillRect(x, 0, 1, h);
		}

		rdr.FillRect(0, ymid, w, 1);
	}

	for(int i=0; i<2; ++i) {
		ATAudioMonitor *mon = mpAudioMonitors[i];
		if (!mon)
			continue;

		const vdfastvector<float>& waveform = mWaveforms[i];
		if (waveform.empty())
			continue;

		size_t n = waveform.size();

		rdr.SetColorRGB(i ? 0x008A00 : 0xFF0000);

		if (rdr.GetCaps().mbSupportsPolyLineF) {
			float xoffset2 = xoffset + 0.5f;
			float yoffset2 = yoffset + 0.5f;

			mLineDataF.resize(n);

			for(size_t j=0; j<n; ++j) {
				mLineDataF[j] = vdfloat2{(float)j * xscale + xoffset2, waveform[j] * yscale + yoffset2};
			}

			rdr.PolyLineF(mLineDataF.data(), n - 1, true);
		} else {
			mLineData.resize(n);

			for(size_t j=0; j<n; ++j) {
				mLineData[j] = vdpoint32(VDRoundToInt32((float)j * xscale + xoffset), VDRoundToInt32(waveform[j] * yscale + yoffset));
			}

			rdr.PolyLine(mLineData.data(), n - 1);
		}
	}

	ATUIContainer::Paint(rdr, w, h);
}

void ATUIAudioScope::AdjustRate(int delta) {
	mRateIndex = (uint32)std::clamp<int>((int)mRateIndex + delta, 0, (int)vdcountof(kUsPerDiv) - 1);

	UpdateSampleCounts(0);
	UpdateSampleCounts(1);
	UpdateRateLabel();
}

void ATUIAudioScope::UpdateRateLabel() {
	float usPerDiv = kUsPerDiv[mRateIndex];

	if (usPerDiv < 1000.0f)
		mpRateLabel->SetTextF(L"%.1f ms/div", usPerDiv / 1000.0f);
	else
		mpRateLabel->SetTextF(L"%.0f ms/div", usPerDiv / 1000.0f);
}

void ATUIAudioScope::UpdateSampleCounts(int i) {
	float divsPerView = 10.0f;
	float usPerDiv = kUsPerDiv[mRateIndex];
	float usPerView = usPerDiv * divsPerView;
	float secsPerView = usPerView / 1000000.0f;
	float samplesPerSec = 63920.8f;
	float samplesPerViewF = samplesPerSec * secsPerView;

	uint32 n = (uint32)VDCeilToInt(samplesPerViewF);

	mSamplesRequested = n;

	if (mpAudioMonitors[i])
		mpAudioMonitors[i]->SetMixedSampleCount(n);
}

////////////////////////////////////////////////////////////////////////////////

class ATUIAutoHideContainer final : public ATUIContainer {
public:
	static constexpr auto kTypeID = "ATUIAutoHideContainer"_vdtypeid;

	void SetTargetAnimator(ATUIWidgetOffsetAnimator *target);

	void SetOnShow(vdfunction<void()> fn);

	void LockOpen(bool lock);

	void OnCreate() override;
	void OnDestroy() override;
	void OnSize() override;
	void OnTrackCursorChanges(ATUIWidget *w) override;

private:
	void UpdateAnimatorOffsets();

	void SetContainsCursor(bool contained);
	void UpdateHideState();

	vdrefptr<ATUIWidgetOffsetAnimator> mpAutoHideAnimator;
	ATUITimerHandle mHideTimer {};

	bool mbContainsCursor = false;
	bool mbLockedOpen = false;
	bool mbHide = true;

	vdfunction<void()> mpOnShow;
};

void ATUIAutoHideContainer::SetTargetAnimator(ATUIWidgetOffsetAnimator *target) {
	mpAutoHideAnimator = target;

	UpdateAnimatorOffsets();
}

void ATUIAutoHideContainer::SetOnShow(vdfunction<void()> fn) {
	mpOnShow = std::move(fn);
}

void ATUIAutoHideContainer::LockOpen(bool lock) {
	if (mbLockedOpen != lock) {
		mbLockedOpen = lock;

		UpdateHideState();
	}
}

void ATUIAutoHideContainer::OnCreate() {
	ATUIContainer::OnCreate();

	mpManager->AddTrackingWindow(this);
}

void ATUIAutoHideContainer::OnDestroy() {
	mpManager->RemoveTrackingWindow(this);

	ATUIContainer::OnDestroy();
}

void ATUIAutoHideContainer::OnSize() {
	ATUIContainer::OnSize();

	UpdateAnimatorOffsets();
}

void ATUIAutoHideContainer::OnTrackCursorChanges(ATUIWidget *w) {
	SetContainsCursor(w != nullptr);
}

void ATUIAutoHideContainer::UpdateAnimatorOffsets() {
	if (mpAutoHideAnimator) {
		mpAutoHideAnimator->SetEndpoints(
			vdfloat2 { 0.0f, 0.0f },
			vdfloat2 { 0.0f, (float)GetClientArea().height() }
		);
	}
}

void ATUIAutoHideContainer::SetContainsCursor(bool contained) {
	if (mbContainsCursor != contained) {
		mbContainsCursor = contained;

		UpdateHideState();
	}
}

void ATUIAutoHideContainer::UpdateHideState() {
	const bool shouldHide = !mbContainsCursor && !mbLockedOpen;

	if (mbHide != shouldHide) {
		mbHide = shouldHide;

		if (shouldHide) {
			mHideTimer = StartTimer(0.2f, 0.0f,
				[this] {
					if (mpAutoHideAnimator) {
						mpAutoHideAnimator->SetForward(true);
						mpAutoHideAnimator->SetRate(2.0f);
						mpAutoHideAnimator->Start();
					}
				}
			);
		} else {
			StopTimer(mHideTimer);
			mHideTimer = {};

			if (mpAutoHideAnimator) {
				mpAutoHideAnimator->SetForward(false);
				mpAutoHideAnimator->SetRate(4.0f);
				mpAutoHideAnimator->Start();
			}

			if (mpOnShow)
				mpOnShow();
		}
	}
}

////////////////////////////////////////////////////////////////////////////////

class ATUIDefinedHeightContainer final : public ATUIContainer {
public:
	void SetDesiredHeight(sint32 h);

	ATUIWidgetMetrics OnMeasure() override;

private:
	sint32 mDesiredHeight = 0;
};

void ATUIDefinedHeightContainer::SetDesiredHeight(sint32 h) {
	if (mDesiredHeight != h) {
		mDesiredHeight = h;

		InvalidateMeasure();
	}
}

ATUIWidgetMetrics ATUIDefinedHeightContainer::OnMeasure() {
	ATUIWidgetMetrics m;
	m.mDesiredSize.h = mDesiredHeight;

	return m;
}

////////////////////////////////////////////////////////////////////////////////

class ATUIRenderer final : public vdrefcount, public IATUIRenderer, public IVDTimerCallback {
public:
	ATUIRenderer();
	~ATUIRenderer();

	int AddRef() override { return vdrefcount::AddRef(); }
	int Release() override { return vdrefcount::Release(); }

	bool IsVisible() const override;
	void SetVisible(bool visible) override;

	void SetStatusFlags(uint32 flags) override;
	void ResetStatusFlags(uint32 flags, uint32 holdTime) override;
	void PulseStatusFlags(uint32 flags) override;

	void SetCyclesPerSecond(double rate) override;

	void SetStatusCounter(uint32 index, uint32 value) override;
	void SetDiskLEDState(uint32 index, sint32 ledDisplay) override;
	void SetDiskMotorActivity(uint32 index, bool on) override;
	void SetDiskErrorState(uint32 index, bool on) override;

	void SetHActivity(bool write) override;
	void SetPCLinkActivity(bool write) override;
	void SetIDEActivity(bool write, uint32 lba) override;
	void SetCartridgeActivity(sint32 color1, sint32 color2) override;
	void SetFlashWriteActivity() override;

	void SetCassetteIndicatorVisible(bool vis) override { mbShowCassetteIndicator = vis; }
	void SetCassettePosition(float pos, float len, bool recordMode, bool fskMode) override;

	void SetRecordingPosition() override;
	void SetRecordingPositionPaused() override;
	void SetRecordingPosition(float time, sint64 size, bool paused) override;

	void SetTracingSize(sint64 size) override;

	void SetModemConnection(const char *str) override;

	void SetStatusMessage(const wchar_t *s) override;

	uint32 AllocateErrorSourceId() override;
	void ClearErrors(uint32 sourceId) override;
	void ReportError(uint32 sourceId, const wchar_t *s) override;

	void SetLedStatus(uint8 ledMask) override;
	void SetHeldButtonStatus(uint8 consolMask) override;
	void SetPendingHoldMode(bool enabled) override;
	void SetPendingHeldKey(int key) override;
	void SetPendingHeldButtons(uint8 consolMask) override;

	void ClearWatchedValue(int index) override;
	void SetWatchedValue(int index, uint32 value, WatchFormat format) override;
	void SetAudioStatus(const ATUIAudioStatus *status) override;
	void SetAudioMonitor(bool secondary, ATAudioMonitor *monitor) override;
	void SetAudioDisplayEnabled(bool secondary, bool enable) override;
	void SetAudioScopeEnabled(bool enable) override;
	void SetSlightSID(ATSlightSIDEmulator *emu) override;

	vdrect32 GetPadArea() const override;
	void SetPadInputEnabled(bool enable) override;

	void SetFpsIndicator(float fps) override;

	void SetMessage(StatusPriority priority, const wchar_t *msg) override;
	void ClearMessage(StatusPriority priority) override;

	void SetHoverTip(int px, int py, const wchar_t *text) override;

	void SetPaused(bool paused) override;

	void SetUIManager(ATUIManager *m) override;

	void Relayout(int w, int h) override;
	void Update() override;

	sint32 GetIndicatorSafeHeight() const override;

	void AddIndicatorSafeHeightChangedHandler(const vdfunction<void()> *pfn) override;
	void RemoveIndicatorSafeHeightChangedHandler(const vdfunction<void()> *pfn) override;

	void SetQuickBarEnabled(bool enabled) override;

	void BeginCustomization() override;

public:
	void TimerCallback() override;

protected:
	void InvalidateLayout();
	void RelayoutStatic();

	void UpdatePendingHoldLabel();
	void RelayoutErrors();
	void UpdateHostDeviceLabel();
	void UpdatePCLinkLabel();
	void UpdateHoverTipPos();
	void RemakeLEDFont();

	void UpdateStatusMessage();

	double	mCyclesPerSecond = 1;

	// 0..14 are disk drives, 16 is cassette
	static constexpr uint32 kValidFlagsMask = 0x17FFF;
	uint32	mStatusFlags = 0;
	uint32	mStatusHoldFlags = 0;
	uint32	mStatusHoldCounters[17] {};

	uint32	mStatusCounter[15] = {};
	sint32	mStatusLEDs[15] = {};
	uint32	mDiskMotorFlags = 0;
	uint32	mDiskErrorFlags = 0;
	float	mCassettePos = 0;
	int		mRecordingPos = -1;
	bool	mbRecordingPaused = false;
	sint64	mRecordingSize = -1;
	sint64	mTracingSize = -1;
	bool	mbShowCassetteIndicator = false;
	int		mShowCassetteIndicatorCounter = 0;

	uint32	mHardDiskLBA = 0;
	uint8	mHardDiskCounter = 0;
	bool	mbHardDiskRead = false;
	bool	mbHardDiskWrite = false;

	uint8	mHReadCounter = 0;
	uint8	mHWriteCounter = 0;
	uint8	mPCLinkReadCounter = 0;
	uint8	mPCLinkWriteCounter = 0;
	uint8	mFlashWriteCounter = 0;

	VDStringW	mMessages[(int)StatusPriority::Max + 1];
	int mMessageLevel = -1;

	uint8	mLedStatus = 0;

	sint32 mIndicatorSafeHeight = 0;
	ATNotifyList<const vdfunction<void()> *> mIndicatorSafeAreaListeners;

	uint32	mWatchedValues[8];
	WatchFormat mWatchedValueFormats[8];

	ATAudioMonitor	*mpAudioMonitors[2] = {};
	ATSlightSIDEmulator *mpSlightSID = nullptr;

	VDDisplaySubRenderCache mFpsRenderCache;
	float mFps = -1.0f;

	vdrefptr<IVDDisplayFont> mpSysFont;
	vdrefptr<IVDDisplayFont> mpSmallMonoSysFont;
	vdrefptr<IVDDisplayFont> mpSysMonoFont;
	vdrefptr<IVDDisplayFont> mpSysHoverTipFont;
	vdrefptr<IVDDisplayFont> mpSysBoldHoverTipFont;
	int mSysFontDigitWidth = 0;
	int mSysFontDigitHeight = 0;
	int mSysFontDigitAscent = 0;
	int mSysFontDigitInternalLeading = 0;
	int mSysMonoFontHeight = 0;

	sint32	mPrevLayoutWidth = 0;
	sint32	mPrevLayoutHeight = 0;

	int		mPendingHeldKey = -1;
	uint8	mPendingHeldButtons = 0;
	bool	mbPendingHoldMode = false;

	int		mLEDFontCellWidth = 0;
	int		mLEDFontCellAscent = 0;
	vdrefptr<IVDDisplayFont> mpLEDFont;

	bool	mbQuickBarEnabled = false;

	vdrefptr<ATUIContainer> mpContainer;
	vdrefptr<ATUIAutoHideContainer> mpQuickBarContainer;
	vdrefptr<ATUIQuickBarWidget> mpQuickBar;
	vdrefptr<ATUILabel> mpDiskDriveIndicatorLabels[15];
	vdrefptr<ATUILabel> mpFpsLabel;
	vdrefptr<ATUILabel> mpStatusMessageLabel;
	vdrefptr<ATUILabel> mpWatchLabels[8];
	vdrefptr<ATUILabel> mpHardDiskDeviceLabel;
	vdrefptr<ATUILabel> mpRecordingLabel;
	vdrefptr<ATUILabel> mpTracingLabel;
	vdrefptr<ATUILabel> mpFlashWriteLabel;
	vdrefptr<ATUILabel> mpHostDeviceLabel;
	vdrefptr<ATUILabel> mpPCLinkLabel;
	vdrefptr<ATUILabel> mpLedLabels[2];
	vdrefptr<ATUILabel> mpCassetteLabel;
	vdrefptr<ATUILabel> mpCassetteTimeLabel;
	vdrefptr<ATUILabel> mpPausedLabel;
	vdrefptr<ATUILabel> mpHeldButtonLabels[3];
	vdrefptr<ATUILabel> mpPendingHeldKeyLabel;
	vdrefptr<ATUIAudioStatusDisplay> mpAudioStatusDisplay;
	vdrefptr<ATUIAudioDisplay> mpAudioDisplays[2];
	vdrefptr<ATUIAudioScope> mpAudioScope;
	vdrefptr<ATUIWidget> mpPadInput;
	vdrefptr<ATUIOverlayCustomization> mpOverlayCustomization;
	vdrefptr<ATUIWidget> mpCartridgeActivityIcon1;
	vdrefptr<ATUIWidget> mpCartridgeActivityIcon2;

	struct ErrorEntry {
		vdrefptr<ATUILabel> mpLabel;
		uint32 mExpirationTime = 0;
		uint32 mSourceId = 0;
	};

	static constexpr size_t kMaxErrors = 10;
	vdvector<ErrorEntry> mErrors;
	uint32 mErrorSourceCounter;

	vdrefptr<ATUILabel> mpHoverTip;
	int mHoverTipX = 0;
	int mHoverTipY = 0;

	VDLazyTimer mStatusTimer;

	static const uint32 kDiskColors[8][2];

	static constexpr char kTagAudioDisplay[] = "audio_display";
	static constexpr char kTagAudioDisplay2[] = "audio_display_2";
	static constexpr char kTagAudioScope[] = "audio_scope";
	static constexpr char kTagPadInput[] = "pad_input";
};

constexpr uint32 ATUIRenderer::kDiskColors[8][2]={
	{ 0x91a100, 0xffff67 },
	{ 0xd37040, 0xffe7b7 },
	{ 0xd454cf, 0xffcbff },
	{ 0x9266ff, 0xffddff },
	{ 0x4796ec, 0xbeffff },
	{ 0x35ba61, 0xacffd8 },
	{ 0x6cb200, 0xe3ff6f },
	{ 0xbb860e, 0xfffd85 },
};

void ATCreateUIRenderer(IATUIRenderer **r) {
	*r = new ATUIRenderer;
	(*r)->AddRef();
}

ATUIRenderer::ATUIRenderer() {
	for(int i=0; i<15; ++i) {
		mStatusCounter[i] = i+1;
		mStatusLEDs[i] = -1;
	}

	for(auto& v : mWatchedValueFormats)
		v = WatchFormat::None;

	mpContainer = new ATUIContainer;
	mpContainer->SetPlacement(vdrect32f(0, 0, 1, 1), vdpoint32(0, 0), vdfloat2{0, 0});
	mpContainer->SetSizeOffset(vdsize32(0, 0));
	mpContainer->SetHitTransparent(true);

	vdrefptr<ATUIWidgetOffsetAnimator> autoHideAnim(new ATUIWidgetOffsetAnimator);

	mpQuickBar = new ATUIQuickBarWidget;
	mpQuickBar->SetAutoSize();
	mpQuickBar->AddAnimator(*autoHideAnim);

	mpQuickBarContainer = new ATUIAutoHideContainer;
	mpQuickBarContainer->SetVisible(mbQuickBarEnabled);
	mpQuickBarContainer->SetDockMode(kATUIDockMode_None);
	mpQuickBarContainer->SetPlacement(vdrect32f(1, 1, 1, 1), vdpoint32(0, 0), vdfloat2(1, 1));
	mpQuickBarContainer->SetAutoSize();
	mpQuickBarContainer->SetTargetAnimator(autoHideAnim);
	mpQuickBarContainer->SetOnShow(
		[this] {
			if (mpQuickBar && mpQuickBar->GetManager())
				mpQuickBar->ForceRefresh();
		}
	);

	mpQuickBar->SetOnSubMenuChange(
		[this](bool open) {
			if (mpQuickBarContainer && mpQuickBarContainer->GetManager())
				mpQuickBarContainer->LockOpen(open);
		}
	);

	autoHideAnim->SetProgress(1.0f);

	VDPixmapBuffer iconBuffer;

	ATUILoadCommandIcon("submenu", iconBuffer);
	mpQuickBar->SetSubmenuOverlay(iconBuffer);

	ATUILoadCommandIcon("configure", iconBuffer);
	mpQuickBar->AddCommand("System.Configure", nullptr, &iconBuffer);
	mpQuickBar->AddItem(1);

	mpQuickBar->BeginSubMenu();

	ATUILoadCommandIcon("artifacting_off", iconBuffer);
	mpQuickBar->AddCommand("Video.ArtifactingNone", nullptr, &iconBuffer);
	ATUILoadCommandIcon("artifacting_lo", iconBuffer);
	mpQuickBar->AddCommand("Video.ArtifactingAuto", nullptr, &iconBuffer);
	ATUILoadCommandIcon("artifacting_hi", iconBuffer);
	mpQuickBar->AddCommand("Video.ArtifactingAutoHi", nullptr, &iconBuffer);
	mpQuickBar->AddItem(3);

	ATUILoadCommandIcon("view_blend", iconBuffer);
	mpQuickBar->AddCommand("Video.ToggleFrameBlending", nullptr, &iconBuffer);
	mpQuickBar->AddItem(1);
	ATUILoadCommandIcon("view_scanlines", iconBuffer);
	mpQuickBar->AddCommand("Video.ToggleScanlines", nullptr, &iconBuffer);
	mpQuickBar->AddItem(1);

	ATUILoadCommandIcon("overscan_os", iconBuffer);
	mpQuickBar->AddCommand("View.OverscanOSScreen", nullptr, &iconBuffer);
	ATUILoadCommandIcon("overscan_normal", iconBuffer);
	mpQuickBar->AddCommand("View.OverscanNormal", nullptr, &iconBuffer);
	ATUILoadCommandIcon("overscan_wide", iconBuffer);
	mpQuickBar->AddCommand("View.OverscanWidescreen", nullptr, &iconBuffer);
	ATUILoadCommandIcon("overscan_ext", iconBuffer);
	mpQuickBar->AddCommand("View.OverscanExtended", nullptr, &iconBuffer);
	ATUILoadCommandIcon("overscan_full", iconBuffer);
	mpQuickBar->AddCommand("View.OverscanFull", nullptr, &iconBuffer);
	mpQuickBar->AddItem(5);

	mpQuickBar->AddSeparator();

	ATUILoadCommandIcon("view_fullscreen", iconBuffer);
	mpQuickBar->AddCommand("View.ToggleFullScreen", nullptr, &iconBuffer);
	mpQuickBar->AddItem(1);

	ATUILoadCommandIcon("view", iconBuffer);
	mpQuickBar->EndSubMenu(nullptr, &iconBuffer);

	mpQuickBar->AddSeparator();

	ATUILoadCommandIcon("image_boot", iconBuffer);
	mpQuickBar->AddCommand("File.BootImage", nullptr, &iconBuffer);
	mpQuickBar->AddItem(1);

	ATUILoadCommandIcon("image_open", iconBuffer);
	mpQuickBar->AddCommand("File.OpenImage", nullptr, &iconBuffer);
	mpQuickBar->AddItem(1);

	mpQuickBar->AddSeparator();

	ATUILoadCommandIcon("speed_normal", iconBuffer);
	mpQuickBar->AddCommand("System.ToggleWarpSpeed-", nullptr, &iconBuffer);
	ATUILoadCommandIcon("speed_warp", iconBuffer);
	mpQuickBar->AddCommand("System.ToggleWarpSpeed+", nullptr, &iconBuffer);
	mpQuickBar->AddItem(2);

	ATUILoadCommandIcon("speed_pause", iconBuffer);
	mpQuickBar->AddCommand("System.TogglePause", nullptr, &iconBuffer);
	mpQuickBar->AddItem(1);

	ATUILoadCommandIcon("speed_slow", iconBuffer);
	mpQuickBar->AddCommand("System.ToggleSlowMotion", nullptr, &iconBuffer);
	mpQuickBar->AddItem(1);

	mpQuickBar->AddSeparator();

	ATUILoadCommandIcon("cold_reset", iconBuffer);
	mpQuickBar->AddCommand("System.ColdReset", nullptr, &iconBuffer);
	mpQuickBar->AddItem(1);

	ATUILoadCommandIcon("warm_reset", iconBuffer);
	mpQuickBar->AddCommand("System.WarmReset", nullptr, &iconBuffer);
	mpQuickBar->AddItem(1);

	mpQuickBar->AddSeparator();

	ATUILoadCommandIcon("controller_none", iconBuffer);
	mpQuickBar->AddCommand("Input.SelectQuickMapNone", nullptr, &iconBuffer);

	ATUILoadCommandIcon("controller_joystick", iconBuffer);
	mpQuickBar->AddCommand("Input.SelectQuickMapJoystick", nullptr, &iconBuffer);

	ATUILoadCommandIcon("controller_paddles", iconBuffer);
	mpQuickBar->AddCommand("Input.SelectQuickMapPaddle", nullptr, &iconBuffer);
	mpQuickBar->AddItem(3);

	mpQuickBar->AddSeparator();

	ATUILoadCommandIcon("sio_c_patch", iconBuffer);
	mpQuickBar->AddCommand("Cassette.ToggleSIOPatch", nullptr, &iconBuffer);
	mpQuickBar->AddItem(1);

	ATUILoadCommandIcon("sio_d_patch", iconBuffer);
	mpQuickBar->AddCommand("Disk.ToggleSIOPatch", nullptr, &iconBuffer);
	mpQuickBar->AddItem(1);

	mpQuickBar->AddSeparator();

	ATUILoadCommandIcon("basic", iconBuffer);
	mpQuickBar->AddCommand("System.ToggleBASIC", nullptr, &iconBuffer);
	mpQuickBar->AddItem(1);

	ATUILoadCommandIcon("60hz", iconBuffer);
	mpQuickBar->AddCommand("Video.StandardNTSC", nullptr, &iconBuffer);
	ATUILoadCommandIcon("50hz", iconBuffer);
	mpQuickBar->AddCommand("Video.StandardPAL", nullptr, &iconBuffer);
	mpQuickBar->AddItem(2);

	mpQuickBar->FinalizeItems();

	for(int i=0; i<15; ++i) {
		ATUILabel *label = new ATUILabel;

		mpDiskDriveIndicatorLabels[i] = label;

		label->SetTextColor(0);
		label->SetVisible(false);
		label->SetTextOffset(2, 1);
	}

	for(int i=0; i<8; ++i) {
		ATUILabel *label = new ATUILabel;

		mpWatchLabels[i] = label;

		label->SetFillColor(0);
		label->SetTextColor(0xFFFFFF);
		label->SetVisible(false);
		label->SetTextOffset(2, 1);
	}

	mpStatusMessageLabel = new ATUILabel;
	mpStatusMessageLabel->SetVisible(false);
	mpStatusMessageLabel->SetTextOffset(6, 2);

	mpFpsLabel = new ATUILabel;
	mpFpsLabel->SetVisible(false);
	mpFpsLabel->SetTextColor(0xFFFFFF);
	mpFpsLabel->SetFillColor(0);
	mpFpsLabel->SetTextOffset(2, 0);

	mpAudioStatusDisplay = new ATUIAudioStatusDisplay;
	mpAudioStatusDisplay->SetVisible(false);
	mpAudioStatusDisplay->SetAlphaFillColor(0x80000000);

	for(auto& disp : mpAudioDisplays) {
		if (disp)
			disp->SetSmallFont(mpSmallMonoSysFont);
	}

	mpHardDiskDeviceLabel = new ATUILabel;
	mpHardDiskDeviceLabel->SetVisible(false);
	mpHardDiskDeviceLabel->SetFillColor(0x91d81d);
	mpHardDiskDeviceLabel->SetTextColor(0);

	mpRecordingLabel = new ATUILabel;
	mpRecordingLabel->SetVisible(false);
	mpRecordingLabel->SetFillColor(0x932f00);
	mpRecordingLabel->SetTextColor(0xffffff);

	mpTracingLabel = new ATUILabel;
	mpTracingLabel->SetVisible(false);
	mpTracingLabel->SetFillColor(0x932f00);
	mpTracingLabel->SetTextColor(0xffffff);

	mpFlashWriteLabel = new ATUILabel;
	mpFlashWriteLabel->SetVisible(false);
	mpFlashWriteLabel->SetFillColor(0xd37040);
	mpFlashWriteLabel->SetTextColor(0x000000);
	mpFlashWriteLabel->SetText(L"F");

	for(int i=0; i<2; ++i) {
		ATUILabel *label = new ATUILabel;

		mpLedLabels[i] = label;

		label->SetVisible(false);
		label->SetFillColor(0xffffff);
		label->SetTextColor(0xdd5d87);
		label->SetText(i ? L"2" : L"1");
	}

	mpPCLinkLabel = new ATUILabel;
	mpPCLinkLabel->SetVisible(false);
	mpPCLinkLabel->SetFillColor(0x007920);
	mpPCLinkLabel->SetTextColor(0x000000);

	mpHostDeviceLabel = new ATUILabel;
	mpHostDeviceLabel->SetVisible(false);
	mpHostDeviceLabel->SetFillColor(0x007920);
	mpHostDeviceLabel->SetTextColor(0x000000);

	mpCassetteLabel = new ATUILabel;
	mpCassetteLabel->SetVisible(false);
	mpCassetteLabel->SetFillColor(0x93e1ff);
	mpCassetteLabel->SetTextColor(0);
	mpCassetteLabel->SetText(L"C");

	mpCassetteTimeLabel = new ATUILabel;
	mpCassetteTimeLabel->SetVisible(false);
	mpCassetteTimeLabel->SetFillColor(0);

	mpPausedLabel = new ATUILabel;
	mpPausedLabel->SetVisible(false);
	mpPausedLabel->SetFont(mpSysFont);
	mpPausedLabel->SetTextOffset(4, 2);
	mpPausedLabel->SetTextColor(0xffffff);
	mpPausedLabel->SetFillColor(0x404040);
	mpPausedLabel->SetBorderColor(0xffffff);
	mpPausedLabel->SetText(L"Paused");

	mpHoverTip = new ATUILabel;
	mpHoverTip->SetVisible(false);
	mpHoverTip->SetTextColor(0);
	mpHoverTip->SetFillColor(0xffffe1);
	mpHoverTip->SetBorderColor(0);
	mpHoverTip->SetTextOffset(4, 4);

	static const wchar_t *const kHeldButtonLabels[]={
		L"Start",
		L"Select",
		L"Option",
	};

	VDASSERTCT(vdcountof(kHeldButtonLabels) == vdcountof(mpHeldButtonLabels));

	for(int i=0; i<(int)vdcountof(mpHeldButtonLabels); ++i) {
		mpHeldButtonLabels[i] = new ATUILabel;
		mpHeldButtonLabels[i]->SetVisible(false);
		mpHeldButtonLabels[i]->SetTextColor(0);
		mpHeldButtonLabels[i]->SetFillColor(0xd4d080);
		mpHeldButtonLabels[i]->SetText(kHeldButtonLabels[i]);
	}

	mpPendingHeldKeyLabel = new ATUILabel;
	mpPendingHeldKeyLabel->SetVisible(false);
	mpPendingHeldKeyLabel->SetBorderColor(0xffffff);
	mpPendingHeldKeyLabel->SetTextOffset(2, 2);
	mpPendingHeldKeyLabel->SetTextColor(0xffffff);
	mpPendingHeldKeyLabel->SetFillColor(0xa44050);

	mpOverlayCustomization = new ATUIOverlayCustomization;
	mpOverlayCustomization->Init();
	mpOverlayCustomization->SetVisible(false);
	mpOverlayCustomization->SetSizeOffset(vdsize32(0,0));

	mpCartridgeActivityIcon1 = new ATUIImage;
	mpCartridgeActivityIcon1->SetVisible(false);
	mpCartridgeActivityIcon2 = new ATUIImage;
	mpCartridgeActivityIcon2->SetVisible(false);

	RelayoutStatic();
}

ATUIRenderer::~ATUIRenderer() {
	if (mpOverlayCustomization)
		mpOverlayCustomization->Shutdown();
}

bool ATUIRenderer::IsVisible() const {
	return mpContainer->IsVisible();
}

void ATUIRenderer::SetVisible(bool visible) {
	mpContainer->SetVisible(visible);
}

void ATUIRenderer::SetStatusFlags(uint32 flags) {
	mStatusFlags |= flags;
}

void ATUIRenderer::ResetStatusFlags(uint32 flags, uint32 holdTime) {
	flags &= kValidFlagsMask;

	if (!flags)
		return;

	mStatusFlags &= ~flags;

	if (holdTime)
		mStatusHoldFlags |= flags;
	else
		mStatusHoldFlags &= ~flags;

	while(flags) {
		int idx = VDFindLowestSetBitFast(flags);

		mStatusHoldCounters[idx] = holdTime;

		flags &= (flags - 1);
	}
}

void ATUIRenderer::PulseStatusFlags(uint32 flags) {
	SetStatusFlags(flags);
	ResetStatusFlags(flags, 1);
}

void ATUIRenderer::SetCyclesPerSecond(double rate) {
	mCyclesPerSecond = rate;

	for(ATUIAudioDisplay *disp : mpAudioDisplays) {
		if (disp)
			disp->SetCyclesPerSecond(rate);
	}
}

void ATUIRenderer::SetStatusCounter(uint32 index, uint32 value) {
	mStatusCounter[index] = value;
}

void ATUIRenderer::SetDiskLEDState(uint32 index, sint32 ledDisplay) {
	mStatusLEDs[index] = ledDisplay;
}

void ATUIRenderer::SetDiskMotorActivity(uint32 index, bool on) {
	if (on)
		mDiskMotorFlags |= (1 << index);
	else
		mDiskMotorFlags &= ~(1 << index);
}

void ATUIRenderer::SetDiskErrorState(uint32 index, bool on) {
	if (on)
		mDiskErrorFlags |= (1 << index);
	else
		mDiskErrorFlags &= ~(1 << index);
}

void ATUIRenderer::SetHActivity(bool write) {
	bool update = false;

	if (write) {
		if (mHWriteCounter < 25)
			update = true;

		mHWriteCounter = 30;
	} else {
		if (mHReadCounter < 25)
			update = true;

		mHReadCounter = 30;
	}

	if (update)
		UpdateHostDeviceLabel();
}

void ATUIRenderer::SetPCLinkActivity(bool write) {
	bool update = false;

	if (write) {
		if (mPCLinkWriteCounter < 25)
			update = true;

		mPCLinkWriteCounter = 30;
	} else {
		if (mPCLinkReadCounter < 25)
			update = true;

		mPCLinkReadCounter = 30;
	}

	if (update)
		UpdatePCLinkLabel();
}

void ATUIRenderer::SetIDEActivity(bool write, uint32 lba) {
	if (mHardDiskLBA != lba) {
		mbHardDiskWrite = false;
		mbHardDiskRead = false;
	}

	mHardDiskCounter = 3;

	if (write)
		mbHardDiskWrite = true;
	else
		mbHardDiskRead = true;

	mHardDiskLBA = lba;

	mpHardDiskDeviceLabel->SetVisible(true);
	mpHardDiskDeviceLabel->SetTextF(L"%lc%u", mbHardDiskWrite ? L'W' : L'R', mHardDiskLBA);
}

void ATUIRenderer::SetCartridgeActivity(sint32 color1, sint32 color2) {
	mpCartridgeActivityIcon1->SetVisible(color1 >= 0);
	mpCartridgeActivityIcon2->SetVisible(color2 >= 0);

	if (color1 >= 0)
		mpCartridgeActivityIcon1->SetFillColor((uint32)color1);

	if (color2 >= 0)
		mpCartridgeActivityIcon2->SetFillColor((uint32)color2);
}

void ATUIRenderer::SetFlashWriteActivity() {
	mFlashWriteCounter = 20;

	mpFlashWriteLabel->SetVisible(true);
}

namespace {
	const uint32 kErrorMessageBkColor = 0x4a0500;
	const uint32 kErrorMessageFgColor = 0xffc080;
}

void ATUIRenderer::SetModemConnection(const char *str) {
	if (str && *str)
		SetMessage(StatusPriority::Modem, VDTextAToW(str).c_str());
	else
		ClearMessage(StatusPriority::Modem);
}

void ATUIRenderer::SetStatusMessage(const wchar_t *s) {
	mStatusTimer.SetOneShot(this, 1500);

	SetMessage(StatusPriority::Status, s);
}

uint32 ATUIRenderer::AllocateErrorSourceId() {
	if (!++mErrorSourceCounter)
		++mErrorSourceCounter;

	return mErrorSourceCounter;
}

void ATUIRenderer::ClearErrors(uint32 sourceId) {
	if (!sourceId)
		return;

	auto it = std::remove_if(
		mErrors.begin(),
		mErrors.end(),
		[sourceId](ErrorEntry& ee) {
			if (ee.mSourceId != sourceId)
				return false;

			if (ee.mpLabel) {
				ee.mpLabel->Destroy();
				ee.mpLabel = nullptr;
			}

			return true;
		}
	);

	if (it != mErrors.end()) {
		mErrors.erase(it, mErrors.end());

		RelayoutErrors();
	}
}

void ATUIRenderer::ReportError(uint32 sourceId, const wchar_t *s) {
	if (mErrors.size() >= kMaxErrors) {
		mErrors.front().mpLabel->Destroy();
		mErrors.erase(mErrors.begin());
	}

	vdrefptr<ATUILabel> label(new ATUILabel);
	mpContainer->AddChild(label);
	label->SetFont(mpSysFont);
	label->SetVisible(true);
	label->SetFillColor(kErrorMessageBkColor);
	label->SetTextColor(kErrorMessageFgColor);
	label->SetBorderColor(kErrorMessageFgColor);
	label->SetTextOffset(6, 2);
	label->SetText(s);
	label->AutoSize();

	ErrorEntry& ee = mErrors.emplace_back();
	ee.mpLabel = std::move(label);
	ee.mSourceId = sourceId;

	const uint32 kErrorTimeout = 8000;
	ee.mExpirationTime = VDGetCurrentTick() + kErrorTimeout;

	RelayoutErrors();
}

void ATUIRenderer::SetRecordingPosition() {
	mRecordingPos = -1;
	mRecordingSize = -1;
	mpRecordingLabel->SetVisible(false);
}

void ATUIRenderer::SetRecordingPositionPaused() {
	SetRecordingPosition(
		mRecordingPos < 0 ? 0 : mRecordingPos,
		mRecordingSize < 0 ? 0 : mRecordingSize,
		true
	);
}

void ATUIRenderer::SetRecordingPosition(float time, sint64 size, bool paused) {
	int cpos = VDRoundToInt(time);
	uint32 csize = (uint32)((size * 10) >> 10);
	bool usemb = false;

	if (csize >= 10240) {
		csize &= 0xFFFFFC00;
		usemb = true;
	} else {
		csize -= csize % 10;
	}

	if (mRecordingPos == cpos && mRecordingSize == csize && mbRecordingPaused == paused)
		return;

	mRecordingPos = cpos;
	mRecordingSize = csize;
	mbRecordingPaused = paused;

	int secs = cpos % 60;
	int mins = cpos / 60;
	int hours = mins / 60;
	mins %= 60;

	const wchar_t *desc = paused ? L"Paused " : L"R";

	if (usemb)
		mpRecordingLabel->SetTextF(L"%ls%02u:%02u:%02u (%.1fM)", desc, hours, mins, secs, (float)csize / 10240.0f);
	else
		mpRecordingLabel->SetTextF(L"%ls%02u:%02u:%02u (%uK)", desc, hours, mins, secs, csize / 10);

	mpRecordingLabel->SetVisible(true);
}

void ATUIRenderer::SetTracingSize(sint64 size) {
	if (mTracingSize != size) {
		mpTracingLabel->SetVisible(size >= 0);

		if (size >= 0) {
			if ((mTracingSize ^ size) >> 18) {
				mpTracingLabel->SetTextF(L"Tracing %.1fM", (double)size / 1048576.0);
			}
		}

		mTracingSize = size;
	}
}

void ATUIRenderer::SetLedStatus(uint8 ledMask) {
	if (mLedStatus == ledMask)
		return;

	mLedStatus = ledMask;

	mpLedLabels[0]->SetVisible((ledMask & 1) != 0);
	mpLedLabels[1]->SetVisible((ledMask & 2) != 0);
}

void ATUIRenderer::SetHeldButtonStatus(uint8 consolMask) {
	for(int i=0; i<(int)vdcountof(mpHeldButtonLabels); ++i)
		mpHeldButtonLabels[i]->SetVisible((consolMask & (1 << i)) != 0);
}

void ATUIRenderer::SetPendingHoldMode(bool enable) {
	if (mbPendingHoldMode != enable) {
		mbPendingHoldMode = enable;

		UpdatePendingHoldLabel();
	}
}

void ATUIRenderer::SetPendingHeldKey(int key) {
	if (mPendingHeldKey != key) {
		mPendingHeldKey = key;

		UpdatePendingHoldLabel();
	}
}

void ATUIRenderer::SetPendingHeldButtons(uint8 consolMask) {
	if (mPendingHeldButtons != consolMask) {
		mPendingHeldButtons = consolMask;

		UpdatePendingHoldLabel();
	}
}

void ATUIRenderer::SetCassettePosition(float pos, float len, bool recordMode, bool fskMode) {
	mpCassetteTimeLabel->SetTextColor(recordMode ? 0xff8040 : 0x93e1ff);

	if (mCassettePos == pos)
		return;

	mCassettePos = pos;

	int cpos = VDRoundToInt(mCassettePos);

	int secs = cpos % 60;
	int mins = cpos / 60;
	int hours = mins / 60;
	mins %= 60;

	const float frac = len > 0.01f ? pos / len : 0.0f;

	mpCassetteTimeLabel->SetTextF(L"%02u:%02u:%02u [%d%%] %ls%ls", hours, mins, secs, (int)(frac * 100.0f), fskMode ? L"" : L"T-", recordMode ? L"REC" : L"Play");
}

void ATUIRenderer::ClearWatchedValue(int index) {
	if ((unsigned)index >= 8)
		return;

	mWatchedValueFormats[index] = WatchFormat::None;
	mpWatchLabels[index]->SetVisible(false);
}

void ATUIRenderer::SetWatchedValue(int index, uint32 value, WatchFormat format) {
	if ((unsigned)index >= 8)
		return;

	bool changed = false;

	if (mWatchedValueFormats[index] != format) {
		mWatchedValueFormats[index] = format;
		mpWatchLabels[index]->SetVisible(true);
		changed = true;
	}

	if (mWatchedValues[index] != value) {
		mWatchedValues[index] = value;
		changed = true;
	}

	if (changed) {
		// draw watched values
		ATUILabel& label = *mpWatchLabels[index];

		switch(format) {
			case WatchFormat::Dec:
				label.SetTextF(L"%d", (int)value);
				break;

			case WatchFormat::Hex8:
				label.SetTextF(L"%02X", value);
				break;

			case WatchFormat::Hex16:
				label.SetTextF(L"%04X", value);
				break;

			case WatchFormat::Hex32:
				label.SetTextF(L"%08X", value);
				break;
		}
	}
}

void ATUIRenderer::SetAudioStatus(const ATUIAudioStatus *status) {
	if (status) {
		mpAudioStatusDisplay->Update(*status);
		mpAudioStatusDisplay->SetVisible(true);
	} else {
		mpAudioStatusDisplay->SetVisible(false);
	}
}

void ATUIRenderer::SetAudioMonitor(bool secondary, ATAudioMonitor *monitor) {
	mpAudioMonitors[secondary] = monitor;

	if (mpAudioDisplays[secondary])
		mpAudioDisplays[secondary]->SetAudioMonitor(monitor);

	if (mpAudioScope)
		mpAudioScope->SetAudioMonitor(secondary, monitor);
}

void ATUIRenderer::SetAudioDisplayEnabled(bool secondary, bool enable) {
	ATUIAudioDisplay *disp = mpAudioDisplays[secondary];

	if (enable) {
		if (!disp) {
			disp = new ATUIAudioDisplay;
			mpAudioDisplays[secondary] = disp;
			mpContainer->AddChild(disp);
			disp->SetCyclesPerSecond(mCyclesPerSecond);
			disp->SetAlphaFillColor(0x80000000);
			disp->SetBigFont(mpSysMonoFont);
			disp->SetSmallFont(mpSmallMonoSysFont);

			if (!secondary)
				disp->SetSlightSID(mpSlightSID);

			disp->SetAudioMonitor(mpAudioMonitors[secondary]);

			mpOverlayCustomization->BindCustomizableWidget(secondary ? kTagAudioDisplay2 : kTagAudioDisplay, disp);
		}
	} else {
		if (disp) {
			mpOverlayCustomization->BindCustomizableWidget(secondary ? kTagAudioDisplay2 : kTagAudioDisplay, nullptr);
			disp->SetAudioMonitor(nullptr);
			disp->Destroy();
			mpAudioDisplays[secondary] = nullptr;
		}
	}
}

void ATUIRenderer::SetAudioScopeEnabled(bool enable) {
	if (enable) {
		if (mpAudioScope)
			return;

		mpAudioScope = new ATUIAudioScope;
		mpContainer->AddChild(mpAudioScope);
		mpAudioScope->SetAlphaFillColor(0xC0000000);
		mpAudioScope->SetAudioMonitor(false, mpAudioMonitors[0]);
		mpAudioScope->SetAudioMonitor(true, mpAudioMonitors[1]);

		mpOverlayCustomization->BindCustomizableWidget(kTagAudioScope, mpAudioScope);
	} else {
		if (!mpAudioScope)
			return;

		mpOverlayCustomization->BindCustomizableWidget(kTagAudioScope, nullptr);
		mpAudioScope->SetAudioMonitor(false, nullptr);
		mpAudioScope->SetAudioMonitor(true, nullptr);
		mpAudioScope->Destroy();
		mpAudioScope = nullptr;
	}
}

void ATUIRenderer::SetSlightSID(ATSlightSIDEmulator *emu) {
	mpSlightSID = emu;

	if (mpAudioDisplays[0])
		mpAudioDisplays[0]->SetSlightSID(emu);
	InvalidateLayout();
}

vdrect32 ATUIRenderer::GetPadArea() const {
	if (mpPadInput)
		return mpPadInput->GetArea();
	else
		return vdrect32();
}

void ATUIRenderer::SetPadInputEnabled(bool enable) {
	if (enable) {
		if (mpPadInput)
			return;

		class PadInputWidget final : public ATUIWidget {
		public:
			void Paint(IVDDisplayRenderer& r, sint32 w, sint32 h) {}
		};

		mpPadInput = new PadInputWidget;
		mpContainer->AddChild(mpPadInput);
		mpPadInput->SetHitTransparent(true);
		mpPadInput->SetAlphaFillColor(0);
		mpOverlayCustomization->BindCustomizableWidget(kTagPadInput, mpPadInput);
	} else {
		if (!mpPadInput)
			return;

		mpOverlayCustomization->BindCustomizableWidget(kTagPadInput, nullptr);
		mpPadInput->Destroy();
		mpPadInput = nullptr;
	}
}

void ATUIRenderer::SetFpsIndicator(float fps) {
	if (mFps != fps) {
		mFps = fps;

		if (fps < 0) {
			mpFpsLabel->SetVisible(false);
		} else {
			mpFpsLabel->SetVisible(true);
			mpFpsLabel->SetTextF(L"%.3f fps", fps);
		}
	}
}

void ATUIRenderer::SetMessage(StatusPriority priority, const wchar_t *msg) {
	if (!msg || !*msg)
		ClearMessage(priority);

	mMessages[(int)priority] = msg;

	if (mMessageLevel <= (int)priority)
		UpdateStatusMessage();
}

void ATUIRenderer::ClearMessage(StatusPriority priority) {
	int priIndex = (int)priority;

	if (!mMessages[priIndex].empty()) {
		mMessages[priIndex].clear();

		if (mMessageLevel == priIndex)
			UpdateStatusMessage();
	}
}

void ATUIRenderer::SetHoverTip(int px, int py, const wchar_t *text) {
	if (!text || !*text) {
		mpHoverTip->SetVisible(false);
	} else {
		mHoverTipX = px;
		mHoverTipY = py;

		mpHoverTip->SetHTMLText(text);
		mpHoverTip->AutoSize();

		mpHoverTip->SetVisible(true);
		UpdateHoverTipPos();
	}
}

void ATUIRenderer::SetPaused(bool paused) {
	mpPausedLabel->SetVisible(paused);
}

void ATUIRenderer::SetUIManager(ATUIManager *m) {
	if (m) {
		m->GetMainWindow()->AddChild(mpContainer);

		ATUIContainer *c = mpContainer;

		for(int i = 14; i >= 0; --i)
			c->AddChild(mpDiskDriveIndicatorLabels[i]);

		c->AddChild(mpFpsLabel);
		c->AddChild(mpCassetteLabel);
		c->AddChild(mpCassetteTimeLabel);

		for(int i=0; i<2; ++i)
			c->AddChild(mpLedLabels[i]);

		for(auto&& p : mpHeldButtonLabels)
			c->AddChild(p);

		c->AddChild(mpPendingHeldKeyLabel);

		c->AddChild(mpHostDeviceLabel);
		c->AddChild(mpPCLinkLabel);
		c->AddChild(mpRecordingLabel);
		c->AddChild(mpTracingLabel);
		c->AddChild(mpHardDiskDeviceLabel);
		c->AddChild(mpFlashWriteLabel);
		c->AddChild(mpStatusMessageLabel);

		for(int i=0; i<8; ++i)
			c->AddChild(mpWatchLabels[i]);

		c->AddChild(mpAudioStatusDisplay);
		c->AddChild(mpPausedLabel);
		c->AddChild(mpHoverTip);

		m->GetMainWindow()->AddChild(mpOverlayCustomization);
		mpOverlayCustomization->AddCustomizableWidget(kTagAudioDisplay, mpAudioDisplays[0], L"Audio display (left/mono channel)");
		mpOverlayCustomization->AddCustomizableWidget(kTagAudioDisplay2, mpAudioDisplays[1], L"Audio display (right channel)");
		mpOverlayCustomization->AddCustomizableWidget(kTagAudioScope, mpAudioScope, L"Audio scope");
		mpOverlayCustomization->AddCustomizableWidget(kTagPadInput, mpPadInput, L"Pad input");
		mpOverlayCustomization->SetPlacement(vdrect32f(0, 0, 1, 1), vdpoint32(0, 0), vdfloat2{0, 0});

		// update fonts
		mpSysFont = m->GetThemeFont(kATUIThemeFont_Header);
		mpSmallMonoSysFont = m->GetThemeFont(kATUIThemeFont_MonoSmall);
		mpSysMonoFont = m->GetThemeFont(kATUIThemeFont_Mono);
		mpSysHoverTipFont = m->GetThemeFont(kATUIThemeFont_Tooltip);
		mpSysBoldHoverTipFont = m->GetThemeFont(kATUIThemeFont_TooltipBold);

		if (mpSysFont) {
			vdsize32 digitSize = mpSysFont->MeasureString(L"0123456789", 10, false);
			mSysFontDigitWidth = digitSize.w / 10;
			mSysFontDigitHeight = digitSize.h;

			VDDisplayFontMetrics sysFontMetrics;
			mpSysFont->GetMetrics(sysFontMetrics);
			mSysFontDigitAscent = sysFontMetrics.mAscent;
		}

		mSysMonoFontHeight = 0;

		if (mpSysMonoFont) {
			VDDisplayFontMetrics metrics;
			mpSysMonoFont->GetMetrics(metrics);

			mSysMonoFontHeight = metrics.mAscent + metrics.mDescent;
		}

		RemakeLEDFont();

		for(int i=0; i<15; ++i) {
			mpDiskDriveIndicatorLabels[i]->SetFont(mpSysFont);
			mpDiskDriveIndicatorLabels[i]->SetBoldFont(mpLEDFont);
		}

		for(int i=0; i<8; ++i)
			mpWatchLabels[i]->SetFont(mpSysFont);

		mpStatusMessageLabel->SetFont(mpSysFont);

		for(const ErrorEntry& ee : mErrors) {
			ee.mpLabel->SetFont(mpSysFont);
			ee.mpLabel->AutoSize();
		}

		mpFpsLabel->SetFont(mpSysFont);

		c->AddChild(mpQuickBarContainer);
		mpQuickBarContainer->AddChild(mpQuickBar);

		mpAudioStatusDisplay->SetFont(mpSysFont);
		mpAudioStatusDisplay->AutoSize();

		for(ATUIAudioDisplay *disp : mpAudioDisplays) {
			if (disp) {
				disp->SetBigFont(mpSysMonoFont);
				disp->SetSmallFont(mpSmallMonoSysFont);
			}
		}

		const sint32 audioDisplayMargin = mSysFontDigitHeight * 4;

		mpOverlayCustomization->SetDefaultPlacement(kTagAudioDisplay,
			vdrect32f(0, 1, 0, 1), vdpoint32(8, -audioDisplayMargin), vdfloat2{0, 1}, vdsize32(), true);
		mpOverlayCustomization->SetDefaultPlacement(kTagAudioDisplay2,
			vdrect32f(1, 1, 1, 1), vdpoint32(-8, -audioDisplayMargin), vdfloat2{1, 1}, vdsize32(), true);
		mpOverlayCustomization->SetDefaultPlacement(kTagAudioScope,
			vdrect32f(0, 0, 0, 0), vdpoint32(32, 32), vdfloat2{0, 0}, vdsize32(), true);
		mpOverlayCustomization->SetDefaultPlacement(kTagPadInput,
			vdrect32f(0, 0, 1, 1), vdpoint32(0, 0), vdfloat2{0.5f, 0.5f}, vdsize32(), false);

		mpHardDiskDeviceLabel->SetFont(mpSysFont);
		mpRecordingLabel->SetFont(mpSysFont);
		mpTracingLabel->SetFont(mpSysFont);
		mpFlashWriteLabel->SetFont(mpSysFont);

		for(int i=0; i<2; ++i) {
			mpLedLabels[i]->SetFont(mpSysFont);
		}

		for(auto&& p : mpHeldButtonLabels) {
			p->SetFont(mpSysFont);
		}

		mpPendingHeldKeyLabel->SetFont(mpSysFont);

		mpPCLinkLabel->SetFont(mpSysFont);
		mpHostDeviceLabel->SetFont(mpSysFont);
		mpCassetteLabel->SetFont(mpSysFont);
		mpCassetteTimeLabel->SetFont(mpSysFont);
		mpPausedLabel->SetFont(mpSysFont);
		mpHoverTip->SetFont(mpSysHoverTipFont);
		mpHoverTip->SetBoldFont(mpSysBoldHoverTipFont);

		c->AddChild(mpCartridgeActivityIcon1);
		c->AddChild(mpCartridgeActivityIcon2);

		// update layout
		RelayoutStatic();
		InvalidateLayout();
	}
}

void ATUIRenderer::Update() {
	const bool showAllIndicators = false;
	if (showAllIndicators) {
		SetStatusFlags(0x7FFF);
		for(int i=0; i<15; ++i) {
			SetStatusCounter(i, 720);
			SetDiskMotorActivity(i, true);
		}

		SetHActivity(true);
		SetIDEActivity(true, 0xFFFFFF);
		SetPCLinkActivity(true);
		SetFlashWriteActivity();
		SetCassetteIndicatorVisible(true);
		SetCassettePosition(3600.0f, 3600.0f, true, true);
		SetRecordingPosition(3600.0f, 0xFFFFFFF, false);
		SetStatusMessage(L"(Status message)");
		ReportError(0, L"(Error message)");

		SetLedStatus(3);
		SetHeldButtonStatus(7);
		SetPendingHoldMode(true);
		SetPendingHeldKey(0x00);
		SetPendingHeldButtons(7);
		for(int i=0; i<7; ++i)
			SetWatchedValue(0, 0xFFFF, WatchFormat::Hex16);
		SetTracingSize(0xFFFFFFFF);
		SetFpsIndicator(60.0f);
		SetPaused(true);
	}

	uint32 statusFlags = mStatusFlags | mStatusHoldFlags;

	for(uint32 flags = mStatusHoldFlags; flags; flags &= flags - 1) {
		int idx = VDFindLowestSetBitFast(flags);

		if (!--mStatusHoldCounters[idx])
			mStatusHoldFlags &= ~(UINT32_C(1) << idx);
	}

	int x = 0;

	const uint32 diskErrorFlags = (VDGetCurrentTick() % 1000) >= 500 ? mDiskErrorFlags : 0;
	VDStringW s;

	for(int i = 14; i >= 0; --i) {
		ATUILabel& label = *mpDiskDriveIndicatorLabels[i];
		const uint32 flag = (1 << i);
		sint32 leds = mStatusLEDs[i];

		const bool isActive = ((diskErrorFlags | statusFlags) & flag) != 0;
		const bool shouldShow = ((statusFlags | mDiskMotorFlags | diskErrorFlags) & flag) != 0;
		if (leds >= 0 || shouldShow) {
			if (leds >= 0) {
				s.clear();
				if (shouldShow)
					s.append_sprintf(L"%u  ", mStatusCounter[i]);
				s += L"<bg=#404040><fg=#ff4018> <b>";
				s += (wchar_t)(((uint32)leds & 0xFF) + 0x80);
				s += (wchar_t)((((uint32)leds >> 8) & 0xFF) + 0x80);
				s += L"</b> </fg></bg>";
				label.SetHTMLText(s.c_str());
			} else {
				label.SetTextF(L"%u", mStatusCounter[i]);
			}

			label.SetPlacement(vdrect32f(1, 1, 1, 1), vdpoint32(x, 0), vdfloat2{1, 1});
			const auto& m = label.Measure();
			x -= m.mDesiredSize.w;

			label.SetTextColor(0xFF000000);
			label.SetFillColor(kDiskColors[i & 7][isActive]);
			label.SetVisible(true);
		} else {
			label.SetVisible(false);
			x -= mSysFontDigitWidth;
		}
	}

	if (statusFlags & 0x10000) {
		mpCassetteLabel->SetVisible(true);

		mShowCassetteIndicatorCounter = 60;
	} else {
		mpCassetteLabel->SetVisible(false);

		if (mbShowCassetteIndicator)
			mShowCassetteIndicatorCounter = 60;
	}

	if (mShowCassetteIndicatorCounter) {
		--mShowCassetteIndicatorCounter;

		mpCassetteTimeLabel->SetVisible(true);
	} else {
		mpCassetteTimeLabel->SetVisible(false);
	}

	// draw H: indicators
	bool updateH = false;

	if (mHReadCounter) {
		--mHReadCounter;

		if (mHReadCounter == 24)
			updateH = true;
		else if (!mHReadCounter && !mHWriteCounter)
			updateH = true;
	}

	if (mHWriteCounter) {
		--mHWriteCounter;

		if (mHWriteCounter == 24)
			updateH = true;
		else if (!mHWriteCounter && !mHReadCounter)
			updateH = true;
	}

	if (updateH)
		UpdateHostDeviceLabel();

	// draw PCLink indicators (same place as H:)
	if (mPCLinkReadCounter || mPCLinkWriteCounter) {
		if (mPCLinkReadCounter)
			--mPCLinkReadCounter;

		if (mPCLinkWriteCounter)
			--mPCLinkWriteCounter;

		UpdatePCLinkLabel();
	}

	// draw H: indicators
	if (mbHardDiskRead || mbHardDiskWrite) {
		if (!--mHardDiskCounter) {
			mbHardDiskRead = false;
			mbHardDiskWrite = false;
		}
	} else {
		mpHardDiskDeviceLabel->SetVisible(false);
	}

	// draw flash write counter
	if (mFlashWriteCounter) {
		if (!--mFlashWriteCounter)
			mpFlashWriteLabel->SetVisible(false);
	}

	// update audio monitor
	for(ATUIAudioDisplay *disp : mpAudioDisplays) {
		if (disp)
			disp->Update();
	}

	if (mpAudioScope)
		mpAudioScope->Update();

	// update indicator safe area
	sint32 ish = mSysFontDigitHeight * 2 + 6;

	if (mIndicatorSafeHeight != ish) {
		mIndicatorSafeHeight = ish;

		mIndicatorSafeAreaListeners.Notify([](const vdfunction<void()> *pfn) { (*pfn)(); return false; });
	}

	// tick errors
	bool errorsChanged = false;

	while(!mErrors.empty()) {
		ErrorEntry& ee = mErrors.front();

		if (VDGetCurrentTick() - ee.mExpirationTime >= UINT32_C(0x80000000))
			break;

		ee.mpLabel->Destroy();
		mErrors.erase(mErrors.begin());

		errorsChanged = true;
	}

	if (errorsChanged)
		RelayoutErrors();
}

sint32 ATUIRenderer::GetIndicatorSafeHeight() const {
	return mpContainer->IsVisible() ? mIndicatorSafeHeight : 0;
}

void ATUIRenderer::AddIndicatorSafeHeightChangedHandler(const vdfunction<void()> *pfn) {
	mIndicatorSafeAreaListeners.Add(pfn);
}

void ATUIRenderer::RemoveIndicatorSafeHeightChangedHandler(const vdfunction<void()> *pfn) {
	mIndicatorSafeAreaListeners.Remove(pfn);
}

void ATUIRenderer::SetQuickBarEnabled(bool enabled) {
	mbQuickBarEnabled = enabled;

	if (mpQuickBarContainer)
		mpQuickBarContainer->SetVisible(enabled);
}

void ATUIRenderer::BeginCustomization() {
	mpOverlayCustomization->SetVisible(true);
	mpOverlayCustomization->Focus();
}

void ATUIRenderer::TimerCallback() {
	ClearMessage(StatusPriority::Status);
}

void ATUIRenderer::InvalidateLayout() {
	Relayout(mPrevLayoutWidth, mPrevLayoutHeight);
}

void ATUIRenderer::RelayoutStatic() {
	const vdrect32f kAnchorTL{0, 0, 0, 0};
	const vdrect32f kAnchorTC{0.5f, 0, 0.5f, 0};
	const vdrect32f kAnchorTR{1, 0, 1, 0};
	const vdrect32f kAnchorBL{0, 1, 0, 1};
	const vdrect32f kAnchorBR{1, 1, 1, 1};

	mpFpsLabel->SetPlacement(kAnchorTR, vdpoint32(-10, 10), vdfloat2{1, 0});
	mpStatusMessageLabel->SetPlacement(kAnchorBL, vdpoint32(1, -mSysFontDigitHeight*2 - 4), vdfloat2{0, 1});

	for(int i=0; i<8; ++i) {
		ATUILabel& label = *mpWatchLabels[i];

		label.SetPlacement(kAnchorBL, vdpoint32(64, - 4*mSysFontDigitHeight - (7 - i)*mSysMonoFontHeight), vdfloat2{0, 1});
	}

	int ystats = 0;

	mpHardDiskDeviceLabel->SetPlacement(kAnchorBL, vdpoint32(mSysFontDigitWidth * 36, ystats), vdfloat2{0, 1});
	mpRecordingLabel->SetPlacement(kAnchorBL, vdpoint32(mSysFontDigitWidth * 27, ystats), vdfloat2{0, 1});
	mpTracingLabel->SetPlacement(kAnchorBL, vdpoint32(mSysFontDigitWidth * 37, ystats), vdfloat2{0, 1});
	mpFlashWriteLabel->SetPlacement(kAnchorBL, vdpoint32(mSysFontDigitWidth * 47, ystats), vdfloat2{0, 1});
	mpCartridgeActivityIcon1->SetPlacement(kAnchorBL, vdpoint32(mSysFontDigitWidth * 49, ystats -(mSysFontDigitHeight >> 1)), vdfloat2{0, 1});
	mpCartridgeActivityIcon1->SetSizeOffset(vdsize32(mSysFontDigitHeight >> 1, mSysFontDigitHeight >> 1));
	mpCartridgeActivityIcon2->SetPlacement(kAnchorBL, vdpoint32(mSysFontDigitWidth * 49, ystats), vdfloat2{0, 1});
	mpCartridgeActivityIcon2->SetSizeOffset(vdsize32(mSysFontDigitHeight >> 1, (mSysFontDigitHeight + 1) >> 1));

	mpPCLinkLabel->SetPlacement(kAnchorBL, vdpoint32(mSysFontDigitWidth * 19, ystats), vdfloat2{0, 1});
	mpHostDeviceLabel->SetPlacement(kAnchorBL, vdpoint32(mSysFontDigitWidth * 19, ystats), vdfloat2{0, 1});

	for(int i=0; i<2; ++i)
		mpLedLabels[i]->SetPlacement(kAnchorBL, vdpoint32(mSysFontDigitWidth * (24+i), ystats), vdfloat2{0, 1});

	mpCassetteLabel->SetPlacement(kAnchorBL, vdpoint32(0, ystats), vdfloat2{0, 1});
	mpCassetteTimeLabel->SetPlacement(kAnchorBL, vdpoint32(mpCassetteLabel->Measure().mDesiredSize.w, ystats), vdfloat2{0, 1});

	const int ystats2 = ystats - (mSysFontDigitHeight * 5) / 4;
	const int ystats3 = ystats2 - (mSysFontDigitHeight * 5) / 4;
	int x = 0;

	for(int i=(int)vdcountof(mpHeldButtonLabels)-1; i>=0; --i) {
		ATUILabel& label = *mpHeldButtonLabels[i];

		x -= label.Measure().mDesiredSize.w;
		label.SetPlacement(kAnchorBR, vdpoint32(x, ystats2), vdfloat2{0, 1});
	}

	mpPendingHeldKeyLabel->SetPlacement(kAnchorBR, vdpoint32(0, ystats3), vdfloat2{1,1});

	mpAudioStatusDisplay->SetPlacement(kAnchorTL, vdpoint32(16, 16), vdfloat2{0, 0});
	mpPausedLabel->SetPlacement(kAnchorTC, vdpoint32(0, 64), vdfloat2{0.5f, 0});
}

void ATUIRenderer::Relayout(int w, int h) {
	mPrevLayoutWidth = w;
	mPrevLayoutHeight = h;

	RelayoutErrors();

	UpdateHoverTipPos();
}

void ATUIRenderer::UpdatePendingHoldLabel() {
	if (mbPendingHoldMode || mPendingHeldButtons || mPendingHeldKey >= 0) {
		VDStringW s;

		if (mbPendingHoldMode)
			s = L"Press keys to hold on next reset: ";

		if (mPendingHeldButtons & 1)
			s += L"Start+";

		if (mPendingHeldButtons & 2)
			s += L"Select+";

		if (mPendingHeldButtons & 4)
			s += L"Option+";

		if (mPendingHeldKey >= 0) {
			const wchar_t *label = ATUIGetNameForKeyCode((uint8)mPendingHeldKey);

			if (label)
				s += label;
			else
				s.append_sprintf(L"[$%02X]", mPendingHeldKey);
		}

		if (!s.empty() && s.back() == L'+')
			s.pop_back();

		mpPendingHeldKeyLabel->SetText(s.c_str());
		mpPendingHeldKeyLabel->SetVisible(true);
	} else {
		mpPendingHeldKeyLabel->SetVisible(false);
	}
}

void ATUIRenderer::RelayoutErrors() {
	vdrect32 r = mpStatusMessageLabel->GetArea();
	int y = mpStatusMessageLabel->IsVisible() ? r.top : r.bottom;

	for(auto it = mErrors.rbegin(), itEnd = mErrors.rend(); it != itEnd; ++it) {
		const ErrorEntry& ee = *it;

		vdrect32 er = ee.mpLabel->GetArea();
		int dy = y - er.bottom;
		er.top += dy;
		er.bottom += dy;

		ee.mpLabel->SetArea(er);

		y -= er.height();
	}
}

void ATUIRenderer::UpdateHostDeviceLabel() {
	if (!mHReadCounter && !mHWriteCounter) {
		mpHostDeviceLabel->SetVisible(false);
		return;
	}

	mpHostDeviceLabel->Clear();
	mpHostDeviceLabel->AppendFormattedText(0, L"H:");

	mpHostDeviceLabel->AppendFormattedText(
		mHReadCounter >= 25 ? 0xFFFFFF : mHReadCounter ? 0x000000 : 0x007920,
		L"R");
	mpHostDeviceLabel->AppendFormattedText(
		mHWriteCounter >= 25 ? 0xFFFFFF : mHWriteCounter ? 0x000000 : 0x007920,
		L"W");

	mpHostDeviceLabel->SetVisible(true);
}

void ATUIRenderer::UpdatePCLinkLabel() {
	if (!mPCLinkReadCounter && !mPCLinkWriteCounter) {
		mpPCLinkLabel->SetVisible(false);
		return;
	}

	mpPCLinkLabel->Clear();
	mpPCLinkLabel->AppendFormattedText(0, L"PCL:");

	mpPCLinkLabel->AppendFormattedText(
		mPCLinkReadCounter >= 25 ? 0xFFFFFF : mPCLinkReadCounter ? 0x000000 : 0x007920,
		L"R");
	mpPCLinkLabel->AppendFormattedText(
		mPCLinkWriteCounter >= 25 ? 0xFFFFFF : mPCLinkWriteCounter ? 0x000000 : 0x007920,
		L"W");

	mpPCLinkLabel->SetVisible(true);
}

void ATUIRenderer::UpdateHoverTipPos() {
	if (mpHoverTip->IsVisible()) {
		const vdsize32 htsize = mpHoverTip->GetArea().size();

		int x = mHoverTipX;
		int y = mHoverTipY + 32;

		if (x + htsize.w > mPrevLayoutWidth)
			x = std::max<int>(0, mPrevLayoutWidth - htsize.w);

		if (y + htsize.h > mPrevLayoutHeight) {
			int y2 = mHoverTipY - 32 - htsize.h;

			if (y2 >= 0)
				y = y2;
		}

		mpHoverTip->SetPosition(vdpoint32(x, y));
	}
}

void ATUIRenderer::RemakeLEDFont() {
	if (mpLEDFont) {
		if (mLEDFontCellWidth == mSysFontDigitWidth && mLEDFontCellAscent == mSysFontDigitAscent)
			return;
	}

	class RefCountedBitmap : public vdrefcounted<VDPixmapBuffer, IVDRefCount> {};
	vdrefptr<RefCountedBitmap> p(new RefCountedBitmap);

	mLEDFontCellWidth = mSysFontDigitWidth;
	mLEDFontCellAscent = mSysFontDigitAscent;

	wchar_t chars[128] = {};
	for(uint32 i=0; i<128; ++i)
		chars[i] = (wchar_t)(i + 0x80);

	int w = mLEDFontCellWidth;
	int h = mLEDFontCellAscent;
	int pad = 8;
	int tw = w * 8 + 16;
	int th = h * 8 + 16;

	while(tw < 128 && th < 128) {
		tw += tw;
		th += th;
		pad += pad;
	}

	VDPixmapBuffer tempBuf(tw, th, nsVDPixmap::kPixFormat_XRGB8888);

	p->init(w + 2, (h + 2) * 128, nsVDPixmap::kPixFormat_XRGB8888);

	VDPixmap pxDstCell = *p;
	pxDstCell.w = w + 2;
	pxDstCell.h = h + 2;

	VDDisplayRendererSoft rs;
	rs.Init();
	rs.Begin(tempBuf);

	const int stemWidth = std::min<int>(tw, th) / 10;
	const int endOffset = tw / 16;
	const int gridX1 = pad + tw / 6;
	const int gridX2 = pad + tw - tw / 6;
	const int gridY1 = pad + th / 6;
	const int gridY2 = pad + th / 2;
	const int gridY3 = pad + th - th / 6;
	const int descent = (th - (gridY3 + stemWidth)) / pad;

	const vdrect32 segmentRects[7]={
		vdrect32(gridX1 + endOffset, gridY1 - stemWidth, gridX2 - endOffset, gridY1 + stemWidth),	// A (top)
		vdrect32(gridX2 - stemWidth, gridY1 + endOffset, gridX2 + stemWidth, gridY2 - endOffset),	// B (top right)
		vdrect32(gridX2 - stemWidth, gridY2 + endOffset, gridX2 + stemWidth, gridY3 - endOffset),	// C (bottom right)
		vdrect32(gridX1 + endOffset, gridY3 - stemWidth, gridX2 - endOffset, gridY3 + stemWidth),	// D (bottom)
		vdrect32(gridX1 - stemWidth, gridY2 + endOffset, gridX1 + stemWidth, gridY3 - endOffset),	// E (bottom left)
		vdrect32(gridX1 - stemWidth, gridY1 + endOffset, gridX1 + stemWidth, gridY2 - endOffset),	// F (top left)
		vdrect32(gridX1 + endOffset, gridY2 - stemWidth, gridX2 - endOffset, gridY2 + stemWidth),	// G (center)
	};

	for(uint32 i=0; i<128; ++i) {
		rs.SetColorRGB(0);
		rs.FillRect(0, 0, tw, th);

		rs.SetColorRGB(0xFFFFFF);
		for(uint32 bit=0; bit<7; ++bit) {
			if (i & (1 << bit)) {
				const auto& r = segmentRects[bit];
				rs.FillRect(r.left, r.top, r.width(), r.height());
			}
		}

		pxDstCell.data = (char *)p->data + (p->pitch * (h + 2)) * i;
		VDPixmapResample(pxDstCell, tempBuf, IVDPixmapResampler::kFilterLinear);
	}

	vdblock<VDDisplayBitmapFontGlyphInfo> glyphInfos(128);
	int cellPad = std::max<int>(1, mLEDFontCellWidth / 10 + 1);
	for(uint32 i=0; i<128; ++i) {
		auto& gi = glyphInfos[i];
		gi.mAdvance = mLEDFontCellWidth + (cellPad + 1) / 2;
		gi.mCellX = -1 + cellPad / 2;
		gi.mCellY = -(mLEDFontCellAscent + 1) + descent;
		gi.mWidth = mLEDFontCellWidth + 2;
		gi.mHeight = mLEDFontCellAscent + 2;
		gi.mBitmapX = 0;
		gi.mBitmapY = (mLEDFontCellAscent + 2) * i;
	}

	VDDisplayFontMetrics metrics {};
	metrics.mAscent = mLEDFontCellAscent - descent;
	metrics.mDescent = descent;
	VDCreateDisplayBitmapFont(metrics, 128, chars, glyphInfos.data(), *p, 0, p, ~mpLEDFont);
}

void ATUIRenderer::UpdateStatusMessage() {
	static constexpr struct {
		uint32 mBkColor;
		uint32 mFgColor;
	} kMessageStyles[] {
		{ 0x303850, 0xFFFFFF },		// status
		{ 0x1E00AC, 0x8458FF },		// modem
		{ 0x0020AC, 0xFFFFFF },		// prompt
	};

	static_assert(vdcountof(kMessageStyles) == (int)StatusPriority::Max + 1);

	mMessageLevel = (int)StatusPriority::Max;

	while(mMessageLevel >= 0) {
		if (!mMessages[mMessageLevel].empty()) {
			mpStatusMessageLabel->SetVisible(true);
			mpStatusMessageLabel->SetFillColor(kMessageStyles[mMessageLevel].mBkColor);
			mpStatusMessageLabel->SetTextColor(kMessageStyles[mMessageLevel].mFgColor);
			mpStatusMessageLabel->SetBorderColor(kMessageStyles[mMessageLevel].mFgColor);
			mpStatusMessageLabel->SetText(mMessages[mMessageLevel].c_str());

			mpContainer->UpdateLayout();
			RelayoutErrors();
			return;
		}

		--mMessageLevel;
	}

	mpStatusMessageLabel->SetVisible(false);
	mpContainer->UpdateLayout();
	RelayoutErrors();
}
