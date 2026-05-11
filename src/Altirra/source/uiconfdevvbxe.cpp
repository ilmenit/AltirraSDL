//	Altirra - Atari 800/800XL/5200 emulator
//	Copyright (C) 2009-2026 Avery Lee
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
#include <at/atcore/propertyset.h>
#include "uiconfgeneric.h"

bool ATUIConfDevVBXE(VDGUIHandle hParent, ATPropertySet& props) {
	return ATUIShowDialogGenericConfig(
		hParent,
		props,
		L"VideoBoard XE Options",
		[](IATUIConfigView& view) {
			view.AddIntDropDown()
				.SetLabel(L"&Core version")
				.SetTag("version")
				.AddChoice(126, L"FX 1.26")
				.AddChoice(124, L"FX 1.24")
				.AddChoice(120, L"FX 1.20");

			view.AddVerticalSpace();

			view.AddBoolChoice()
				.SetLabel(L"&Base address")
				.SetTag("alt_page")
				.SetFirstChoiceValue(false)
				.SetFirstChoiceText(L"$D&600-D6FF")
				.SetSecondChoiceText(L"$D&700-D7FF")
				.SetValue(false);

			view.AddVerticalSpace();

			view.AddCheckbox()
				.SetLabel(L"&Shared memory mode")
				.SetTag("shared_mem")
				.SetText(L"Use VBXE for PORTB extended memory");
		}
	);
}
