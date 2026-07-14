//	Altirra - Atari 800/800XL/5200 emulator
//	Copyright (C) 2009-2018 Avery Lee
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
#include "printertypes.h"
#include "uiconfgeneric.h"

bool ATUIConfDevPrinter(VDGUIHandle hParent, ATPropertySet& props) {
	return ATUIShowDialogGenericConfig(hParent, props, L"820/1025/1029 Printer",
		[](IATUIConfigView& view) {
			auto& graphicsOption = view.AddCheckbox();
			graphicsOption.SetText(L"Enable &graphical output").SetTag("graphics").SetLabel(L"Options");

			auto& accurateOption = view.AddCheckbox();
			accurateOption.SetText(L"Enable accurate &timing").SetTag("accurate_timing").SetEnableExpr(
				[&] { return graphicsOption.GetValue(); }
			);

			view.AddCheckbox().SetText(L"Enable &sound").SetTag("sound").SetEnableExpr(
				[&] { return graphicsOption.GetValue() && accurateOption.GetValue(); }
			);
		}
	);
}

bool ATUIConfDevPrinterHLE(VDGUIHandle hParent, ATPropertySet& props) {
	return ATUIShowDialogGenericConfig(hParent, props, L"Printer (P:)",
		[](IATUIConfigView& view) {
			auto& translationModeOption = view.AddDropDown<ATPrinterPortTranslationMode>();
			translationModeOption.AddChoice(ATPrinterPortTranslationMode::Default, L"Default: Translate EOL -> CR");
			translationModeOption.AddChoice(ATPrinterPortTranslationMode::Raw, L"Raw: No translation");
			translationModeOption.AddChoice(ATPrinterPortTranslationMode::AtasciiToUtf8, L"ATASCII to UTF-8");
			translationModeOption.SetTag("translation_mode").SetLabel(L"Port &Translation");
		}
	);
}
