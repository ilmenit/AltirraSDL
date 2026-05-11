//	Altirra - Atari 800/800XL/5200 emulator
//	Copyright (C) 2009-2020 Avery Lee
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
#include <at/atcore/media.h>
#include <at/atnativeui/genericdialog.h>
#include <at/atui/uicommandmanager.h>
#include "cmdhelpers.h"
#include "options.h"
#include "oshelper.h"
#include "uiaccessors.h"
#include "uicommondialogs.h"

void ATUIShowDialogSetFileAssociations(VDGUIHandle parent, bool allowElevation, bool userOnly);
void ATUIShowDialogRemoveFileAssociations(VDGUIHandle parent, bool allowElevation, bool userOnly);

template<auto T_Field>
constexpr ATUICommand ATMakeOnCommandOptionToggle(const char *name, ATUICmdTestFn testFn = nullptr) {
	using namespace ATCommands;

	return ATUICommand {
		name,
		[] {
			auto prev = g_ATOptions;
			g_ATOptions.*T_Field = !(g_ATOptions.*T_Field);
			ATOptionsRunUpdateCallbacks(&prev);
			g_ATOptions.mbDirty = true;
			ATOptionsSave();
		},
		testFn,
		[] {
			return ToChecked(g_ATOptions.*T_Field);
		},
		nullptr
	};
}

template<auto T_Field, auto T_Value>
constexpr ATUICommand ATMakeOnCommandOptionRadio(const char *name) {
	using namespace ATCommands;

	return ATUICommand {
		name,
		[] {
			auto prev = g_ATOptions;

			if (g_ATOptions.*T_Field != T_Value) {
				g_ATOptions.*T_Field = T_Value;

				ATOptionsRunUpdateCallbacks(&prev);
				g_ATOptions.mbDirty = true;
				ATOptionsSave();
			}
		},
		nullptr,
		[] {
			return ToRadio(g_ATOptions.*T_Field == T_Value);
		},
		nullptr
	};
}

void ATUICmdResetAllDialogs(ATUICommandContext& ctx) {
	if (ctx.mbQuiet || ATUIShowWarningConfirm(ATUIGetNewPopupOwner(),
		L"This will re-enable all dialogs previously hidden using the \"don't show this again\" option. Are you sure?",
		L"Reset All Dialogs"))
	{
		ATUIGenericDialogUndoAllIgnores();
	}
}

void ATUICmdSetFileAssociationsForUser() {
	ATUIShowDialogSetFileAssociations(ATUIGetNewPopupOwner(), true, true);
}

void ATUICmdSetFileAssociationsForAll() {
	ATUIShowDialogSetFileAssociations(ATUIGetNewPopupOwner(), true, false);
}

void ATUICmdUnsetFileAssociationsForUser() {
	ATUIShowDialogRemoveFileAssociations(ATUIGetNewPopupOwner(), true, true);
}

void ATUICmdUnsetFileAssociationsForAll() {
	ATUIShowDialogRemoveFileAssociations(ATUIGetNewPopupOwner(), true, false);
}

void ATUIInitCommandMappingsOption(ATUICommandManager& cmdMgr) {
	static constexpr ATUICommand kCommands[]={
		ATMakeOnCommandOptionToggle<&ATOptions::mbSingleInstance>("Options.ToggleSingleInstance"),
		ATMakeOnCommandOptionToggle<&ATOptions::mbPauseDuringMenu>("Options.PauseDuringMenu"),
		ATMakeOnCommandOptionToggle<&ATOptions::mbPollDirectories>("Options.ToggleDirectoryPolling"),
		ATMakeOnCommandOptionToggle<&ATOptions::mbDarkTheme>("Options.UseDarkTheme"),
		ATMakeOnCommandOptionRadio<&ATOptions::mEfficiencyMode, ATProcessEfficiencyMode::Default>("Options.EfficiencyModeDefault"),
		ATMakeOnCommandOptionRadio<&ATOptions::mEfficiencyMode, ATProcessEfficiencyMode::Performance>("Options.EfficiencyModePerformance"),
		ATMakeOnCommandOptionRadio<&ATOptions::mEfficiencyMode, ATProcessEfficiencyMode::Efficiency>("Options.EfficiencyModeEfficiency"),
		{ "Options.ResetAllDialogs", ATUICmdResetAllDialogs },
		ATMakeOnCommandOptionToggle<&ATOptions::mbLaunchAutoProfile>("Options.ToggleLaunchAutoProfile"),
		{ "Options.SetFileAssocForUser", ATUICmdSetFileAssociationsForUser },
		{ "Options.SetFileAssocForAll", ATUICmdSetFileAssociationsForAll },
		{ "Options.UnsetFileAssocForUser", ATUICmdUnsetFileAssociationsForUser },
		{ "Options.UnsetFileAssocForAll", ATUICmdUnsetFileAssociationsForAll },
		ATMakeOnCommandOptionRadio<&ATOptions::mErrorMode, kATErrorMode_Dialog>("Options.ErrorModeDialog"),
		ATMakeOnCommandOptionRadio<&ATOptions::mErrorMode, kATErrorMode_Debug>("Options.ErrorModeDebug"),
		ATMakeOnCommandOptionRadio<&ATOptions::mErrorMode, kATErrorMode_Pause>("Options.ErrorModePause"),
		ATMakeOnCommandOptionRadio<&ATOptions::mErrorMode, kATErrorMode_ColdReset>("Options.ErrorModeReset"),

		ATMakeOnCommandOptionRadio<&ATOptions::mDefaultWriteMode, kATMediaWriteMode_RO>("Options.MediaDefaultModeRO"),
		ATMakeOnCommandOptionRadio<&ATOptions::mDefaultWriteMode, kATMediaWriteMode_VRWSafe>("Options.MediaDefaultModeVRWSafe"),
		ATMakeOnCommandOptionRadio<&ATOptions::mDefaultWriteMode, kATMediaWriteMode_VRW>("Options.MediaDefaultModeVRW"),
		ATMakeOnCommandOptionRadio<&ATOptions::mDefaultWriteMode, kATMediaWriteMode_RW>("Options.MediaDefaultModeRW"),

		ATMakeOnCommandOptionToggle<&ATOptions::mbDisplayD3D9>("Options.ToggleDisplayD3D9"),
		ATMakeOnCommandOptionToggle<&ATOptions::mbDisplay3D>("Options.ToggleDisplayD3D11"),
		ATMakeOnCommandOptionToggle<&ATOptions::mbDisplay16Bit>("Options.ToggleDisplay16Bit", [] { return g_ATOptions.mbDisplayD3D9; }),
		ATMakeOnCommandOptionToggle<&ATOptions::mbDisplayCustomRefresh>("Options.ToggleDisplayCustomRefresh", [] { return g_ATOptions.mbDisplay3D; }),

		ATMakeOnCommandOptionToggle<&ATOptions::mbTextOutputLFOnly>("Options.ToggleTextOutputLFOnly"),
	};

	cmdMgr.RegisterCommands(kCommands, vdcountof(kCommands));
}
