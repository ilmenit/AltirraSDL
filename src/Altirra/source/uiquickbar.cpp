//	Altirra - Atari 800/800XL/5200 emulator
//	Copyright (C) 2026 Avery Lee
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
//	You should have received a copy of the GNU General Public License along
//	with this program. If not, see <http://www.gnu.org/licenses/>.

#include <stdafx.h>
#include <vd2/VDDisplay/renderer.h>
#include <vd2/VDDisplay/textrenderer.h>
#include <vd2/Kasumi/pixmapops.h>
#include <vd2/Kasumi/resample.h>
#include <at/atui/uicommandmanager.h>
#include <at/atui/uimanager.h>
#include <at/atui/uiwidgetanimator.h>
#include "uiaccessors.h"
#include "uicommondialogs.h"
#include "uiquickbar.h"

struct ATUIQuickBarWidget::IconInfo final : public vdrefcount {
	VDPixmapBuffer mIconBuffer;
	VDPixmapBuffer mScaledIconBuffer;
	VDPixmapBuffer mHighlightedIconBuffer;
	VDPixmapBuffer mDisabledIconBuffer;
	VDDisplayImageView mIconView;
	VDDisplayImageView mHighlightedIconView;
	VDDisplayImageView mDisabledIconView;
	bool mbSubMenuOverlay = false;
};

ATUIQuickBarWidget::ATUIQuickBarWidget() {
	mMarginX = 4;
	mMarginY = 4;
	mSpacing = 4;

	SetFillColor(0x141414);
}

ATUIQuickBarWidget::~ATUIQuickBarWidget() {
}

void ATUIQuickBarWidget::SetBaseIconSize(sint32 size) {
	size = std::max<sint32>(size, 1);

	if (mBaseItemSize != size) {
		mBaseItemSize = size;

		UpdateItemSize();
	}
}

void ATUIQuickBarWidget::SetSubmenuOverlay(const VDPixmap& px) {
	if (px.format != nsVDPixmap::kPixFormat_ARGB8888) {
		VDFAIL("Incorrect format for submenu overlay");
		return;
	}

	mOverlayImage.assign(px);
}

void ATUIQuickBarWidget::Clear() {
	mIcons.clear();
	mItems.clear();
	mCommands.clear();
	mNumTopLevelItems = 0;
	mNumSeparators = 0;
	Invalidate();
}

void ATUIQuickBarWidget::AddCommand(const char *cmdStr, const wchar_t *label, const VDPixmap *icon, bool subMenuOverlay) {
	auto& cm = ATUIGetCommandManager();
	sint32 iconIndex = -1;

	if (icon) {
		iconIndex = (sint32)mIcons.size();
		auto& iconInfo = mIcons.emplace_back();

		iconInfo = new IconInfo;
		iconInfo->mIconBuffer.init(icon->w, icon->h, nsVDPixmap::kPixFormat_ARGB8888);
		VDVERIFY(VDPixmapBlt(iconInfo->mIconBuffer, *icon));

		iconInfo->mbSubMenuOverlay = subMenuOverlay;

		UpdateScaledIcon(*iconInfo);
	}

	auto& cmdEntry = mCommands.emplace_back();

	VDStringSpanA cmdName(cmdStr);

	while(!cmdName.empty()) {
		const char c = cmdName.back();

		if (c == '~')
			cmdEntry.mMode = CommandMode::Invert;
		else if (c == '+')
			cmdEntry.mMode = CommandMode::TurnOn;
		else if (c == '-')
			cmdEntry.mMode = CommandMode::TurnOff;
		else if (c == '!')
			cmdEntry.mbCmdQuiet = true;
		else
			break;

		cmdName.remove_suffix(1);
	}

	cmdEntry.mpCommand = cm.GetCommand(cmdName);
	cmdEntry.mText = VDStringSpanW(label ? label : L"");
	cmdEntry.mIconIndex = iconIndex;
}

void ATUIQuickBarWidget::AddItem(uint32 numCommands) {
	if (!numCommands || numCommands > mCommands.size()) {
		VDFAIL("Invalid item count passed to AddItem");
		return;
	}

	auto& newItem = mItems.emplace_back();
	newItem.mCommandStartIndex = (uint32)(mCommands.size() - numCommands);
	newItem.mCommandCount = numCommands;

	if (mbBuildingSubMenu)
		newItem.mbSubItem = true;
	else
		++mNumTopLevelItems;
}

