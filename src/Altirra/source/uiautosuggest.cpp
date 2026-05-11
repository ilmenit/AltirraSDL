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
#include <at/atui/uimanager.h>
#include "uiautosuggest.h"
#include "autosuggest.h"

////////////////////////////////////////////////////////////////////////////////

struct ATUIAutoSuggestItemData {
	VDStringW mItemText;
	VDStringW mDescriptionText;
};

////////////////////////////////////////////////////////////////////////////////

class ATUIAutoSuggestItemView final : public ATUIWidget {
public:
	ATUIAutoSuggestItemView(IVDDisplayFont& font);

	void SetItemData(const ATUIAutoSuggestItemData& data);

private:
	void OnCreate() override;
	void Paint(IVDDisplayRenderer& rdr, sint32 w, sint32 h) override;

	const vdrefptr<IVDDisplayFont> mpFont;
	VDStringW mItemText;
	VDStringW mDescriptionText;
};

ATUIAutoSuggestItemView::ATUIAutoSuggestItemView(IVDDisplayFont& font)
	: mpFont(&font)
{
	SetAlphaFillColor(0);
}

void ATUIAutoSuggestItemView::SetItemData(const ATUIAutoSuggestItemData& data) {
	mItemText = data.mItemText;
	mDescriptionText = data.mDescriptionText;

	Invalidate();
}

void ATUIAutoSuggestItemView::OnCreate() {
	ATUIWidget::OnCreate();
}

void ATUIAutoSuggestItemView::Paint(IVDDisplayRenderer& rdr, sint32 w, sint32 h) {
	auto *tr = rdr.GetTextRenderer();

	tr->Begin();
	tr->SetFont(mpFont);

	tr->SetAlignment(tr->kAlignLeft, tr->kVertAlignTop);
	tr->SetColorRGB(0xe0e0e0);
	tr->DrawTextLine(1, 1, mItemText.c_str());

	tr->SetAlignment(tr->kAlignRight, tr->kVertAlignTop);
	tr->SetColorRGB(0xa0a0a0);
	tr->DrawTextLine(w - 1, 1, mDescriptionText.c_str());
	tr->End();
}

////////////////////////////////////////////////////////////////////////////////

class ATUIAutoSuggestItemPresenter final : public vdrefcounted<IATUIListViewItemPresenter> {
public:
	ATUIAutoSuggestItemPresenter(IVDDisplayFont& font);

	vdrefptr<ATUIWidget> CreateView() override;
	void SetViewData(ATUIWidget& w, const VDAny& data) override;

private:
	vdrefptr<IVDDisplayFont> mpFont;
};

ATUIAutoSuggestItemPresenter::ATUIAutoSuggestItemPresenter(IVDDisplayFont& font)
	: mpFont(&font)
{
}

vdrefptr<ATUIWidget> ATUIAutoSuggestItemPresenter::CreateView() {
	return vdmakerefcounted<ATUIAutoSuggestItemView>(*mpFont);
}

void ATUIAutoSuggestItemPresenter::SetViewData(ATUIWidget& w, const VDAny& data) {
	auto *p = data.TryGetValue<ATUIAutoSuggestItemData>();
	if (p)
		static_cast<ATUIAutoSuggestItemView&>(w).SetItemData(*p);
}

////////////////////////////////////////////////////////////////////////////////

ATUIAutoSuggestPopup::ATUIAutoSuggestPopup() {
}

void ATUIAutoSuggestPopup::SetOnSelectionChanged(vdfunction<void(sint32)> fn) {
	mpOnSelectionChanged = std::move(fn);
}

void ATUIAutoSuggestPopup::SetOnEntrySelected(vdfunction<void(sint32)> fn) {
	mpOnEntrySelected = std::move(fn);
}

bool ATUIAutoSuggestPopup::HasSelection() const {
	return GetSelectedIndex() >= 0;
}

sint32 ATUIAutoSuggestPopup::GetSelectedIndex() const {
	return mpListView ? mpListView->GetSelectedItem() : -1;
}

vdsize32 ATUIAutoSuggestPopup::GetIdealSize() const {
	if (!mpFont)
		return vdsize32(0, 0);

	const vdsize32& ref = mpFont->MeasureString(L"0A", 2, false);

	return vdsize32(
		(ref.w * 50) / 2,
		ref.h * 20
	);
}

void ATUIAutoSuggestPopup::UpdateSuggestions(const IATAutoSuggestEngine& ase) {
	mpListView->RemoveAllItems();

	const size_t n = ase.GetNumResults();

	for(size_t i = 0; i < n; ++i) {
		const auto& resultInfo = ase.GetResult(i);

		ATUIAutoSuggestItemData data;
		data.mItemText = resultInfo.mItemText;
		data.mDescriptionText = resultInfo.mDescriptionText;

		mpListView->AddItemData(std::move(data));
	}
}

void ATUIAutoSuggestPopup::OnCreate() {
	ATUIContainer::OnCreate();
	
	mpFont = mpManager->GetThemeFont(kATUIThemeFont_Default);

	mpListView = new ATUIListView;
	AddChild(mpListView);
	mpListView->SetPlacementFill();
	mpListView->SetItemPresenter(vdmakerefcounted<ATUIAutoSuggestItemPresenter>(*mpFont));

	mpListView->OnItemSelectedEvent() = [this](sint32 idx) {
		if (mpOnSelectionChanged)
			mpOnSelectionChanged(idx);
	};

	BindAction(kATUIVK_UIUp, ATUIListView::kActionMoveUp, 0, mpListView->GetInstanceId());
	BindAction(kATUIVK_UIDown, ATUIListView::kActionMoveDown, 0, mpListView->GetInstanceId());
	ForwardAction(kActionMoveUp, ATUIListView::kActionMoveUp,  mpListView->GetInstanceId());
	ForwardAction(kActionMoveDown, ATUIListView::kActionMoveDown, mpListView->GetInstanceId());
	ForwardAction(kActionMovePageUp, ATUIListView::kActionMovePagePrev,  mpListView->GetInstanceId());
	ForwardAction(kActionMovePageDown, ATUIListView::kActionMovePageNext, mpListView->GetInstanceId());
}

void ATUIAutoSuggestPopup::OnDestroy() {
	mpListView = nullptr;

	UnbindAllActions();

	ATUIContainer::OnDestroy();
}

void ATUIAutoSuggestPopup::OnActionStart(uint32 action) {
	switch(action) {
		case kActionAccept:
			if (mpOnEntrySelected)
				mpOnEntrySelected(mpListView->GetSelectedItem());
			break;

		default:
			ATUIContainer::OnActionStart(action);
			break;
	}
}
