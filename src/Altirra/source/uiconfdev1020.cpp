//	Altirra - Atari 800/800XL/5200 emulator
//	Copyright (C) 2025 Avery Lee
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
#include "uiconfgeneric.h"

bool ATUIConfDev1020(VDGUIHandle hParent, ATPropertySet& props) {
	return ATUIShowDialogGenericConfig(hParent, props, L"1020 Color Printer",
		[](IATUIConfigView& view) {
			static constexpr uint32 kPalette[] {
				0x000000,	// black
				0x181FF0,	// blue
				0x0B9C2F,	// green
				0xC91B12,	// red
			};

			view.AddCheckbox()
				.SetTag("accurate_bresenham")
				.SetLabel(L"&Options")
				.SetText(L"Use accurate line stepping");

			auto& accurateOption = view.AddCheckbox();
			accurateOption
				.SetTag("accurate_timing")
				.SetText(L"Enable accurate &timing");

			view.AddCheckbox()
				.SetText(L"Enable &sound")
				.SetTag("sound")
				.SetEnableExpr(
					[&] { return accurateOption.GetValue(); }
				);

			view.AddVerticalSpace();

			view.AddColor().SetValue(0).SetFixedPalette(kPalette).SetCustomPaletteKey("1020 Pens").SetLabel(L"Pen &1 (Black)").SetTag("pencolor0");
			view.AddColor().SetValue(0).SetFixedPalette(kPalette).SetCustomPaletteKey("1020 Pens").SetLabel(L"Pen &2 (Blue)").SetTag("pencolor1");
			view.AddColor().SetValue(0).SetFixedPalette(kPalette).SetCustomPaletteKey("1020 Pens").SetLabel(L"Pen &3 (Green)").SetTag("pencolor2");
			view.AddColor().SetValue(0).SetFixedPalette(kPalette).SetCustomPaletteKey("1020 Pens").SetLabel(L"Pen &4 (Red)").SetTag("pencolor3");
		}
	);
}