void ATUIQuickBarWidget::AddSeparator() {
	mItems.emplace_back();

	if (mbBuildingSubMenu)
		mItems.back().mbSubItem = true;
	else {
		++mNumTopLevelItems;
		++mNumSeparators;
	}
}

void ATUIQuickBarWidget::BeginSubMenu() {
	mbBuildingSubMenu = true;
}

void ATUIQuickBarWidget::EndSubMenu(const wchar_t *label, const VDPixmap *icon) {
	mbBuildingSubMenu = false;

	AddCommand("", label, icon, true);

	auto& newItem = mItems.emplace_back();
	newItem.mCommandStartIndex = (uint32)(mCommands.size() - 1);
	newItem.mCommandCount = 1;
	newItem.mbSubMenu = true;
	newItem.mbActiveState = true;

	++mNumTopLevelItems;
}

void ATUIQuickBarWidget::FinalizeItems() {
	UpdateItems();
	Invalidate();

	UpdateItemSize();
}

void ATUIQuickBarWidget::ForceRefresh() {
	UpdateItems();
}

void ATUIQuickBarWidget::SetOnItemActivated(vdfunction<void()> fn) {
	mpOnItemActivated = std::move(fn);
}

void ATUIQuickBarWidget::SetOnCaptureLost(vdfunction<void()> fn) {
	mpOnCaptureLost = std::move(fn);
}

void ATUIQuickBarWidget::SetOnSubMenuChange(vdfunction<void(bool opened)> fn) {
	mpOnSubMenuChange = std::move(fn);
}

void ATUIQuickBarWidget::OnCreate() {
	mpFont = GetManager()->GetThemeFont(kATUIThemeFont_Default);

	UpdateItemSize();
}

void ATUIQuickBarWidget::OnDestroy() {
	if (mpActiveSubMenu) {
		mpActiveSubMenu->Destroy();
		mpActiveSubMenu = nullptr;
	}

	ATUIContainer::OnDestroy();
}

void ATUIQuickBarWidget::OnSize() {
	ATUIContainer::OnSize();

	const auto& r = GetClientArea();

	this->mWidth = r.right;
	this->mHeight = r.bottom;

	if (!mbIsSubMenu) {
		for(const auto& iconInfo : mIcons) {
			UpdateScaledIcon(*iconInfo);
		}
	}
}

void ATUIQuickBarWidget::OnDeactivate() {
	if (mpOnCaptureLost)
		mpOnCaptureLost();
}

ATUIWidgetMetrics ATUIQuickBarWidget::OnMeasure() {
	ATUIWidgetMetrics metrics;

	if (mNumTopLevelItems) {
		if (mbVertical) {
			metrics.mDesiredSize.h
				= mItemHeight * (mNumTopLevelItems - mNumSeparators)
				+ mSeparatorLength * mNumSeparators
				+ mSpacing * (mNumTopLevelItems - 1)
				+ 2 * mMarginY;
		} else {
			metrics.mDesiredSize.w
				= mItemWidth * (mNumTopLevelItems - mNumSeparators)
				+ mSeparatorLength * mNumSeparators
				+ mSpacing * (mNumTopLevelItems - 1)
				+ 2 * mMarginX;
		}
	}

	if (mbVertical)
		metrics.mDesiredSize.w = mItemWidth + 2 * mMarginX;
	else
		metrics.mDesiredSize.h = mItemHeight + 2 * mMarginY;

	return metrics;
}

