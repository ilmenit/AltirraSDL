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
#include "videomanager.h"

void ATVideoManager::ResetActivityCounters() {
	for(OutputInfo& output : mOutputs)
		output.mLastActivityCounter = output.mpOutput->GetActivityCounter();
}

sint32 ATVideoManager::CheckForNewlyActiveOutputs() {
	sint32 idx = 0;
	sint32 newActiveIdx = -1;

	for(OutputInfo& output : mOutputs) {
		uint32 ac = output.mpOutput->GetActivityCounter();

		if (output.mLastActivityCounter != ac) {
			output.mLastActivityCounter = ac;

			if (newActiveIdx < 0)
				newActiveIdx = idx;
		}

		++idx;
	}

	return newActiveIdx;
}

uint32 ATVideoManager::GetOutputCount() const {
	return (uint32)mOutputs.size();
}

uint32 ATVideoManager::GetOutputListChangeCount() const {
	return mListChangeCount;
}

IATDeviceVideoOutput *ATVideoManager::GetOutput(uint32 index) const {
	return index < mOutputs.size() ? mOutputs[index].mpOutput : nullptr;
}

IATDeviceVideoOutput *ATVideoManager::GetOutputByName(const char *name) const {
	for(const OutputInfo& output : mOutputs) {
		if (!strcmp(output.mpOutput->GetName(), name))
			return output.mpOutput;
	}

	return nullptr;
}

sint32 ATVideoManager::IndexOfOutput(IATDeviceVideoOutput *output) const {
	if (!output)
		return -1;

	auto it = std::find_if(mOutputs.begin(), mOutputs.end(), [output](const OutputInfo& oi) { return oi.mpOutput == output; });

	if (it != mOutputs.end())
		return (sint32)(it - mOutputs.begin());
	else
		return -1;
}

void ATVideoManager::AddVideoOutput(IATDeviceVideoOutput *output) {
	if (!output)
		return;

	const OutputInfo newEntry {
		output, output->GetActivityCounter()
	};

	auto it = std::lower_bound(mOutputs.begin(), mOutputs.end(), newEntry,
		[](const OutputInfo& a, const OutputInfo& b) {
			return VDStringSpanW(a.mpOutput->GetDisplayName()).comparei(b.mpOutput->GetDisplayName()) < 0;
		}
	);

	// Fork: outputs are sorted, so notify observers of the insertion
	// position rather than the old size when a new adapter sorts first.
	const uint32 index = (uint32)(it - mOutputs.begin());
	mOutputs.insert(it, newEntry);

	++mListChangeCount;

	mOnAddedOutput.NotifyAll(
		[index](auto fn) {
			(*fn)(index);
		}
	);
}

void ATVideoManager::RemoveVideoOutput(IATDeviceVideoOutput *output) {
	auto it = std::find_if(mOutputs.begin(), mOutputs.end(), [output](const OutputInfo& oi) { return oi.mpOutput == output; });

	if (it != mOutputs.end()) {
		uint32 index = (uint32)(it - mOutputs.begin());
		mOnRemovingOutput.NotifyAll(
			[index](auto fn) {
				(*fn)(index);
			}
		);

		mOutputs.erase(it);

		++mListChangeCount;
	}
}
