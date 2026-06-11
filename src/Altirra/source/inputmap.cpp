//	Altirra - Atari 800/800XL/5200 emulator
//	Copyright (C) 2024 Avery Lee
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
#include <vd2/system/registry.h>
#include "inputmap.h"

ATInputMap::ATInputMap()
	: mSpecificInputUnit(-1)
	, mbQuickMap(false)
{
}

ATInputMap::~ATInputMap() {
}

const wchar_t *ATInputMap::GetName() const {
	return mName.c_str();
}

void ATInputMap::SetName(const wchar_t *name) {
	mName = name;
}

bool ATInputMap::UsesPhysicalPort(int portIdx) const {
	for(Controllers::const_iterator it(mControllers.begin()), itEnd(mControllers.end()); it != itEnd; ++it) {
		const Controller& c = *it;

		switch(c.mType) {
			case kATInputControllerType_Joystick:
			case kATInputControllerType_STMouse:
			case kATInputControllerType_5200Controller:
			case kATInputControllerType_LightPen:
			case kATInputControllerType_Tablet:
			case kATInputControllerType_KoalaPad:
			case kATInputControllerType_AmigaMouse:
			case kATInputControllerType_Keypad:
			case kATInputControllerType_Trackball_CX80:
			case kATInputControllerType_5200Trackball:
			case kATInputControllerType_Driving:
			case kATInputControllerType_Keyboard:
			case kATInputControllerType_LightGun:
			case kATInputControllerType_PowerPad:
			case kATInputControllerType_LightPenStack:
				if (c.mIndex == portIdx)
					return true;
				break;

			case kATInputControllerType_Paddle:
				if ((c.mIndex >> 1) == portIdx)
					return true;
				break;
		}
	}

	return false;
}

void ATInputMap::Clear() {
	mControllers.clear();
	mMappings.clear();
	mSpecificInputUnit = -1;
}

uint32 ATInputMap::GetControllerCount() const {
	return (uint32)mControllers.size();
}

bool ATInputMap::HasControllerType(ATInputControllerType type) const {
	return std::find_if(mControllers.begin(), mControllers.end(),
		[=](const Controller& c) { return c.mType == type; }) != mControllers.end();
}

bool ATInputMap::HasController(ATInputControllerType type, uint32 index) const {
	return std::find_if(mControllers.begin(), mControllers.end(),
		[=](const Controller& c) { return c.mType == type && c.mIndex == index; }) != mControllers.end();
}

const ATInputMap::Controller& ATInputMap::GetController(uint32 i) const {
	return mControllers[i];
}

uint32 ATInputMap::AddController(ATInputControllerType type, uint32 index) {
	uint32 cindex = (uint32)mControllers.size();
	Controller& c = mControllers.push_back();

	c.mType = type;
	c.mIndex = index;

	return cindex;
}

void ATInputMap::AddControllers(std::initializer_list<Controller> controllers) {
	mControllers.insert(mControllers.end(), controllers.begin(), controllers.end());
}

uint32 ATInputMap::GetMappingCount() const {
	return (uint32)mMappings.size();
}

const ATInputMap::Mapping& ATInputMap::GetMapping(uint32 i) const {
	return mMappings[i];
}

void ATInputMap::AddMapping(uint32 inputCode, uint32 controllerId, uint32 code) {
	Mapping& m = mMappings.push_back();

	m.mInputCode = inputCode;
	m.mControllerId = controllerId;
	m.mCode = code;
}

void ATInputMap::AddMappings(std::initializer_list<Mapping> mappings) {
	mMappings.insert(mMappings.end(), mappings.begin(), mappings.end());
}

bool ATInputMap::Load(VDRegistryKey& key, const char *name) {
	int len = key.getBinaryLength(name);

	if (len < 16)
		return false;

	vdfastvector<uint32> heap;
	const uint32 heapWords = (len + 3) >> 2;
	heap.resize(heapWords, 0);

	if (!key.getBinary(name, (char *)heap.data(), len))
		return false;

	const uint32 version = heap[0];
	uint32 headerWords = 4;
	if (version == 2)
		headerWords = 5;
	else if (version != 1)
		return false;

	uint32 nameLen = heap[1];
	uint32 nameWords = (nameLen + 1) >> 1;
	uint32 ctrlCount = heap[2];
	uint32 mapCount = heap[3];

	if (headerWords >= 5) {
		mSpecificInputUnit = heap[4];
	} else {
		mSpecificInputUnit = -1;
	}

	if (((nameLen | ctrlCount | mapCount) & 0xff000000) || headerWords + nameWords + 2*ctrlCount + 3*mapCount > heapWords)
		return false;

	const uint32 *src = heap.data() + headerWords;

#if WCHAR_MAX > 0xFFFF
	// Non-Windows: wchar_t is 32-bit, but the binary format stores 16-bit
	// characters packed two per uint32 (matching Windows wchar_t layout).
	// Widen each uint16 to wchar_t.
	mName.resize(nameLen);
	const uint16 *nameSrc = (const uint16 *)src;
	for (uint32 i = 0; i < nameLen; ++i)
		mName[i] = (wchar_t)nameSrc[i];
#else
	mName.assign((const wchar_t *)src, (const wchar_t *)src + nameLen);
#endif
	src += nameWords;

	mControllers.resize(ctrlCount);
	for(uint32 i=0; i<ctrlCount; ++i) {
		Controller& c = mControllers[i];

		c.mType = (ATInputControllerType)src[0];
		c.mIndex = src[1];
		src += 2;
	}

	mMappings.resize(mapCount);
	for(uint32 i=0; i<mapCount; ++i) {
		Mapping& m = mMappings[i];

		m.mInputCode = src[0];
		m.mControllerId = src[1];
		m.mCode = src[2];
		src += 3;
	}

	return true;
}

void ATInputMap::Save(VDRegistryKey& key, const char *name) {
	vdfastvector<uint32> heap;

	heap.push_back(2);
	heap.push_back(mName.size());
	heap.push_back((uint32)mControllers.size());
	heap.push_back((uint32)mMappings.size());
	heap.push_back(mSpecificInputUnit);

	// The binary format stores the name as 16-bit characters packed two per
	// uint32 word — matching the Windows wchar_t (2-byte) layout.
	uint32 nameWords = ((uint32)mName.size() + 1) >> 1;
	uint32 offset = (uint32)heap.size();
	heap.resize(heap.size() + nameWords, 0);

#if WCHAR_MAX > 0xFFFF
	// Non-Windows: wchar_t is 32-bit.  Narrow each character to uint16
	// before packing into the heap.
	uint16 *nameDst = (uint16 *)&heap[offset];
	for (size_t i = 0; i < mName.size(); ++i)
		nameDst[i] = (uint16)mName[i];
#else
	mName.copy((wchar_t *)&heap[offset], mName.size());
#endif

	for(Controllers::const_iterator it(mControllers.begin()), itEnd(mControllers.end()); it != itEnd; ++it) {
		const Controller& c = *it;

		heap.push_back(c.mType);
		heap.push_back(c.mIndex);
	}

	for(Mappings::const_iterator it(mMappings.begin()), itEnd(mMappings.end()); it != itEnd; ++it) {
		const Mapping& m = *it;

		heap.push_back(m.mInputCode);
		heap.push_back(m.mControllerId);
		heap.push_back(m.mCode);
	}

	key.setBinary(name, (const char *)heap.data(), (int)heap.size() * 4);
}