void ATUIQuickBarWidget::Paint(IVDDisplayRenderer& rdr, sint32 w, sint32 h) {
	auto& tr = *rdr.GetTextRenderer();

	VDDisplayFontMetrics fontMetrics {};
	mpFont->GetMetrics(fontMetrics);

	sint32 pos = mbVertical ? h - mMarginY : w - mMarginX;
	for(sint32 i = (sint32)mItems.size() - 1; i >= 0; --i) {
		const ItemInfo& item = mItems[i];

		if (item.mbSubItem && !mbIsSubMenu)
			continue;

		if (!item.mCommandCount) {

			rdr.SetColorRGB(0x404040);

			pos -= mSeparatorLength;

			if (mbVertical)
				rdr.FillRect(mMarginX, pos + mSeparatorLength / 2, mItemWidth, 1);
			else
				rdr.FillRect(pos + mSeparatorLength / 2, mMarginY, 1, mItemHeight);

			pos -= mSpacing;
			continue;
		}

		if (mbVertical)
			pos -= mItemHeight;
		else
			pos -= mItemWidth;

		const bool selected = mSelectedItem == i;
		const bool depressed = selected && mbSelectedItemDepressed;

		vdrect32f r;
		
		if (mbVertical) {
			r.set(
				(float)mMarginX,
				(float)pos,
				(float)mMarginX + (float)mItemWidth,
				(float)pos + (float)mItemWidth
			);
		} else {
			r.set(
				(float)pos,
				(float)mMarginY,
				(float)pos + (float)mItemWidth,
				(float)mMarginY + (float)mItemHeight
			);
		}

		bool filterIcon = false;
		bool highlightIcon = false;
		if (mSelectedItem == i) {
			filterIcon = true;
			highlightIcon = true;

			r.left -= 1.0f;
			r.right += 1.0f;
			r.top -= 1.0f;
			r.bottom += 1.0f;
		}

		const CommandEntry& ce = mCommands[item.mCommandStartIndex + item.mActiveCommandIndex];

		if (ce.mIconIndex >= 0) {
			IconInfo& iconInfo = *mIcons[ce.mIconIndex];
			VDDisplayBltColorMatrix colorMatrix;
			VDDisplayBltOptions opts {
				filterIcon ? VDDisplayBltOptions::kFilterMode_Bilinear : VDDisplayBltOptions::kFilterMode_Point
			};

			if (!item.mbActiveState) {
				colorMatrix.mRCoeff = vdfloat4 { 0.30f, 0.30f, 0.30f, 0.00f} * 0.75f;
				colorMatrix.mGCoeff = vdfloat4 { 0.59f, 0.59f, 0.59f, 0.00f} * 0.75f;
				colorMatrix.mBCoeff = vdfloat4 { 0.11f, 0.11f, 0.11f, 0.00f} * 0.75f;
				colorMatrix.mCCoeff = vdfloat4 { 0.05f, 0.05f, 0.05f, 0.00f};

				opts.mpColorMatrix = &colorMatrix;
			}

			if (highlightIcon) {
				colorMatrix.mCCoeff += vdfloat4 { 0.10f, 0.10f, 0.10f, 0.00f};

				opts.mpColorMatrix = &colorMatrix;
			}

			rdr.StretchBlt(
				r.left,
				r.top,
				r.width(),
				r.height(),
				iconInfo.mIconView,
				0,
				0,
				mItemWidth,
				mItemHeight,
				opts
			);
		} else if (!ce.mText.empty()) {
			rdr.SetColorRGB(item.mbActiveState
				? depressed ? 0xA0A0A0 : selected ? 0x808080 : 0x606060
				: depressed ? 0x808080 : selected ? 0x606060 : 0x404040
			);

			rdr.FillRect(r.left, r.top, r.width(), r.height());
			
			tr.Begin();
			tr.SetAlignment(tr.kAlignCenter, tr.kVertAlignBaseline);
			tr.SetFont(mpFont);
			tr.SetColorRGB(item.mbActiveState
				? depressed ? 0xFFFFFF : selected ? 0xF0F0F0 : 0xE0E0E0
				: depressed ? 0xFFFFFF : selected ? 0xD0D0D0 : 0xA0A0A0
			);

			tr.DrawTextLine((r.left + r.right) / 2, (r.top + r.bottom + fontMetrics.mAscent) / 2, ce.mText.c_str());
			tr.End();
		}

		pos -= mSpacing;
	}

	ATUIContainer::Paint(rdr, w, h);
}

void ATUIQuickBarWidget::OnMouseDownL(sint32 x, sint32 y) {
	sint32 item = PointToItem(x, y);

	if (item >= 0) {
		mMouseDownItem = item;
		SelectItem(item, true);
	}
}

