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
#include <charconv>
#include <regex>
#include <at/atcore/deviceautosuggest.h>
#include <at/atdebugger/defsymbols.h>
#include "autosuggest.h"
#include "devicemanager.h"
#include "simulator.h"

extern ATSimulator g_sim;

class ATAutoSuggestEngine final : public vdrefcounted<IATAutoSuggestEngine> {
public:
	ATAutoSuggestEngine();

	void AddStaticPattern(
		VDStringSpanA regex,
		VDStringSpanA insertionText,
		VDStringSpanW displayText
	) override;

	void AddDynamicPattern(VDStringSpanA regex, vdfunction<void(const ResultSink& resultSink, const MatchInfo&)> handler) override;

	void Update(VDStringSpanA text) override;

	size_t GetNumResults() const override;
	const ResultInfo& GetResult(size_t i) const override;

private:
	void HandlePeekPoke(bool poke, const ResultSink& resultSink, const MatchInfo& mi);
	void HandleCIOPath(const ResultSink& resultSink, const MatchInfo& mi);

	struct Rule {
		std::regex mRegex;
		vdfunction<void(const ResultSink& resultSink, const MatchInfo&)> mHandler;
	};

	vdvector<Rule> mRules;
	vdvector<ResultInfo> mResults;
};

ATAutoSuggestEngine::ATAutoSuggestEngine() {
	AddDynamicPattern(VDStringSpanA("POKE ([1-9][0-9]*)$"), std::bind_front(&ATAutoSuggestEngine::HandlePeekPoke, this, true));
	AddDynamicPattern(VDStringSpanA("PEEK\\(([1-9][0-9]*)$"), std::bind_front(&ATAutoSuggestEngine::HandlePeekPoke, this, false));
	AddDynamicPattern(VDStringSpanA("[^A-Z][A-Z](?:|[1-9]):([A-Z]*)$"), std::bind_front(&ATAutoSuggestEngine::HandleCIOPath, this));
}

void ATAutoSuggestEngine::AddStaticPattern(
	VDStringSpanA regex,
	VDStringSpanA insertionText,
	VDStringSpanW displayText
) {
	AddDynamicPattern(
		regex,
		[ri = ResultInfo { insertionText, displayText }](const ResultSink& resultSink, const MatchInfo& mi) {
			resultSink(ri);
		}
	);
}

void ATAutoSuggestEngine::AddDynamicPattern(VDStringSpanA regex, vdfunction<void(const ResultSink& resultSink, const MatchInfo&)> handler) {
	mRules.emplace_back(
		Rule {
			std::regex(regex.data(), regex.size()),
			std::move(handler)
		}
	);
}

void ATAutoSuggestEngine::Update(VDStringSpanA text) {
	mResults.clear();

	const vdfunction<void(const ResultInfo&)> resultSink = [this](const ResultInfo& ri) {
		mResults.push_back(ri);
	};

	for(const Rule& rule : mRules) {
		std::cmatch m;

		if (std::regex_search(text.begin(), text.end(), m, rule.mRegex)) {
			MatchInfo mi;
			mi.mMatchedText = VDStringSpanA(m[0].first, m[0].second);

			if (m.size() > 1)
				mi.mMatchedGroup = VDStringSpanA(m[1].first, m[1].second);

			rule.mHandler(resultSink, mi);
		}
	}
}

size_t ATAutoSuggestEngine::GetNumResults() const {
	return mResults.size();
}

const ATAutoSuggestEngine::ResultInfo& ATAutoSuggestEngine::GetResult(size_t i) const {
	return mResults[i];
}

void ATAutoSuggestEngine::HandlePeekPoke(bool poke, const ResultSink& resultSink, const MatchInfo& mi) {
	unsigned v = 0;
	(void)std::from_chars(mi.mMatchedGroup.begin(), mi.mMatchedGroup.end(), v, 10);

	// OS variables are between 512 ($0200) and 1024 ($0400), so we do autosuggest for
	// 51-102 with an extra digit.

	const std::pair<vdspan<const ATDefaultSymbolInfo>, uint32> symSources[] {
		{ ATGetSortedSymbolsForOSVariables(), 0 },
		{ ATGetSortedSymbolsForGTIAVariables(), 0xD000 },
		{ ATGetSortedSymbolsForPOKEYVariables(), 0xD200 },
		{ ATGetSortedSymbolsForPIAVariables(), 0xD300 },
		{ ATGetSortedSymbolsForANTICVariables(), 0 },
	};

	for(uint32 factor : { 10, 100 }) {
		const uint32 vbegin = v * factor;
		const uint32 vend = vbegin + factor;

		for(const auto& [syms, base] : symSources) {
			auto it = std::ranges::lower_bound(
				syms,
				vbegin,
				[](uint32 bin1, uint32 bin2) {
					return bin1 < bin2;
				},
				[base](const ATDefaultSymbolInfo& sym) {
					return sym.mAddress + base;
				}
			);

			auto itEnd = syms.end();

			for(; it != itEnd; ++it) {
				const auto& cand = *it;
				const uint32 address = cand.mAddress + base;

				if (address >= vend)
					break;

				switch(cand.mType) {
					case ATDefaultSymbolType::Default:
						break;

					case ATDefaultSymbolType::ReadOnly:
						if (poke)
							continue;
						break;

					case ATDefaultSymbolType::WriteOnly:
						if (!poke)
							continue;
						break;
				}

				ResultInfo ri;
				ri.mInsertionText.sprintf("%0*u", factor >= 100 ? 2 : 1, address % factor);
				ri.mItemText.sprintf(L"%u", address);
				ri.mDescriptionText.sprintf(L"%u ($%04X) %hs", address, address, cand.mName);

				resultSink(ri);
			}
		}
	}
}

void ATAutoSuggestEngine::HandleCIOPath(const ResultSink& resultSink, const MatchInfo& mi) {
	if (mi.mMatchedText.size() < 3) {
		VDFAIL("Regex should not have matched such a short string");
		return;
	}

	const char cioDevice = mi.mMatchedText[1];
	const char cioUnitChar = mi.mMatchedText[2];
	const uint8 unit = (cioUnitChar >= '1' && cioUnitChar <= '9') ? (uint8)(cioUnitChar - '0') : 1;

	const VDStringA cioRelativePath(mi.mMatchedGroup);

	class AutoSuggestSink final : public IATDeviceAutoSuggestSink {
	public:
		AutoSuggestSink(const ResultSink& rs) : mResultSink(rs) {}

		void AddSuggestion(const char *insertText, const wchar_t *itemText, const wchar_t *descriptionText) override {
			ResultInfo ri;
			ri.mInsertionText = insertText;
			ri.mItemText = itemText;
			ri.mDescriptionText = descriptionText;
			mResultSink(ri);
		}

		const ResultSink& mResultSink;
	} sink(resultSink);

	for(IATDeviceAutoSuggest *devAS : g_sim.GetDeviceManager()->GetInterfaces<IATDeviceAutoSuggest>(false, false, false)) {
		devAS->AutoSuggestCIOPaths(cioDevice, unit, cioRelativePath, sink);
	}
}

////////////////////////////////////////////////////////////////////////////////

vdrefptr<IATAutoSuggestEngine> ATCreateAutoSuggestEngine() {
	return vdmakerefcounted<ATAutoSuggestEngine>();
}
