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

#ifndef f_AT_UIAUTOSUGGEST_H
#define f_AT_UIAUTOSUGGEST_H

#include <at/atui/uicontainer.h>
#include <at/atuicontrols/uilistview.h>

class IATAutoSuggestEngine;

class ATUIAutoSuggestPopup final : public ATUIContainer {
public:
	static constexpr auto kActionMoveUp = kActionCustom;
	static constexpr auto kActionMoveDown = kActionCustom + 1;
	static constexpr auto kActionMovePageUp = kActionCustom + 2;
	static constexpr auto kActionMovePageDown = kActionCustom + 3;
	static constexpr auto kActionAccept = kActionCustom + 4;

	ATUIAutoSuggestPopup();

	void SetOnSelectionChanged(vdfunction<void(sint32)> fn);
	void SetOnEntrySelected(vdfunction<void(sint32)> fn);

	bool HasSelection() const;
	sint32 GetSelectedIndex() const;

	vdsize32 GetIdealSize() const;

	void UpdateSuggestions(const IATAutoSuggestEngine& ase);

private:
	void OnCreate() override;
	void OnDestroy() override;
	void OnActionStart(uint32 action) override;

	vdrefptr<IVDDisplayFont> mpFont;
	vdrefptr<ATUIListView> mpListView;
	vdfunction<void(sint32)> mpOnEntrySelected;
	vdfunction<void(sint32)> mpOnSelectionChanged;
};

#endif