void ATUIQuickBarWidget::OnMouseUpL(sint32 x, sint32 y) {
	const auto item = PointToItem(x, y);
	const bool activate = item >= 0 && item == mMouseDownItem;

	SelectItem(item, false);
	mMouseDownItem = -1;

	if (activate) {
		ActivateItem(item);
	}
}

void ATUIQuickBarWidget::OnMouseMove(sint32 x, sint32 y) {
	sint32 item = PointToItem(x, y);

	if (mMouseDownItem >= 0 && item != mMouseDownItem)
		item = -1;

	SelectItem(item, mMouseDownItem == item);
}

void ATUIQuickBarWidget::OnMouseLeave() {
	SelectItem(-1, false);
	mMouseDownItem = -1;
}

void ATUIQuickBarWidget::OnCaptureLost() {
	if (mpOnCaptureLost)
		mpOnCaptureLost();
}

sint32 ATUIQuickBarWidget::PointToItem(sint32 x, sint32 y) const {
	if (mbVertical) {
		if (x < mMarginX || x >= mWidth - mMarginX)
			return -1;

		if (y >= mHeight - mMarginY)
			return -1;
	} else {
		if (y < mMarginY || y >= mHeight - mMarginY)
			return -1;

		if (x >= mWidth - mMarginX)
			return -1;
	}

	sint32 offset = mbVertical ? (mHeight - mMarginY) - y : (mWidth - mMarginX) - x;

	sint32 i = (sint32)mItems.size() - 1;
	while(i >= 0) {
		const ItemInfo& item = mItems[i];

		if (item.mbSubItem && !mbIsSubMenu) {
			--i;
			continue;
		}

		if (item.mCommandCount > 0) {
			offset -= mbVertical ? mItemHeight : mItemWidth;

			if (offset < 0)
				return i;
		} else {
			offset -= mSeparatorLength;
		}

		offset -= mSpacing;

		if (offset < 0)
			return -1;

		--i;
	}

	return -1;
}

void ATUIQuickBarWidget::SelectItem(sint32 item, bool depressed) {
	if (mSelectedItem != item) {
		InvalidateItem(mSelectedItem);
		mSelectedItem = item;
	} else if (mbSelectedItemDepressed == depressed)
		return;

	mbSelectedItemDepressed = depressed;
	InvalidateItem(mSelectedItem);
}

void ATUIQuickBarWidget::InvalidateItem(sint32 item) {
	if (item < 0 || item >= mNumTopLevelItems)
		return;

	Invalidate();
}

void ATUIQuickBarWidget::InitSubMenu(vdspan<const CommandEntry> commands, vdspan<const ItemInfo> items, vdspan<const vdrefptr<IconInfo>> icons) {
	mbVertical = true;
	mbIsSubMenu = true;

	mNumTopLevelItems = 0;
	mNumSeparators = 0;

	for(const ItemInfo& item : items) {
		if (!item.mCommandCount)
			++mNumSeparators;

		++mNumTopLevelItems;
	}

	mCommands.assign(commands.begin(), commands.end());
	mItems.assign(items.begin(), items.end());
	mIcons.assign(icons.begin(), icons.end());
	FinalizeItems();
}

void ATUIQuickBarWidget::UpdateItemSize() {
	const float scale = mpManager ? mpManager->GetThemeScaleFactor() : 1.0f;

	mItemWidth = std::max<sint32>(1.0f, VDRoundToInt32((float)mBaseItemSize * scale));
	mItemHeight = mItemWidth;

	mSeparatorLength = ((mbVertical ? mItemHeight : mItemWidth) + 1) / 3;

	InvalidateMeasure();
}

