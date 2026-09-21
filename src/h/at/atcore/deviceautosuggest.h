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
//
//	As a special exception, this library can also be redistributed and/or
//	modified under an alternate license. See COPYING.RMT in the same source
//	archive for details.

#ifndef f_AT_ATCORE_DEVICEAUTOSUGGEST_H
#define f_AT_ATCORE_DEVICEAUTOSUGGEST_H

#include <vd2/system/vdtypes.h>
#include <vd2/system/unknown.h>

class VDStringA;
class IATDeviceAutoSuggestSink;

class IATDeviceAutoSuggest {
public:
	static constexpr auto kTypeID = "IATDeviceAutoSuggest"_vdtypeid;

	// Scan for automatic completion suggestions based on the CIO paths that
	// this device knows about. This is based on a partial path that is matched
	// against known or suggested CIO paths, and turned into suggestions to
	// complete the partial path.
	//
	// Suggestions are currently always pure appends, they cannot replace part
	// of the partial path that triggered the suggestion.
	//
	// cioDevice:
	//		CIO device character, i.e. 'D' for disk.
	//
	// unit:
	//		CIO unit number, i.e. 2 for D2:.
	//
	// path:
	//		Partial relative CIO file path to complete. Does not include the
	//		CIO device name, unit number, or colon. May be empty if
	//		auto-suggestion was triggered off only a CIO device reference.
	//
	// sink:
	//		Output sink for any paths discovered by the implementation.
	virtual void AutoSuggestCIOPaths(char cioDevice, uint8 unit, const VDStringA& path, IATDeviceAutoSuggestSink& sink) = 0;
};

class IATDeviceAutoSuggestSink {
public:
	// Add a suggestion.
	//
	// insertText:
	//		Text to insert at the caret if suggestion is accepted, after the
	//		original partial path. This should not include the partial path.
	//		This is the only part that actually affects what the suggestion
	//		does when applied.
	//
	// itemText:
	//		Text to represent the completed token. This should generally contain
	//		both the original partial token and the completed remainder, but may
	//		omit parts for brevity. For instance, a suggestion may complete
	//		"D1:HE" as "D1:HELLO.BAS", but just show "HELLO.BAS" in the UI as
	//		the token being completed.
	//
	// descriptionText:
	//		Additional, optional text to show elaborating on the suggestion that
	//		is not part of the token. For instance, a suggestion to complete
	//		"POKE 542" as "POKE 54272" may use a description of "$D400 (DMACTL)"
	//		to describe the suggestion, even though those aren't part of the
	//		completion.
	//
	//		This may be empty or null if there is no useful description.
	// 
	virtual void AddSuggestion(const char *insertText, const wchar_t *itemText, const wchar_t *descriptionText) = 0;
};

#endif
