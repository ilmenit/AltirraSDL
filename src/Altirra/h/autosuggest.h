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

#ifndef f_AT_AUTOSUGGEST_H
#define f_AT_AUTOSUGGEST_H

#include <vd2/system/refcount.h>
#include <vd2/system/vdstl.h>
#include <vd2/system/VDString.h>

// User-toggleable BASIC-aware suggestion categories.
//
// Address-symbol completion (PEEK/POKE digit-extension) and CIO path
// completion are always on — they were the engine's only behaviour
// before this struct existed and have no failure mode worth gating.
//
// Statement / function / variable suggestions are derived from the
// Atari + Altirra BASIC keyword set and from the BASIC interpreter's
// own variable-name table (VNT, zero-page pointers $82/$84).  They
// only make sense when the BASIC cartridge is active, so a runtime
// toggle keeps the noise out of e.g. assembly listings rendered in
// the screen editor.
struct ATAutoSuggestOptions {
	bool mbStatementKeywords = true;
	bool mbFunctionKeywords  = true;
	bool mbVariableNames     = true;
};

class IATAutoSuggestEngine : public IVDRefCount {
public:
	struct MatchInfo {
		VDStringSpanA mMatchedText;
		VDStringSpanA mMatchedGroup;
	};

	struct ResultInfo {
		// Text that will be inserted for the completion.
		VDStringA mInsertionText;

		// Display text representing the item being completed.
		VDStringW mItemText;

		// Display text describing the item completion.
		VDStringW mDescriptionText;

		// Optional canonical-syntax hint, used by the popup to show a
		// secondary "Syntax: ..." line under the selected entry.  Empty
		// for completions that don't have a manual entry (variables,
		// CIO paths, PEEK/POKE address symbols).
		VDStringW mSyntaxText;
	};

	using ResultSink = vdfunction<void(const ResultInfo&)>;

	virtual void AddStaticPattern(
		VDStringSpanA regex,
		VDStringSpanA insertionText,
		VDStringSpanW displayText
	) = 0;

	virtual void AddDynamicPattern(VDStringSpanA regex, vdfunction<void(const ResultSink&, const MatchInfo&)> handler) = 0;

	// Configure which BASIC-aware suggestion categories are active.
	// Default is "all on"; the SDL3 frontend persists user choices and
	// pushes them via this entry point.  Windows callers can do the
	// same once the Configure Suggestions dialog is ported.
	virtual void SetOptions(const ATAutoSuggestOptions& options) = 0;

	virtual void Update(VDStringSpanA text) = 0;

	virtual size_t GetNumResults() const = 0;
	virtual const ResultInfo& GetResult(size_t i) const = 0;
};

vdrefptr<IATAutoSuggestEngine> ATCreateAutoSuggestEngine();

// Returns true when the BASIC zero-page pointer chain
// (VNTP/VNTD/VVTP/STMTAB/STARP at $82-$8D) is consistent.  Every Atari
// BASIC dialect that shares the original Atari BASIC variable/statement
// layout — Atari BASIC rev A/B/C, Altirra BASIC, BASIC XL/XE, Turbo-
// BASIC XL — passes this test.  Non-BASIC contexts (DOS, machine-code
// monitors, OSS Action!) return false, so BASIC-specific features
// (variable-name completion, auto-line-numbering) can short-circuit
// when there is no BASIC interpreter loaded.
bool ATIsBasicMemoryLayoutValid();

// Returns true if `lineNumber` (0..32767) is already stored in the
// BASIC statement table at STMTAB ($88..$8B).  Stops scanning at the
// direct-mode sentinel (line 32768) or at STARP, whichever comes
// first.  Requires ATIsBasicMemoryLayoutValid() — returns false in
// non-BASIC contexts (so callers don't have to gate every callsite).
bool ATBasicProgramLineExists(uint16 lineNumber);

#endif