void ATUIQuickBarWidget::UpdateItems() {
	for(ItemInfo& info : mItems) {
		if (!info.mCommandCount || (info.mbSubItem && !mbIsSubMenu) || info.mbSubMenu)
			continue;

		sint32 firstEnabledItem = -1;
		sint32 nextEnabledItem = -1;
		sint32 firstActiveItem = -1;
		bool haveStateFn = false;

		for(uint32 idx = 0; idx < info.mCommandCount; ++idx) {
			const CommandEntry& ce = mCommands[idx + info.mCommandStartIndex];

			if (!ce.mpCommand)
				continue;

			if (ce.mpCommand->mpTestFn && !ce.mpCommand->mpTestFn())
				continue;

			if (firstEnabledItem < 0)
				firstEnabledItem = (sint32)idx;

			if (nextEnabledItem < 0 && (sint32)idx > (firstActiveItem < 0 ? (sint32)info.mCommandStartIndex : firstActiveItem))
				nextEnabledItem = (sint32)idx;

			if (ce.mpCommand->mpStateFn) {
				haveStateFn = true;

				if (firstActiveItem < 0) {
					bool isActive = ce.mpCommand->mpStateFn() != ATUICmdState::kATUICmdState_None;

					if (ce.mMode == CommandMode::Invert || ce.mMode == CommandMode::TurnOff)
						isActive = !isActive;

					if (isActive) {
						firstActiveItem = (sint32)idx;
						nextEnabledItem = -1;
					}
				}
			}
		}

		const uint32 newActiveIndex = firstActiveItem >= 0 ? (uint32)firstActiveItem : info.mActiveCommandIndex;

		info.mNextCommandIndex = nextEnabledItem >= 0 ? (uint32)nextEnabledItem :
			firstEnabledItem >= 0 ? (uint32)firstEnabledItem
			: 0;

		const bool newActiveState = !haveStateFn || firstActiveItem >= 0;

		if (info.mActiveCommandIndex != newActiveIndex || info.mbActiveState != newActiveState) {
			info.mActiveCommandIndex = newActiveIndex;
			info.mbActiveState = newActiveState;

			InvalidateItem((sint32)(&info - mItems.data()));
		}
	}
}

