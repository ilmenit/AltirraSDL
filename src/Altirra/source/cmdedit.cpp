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
#include "cmdhelpers.h"
#include "uifrontend.h"

void ATUIOnCommandEditShowSuggestions() {
	ATUIGetFrontEnd().ShowSuggestions();
}

void ATUIOnCommandEditToggleAutoShowSuggestions() {
	auto& fe = ATUIGetFrontEnd();

	fe.SetAutoSuggestEnabled(!fe.IsAutoSuggestEnabled());
}

bool ATUIQueryCommandEditToggleAutoShowSuggestions() {
	return ATUIGetFrontEnd().IsAutoSuggestEnabled();
}

void ATUIInitCommandMappingsEdit(ATUICommandManager& cmdMgr) {
	using namespace ATCommands;

	static constexpr ATUICommand kCommands[] = 
	{
		{ "Edit.ShowSuggestions", ATUIOnCommandEditShowSuggestions },
		{ "Edit.ToggleAutoShowSuggestions", ATUIOnCommandEditToggleAutoShowSuggestions, nullptr, [] { return ToChecked(ATUIQueryCommandEditToggleAutoShowSuggestions()); } },
	};

	cmdMgr.RegisterCommands(kCommands, vdcountof(kCommands));
}