void ATUIQuickBarWidget::UpdateScaledIcon(IconInfo& iconInfo) {
	if (mScaledOverlayImage.w != mItemWidth || mScaledOverlayImage.h != mItemHeight) {
		mScaledOverlayImage.init(mItemWidth, mItemHeight, nsVDPixmap::kPixFormat_ARGB8888);

		// split and resample alpha separately
		VDPixmapBuffer srcAlpha(mOverlayImage.w, mOverlayImage.h, nsVDPixmap::kPixFormat_Y8_FR);
		VDPixmapBuffer dstAlpha(mItemWidth, mItemHeight, nsVDPixmap::kPixFormat_Y8_FR);

		const sint32 srcw = mOverlayImage.w;
		const sint32 srch = mOverlayImage.h;
		const sint32 dstw = mItemWidth;
		const sint32 dsth = mItemHeight;

		for(sint32 y = 0; y < srch; ++y) {
			uint8 *VDRESTRICT dst = srcAlpha.GetPixelRow<uint8>(y);
			const uint8 *VDRESTRICT src = mOverlayImage.GetPixelRow<const uint8>(y);

			for(sint32 x = 0; x < srcw; ++x) {
				*dst++ = src[3];
				src += 4;
			}
		}

		ScaleIcon(dstAlpha, srcAlpha);

		VDPixmap dstColor(mScaledOverlayImage);
		VDPixmap srcColor(mOverlayImage);
		dstColor.format = nsVDPixmap::kPixFormat_XRGB8888;
		srcColor.format = nsVDPixmap::kPixFormat_XRGB8888;
		ScaleIcon(dstColor, srcColor);

		for(sint32 y = 0; y < dsth; ++y) {
			uint8 *VDRESTRICT dst = mScaledOverlayImage.GetPixelRow<uint8>(y);
			const uint8 *VDRESTRICT src = dstAlpha.GetPixelRow<const uint8>(y);

			for(sint32 x = 0; x < dstw; ++x) {
				dst[3] = *src++;
				dst += 4;
			}
		}
	}

	if (iconInfo.mScaledIconBuffer.w != mItemWidth
		|| iconInfo.mScaledIconBuffer.h != mItemHeight)
	{
		VDPixmapBuffer compositeBuffer;
		const sint32 srcw = iconInfo.mIconBuffer.w;
		const sint32 srch = iconInfo.mIconBuffer.h;
		compositeBuffer.init(srcw, srch, nsVDPixmap::kPixFormat_XRGB8888);

		for(sint32 y = 0; y < srch; ++y) {
			uint8 *VDRESTRICT dst = compositeBuffer.GetPixelRow<uint8>(y);
			const uint8 *VDRESTRICT src = iconInfo.mIconBuffer.GetPixelRow<const uint8>(y);

			for(sint32 x = 0; x < srcw; ++x) {
				uint32 b16 = src[0] * src[3] + 0x10 * (255 - src[3]);
				uint32 g16 = src[1] * src[3] + 0x10 * (255 - src[3]);
				uint32 r16 = src[2] * src[3] + 0x10 * (255 - src[3]);

				dst[0] = (b16 + (b16 << 8) + 257) >> 16;
				dst[1] = (g16 + (g16 << 8) + 257) >> 16;
				dst[2] = (r16 + (r16 << 8) + 257) >> 16;
				dst[3] = 0xFF;

				src += 4;
				dst += 4;
			}
		}

		iconInfo.mScaledIconBuffer.init(mItemWidth, mItemHeight, nsVDPixmap::kPixFormat_XRGB8888);

		ScaleIcon(iconInfo.mScaledIconBuffer, compositeBuffer);

		if (iconInfo.mbSubMenuOverlay) {
			const sint32 dstw = mItemWidth;
			const sint32 dsth = mItemHeight;
			for(sint32 y = 0; y < dsth; ++y) {
				uint8 *VDRESTRICT dst = iconInfo.mScaledIconBuffer.GetPixelRow<uint8>(y);
				const uint8 *VDRESTRICT src = mScaledOverlayImage.GetPixelRow<const uint8>(y);

				for(sint32 x = 0; x < dstw; ++x) {
					const uint32 alpha = src[3];

					if (alpha) {
						const uint32 invAlpha = 255 - alpha;

						uint32 b16 = dst[0] * invAlpha + src[0] * alpha;
						uint32 g16 = dst[1] * invAlpha + src[1] * alpha;
						uint32 r16 = dst[2] * invAlpha + src[2] * alpha;

						dst[0] = (b16 + (b16 << 8) + 257) >> 16;
						dst[1] = (g16 + (g16 << 8) + 257) >> 16;
						dst[2] = (r16 + (r16 << 8) + 257) >> 16;
					}

					src += 4;
					dst += 4;
				}
			}
		}

		iconInfo.mHighlightedIconBuffer.init(mItemWidth, mItemHeight, nsVDPixmap::kPixFormat_XRGB8888);
		iconInfo.mDisabledIconBuffer.init(mItemWidth, mItemHeight, nsVDPixmap::kPixFormat_XRGB8888);

		constexpr float disabledIntensity = 0.75f;
		constexpr float disabledBlackLevel = 0.02f;

		constexpr int disabledRedCoeff = (int)(0.30f * disabledIntensity * 256 + 0.5f);
		constexpr int disabledBlueCoeff = (int)(0.11f * disabledIntensity * 256 + 0.5f);
		constexpr int disabledGreenCoeff = (int)(0.59f * disabledIntensity * 256 + 0.5f);
		constexpr int disabledConstCoeff = (int)(disabledBlackLevel * 255 * 256 + 0.5f) + 128;

		for(uint32 y = 0; y < (uint32)mItemHeight; ++y) {
			const uint8 *src = iconInfo.mScaledIconBuffer.GetPixelRow<uint8>(y);
			uint8 *dsth = iconInfo.mHighlightedIconBuffer.GetPixelRow<uint8>(y);
			uint8 *dstd = iconInfo.mDisabledIconBuffer.GetPixelRow<uint8>(y);

			for(uint32 x = 0; x < (uint32)mItemWidth; ++x) {
				int b = src[0];
				int g = src[1];
				int r = src[2];
				src += 4;

				uint8 y = (uint8)((r * disabledRedCoeff + g * disabledGreenCoeff + b * disabledBlueCoeff + disabledConstCoeff) >> 8);

				dstd[0] = y;
				dstd[1] = y;
				dstd[2] = y;
				dstd[3] = 255;
				dstd += 4;

				dsth[0] = (uint8)std::min<int>(b + (b >> 2), 255);
				dsth[1] = (uint8)std::min<int>(g + (g >> 2), 255);
				dsth[2] = (uint8)std::min<int>(r + (r >> 2), 255);
				dsth[3] = 255;
				dsth += 4;
			}
		}

		iconInfo.mIconView.SetImage(iconInfo.mScaledIconBuffer, false);
		iconInfo.mHighlightedIconView.SetImage(iconInfo.mHighlightedIconBuffer, false);
		iconInfo.mDisabledIconView.SetImage(iconInfo.mDisabledIconBuffer, false);
	}
}

void ATUIQuickBarWidget::ScaleIcon(VDPixmap& dst, const VDPixmap& src) {
	if (dst.w == src.w)
		VDVERIFY(VDPixmapResample(dst, src, IVDPixmapResampler::kFilterPoint));
	else if (dst.w >= src.w)
		VDVERIFY(VDPixmapResample(dst, src, IVDPixmapResampler::kFilterCubic));
	else
		VDVERIFY(VDPixmapResample(dst, src, IVDPixmapResampler::kFilterSharpLinear));
}

void ATUIQuickBarWidget::ActivateItem(uint32 idx) {
	const ItemInfo& item = mItems[idx];

	if (mpActiveSubMenu) {
		mpActiveSubMenu->Destroy();
		mpActiveSubMenu = nullptr;
	}

	if (item.mbSubMenu) {
		mpActiveSubMenu = new ATUIQuickBarWidget;

		uint32 idx2 = idx;

		while(idx2 && mItems[idx2 - 1].mbSubItem)
			--idx2;

		mpActiveSubMenu->SetBaseIconSize(mBaseItemSize);
		mpActiveSubMenu->InitSubMenu(mCommands, vdspan(&mItems[idx2], idx - idx2), mIcons);

		ATUIContainer *c = GetManager()->GetMainWindow();

		sint32 xoff = 0;

		for(const ItemInfo& item2 : vdspan(mItems.data(), idx)) {
			if (item2.mbSubItem && !mbIsSubMenu)
				continue;

			if (item2.mCommandCount)
				xoff += mbVertical ? mItemHeight : mItemWidth;
			else
				xoff += mSeparatorLength;

			xoff += mSpacing;
		}

		vdpoint32 pt;
		
		c->TranslateScreenPtToClientPt(TranslateClientPtToScreenPt(vdpoint32(xoff, 0)), pt);
		c->AddChild(mpActiveSubMenu);

		mpActiveSubMenu->SetPlacement(vdrect32f(0.0f, 0.0f, 0.0f, 0.0f), pt, vdfloat2(0, 1));
		mpActiveSubMenu->SetAutoSize();

		vdrefptr<ATUIWidgetRelativeAnimator> relAnim(new ATUIWidgetRelativeAnimator);
		relAnim->SetReference(*this, vdfloat2{0, 0}, vdpoint32(xoff, 0));
		mpActiveSubMenu->AddAnimator(*relAnim);

		mpActiveSubMenu->SetOnCaptureLost(
			[this] {
				CloseSubMenu();
			}
		);

		if (item.mbAutoCloseSubMenu) {
			mpActiveSubMenu->SetOnItemActivated(
				[this] {
					CloseSubMenu();
				}
			);
		}

		GetManager()->SetActiveWindow(mpActiveSubMenu);

		if (mpOnSubMenuChange)
			mpOnSubMenuChange(true);
	} else {
		ActivateCommand(item.mCommandStartIndex + item.mNextCommandIndex);

		if (mpOnItemActivated)
			mpOnItemActivated();
	}
}

void ATUIQuickBarWidget::ActivateCommand(uint32 idx) {
	const CommandEntry& ce = mCommands[idx];
	const ATUICommand *cmd = ce.mpCommand;

	if (cmd) {
		auto& cm = ATUIGetCommandManager();

		ATUICommandOptions opts;
		opts.mbQuiet = ce.mbCmdQuiet;

		try {
			cm.ExecuteCommand(*cmd, opts);
		} catch(const VDException& e) {
			ATUIShowError(e);
		}

		UpdateItems();
	}
}

void ATUIQuickBarWidget::CloseSubMenu() {
	if (mpActiveSubMenu) {
		mpActiveSubMenu->Destroy();
		mpActiveSubMenu = nullptr;

		if (mpOnSubMenuChange)
			mpOnSubMenuChange(false);
	}
}
