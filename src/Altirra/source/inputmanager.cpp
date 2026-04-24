//	Altirra - Atari 800/800XL emulator
//	Copyright (C) 2009 Avery Lee
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
//	You should have received a copy of the GNU General Public License
//	along with this program; if not, write to the Free Software
//	Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.

#include <stdafx.h>
#include <vd2/system/bitmath.h>
#include <vd2/system/hash.h>
#include <vd2/system/math.h>
#include <vd2/system/registry.h>
#include <vd2/system/strutil.h>
#include <vd2/Dita/accel.h>
#include "inputdefs.h"
#include "inputmanager.h"
#include "inputmap.h"
#include "inputcontroller.h"
#include "joystick.h"

namespace {
	// digital - controls acceleration
	// analog - controls speed multiplier
	// impulse - controls impulse scale
	const float kSpeedScaleTable[16]={
		(float)( 0* 0)/25.0f,
		(float)( 1* 1)/25.0f,
		(float)( 2* 2)/25.0f,
		(float)( 3* 3)/25.0f,
		(float)( 4* 4)/25.0f,
		(float)( 5* 5)/25.0f,
		(float)( 6* 6)/25.0f,
		(float)( 7* 7)/25.0f,
		(float)( 8* 8)/25.0f,
		(float)( 9* 9)/25.0f,
		(float)(10*10)/25.0f
		// remaining 5 values are protective values
	};

	const float kAccelScaleTable[16]={
		(float)( 0* 0)/25.0f,
		(float)( 1* 1)/25.0f,
		(float)( 2* 2)/25.0f,
		(float)( 3* 3)/25.0f,
		(float)( 4* 4)/25.0f,
		(float)( 5* 5)/25.0f,
		(float)( 6* 6)/25.0f,
		(float)( 7* 7)/25.0f,
		(float)( 8* 8)/25.0f,
		(float)( 9* 9)/25.0f,
		(float)(10*10)/25.0f
		// remaining 5 values are protective values
	};
}

struct ATInputManager::PresetMapDef {
	bool mbDefault;
	bool mbDefaultQuick;
	const wchar_t *mpName;
	sint8 mUnit;

	std::initializer_list<ATInputMap::Controller> mControllers;
	std::initializer_list<ATInputMap::Mapping> mMappings;
};

///////////////////////////////////////////////////////////////////////

ATInputManager::ATInputManager()
	: mAllocatedUnits(0)
	, m5200ControllerIndex(0)
	, mb5200PotsEnabled(true)
	, mb5200Mode(false)
	, mbMouseMapped(false)
	, mbMouseAbsMode(false)
	, mbMouseActiveTarget(false)
	, mMouseAvgIndex(0)
{
	std::fill(mMouseAvgQueue, mMouseAvgQueue + sizeof(mMouseAvgQueue)/sizeof(mMouseAvgQueue[0]), 0x20002000);
	std::fill(mpUnitNameSources, mpUnitNameSources + vdcountof(mpUnitNameSources), (IATInputUnitNameSource *)NULL);
}

ATInputManager::~ATInputManager() {
}

void ATInputManager::Init(ATScheduler *fastSched, ATScheduler *slowSched, ATPokeyEmulator& pokey, IATDevicePortManager& portMgr, ATLightPenPort *lightPen) {
	mpPokey = &pokey;
	mpLightPen = lightPen;
	mpSlowScheduler = slowSched;
	mpFastScheduler = fastSched;
	mpPortMgr = &portMgr;
}

void ATInputManager::Shutdown() {
	RemoveAllInputMaps();
}

void ATInputManager::Set5200Mode(bool is5200) {
	if (mb5200Mode != is5200) {
		mb5200Mode = is5200;

		RebuildMappings();
	}
}

void ATInputManager::ResetToDefaults() {
	RemoveAllInputMaps();

	for(uint32 i=0; ; ++i) {
		const PresetMapDef *def = GetPresetMapDef(i);
		if (!def)
			break;

		if (!def->mbDefault)
			continue;

		vdrefptr<ATInputMap> imap;
		InitPresetMap(*def, ~imap);

		const uint32 ccnt = imap->GetControllerCount();
		for(uint32 i=0; i<ccnt; ++i) {
			const auto& controller = imap->GetController(i);

			if (ATInputIs5200ControllerType(controller.mType) != mb5200Mode)
				goto reject;
		}

		AddInputMap(imap);
reject:
		;
	}
}

void ATInputManager::Select5200Controller(int index, bool potsEnabled) {
	if (m5200ControllerIndex != index || mb5200PotsEnabled != potsEnabled) {
		m5200ControllerIndex = index;
		mb5200PotsEnabled = potsEnabled;
		Update5200Controller();
	}
}

void ATInputManager::SelectMultiJoy(int multiIndex) {
	const uint8 mask = multiIndex < 0 ? 0xFF : 1 << multiIndex;
	
	if (mMultiMask != mask) {
		mMultiMask = mask;

		for(const ControllerInfo& ci : mInputControllers)
			ci.mpInputController->SelectMultiJoy(mask);
	}
}

void ATInputManager::Update5200Controller() {
	// We do two passes here to make sure that everything is disconnected before
	// we connect a new controller.
	for(InputControllers::const_iterator it(mInputControllers.begin()), itEnd(mInputControllers.end()); it != itEnd; ++it) {
		ATPortInputController *pc = it->mpInputController;
		pc->Select5200Controller(-1, false);
	}

	for(InputControllers::const_iterator it(mInputControllers.begin()), itEnd(mInputControllers.end()); it != itEnd; ++it) {
		ATPortInputController *pc = it->mpInputController;
		pc->Select5200Controller(m5200ControllerIndex, mb5200PotsEnabled);
	}
}

void ATInputManager::ColdReset() {
	for(const ControllerInfo& ci : mInputControllers)
		ci.mpInputController->ColdReset();
}

void ATInputManager::Poll(float dt) {
	uint32 avgres = mMouseAvgQueue[0] + mMouseAvgQueue[1] + mMouseAvgQueue[2] + mMouseAvgQueue[3];
	int avgx = (avgres & 0xffff) - 0x8000;
	int avgy = (avgres >> 16) - 0x8000;
	int avgax = abs(avgx);
	int avgay = abs(avgy);

	// ignore very small deltas
	if (avgax <= 1 && avgay <= 1) {
		avgax = 0;
		avgay = 0;
	} else {
		// tan 22.5 deg = 0.4142135623730950488016887242097 ~= 53/128
		if (avgax * 53 >= avgay * 128)
			avgy = 0;

		if (avgay * 53 >= avgax * 128)
			avgx = 0;
	}

	OnButtonChanged(0, kATInputCode_MouseLeft, avgx < 0);
	OnButtonChanged(0, kATInputCode_MouseRight, avgx > 0);
	OnButtonChanged(0, kATInputCode_MouseUp, avgy < 0);
	OnButtonChanged(0, kATInputCode_MouseDown, avgy > 0);

	mMouseAvgQueue[++mMouseAvgIndex & 3] = 0x20002000;

	const int wheelDelta = VDRoundToInt(mMouseWheelAccum);
	mMouseWheelAccum -= (float)wheelDelta;

	OnButtonChanged(0, kATInputCode_MouseWheelUp, wheelDelta < 0);
	OnButtonChanged(0, kATInputCode_MouseWheelDown, wheelDelta > 0);
	OnButtonChanged(0, kATInputCode_MouseWheel, wheelDelta != 0);

	const int hwheelDelta = VDRoundToInt(mMouseHWheelAccum);
	mMouseHWheelAccum -= (float)hwheelDelta;

	OnButtonChanged(0, kATInputCode_MouseHWheelLeft, hwheelDelta < 0);
	OnButtonChanged(0, kATInputCode_MouseHWheelRight, hwheelDelta > 0);
	OnButtonChanged(0, kATInputCode_MouseHWheel, hwheelDelta != 0);

	// process mappings
	for(Mappings::iterator it(mMappings.begin()), itEnd(mMappings.end()); it != itEnd; ++it) {
		Mapping& mapping = it->second;

		if (!mapping.mbMotionActive)
			continue;

		Trigger& trigger = mTriggers[mapping.mTriggerIdx];
		const uint32 mode = trigger.mId & kATInputTriggerMode_Mask;

		switch(mode) {
			case kATInputTriggerMode_AutoFire:
			case kATInputTriggerMode_ToggleAF:
				if (++mapping.mAutoCounter >= mapping.mAutoPeriod) {
					mapping.mAutoCounter = 0;

					mapping.mAutoValue = !mapping.mAutoValue;

					bool newState = (mapping.mAutoValue != 0);

					SetTrigger(mapping, newState);
				}
				break;

			case kATInputTriggerMode_Relative:
				if (fabsf(mapping.mMotionSpeed) < 1e-4f && fabsf(mapping.mMotionAccel) < 1e-4f) {
					mapping.mbMotionActive = false;
				} else {
					float impulse = 0;

					if (fabsf(mapping.mMotionDrag) < 1e-4f) {
						// Undamped kinematics (d = vt + at^2/2)
						impulse = (0.5f * mapping.mMotionAccel * dt + mapping.mMotionSpeed) * dt;

						mapping.mMotionSpeed += mapping.mMotionAccel * dt;

					} else {
						// Damped kinematics (exponential decay towards terminal velocity)
						const float v0 = mapping.mMotionSpeed;
						const float vt = mapping.mMotionAccel / mapping.mMotionDrag;
						const float inv_tau = mapping.mMotionDrag;

						impulse = vt * dt + (v0 - vt)*(1.0f - expf(-dt*inv_tau))/inv_tau;

						mapping.mMotionSpeed = v0 + (vt - v0)*(1.0f - expf(-dt*inv_tau));
					}

					if (impulse < -2)
						impulse = -2;
					else if (impulse > 2)
						impulse = 2;

					trigger.mpController->ApplyImpulse(trigger.mId & kATInputTrigger_Mask, VDRoundToInt32(impulse * (float)0x10000));

					// clamp speed
					if (!(mapping.mMotionSpeed >= -10.0f && mapping.mMotionSpeed <= 10.0f))
						mapping.mMotionSpeed = (mapping.mMotionSpeed < 0) ? -10.0f : 10.0f;
				}
				break;
		}
	}

	mbMouseActiveTarget = false;

	for(InputControllers::iterator it(mInputControllers.begin()), itEnd(mInputControllers.end()); it != itEnd; ++it) {
		ATPortInputController *c = it->mpInputController;

		c->Tick();

		if (it->mbBoundToMouseAbs && c->IsActive())
			mbMouseActiveTarget = true;
	}
}

int ATInputManager::GetInputUnitCount() const {
	return 32;
}

const wchar_t *ATInputManager::GetInputUnitName(int index) const {
	if ((unsigned)index >= 32)
		return NULL;

	if (!(mAllocatedUnits & ((uint32)1 << index)))
		return NULL;

	return mUnitNames[index].c_str();
}

int ATInputManager::GetInputUnitIndexById(const ATInputUnitIdentifier& id) const {
	for(int i=0; i<32; ++i) {
		if (!(mAllocatedUnits & ((uint32)1 << i)))
			continue;

		if (mUnitIds[i] == id)
			return i;
	}

	return -1;
}

int ATInputManager::RegisterInputUnit(const ATInputUnitIdentifier& id, const wchar_t *name, IATInputUnitNameSource *nameSource) {
	if (mAllocatedUnits == 0xFFFFFFFF)
		return -1;

	int unit = VDFindLowestSetBitFast(~mAllocatedUnits);

	mAllocatedUnits |= (1 << unit);
	mUnitIds[unit] = id;
	mUnitNames[unit] = name;
	mpUnitNameSources[unit] = nameSource;

	return unit;
}

void ATInputManager::UnregisterInputUnit(int unit) {
	if (unit < 0)
		return;

	VDASSERT(unit < 32);
	uint32 bit = (1 << unit);

	VDASSERT(mAllocatedUnits & bit);

	mAllocatedUnits &= ~bit;
	mpUnitNameSources[unit] = NULL;
}

void ATInputManager::SetRestrictedMode(bool restricted) {
	mbRestrictedMode = restricted;
}

void ATInputManager::SetPortRedirect(int srcPortIdx, IATDeviceControllerPort *dstPort) {
	// Install / uninstall matches the semantics requested by the caller
	// regardless of prior state: a null dstPort clears any existing
	// redirect on matching controllers; a non-null dstPort installs
	// (or swaps to) the new target.

	// First undo any prior redirect on controllers that match the old
	// source but not the new one, or any that are currently redirected
	// (so the new target replaces cleanly).
	for (ControllerInfo& ci : mInputControllers) {
		if (ci.mpInputController && ci.mpInputController->GetAttachedPortIndex() == mPortRedirectSrc)
			ci.mpInputController->SetRedirectPort(nullptr);
	}

	mPortRedirectSrc = dstPort ? srcPortIdx : -1;
	mpPortRedirectDst = dstPort;

	if (dstPort) {
		for (ControllerInfo& ci : mInputControllers) {
			if (ci.mpInputController && ci.mpInputController->GetAttachedPortIndex() == srcPortIdx)
				ci.mpInputController->SetRedirectPort(dstPort);
		}
	}
}

bool ATInputManager::IsInputMapped(int unit, uint32 inputCode) const {
	return mMappings.find(inputCode) != mMappings.end()
		|| mMappings.find(inputCode | kATInputCode_SpecificUnit | (unit << kATInputCode_UnitShift)) != mMappings.end();
}

void ATInputManager::OnButtonChanged(int unit, int id, bool down) {
	if (down)
		OnButtonDown(unit, id);
	else
		OnButtonUp(unit, id);
}

void ATInputManager::OnButtonDown(int unit, int id) {
	Buttons::iterator it(mButtons.insert(Buttons::value_type(id, 0)).first);
	uint32 oldVal = it->second;
	
	const uint32 bit = (1 << unit);

	if (!(it->second & bit)) {
		it->second |= bit;

		ActivateMappings(id | kATInputCode_SpecificUnit | (unit << kATInputCode_UnitShift), true);

		if (!oldVal)
			ActivateMappings(id, true);
	}
}

void ATInputManager::OnButtonUp(int unit, int id) {
	Buttons::iterator it(mButtons.find(id));
	if (it == mButtons.end())
		return;

	const uint32 bit = (1 << unit);

	if (it->second & bit) {
		it->second &= ~bit;

		ActivateMappings(id | kATInputCode_SpecificUnit | (unit << kATInputCode_UnitShift), false);

		if (!it->second) 
			ActivateMappings(id, false);
	}
}

void ATInputManager::ReleaseButtons(uint32 idmin, uint32 idmax) {
	vdfastvector<uint32> ids;

	mButtons.get_keys(ids);

	for(uint32 id : ids) {
		if (id < idmin || id > idmax)
			continue;

		auto it = mButtons.find(id);

		if (it != mButtons.end()) {
			uint32 unitMask = it->second;

			while(unitMask) {
				uint32 unit = VDFindLowestSetBit(unitMask);
				unitMask &= ~(1 << unit);

				ActivateMappings(id | kATInputCode_SpecificUnit | (unit << kATInputCode_UnitShift), false);
			}

			ActivateMappings(id, false);

			it->second = 0;
		}
	}
}

void ATInputManager::OnAxisInput(int unit, int axis, sint32 value, sint32 deadifiedValue) {
	ActivateAnalogMappings(axis, value, deadifiedValue);
	ActivateAnalogMappings(axis | kATInputCode_SpecificUnit | (unit << kATInputCode_UnitShift), value, deadifiedValue);
}

void ATInputManager::OnMouseMove(int unit, int dx, int dy) {
	mMouseAvgQueue[mMouseAvgIndex & 3] += (uint32)(dx + (dy << 16));

	// Scale dx/dy to something reasonable in the +-1 range.
	dx *= 0x80;
	dy *= 0x80;

	ActivateImpulseMappings(kATInputCode_MouseHoriz, dx);
	ActivateImpulseMappings(kATInputCode_MouseHoriz | kATInputCode_SpecificUnit | (unit << kATInputCode_UnitShift), dx);
	ActivateImpulseMappings(kATInputCode_MouseVert, dy);
	ActivateImpulseMappings(kATInputCode_MouseVert | kATInputCode_SpecificUnit | (unit << kATInputCode_UnitShift), dy);
}

bool ATInputManager::OnMouseWheel(int unit, float delta) {
	const uint32 dirInputCode = delta < 0 ? kATInputCode_MouseWheelUp : kATInputCode_MouseWheelDown;
	const sint32 xdelta = VDRoundToInt(0x1p16f * delta);
	bool handled = false;

	// activate analog directional up/down mappings
	handled |= ActivateImpulseMappings(dirInputCode, abs(xdelta));
	handled |= ActivateImpulseMappings(dirInputCode | kATInputCode_SpecificUnit | (unit << kATInputCode_UnitShift), abs(xdelta));
	handled |= ActivateImpulseMappings(kATInputCode_MouseWheel, xdelta);
	handled |= ActivateImpulseMappings(kATInputCode_MouseWheel | kATInputCode_SpecificUnit | (unit << kATInputCode_UnitShift), xdelta);

	// accumulate digital mappings for activation in Poll()
	mMouseWheelAccum += delta;

	return handled || IsInputMapped(0, kATInputCode_MouseWheel) || IsInputMapped(0, kATInputCode_MouseWheelUp) || IsInputMapped(0, kATInputCode_MouseWheelDown);
}

bool ATInputManager::OnMouseHWheel(int unit, float delta) {
	const uint32 dirInputCode = delta < 0 ? kATInputCode_MouseHWheelLeft : kATInputCode_MouseHWheelRight;
	const sint32 xdelta = VDRoundToInt(0x1p16f * delta);
	bool handled = false;

	// activate analog directional up/down mappings
	handled |= ActivateImpulseMappings(dirInputCode, abs(xdelta));
	handled |= ActivateImpulseMappings(dirInputCode | kATInputCode_SpecificUnit | (unit << kATInputCode_UnitShift), abs(xdelta));
	handled |= ActivateImpulseMappings(kATInputCode_MouseHWheel, xdelta);
	handled |= ActivateImpulseMappings(kATInputCode_MouseHWheel | kATInputCode_SpecificUnit | (unit << kATInputCode_UnitShift), xdelta);

	// accumulate digital mappings for activation in Poll()
	mMouseWheelAccum += delta;

	return handled || IsInputMapped(0, kATInputCode_MouseHWheel) || IsInputMapped(0, kATInputCode_MouseHWheelLeft) || IsInputMapped(0, kATInputCode_MouseHWheelRight);
}

void ATInputManager::SetMouseBeamPos(int beamX, int beamY) {
	ActivateAnalogMappings(kATInputCode_MouseBeamX, beamX, beamX);
	ActivateAnalogMappings(kATInputCode_MouseBeamX | kATInputCode_SpecificUnit, beamX, beamX);
	ActivateAnalogMappings(kATInputCode_MouseBeamY, beamY, beamY);
	ActivateAnalogMappings(kATInputCode_MouseBeamY | kATInputCode_SpecificUnit, beamY, beamY);
}

void ATInputManager::SetMousePadPos(int padX, int padY) {
	ActivateAnalogMappings(kATInputCode_MousePadX, padX, padX);
	ActivateAnalogMappings(kATInputCode_MousePadX | kATInputCode_SpecificUnit, padX, padX);
	ActivateAnalogMappings(kATInputCode_MousePadY, padY, padY);
	ActivateAnalogMappings(kATInputCode_MousePadY | kATInputCode_SpecificUnit, padY, padY);
}

void ATInputManager::SetMouseVirtualStickPos(int padX, int padY) {
	ActivateAnalogMappings(kATInputCode_MouseEmuStickX, padX, padX);
	ActivateAnalogMappings(kATInputCode_MouseEmuStickX | kATInputCode_SpecificUnit, padX, padX);
	ActivateAnalogMappings(kATInputCode_MouseEmuStickY, padY, padY);
	ActivateAnalogMappings(kATInputCode_MouseEmuStickY | kATInputCode_SpecificUnit, padY, padY);
}

void ATInputManager::GetNameForInputCode(uint32 code, VDStringW& name) const {
	code &= 0xffff;

	for(size_t i=0; i<vdcountof(mpUnitNameSources); ++i) {
		IATInputUnitNameSource *nameSrc = mpUnitNameSources[i];

		if (nameSrc && nameSrc->GetInputCodeName(code, name))
			return;
	}

	switch(code) {
		case kATInputCode_None:
			name = L"None";
			break;

		case kATInputCode_KeyLShift:
			name = L"Key: Left Shift";
			break;

		case kATInputCode_KeyRShift:
			name = L"Key: Right Shift";
			break;

		case kATInputCode_KeyLControl:
			name = L"Key: Left Ctrl";
			break;

		case kATInputCode_KeyRControl:
			name = L"Key: Right Ctrl";
			break;

		case kATInputCode_MouseHoriz:
			name = L"Mouse Move Horiz";
			break;

		case kATInputCode_MouseVert:
			name = L"Mouse Move Vert";
			break;

		case kATInputCode_MousePadX:
			name = L"Mouse Pos X (pad)";
			break;

		case kATInputCode_MousePadY:
			name = L"Mouse Pos Y (pad)";
			break;

		case kATInputCode_MouseBeamX:
			name = L"Mouse Pos X (light pen)";
			break;

		case kATInputCode_MouseBeamY:
			name = L"Mouse Pos Y (light pen)";
			break;

		case kATInputCode_MouseEmuStickX:
			name = L"Mouse Pos X (virtual stick)";
			break;

		case kATInputCode_MouseEmuStickY:
			name = L"Mouse Pos Y (virtual stick)";
			break;

		case kATInputCode_MouseLeft:
			name = L"Mouse Left";
			break;
		case kATInputCode_MouseRight:
			name = L"Mouse Right";
			break;
		case kATInputCode_MouseUp:
			name = L"Mouse Up";
			break;
		case kATInputCode_MouseDown:
			name = L"Mouse Down";
			break;
		case kATInputCode_MouseWheelUp:
			name = L"Mouse Wheel Up";
			break;
		case kATInputCode_MouseWheelDown:
			name = L"Mouse Wheel Down";
			break;
		case kATInputCode_MouseWheel:
			name = L"Mouse Wheel";
			break;
		case kATInputCode_MouseHWheelLeft:
			name = L"Mouse HWheel Left";
			break;
		case kATInputCode_MouseHWheelRight:
			name = L"Mouse HWheel Right";
			break;
		case kATInputCode_MouseHWheel:
			name = L"Mouse HWheel";
			break;
		case kATInputCode_MouseLMB:
			name = L"Mouse LMB";
			break;
		case kATInputCode_MouseMMB:
			name = L"Mouse MMB";
			break;
		case kATInputCode_MouseRMB:
			name = L"Mouse RMB";
			break;
		case kATInputCode_MouseX1B:
			name = L"Mouse X1B";
			break;
		case kATInputCode_MouseX2B:
			name = L"Mouse X2B";
			break;
		case kATInputCode_JoyHoriz1:
			name = L"Joy Axis 1H";
			break;
		case kATInputCode_JoyVert1:
			name = L"Joy Axis 1V";
			break;
		case kATInputCode_JoyVert2:
			name = L"Joy Axis 2V";
			break;
		case kATInputCode_JoyHoriz3:
			name = L"Joy Axis 3H";
			break;
		case kATInputCode_JoyVert3:
			name = L"Joy Axis 3V";
			break;
		case kATInputCode_JoyVert4:
			name = L"Joy Axis 4V";
			break;
		case kATInputCode_JoyPOVHoriz:
			name = L"Joy POV H";
			break;
		case kATInputCode_JoyPOVVert:
			name = L"Joy POV V";
			break;
		case kATInputCode_JoyStick1Left:
			name = L"Joy Axis 1L";
			break;
		case kATInputCode_JoyStick1Right:
			name = L"Joy Axis 1R";
			break;
		case kATInputCode_JoyStick1Up:
			name = L"Joy Axis 1U";
			break;
		case kATInputCode_JoyStick1Down:
			name = L"Joy Axis 1D";
			break;
		case kATInputCode_JoyStick2Up:
			name = L"Joy Axis 2U";
			break;
		case kATInputCode_JoyStick2Down:
			name = L"Joy Axis 2D";
			break;
		case kATInputCode_JoyStick3Left:
			name = L"Joy Axis 3L";
			break;
		case kATInputCode_JoyStick3Right:
			name = L"Joy Axis 3R";
			break;
		case kATInputCode_JoyStick3Up:
			name = L"Joy Axis 3U";
			break;
		case kATInputCode_JoyStick3Down:
			name = L"Joy Axis 3D";
			break;
		case kATInputCode_JoyStick4Up:
			name = L"Joy Axis 4U";
			break;
		case kATInputCode_JoyStick4Down:
			name = L"Joy Axis 4D";
			break;
		case kATInputCode_JoyPOVLeft:
			name = L"Joy POV Left";
			break;
		case kATInputCode_JoyPOVRight:
			name = L"Joy POV Right";
			break;
		case kATInputCode_JoyPOVUp:
			name = L"Joy POV Up";
			break;
		case kATInputCode_JoyPOVDown:
			name = L"Joy POV Down";
			break;

		default:
			if (code < 0x200) {
				VDUIAccelerator accel;
				accel.mModifiers = 0;

				switch(code) {
					case kATInputCode_KeyNumpadEnter:
						code = kATInputCode_KeyReturn;
						accel.mModifiers |= VDUIAccelerator::kModExtended;
						break;
					case kATInputCode_KeyInsert:
					case kATInputCode_KeyDelete:
					case kATInputCode_KeyHome:
					case kATInputCode_KeyEnd:
					case kATInputCode_KeyNext:
					case kATInputCode_KeyPrior:
					case kATInputCode_KeyLeft:
					case kATInputCode_KeyRight:
					case kATInputCode_KeyUp:
					case kATInputCode_KeyDown:
						accel.mModifiers |= VDUIAccelerator::kModExtended;
						break;
				}

				accel.mVirtKey = code & 0xff;
				VDUIGetAcceleratorString(accel, name);
				name = VDStringW(L"Key: ") + name;
			} else if ((code & ~0xff) == kATInputCode_JoyButton0)
				name.sprintf(L"Joy Button %d", (code & 0xff) + 1);
			else
				name.sprintf(L"Unknown %x", code);
			break;
	}
}

void ATInputManager::GetNameForTargetCode(uint32 code, ATInputControllerType type, VDStringW& name) const {
	static const wchar_t *const kKeypadButtons[]={
		L"1 Key",
		L"2 Key",
		L"3 Key",
		L"4 Key",
		L"5 Key",
		L"6 Key",
		L"7 Key",
		L"8 Key",
		L"9 Key",
		L"0 Key",
		L"Period",
		L"Plus/Enter",
		L"Minus",
		L"Y",
		L"N",
		L"Del",
		L"Esc"
	};

	static const wchar_t *const kKeyboardButtons[]={
		L"1 Key",
		L"2 Key",
		L"3 Key",
		L"4 Key",
		L"5 Key",
		L"6 Key",
		L"7 Key",
		L"8 Key",
		L"9 Key",
		L"* Key",
		L"0 Key",
		L"# Key",
	};

	static const wchar_t *const kLightGunButtons[]={
		L"Gun trigger",
		nullptr,
		L"On-screen",
	};

	static const wchar_t *const kLightPenButtons[]={
		L"Pen tip switch",
		nullptr,
		L"On-screen",
	};

	static const wchar_t *const kTabletButtons[]={
		L"Stylus button",
		L"Left tablet button",
		L"Right tablet button",
		L"Raise stylus",
	};

	static const wchar_t *const kPowerPadAxes[]={
		L"Pad position X",
		L"Pad position Y",
		L"Pointer touch size",
	};

	static const wchar_t *const kPowerPadButtons[]={
		L"Touch 1",
		L"Touch 2",
		L"Touch 3",
		L"Touch 4",
		L"Set touch 2+ shift",
	};

	static const wchar_t *const kPaddleAxes[]={
		L"Paddle knob (linear)",
		L"Paddle knob (2D rotation X)",
		L"Paddle knob (2D rotation Y)"
	};

	static constexpr const wchar_t *k5200Axes[]={
		L"Analog stick horiz.",
		L"Analog stick vert.",
	};

	name.clear();

	uint32 index = code & 0xFF;
	switch(code & 0xFF00) {
		case kATInputTrigger_Button0:
			switch(type) {
				case kATInputControllerType_Keypad:
					if (index < sizeof(kKeypadButtons)/sizeof(kKeypadButtons[0])) {
						name = kKeypadButtons[index];
						return;
					}
					break;

				case kATInputControllerType_Tablet:
				case kATInputControllerType_KoalaPad:
					if (index < sizeof(kTabletButtons)/sizeof(kTabletButtons[0])) {
						name = kTabletButtons[index];
						return;
					}
					break;

				case kATInputControllerType_LightPen:
				case kATInputControllerType_LightPenStack:
					if (index < sizeof(kLightPenButtons)/sizeof(kLightPenButtons[0])) {
						if (kLightPenButtons[index]) {
							name = kLightPenButtons[index];
							return;
						}
					}
					break;

				case kATInputControllerType_LightGun:
					if (index < sizeof(kLightGunButtons)/sizeof(kLightGunButtons[0])) {
						if (kLightGunButtons[index]) {
							name = kLightGunButtons[index];
							return;
						}
					}
					break;

				case kATInputControllerType_Keyboard:
					if (index < vdcountof(kKeyboardButtons)) {
						name = kKeyboardButtons[index];
						return;
					}
					break;

				case kATInputControllerType_PowerPad:
					if (index < vdcountof(kPowerPadButtons)) {
						name = kPowerPadButtons[index];
						return;
					}
					break;
			}

			name.sprintf(L"Button %d", index + 1);
			break;
		case kATInputTrigger_Axis0:
			switch(type) {
				case kATInputControllerType_Paddle:
					if (index < sizeof(kPaddleAxes)/sizeof(kPaddleAxes[0])) {
						name = kPaddleAxes[index];
						return;
					}
					break;

				case kATInputControllerType_PowerPad:
					if (index < vdcountof(kPowerPadAxes)) {
						name = kPowerPadAxes[index];
						return;
					}
					break;

				case kATInputControllerType_5200Controller:
				case kATInputControllerType_5200Trackball:
					if (index < vdcountof(k5200Axes)) {
						name = k5200Axes[index];
						return;
					}
					break;
			}

			name.sprintf(L"Axis %d", index + 1);
			break;
		case kATInputTrigger_Flag0:
			name.sprintf(L"Flag %d", index + 1);
			break;
		default:
			switch(code) {
			case kATInputTrigger_Up:
				name = L"Up";
				break;
			case kATInputTrigger_Down:
				name = L"Down";
				break;
			case kATInputTrigger_Left:
				name = L"Left";
				break;
			case kATInputTrigger_Right:
				name = L"Right";
				break;
			case kATInputTrigger_ScrollUp:
				name = L"Scroll Up";
				break;
			case kATInputTrigger_ScrollDown:
				name = L"Scroll Down";
				break;
			case kATInputTrigger_Start:
				name = L"Start";
				break;
			case kATInputTrigger_Select:
				name = L"Select";
				break;
			case kATInputTrigger_Option:
				name = L"Option";
				break;
			case kATInputTrigger_Turbo:
				name = L"Turbo";
				break;
			case kATInputTrigger_Rewind:
				name = L"Quick Rewind";
				break;
			case kATInputTrigger_RewindMenu:
				name = L"Show Rewind Menu";
				break;
			case kATInputTrigger_ColdReset:
				name = L"Cold Reset";
				break;
			case kATInputTrigger_WarmReset:
				name = L"Warm Reset";
				break;
			case kATInputTrigger_KeySpace:
				name = L"Space Bar";
				break;
			case kATInputTrigger_5200_0:
				name = L"0 Key";
				break;
			case kATInputTrigger_5200_1:
				name = L"1 Key";
				break;
			case kATInputTrigger_5200_2:
				name = L"2 Key";
				break;
			case kATInputTrigger_5200_3:
				name = L"3 Key";
				break;
			case kATInputTrigger_5200_4:
				name = L"4 Key";
				break;
			case kATInputTrigger_5200_5:
				name = L"5 Key";
				break;
			case kATInputTrigger_5200_6:
				name = L"6 Key";
				break;
			case kATInputTrigger_5200_7:
				name = L"7 Key";
				break;
			case kATInputTrigger_5200_8:
				name = L"8 Key";
				break;
			case kATInputTrigger_5200_9:
				name = L"9 Key";
				break;
			case kATInputTrigger_5200_Pound:
				name = L"# Key";
				break;
			case kATInputTrigger_5200_Star:
				name = L"* Key";
				break;
			case kATInputTrigger_5200_Start:
				name = L"Start";
				break;
			case kATInputTrigger_5200_Pause:
				name = L"Pause";
				break;
			case kATInputTrigger_5200_Reset:
				name = L"Reset";
				break;
			case kATInputTrigger_UILeft:
				name = L"UI Left";
				break;
			case kATInputTrigger_UIRight:
				name = L"UI Right";
				break;
			case kATInputTrigger_UIUp:
				name = L"UI Up";
				break;
			case kATInputTrigger_UIDown:
				name = L"UI Down";
				break;
			case kATInputTrigger_UIAccept:
				name = L"UI Accept";
				break;
			case kATInputTrigger_UIReject:
				name = L"UI Reject";
				break;
			case kATInputTrigger_UIMenu:
				name = L"UI Menu";
				break;
			case kATInputTrigger_UIOption:
				name = L"UI Option";
				break;
			case kATInputTrigger_UISwitchLeft:
				name = L"UI Switch Left";
				break;
			case kATInputTrigger_UISwitchRight:
				name = L"UI Switch Right";
				break;
			case kATInputTrigger_UILeftShift:
				name = L"UI Left Shift";
				break;
			case kATInputTrigger_UIRightShift:
				name = L"UI Right Shift";
				break;
		}

		break;
	}

	if (name.empty())
		name.sprintf(L"Unknown %x", code);
}

bool ATInputManager::IsAnalogTrigger(uint32 code, ATInputControllerType type) const {
	const uint32 triggerClass = code & kATInputTrigger_ClassMask;

	switch(type) {
		case kATInputControllerType_LightPen:
		case kATInputControllerType_LightPenStack:
		case kATInputControllerType_LightGun:
			switch(code) {
				case kATInputTrigger_Up:
				case kATInputTrigger_Down:
				case kATInputTrigger_Left:
				case kATInputTrigger_Right:
					return true;
			}

			return triggerClass == kATInputTrigger_Axis0;

		case kATInputControllerType_Paddle:
		case kATInputControllerType_Driving:
		case kATInputControllerType_5200Controller:
		case kATInputControllerType_5200Trackball:
		case kATInputControllerType_Tablet:
		case kATInputControllerType_KoalaPad:
			return triggerClass == kATInputTrigger_Axis0;
	}

	return false;
}

uint32 ATInputManager::GetInputMapCount() const {
	return (uint32)mInputMaps.size();
}

bool ATInputManager::GetInputMapByIndex(uint32 index, ATInputMap **ppimap) const {
	if (index >= mInputMaps.size())
		return false;

	InputMaps::const_iterator it(mInputMaps.begin());
	std::advance(it, index);

	ATInputMap *imap = it->first;
	imap->AddRef();

	*ppimap = imap;
	return true;
}

bool ATInputManager::IsInputMapEnabled(ATInputMap *imap) const {
	InputMaps::const_iterator it(mInputMaps.find(imap));
	if (it == mInputMaps.end())
		return false;

	return it->second;
}

void ATInputManager::AddInputMap(ATInputMap *imap) {
	if (mInputMaps.insert(InputMaps::value_type(imap, false)).second)
		imap->AddRef();
}

void ATInputManager::RemoveInputMap(ATInputMap *imap) {
	InputMaps::iterator it = mInputMaps.find(imap);
	if (it != mInputMaps.end()) {
		mInputMaps.erase(it);
		imap->Release();
	}

	RebuildMappings();
}

void ATInputManager::RemoveAllInputMaps() {
	for(InputMaps::iterator it(mInputMaps.begin()), itEnd(mInputMaps.end()); it != itEnd; ++it) {
		it->first->Release();
	}

	mInputMaps.clear();

	RebuildMappings();
}

void ATInputManager::ActivateInputMap(ATInputMap *imap, bool enable) {
	InputMaps::iterator it = mInputMaps.find(imap);

	if (it != mInputMaps.end()) {
		if (it->second != enable) {
			it->second = enable;
			RebuildMappings();
		}
	}
}

ATInputMap *ATInputManager::CycleQuickMaps() {
	using InputMapEntry = std::pair<ATInputMap * const, bool>;
	vdfastvector<InputMapEntry *> quickMaps;

	// filter input maps to only quick maps
	for (InputMapEntry& entry : mInputMaps) {
		if (entry.first->IsQuickMap())
			quickMaps.push_back(&entry);
	}

	if (quickMaps.empty())
		return nullptr;

	// sort by name, case insensitive
	std::sort(quickMaps.begin(), quickMaps.end(),
		[](const InputMapEntry *p, const InputMapEntry *q) {
			return vdwcsicmp(p->first->GetName(), q->first->GetName()) < 0;
		}
	);

	// disable all quick maps and find last map that's currently enabled
	auto it = quickMaps.end();

	for (InputMapEntry *&e : quickMaps) {
		if (e->second) {
			it = quickMaps.begin() + (&e - quickMaps.data());
			e->second = false;
		}
	}

	// advance one step, including the end iterator for disabled
	if (it == quickMaps.end())
		it = quickMaps.begin();
	else
		++it;

	// enable the next map, if any
	ATInputMap *newActive = nullptr;
	if (it != quickMaps.end()) {
		(*it)->second = true;
		newActive = (*it)->first;
	}

	RebuildMappings();
	return newActive;
}

uint32 ATInputManager::GetPresetInputMapCount() const {
	return GetPresetMapDefCount();
}

bool ATInputManager::GetPresetInputMapByIndex(uint32 index, ATInputMap **imap) const {
	const PresetMapDef *def = GetPresetMapDef(index);

	if (!def)
		return false;

	InitPresetMap(*def, imap);
	(*imap)->SetQuickMap(false);
	return true;
}

namespace {
	struct InputMapSorter {
		bool operator()(const std::pair<ATInputMap *, bool>& x, const std::pair<ATInputMap *, bool>& y) const {
			return vdwcsicmp(x.first->GetName(), y.first->GetName()) < 0;
		}
	};

	const uint32 kMaxInputMaps = 1000;
}

bool ATInputManager::LoadMaps(VDRegistryKey& key) {
	RemoveAllInputMaps();

	VDStringA valName;
	for(uint32 i=0; i<kMaxInputMaps; ++i) {
		valName.sprintf("Input map %u", i);

		vdrefptr<ATInputMap> imap(new ATInputMap);

		if (!imap->Load(key, valName.c_str()))
			break;

		AddInputMap(imap);
	}

	if (mInputMaps.empty())
		ResetToDefaults();

	return true;
}

void ATInputManager::LoadSelections(VDRegistryKey& key, ATInputControllerType defaultControllerType) {
	const uint8 kIsActive = 1;
	const uint8 kIsQuick = 2;

	vdhashmap<VDStringW, uint8, vdhash<VDStringW>, vdstringpred> mapStateLookup;

	static const char *const kKeyNames[] = {
		"Input: Active map names",
		"Input: Quick map names"
	};

	bool foundActive = true;
	bool foundQuick = true;
	bool hasActiveSelection = false;

	for(int mapType = 0; mapType < 2; ++mapType) {
		VDStringW mapNames;
		if (!key.getString(kKeyNames[mapType], mapNames)) {
			if (!mapType)
				foundActive = false;
			else
				foundQuick = false;

			continue;
		}

		VDStringRefW parser(mapNames);
		while(!parser.empty()) {
			VDStringRefW token;
			if (!parser.split('\n', token)) {
				token = parser;
				parser.clear();
			}

			if (!token.empty() && mapType == 0)
				hasActiveSelection = true;

			mapStateLookup[VDStringW(token)] |= (uint8)(1 << mapType);
		}
	}

	// Set quick map flags if we found the quick map entry. If not, leave them as-is -- unset if we
	// loaded maps, defaults if we're grabbing from presets.
	if (foundQuick) {
		for(const auto& mapEntry : mInputMaps) {
			ATInputMap *imap = mapEntry.first;
			uint8 flags = 0;

			auto it = mapStateLookup.find_as(imap->GetName());
			if (it != mapStateLookup.end())
				flags = it->second;

			imap->SetQuickMap((flags & kIsQuick) != 0);
		}
	}

	// If there was no setting for active maps, look for the first input map with the specified
	// default controller type. We sort by name so the result is deterministic.
	if ((!foundActive || !hasActiveSelection) && defaultControllerType) {
		vdfastvector<ATInputMap *> sortedMaps;
		sortedMaps.reserve(mInputMaps.size());

		// filter out maps that match the given controller type
		for(const auto& mapEntry : mInputMaps) {
			if (mapEntry.first->HasControllerType(defaultControllerType))
				sortedMaps.push_back(mapEntry.first);
		}

		// sort by name
		std::sort(sortedMaps.begin(), sortedMaps.end(), [](ATInputMap *x, ATInputMap *y) { return vdwcsicmp(x->GetName(), y->GetName()) < 0; });

		// check if we have a quick map... if so, use that
		auto it = std::find_if(sortedMaps.begin(), sortedMaps.end(), [=](ATInputMap *p) { return p->IsQuickMap(); });

		if (it == sortedMaps.end())
			it = sortedMaps.begin();

		if (it != sortedMaps.end())
			mapStateLookup.insert_as((*it)->GetName()).first->second |= kIsActive;
	}


	for(const auto& mapEntry : mInputMaps) {
		ATInputMap *imap = mapEntry.first;
		uint8 flags = 0;

		auto it = mapStateLookup.find_as(imap->GetName());
		if (it != mapStateLookup.end())
			flags = it->second;

		ActivateInputMap(imap, (flags & kIsActive) != 0);
	}
}

void ATInputManager::SaveMaps(VDRegistryKey& key) {
	vdvector<std::pair<ATInputMap *, bool> > sortedMaps;

	for(InputMaps::iterator it(mInputMaps.begin()), itEnd(mInputMaps.end()); it != itEnd; ++it)
		sortedMaps.push_back(*it);

	std::sort(sortedMaps.begin(), sortedMaps.end(), InputMapSorter());

	VDStringA valName;
	const uint32 n = (uint32)sortedMaps.size();
	for(uint32 i=0; i<n; ++i) {
		ATInputMap *imap = sortedMaps[i].first;

		valName.sprintf("Input map %u", i);
		imap->Save(key, valName.c_str());
	}

	// wipe any additional maps
	for(uint32 i=n; i<kMaxInputMaps; ++i) {
		valName.sprintf("Input map %u", i);

		if (!key.removeValue(valName.c_str()))
			break;
	}
}

void ATInputManager::SaveSelections(VDRegistryKey& key) {
	vdvector<std::pair<ATInputMap *, bool> > sortedMaps;

	for(InputMaps::iterator it(mInputMaps.begin()), itEnd(mInputMaps.end()); it != itEnd; ++it)
		sortedMaps.push_back(*it);

	std::sort(sortedMaps.begin(), sortedMaps.end(), InputMapSorter());

	VDStringW activeMapNames;
	VDStringW quickMapNames;

	for(const auto& entry : sortedMaps) {
		ATInputMap *imap = entry.first;

		if (entry.second) {
			if (!activeMapNames.empty())
				activeMapNames += L'\n';

			activeMapNames.append(imap->GetName());
		}

		if (imap->IsQuickMap()) {
			if (!quickMapNames.empty())
				quickMapNames += L'\n';

			quickMapNames.append(imap->GetName());
		}
	}
	
	key.setString("Input: Active map names", activeMapNames.c_str());
	key.setString("Input: Quick map names", quickMapNames.c_str());
}

void ATInputManager::RebuildMappings() {
	ClearTriggers();

	for(InputControllers::const_iterator it(mInputControllers.begin()), itEnd(mInputControllers.end()); it != itEnd; ++it) {
		ATPortInputController *pc = it->mpInputController;
		pc->Detach();
		delete pc;
	}

	mInputControllers.clear();
	mMappings.clear();
	mFlags.clear();
	mFlags.push_back(true);

	mbMouseAbsMode = false;
	mbMouseMapped = false;
	mbMouseActiveTarget = false;
	mbControllerHasNonBeamPointer = false;
	
	uint32 triggerCount = 0;

	vdfastvector<int> controllerTable;
	typedef vdhashmap<uint32, int> ControllerMap;
	ControllerMap controllerMap;
	for(InputMaps::iterator it(mInputMaps.begin()), itEnd(mInputMaps.end()); it != itEnd; ++it) {
		const bool enabled = it->second;
		if (!enabled)
			continue;

		uint32 flagIndex = (uint32)mFlags.size();
		mFlags.push_back(false);
		mFlags.push_back(false);

		ATInputMap *imap = it->first;
		const uint32 controllerCount = imap->GetControllerCount();
		int specificUnit = imap->GetSpecificInputUnit();

		controllerTable.clear();
		controllerTable.resize(controllerCount, -1);
		for(uint32 i=0; i<controllerCount; ++i) {
			const ATInputMap::Controller& c = imap->GetController(i);

			// The input state controller must not be shared between input maps; it must
			// be instanced per input map.
			const uint32 code = (c.mType << 16) + (c.mType == kATInputControllerType_InputState ? flagIndex : c.mIndex);
			ControllerMap::iterator itC(controllerMap.find(code));
			ATPortInputController *pic = NULL;
			if (itC != controllerMap.end()) {
				controllerTable[i] = itC->second;
			} else {
				const bool is5200Controller = ATInputIs5200ControllerType(c.mType);

				// skip controller if it is not compatible with current mode
				if (is5200Controller != mb5200Mode && c.mType != kATInputControllerType_InputState && c.mType != kATInputControllerType_Console)
					continue;

				switch(c.mType) {
					case kATInputControllerType_Joystick:
						if (c.mIndex < 12) {
							ATJoystickController *joy = new ATJoystickController;

							if (c.mIndex >= 4)
								joy->Attach(*mpPortMgr, 0, c.mIndex - 4);
							else
								joy->Attach(*mpPortMgr, c.mIndex, -1);

							pic = joy;
						}
						break;

					case kATInputControllerType_STMouse:
					case kATInputControllerType_AmigaMouse:
						if (c.mIndex < 4) {
							ATMouseController *mouse = new ATMouseController(c.mType == kATInputControllerType_AmigaMouse);

							mouse->Init(mpSlowScheduler);
							mouse->Attach(*mpPortMgr, c.mIndex);

							pic = mouse;
						}
						break;

					case kATInputControllerType_Paddle:
						if (c.mIndex < 8) {
							ATPaddleController *paddle = new ATPaddleController;

							paddle->SetHalf((c.mIndex & 1) != 0);
							paddle->Attach(*mpPortMgr, c.mIndex >> 1);

							pic = paddle;
						}
						break;

					case kATInputControllerType_Console:
						{
							ATConsoleController *console = new ATConsoleController(this);
							console->Attach(*mpPortMgr, 0);
							pic = console;
						}
						break;

					case kATInputControllerType_5200Controller:
					case kATInputControllerType_5200Trackball:
						if (c.mIndex < 4) {
							AT5200ControllerController *ctrl = new AT5200ControllerController(c.mIndex, c.mType == kATInputControllerType_5200Trackball, *mpPokey);
							ctrl->Attach(*mpPortMgr, c.mIndex);
							pic = ctrl;
						}
						break;

					case kATInputControllerType_InputState:
						{
							ATInputStateController *ctrl = new ATInputStateController(this, flagIndex);
							pic = ctrl;
						}
						break;

					case kATInputControllerType_LightGun:
						if (c.mIndex < 4) {
							ATLightPenController *lp = new ATLightPenController(ATLightPenController::Type::LightGun);

							lp->Init(mpFastScheduler, mpLightPen);
							lp->Attach(*mpPortMgr, c.mIndex);

							pic = lp;
						}
						break;

					case kATInputControllerType_Tablet:
						if (c.mIndex < 4) {
							ATTabletController *tc = new ATTabletController(228, true);

							tc->Attach(*mpPortMgr, c.mIndex);

							pic = tc;
						}
						break;

					case kATInputControllerType_KoalaPad:
						if (c.mIndex < 4) {
							ATTabletController *tc = new ATTabletController(0, false);

							tc->Attach(*mpPortMgr, c.mIndex);

							pic = tc;
						}
						break;

					case kATInputControllerType_Keypad:
						if (c.mIndex < 4) {
							ATKeypadController *kpc = new ATKeypadController;

							kpc->Attach(*mpPortMgr, c.mIndex);

							pic = kpc;
						}
						break;

					case kATInputControllerType_Trackball_CX80:
						if (c.mIndex < 4) {
							ATTrackballController *trakball = new ATTrackballController;

							trakball->Init(mpSlowScheduler);
							trakball->Attach(*mpPortMgr, c.mIndex);

							pic = trakball;
						}
						break;

					case kATInputControllerType_Driving:
						if (c.mIndex < 12) {
							ATDrivingController *drv = new ATDrivingController;

							if (c.mIndex >= 4)
								drv->Attach(*mpPortMgr, 0, c.mIndex - 4);
							else
								drv->Attach(*mpPortMgr, c.mIndex, -1);

							pic = drv;
						}
						break;

					case kATInputControllerType_Keyboard:
						if (c.mIndex < 4) {
							ATKeyboardController *kbc = new ATKeyboardController;

							kbc->Attach(*mpPortMgr, c.mIndex, -1);
							pic = kbc;
						}
						break;

					case kATInputControllerType_LightPen:
						if (c.mIndex < 4) {
							ATLightPenController *lp = new ATLightPenController(ATLightPenController::Type::LightPen);

							lp->Init(mpFastScheduler, mpLightPen);
							lp->Attach(*mpPortMgr, c.mIndex);

							pic = lp;
						}
						break;

					case kATInputControllerType_PowerPad:
						if (c.mIndex < 4) {
							ATPowerPadController *ppc = new ATPowerPadController;

							ppc->Attach(*mpPortMgr, c.mIndex);

							pic = ppc;
						}
						break;

					case kATInputControllerType_LightPenStack:
						if (c.mIndex < 4) {
							ATLightPenController *lp = new ATLightPenController(ATLightPenController::Type::LightPenStack);

							lp->Init(mpFastScheduler, mpLightPen);
							lp->Attach(*mpPortMgr, c.mIndex);

							pic = lp;
						}
						break;
				}

				if (pic) {
					pic->SelectMultiJoy(mMultiMask);

					controllerMap.insert(ControllerMap::value_type(code, (int)mInputControllers.size()));
					controllerTable[i] = (int)mInputControllers.size();

					ControllerInfo& ci = mInputControllers.push_back();
					ci.mpInputController = pic;
					ci.mbBoundToMouseAbs = false;

					if (!mbControllerHasNonBeamPointer && pic->GetPointerCoordinateSpace() == ATInputPointerCoordinateSpace::Normalized)
						mbControllerHasNonBeamPointer = true;
				}
			}
		}

		const uint32 mappingCount = imap->GetMappingCount();
		for(uint32 i=0; i<mappingCount; ++i) {
			const ATInputMap::Mapping& m = imap->GetMapping(i);
			const int controllerIndex = controllerTable[m.mControllerId];

			if (controllerIndex < 0)
				continue;

			ControllerInfo& ci = mInputControllers[controllerIndex];
			ATPortInputController *pic = ci.mpInputController;

			int32 triggerIdx = -1;
			for(uint32 j=0; j<triggerCount; ++j) {
				const Trigger& t = mTriggers[j];

				if (t.mId == m.mCode && t.mpController == pic) {
					triggerIdx = j;
					break;
				}
			}

			if (triggerIdx < 0) {
				triggerIdx = (int32)mTriggers.size();
				Trigger& t = mTriggers.push_back();

				t.mId = m.mCode;
				t.mpController = pic;
				t.mCount = 0;

				++triggerCount;
			}

			uint32 inputCode = m.mInputCode;
			switch(inputCode & kATInputCode_ClassMask) {
				case kATInputCode_JoyClass:
					if (specificUnit >= 0) {
						inputCode |= kATInputCode_SpecificUnit;
						inputCode |= specificUnit << kATInputCode_UnitShift;
					}
					break;

				case kATInputCode_MouseClass:
					mbMouseMapped = true;
					switch(inputCode & kATInputCode_IdMask) {
						case kATInputCode_MousePadX:
						case kATInputCode_MousePadY:
						case kATInputCode_MouseBeamX:
						case kATInputCode_MouseBeamY:
						case kATInputCode_MouseEmuStickX:
						case kATInputCode_MouseEmuStickY:
							mbMouseAbsMode = true;
							ci.mbBoundToMouseAbs = true;
							break;
					}
					break;
			}

			Mapping& mapping = mMappings.insert(Mappings::value_type(inputCode & ~kATInputCode_FlagMask, Mapping()))->second;
			mapping.mTriggerIdx = triggerIdx;
			mapping.mbTriggerActivated = false;
			mapping.mbMotionActive = false;
			mapping.mAutoCounter = 0;
			mapping.mAutoPeriod = 0;
			mapping.mAutoValue = 0;
			mapping.mMotionSpeed = 0;
			mapping.mMotionAccel = 0;
			mapping.mMotionDrag = 0;

			if (inputCode & kATInputCode_FlagCheck0) {
				mapping.mFlagIndex1 = flagIndex + 0;
				mapping.mbFlagValue1 = (inputCode & kATInputCode_FlagValue0) != 0;
			} else {
				mapping.mFlagIndex1 = 0;
				mapping.mbFlagValue1 = true;
			}

			if (inputCode & kATInputCode_FlagCheck1) {
				mapping.mFlagIndex2 = flagIndex + 1;
				mapping.mbFlagValue2 = (inputCode & kATInputCode_FlagValue1) != 0;
			} else {
				mapping.mFlagIndex2 = 0;
				mapping.mbFlagValue2 = true;
			}
		}
	}

	mbMouseAbsMappedToPointer = false;
	for(const ControllerInfo& ci : mInputControllers) {
		if (ci.mbBoundToMouseAbs && ci.mpInputController->GetPointerCoordinateSpace() != ATInputPointerCoordinateSpace::None) {
			mbMouseAbsMappedToPointer = true;
			break;
		}
	}

	Update5200Controller();

	// Pre-activate all inverted triggers.
	for(Triggers::iterator it(mTriggers.begin()), itEnd(mTriggers.end()); it != itEnd; ++it) {
		Trigger& trigger = *it;

		if ((trigger.mId & kATInputTriggerMode_Mask) != kATInputTriggerMode_Inverted)
			continue;

		const uint32 id = trigger.mId & kATInputTrigger_Mask;

		trigger.mpController->SetDigitalTrigger(id, true);
	}

	// If a port redirect is installed (netplay), re-apply it to the
	// freshly-built controllers so newly-loaded input maps also have
	// their port-1 output captured instead of driving the real port.
	if (mPortRedirectSrc >= 0 && mpPortRedirectDst) {
		for (ControllerInfo& ci : mInputControllers) {
			if (ci.mpInputController && ci.mpInputController->GetAttachedPortIndex() == mPortRedirectSrc)
				ci.mpInputController->SetRedirectPort(mpPortRedirectDst);
		}
	}
}

void ATInputManager::ActivateMappings(uint32 id, bool state) {
	std::pair<Mappings::iterator, Mappings::iterator> result(mMappings.equal_range(id));

	for(; result.first != result.second; ++result.first) {
		Mapping& mapping = result.first->second;

		if (mFlags[mapping.mFlagIndex1] != mapping.mbFlagValue1
			|| mFlags[mapping.mFlagIndex2] != mapping.mbFlagValue2)
		{
			continue;
		}

		const uint32 triggerIdx = mapping.mTriggerIdx;
		const Trigger& trigger = mTriggers[triggerIdx];

		const bool restricted = IsTriggerRestricted(trigger);

		switch(trigger.mId & kATInputTriggerMode_Mask) {
			case kATInputTriggerMode_Toggle:
				if (!state || restricted)
					break;

				state = !mapping.mbTriggerActivated;
				// fall through

			case kATInputTriggerMode_Default:
			case kATInputTriggerMode_Inverted:
			default:
				SetTrigger(mapping, state && !restricted);
				break;

			case kATInputTriggerMode_ToggleAF:
				if (!state)
					break;

				state = !mapping.mbTriggerActivated;
				// fall through

			case kATInputTriggerMode_AutoFire:
				if (state && !restricted) {
					if (!mapping.mbMotionActive) {
						mapping.mbMotionActive = true;
						mapping.mAutoCounter = 0;
						mapping.mAutoPeriod = 3;

						SetTrigger(mapping, true);
					}
				} else {
					if (mapping.mbMotionActive) {
						mapping.mbMotionActive = false;

						SetTrigger(mapping, false);
					}
				}
				break;

			case kATInputTriggerMode_Relative:
				if (state && !restricted) {
					const int speedIndex = ((trigger.mId & kATInputTriggerSpeed_Mask) >> kATInputTriggerSpeed_Shift);
					const int accelIndex = ((trigger.mId & kATInputTriggerAccel_Mask) >> kATInputTriggerAccel_Shift);
					const float speedVal = kSpeedScaleTable[speedIndex];
					const float accelVal = kAccelScaleTable[accelIndex];

					// fall through
					mapping.mbMotionActive = true;
					mapping.mMotionAccel = accelVal;
					mapping.mMotionSpeed = speedVal;
					mapping.mMotionDrag = 0;
				} else {
					mapping.mMotionAccel = 0;
					mapping.mMotionDrag = 50.0f;
				}
				break;
		}
	}
}

void ATInputManager::ActivateAnalogMappings(uint32 id, int ds, int dsdead) {
	std::pair<Mappings::iterator, Mappings::iterator> result(mMappings.equal_range(id));

	for(; result.first != result.second; ++result.first) {
		Mapping& mapping = result.first->second;

		if (mFlags[mapping.mFlagIndex1] != mapping.mbFlagValue1
			|| mFlags[mapping.mFlagIndex2] != mapping.mbFlagValue2)
		{
			continue;
		}

		Trigger& trigger = mTriggers[mapping.mTriggerIdx];
		if (IsTriggerRestricted(trigger))
			continue;
		
		const uint32 id = trigger.mId & kATInputTrigger_Mask;
		int dstemp = ds;

		switch(trigger.mId & kATInputTriggerMode_Mask) {
			case kATInputTriggerMode_Inverted:
				dstemp = -dstemp;
				// fall through

			case kATInputTriggerMode_Default:
			case kATInputTriggerMode_Absolute:
			default:
				trigger.mpController->ApplyAnalogInput(id, dstemp);
				break;

			case kATInputTriggerMode_Relative:
				{
					const int speedIndex = ((trigger.mId & kATInputTriggerSpeed_Mask) >> kATInputTriggerSpeed_Shift);
					const int accelIndex = ((trigger.mId & kATInputTriggerAccel_Mask) >> kATInputTriggerAccel_Shift);
					const float speedVal = kSpeedScaleTable[speedIndex];
					const float accelVal = kAccelScaleTable[accelIndex];
					float frac = ((float)dsdead / (float)0x10000);

					mapping.mMotionSpeed = frac * speedVal;
					mapping.mMotionAccel = frac * accelVal;
				}

				mapping.mMotionDrag = 0;
				if (dsdead)
					mapping.mbMotionActive = true;
				else
					mapping.mbMotionActive = false;

				break;
		}
	}
}

bool ATInputManager::ActivateImpulseMappings(uint32 id, int ds) {
	std::pair<Mappings::iterator, Mappings::iterator> result(mMappings.equal_range(id));
	bool somethingHappened = false;

	for(; result.first != result.second; ++result.first) {
		const Mapping& mapping = result.first->second;

		if (mFlags[mapping.mFlagIndex1] != mapping.mbFlagValue1
			|| mFlags[mapping.mFlagIndex2] != mapping.mbFlagValue2)
		{
			continue;
		}

		const uint32 triggerIdx = mapping.mTriggerIdx;
		Trigger& trigger = mTriggers[triggerIdx];

		if (IsTriggerRestricted(trigger))
			continue;
		
		const uint32 id = trigger.mId & kATInputTrigger_Mask;

		switch(trigger.mId & kATInputTriggerMode_Mask) {
			case kATInputTriggerMode_Absolute:
				trigger.mpController->ApplyAnalogInput(id, ds);
				break;

			case kATInputTriggerMode_Relative:
				ds = VDRoundToInt((float)ds * kSpeedScaleTable[((trigger.mId & kATInputTriggerSpeed_Mask) >> kATInputTriggerSpeed_Shift)]);
				// fall through
			case kATInputTriggerMode_Default:
			default:
				trigger.mpController->ApplyImpulse(id, ds);
				somethingHappened = true;
				break;
		}
	}

	return somethingHappened;
}

bool ATInputManager::HasAbsMousePointer() const {
	return mbMouseAbsMappedToPointer;
}

bool ATInputManager::HasNonBeamPointer() const {
	return mbControllerHasNonBeamPointer;
}

void ATInputManager::GetPointers(vdfastvector<ATInputPointerInfo>& pointers) const {
	for(const ControllerInfo& cinfo : mInputControllers) {
		if (cinfo.mpInputController)
			cinfo.mpInputController->GetPointers(pointers);
	}
}

void ATInputManager::ActivateFlag(uint32 id, bool state) {
	if (mFlags[id] == state)
		return;

	mFlags[id] = state;

	// Check for any mappings that need to be deactivated.
	for(Mappings::iterator it(mMappings.begin()), itEnd(mMappings.end()); it != itEnd; ++it) {
		Mapping& mapping = it->second;

		// Check if this mapping uses the flag being changed and that it's active
		// with the current flag set.
		if (mapping.mFlagIndex1 == id) {
			if (mapping.mbFlagValue1 == state || mFlags[mapping.mFlagIndex2] != mapping.mbFlagValue2)
				continue;
		} else if (mapping.mFlagIndex2 == id) {
			if (mapping.mbFlagValue2 == state || mFlags[mapping.mFlagIndex1] != mapping.mbFlagValue1)
				continue;
		} else {
			continue;
		}

		// Deactivate any triggers.
		if (mapping.mbTriggerActivated)
			SetTrigger(mapping, false);

		// Kill any motion.
		if (mapping.mbMotionActive) {
			mapping.mbMotionActive = false;
			mapping.mAutoCounter = 0;
			mapping.mAutoValue = 0;
			mapping.mMotionSpeed = 0;
			mapping.mMotionAccel = 0;
		}
	}
}

void ATInputManager::ClearTriggers() {
	while(!mTriggers.empty()) {
		const Trigger& trigger = mTriggers.back();
		bool triggerActive = trigger.mCount != 0;

		if ((trigger.mId & kATInputTriggerMode_Mask) == kATInputTriggerMode_Inverted)
			triggerActive = !triggerActive;

		if (triggerActive) {
			const uint32 id = trigger.mId & kATInputTrigger_Mask;

			trigger.mpController->SetDigitalTrigger(id, false);
		}

		mTriggers.pop_back();
	}
}

void ATInputManager::SetTrigger(Mapping& mapping, bool state) {
	if (mapping.mbTriggerActivated == state)
		return;

	mapping.mbTriggerActivated = state;

	const uint32 triggerIdx = mapping.mTriggerIdx;
	Trigger& trigger = mTriggers[triggerIdx];
	const uint32 id = trigger.mId & kATInputTrigger_Mask;

	bool inverted = (trigger.mId & kATInputTriggerMode_Mask) == kATInputTriggerMode_Inverted;

	if (state) {
		if (!trigger.mCount++)
			trigger.mpController->SetDigitalTrigger(id, !inverted);
	} else {
		VDASSERT(trigger.mCount);
		if (!--trigger.mCount)
			trigger.mpController->SetDigitalTrigger(id, inverted);
	}
}

const ATInputManager::PresetMapDef ATInputManager::kPresetMapDefs[] = {
	{
		true, true, L"Arrow Keys -> Joystick (port 1)", -1,
		{
			{ kATInputControllerType_Joystick, 0 }
		},
		{
			{ kATInputCode_KeyLeft, 0, kATInputTrigger_Left },
			{ kATInputCode_KeyRight, 0, kATInputTrigger_Right },
			{ kATInputCode_KeyUp, 0, kATInputTrigger_Up },
			{ kATInputCode_KeyDown, 0, kATInputTrigger_Down },
			{ kATInputCode_KeyLControl, 0, kATInputTrigger_Button0 },
		}
	},
	{
		true, false, L"Numpad -> Joystick (port 1)", -1,
		{
			{ kATInputControllerType_Joystick, 0 }
		},
		{
			{ kATInputCode_KeyNumpad1, 0, kATInputTrigger_Left },
			{ kATInputCode_KeyNumpad1, 0, kATInputTrigger_Down },
			{ kATInputCode_KeyNumpad2, 0, kATInputTrigger_Down },
			{ kATInputCode_KeyNumpad3, 0, kATInputTrigger_Down },
			{ kATInputCode_KeyNumpad3, 0, kATInputTrigger_Right },
			{ kATInputCode_KeyNumpad4, 0, kATInputTrigger_Left },
			{ kATInputCode_KeyNumpad6, 0, kATInputTrigger_Right },
			{ kATInputCode_KeyNumpad7, 0, kATInputTrigger_Left },
			{ kATInputCode_KeyNumpad7, 0, kATInputTrigger_Up },
			{ kATInputCode_KeyNumpad8, 0, kATInputTrigger_Up },
			{ kATInputCode_KeyNumpad9, 0, kATInputTrigger_Up },
			{ kATInputCode_KeyNumpad9, 0, kATInputTrigger_Right },
			{ kATInputCode_KeyNumpad0, 0, kATInputTrigger_Button0 },
		},
	},
	{
		true, false, L"Arrow Keys -> Paddle A (port 1)", -1,
		{
			{ kATInputControllerType_Paddle, 0 },
		},
		{
			{ kATInputCode_KeyLeft, 0, kATInputTrigger_Left | kATInputTriggerMode_Relative | (6 << kATInputTriggerSpeed_Shift) | (10 << kATInputTriggerAccel_Shift) },
			{ kATInputCode_KeyRight, 0, kATInputTrigger_Right | kATInputTriggerMode_Relative | (6 << kATInputTriggerSpeed_Shift) | (10 << kATInputTriggerAccel_Shift) },
			{ kATInputCode_KeyLControl, 0, kATInputTrigger_Button0 },
		},
	},
	{
		true, false, L"Mouse -> Paddle A (port 1)", -1,
		{
			{ kATInputControllerType_Paddle, 0 },
		},
		{
			{ kATInputCode_MouseHoriz, 0, kATInputTrigger_Axis0 },
			{ kATInputCode_MouseLMB, 0, kATInputTrigger_Button0 },
		}
	},
	{
		true, false, L"Mouse -> ST Mouse (port 1)", -1,
		{
			{ kATInputControllerType_STMouse, 0 },
		},
		{
			{ kATInputCode_MouseHoriz, 0, kATInputTrigger_Axis0 },
			{ kATInputCode_MouseVert, 0, kATInputTrigger_Axis0+1 },
			{ kATInputCode_MouseLMB, 0, kATInputTrigger_Button0 },
			{ kATInputCode_MouseRMB, 0, kATInputTrigger_Button0+1 },
		},
	},
	{
		true, true, L"Arrow Keys -> Joystick (port 2)", -1,
		{
			{ kATInputControllerType_Joystick, 1 },
		},
		{
			{ kATInputCode_KeyLeft, 0, kATInputTrigger_Left },
			{ kATInputCode_KeyRight, 0, kATInputTrigger_Right },
			{ kATInputCode_KeyUp, 0, kATInputTrigger_Up },
			{ kATInputCode_KeyDown, 0, kATInputTrigger_Down },
			{ kATInputCode_KeyLControl, 0, kATInputTrigger_Button0 },
		},
	},
	{
		true, false, L"Mouse -> ST Mouse (port 2)", -1,
		{
			{ kATInputControllerType_STMouse, 1 },
		},
		{
			{ kATInputCode_MouseHoriz, 0, kATInputTrigger_Axis0 },
			{ kATInputCode_MouseVert, 0, kATInputTrigger_Axis0+1 },
			{ kATInputCode_MouseLMB, 0, kATInputTrigger_Button0 },
			{ kATInputCode_MouseRMB, 0, kATInputTrigger_Button0+1 },
		}
	},
	{
		true, false, L"Gamepad -> Joystick (port 1)", -1,
		{
			{ kATInputControllerType_Joystick, 0 },
		},
		{
			{ kATInputCode_JoyPOVUp, 0, kATInputTrigger_Up },
			{ kATInputCode_JoyPOVDown, 0, kATInputTrigger_Down },
			{ kATInputCode_JoyPOVLeft, 0, kATInputTrigger_Left },
			{ kATInputCode_JoyPOVRight, 0, kATInputTrigger_Right },
			{ kATInputCode_JoyStick1Up, 0, kATInputTrigger_Up },
			{ kATInputCode_JoyStick1Down, 0, kATInputTrigger_Down },
			{ kATInputCode_JoyStick1Left, 0, kATInputTrigger_Left },
			{ kATInputCode_JoyStick1Right, 0, kATInputTrigger_Right },
			{ kATInputCode_JoyButton0, 0, kATInputTrigger_Button0 },
			{ kATInputCode_JoyButton0+1, 0, kATInputTrigger_Button0 },
			{ kATInputCode_JoyButton0+2, 0, kATInputTrigger_Button0 },
			{ kATInputCode_JoyButton0+3, 0, kATInputTrigger_Button0 },
		}
	},
	{
		true, false, L"Gamepad 1 -> Joystick (port 1)", 0,
		{
			{ kATInputControllerType_Joystick, 0 },
		},
		{
			{ kATInputCode_JoyPOVUp, 0, kATInputTrigger_Up },
			{ kATInputCode_JoyPOVDown, 0, kATInputTrigger_Down },
			{ kATInputCode_JoyPOVLeft, 0, kATInputTrigger_Left },
			{ kATInputCode_JoyPOVRight, 0, kATInputTrigger_Right },
			{ kATInputCode_JoyStick1Up, 0, kATInputTrigger_Up },
			{ kATInputCode_JoyStick1Down, 0, kATInputTrigger_Down },
			{ kATInputCode_JoyStick1Left, 0, kATInputTrigger_Left },
			{ kATInputCode_JoyStick1Right, 0, kATInputTrigger_Right },
			{ kATInputCode_JoyButton0, 0, kATInputTrigger_Button0 },
			{ kATInputCode_JoyButton0+1, 0, kATInputTrigger_Button0 },
			{ kATInputCode_JoyButton0+2, 0, kATInputTrigger_Button0 },
			{ kATInputCode_JoyButton0+3, 0, kATInputTrigger_Button0 },
		}
	},
	{
		true, false, L"Gamepad 2 -> Joystick (port 2)", 1,
		{
			{ kATInputControllerType_Joystick, 1 },
		},
		{
			{ kATInputCode_JoyPOVUp, 0, kATInputTrigger_Up },
			{ kATInputCode_JoyPOVDown, 0, kATInputTrigger_Down },
			{ kATInputCode_JoyPOVLeft, 0, kATInputTrigger_Left },
			{ kATInputCode_JoyPOVRight, 0, kATInputTrigger_Right },
			{ kATInputCode_JoyStick1Up, 0, kATInputTrigger_Up },
			{ kATInputCode_JoyStick1Down, 0, kATInputTrigger_Down },
			{ kATInputCode_JoyStick1Left, 0, kATInputTrigger_Left },
			{ kATInputCode_JoyStick1Right, 0, kATInputTrigger_Right },
			{ kATInputCode_JoyButton0, 0, kATInputTrigger_Button0 },
			{ kATInputCode_JoyButton0+1, 0, kATInputTrigger_Button0 },
			{ kATInputCode_JoyButton0+2, 0, kATInputTrigger_Button0 },
			{ kATInputCode_JoyButton0+3, 0, kATInputTrigger_Button0 },
		}
	},
	{
		true, true, L"Keyboard -> 5200 Controller (absolute; port 1)", -1,
		{
			{ kATInputControllerType_5200Controller, 0 },
		},
		{
			{ kATInputCode_KeyLeft, 0, kATInputTrigger_Left },
			{ kATInputCode_KeyRight, 0, kATInputTrigger_Right },
			{ kATInputCode_KeyUp, 0, kATInputTrigger_Up },
			{ kATInputCode_KeyDown, 0, kATInputTrigger_Down },
			{ kATInputCode_KeyLControl, 0, kATInputTrigger_Button0 },
			{ kATInputCode_KeyLShift, 0, kATInputTrigger_Button0+1 },
			{ kATInputCode_Key0, 0, kATInputTrigger_5200_0 },
			{ kATInputCode_Key1, 0, kATInputTrigger_5200_1 },
			{ kATInputCode_Key2, 0, kATInputTrigger_5200_2 },
			{ kATInputCode_Key3, 0, kATInputTrigger_5200_3 },
			{ kATInputCode_Key4, 0, kATInputTrigger_5200_4 },
			{ kATInputCode_Key5, 0, kATInputTrigger_5200_5 },
			{ kATInputCode_Key6, 0, kATInputTrigger_5200_6 },
			{ kATInputCode_Key7, 0, kATInputTrigger_5200_7 },
			{ kATInputCode_Key8, 0, kATInputTrigger_5200_8 },
			{ kATInputCode_Key9, 0, kATInputTrigger_5200_9 },
			{ kATInputCode_KeyOemMinus, 0, kATInputTrigger_5200_Star },
			{ kATInputCode_KeyOemPlus, 0, kATInputTrigger_5200_Pound },
			{ kATInputCode_KeyF2, 0, kATInputTrigger_5200_Start },
			{ kATInputCode_KeyF3, 0, kATInputTrigger_5200_Pause },
			{ kATInputCode_KeyF4, 0, kATInputTrigger_5200_Reset },
		},
	},
	{
		true, true, L"Keyboard -> 5200 Controller (relative; port 1)", -1,
		{
			{ kATInputControllerType_5200Controller, 0 },
		},
		{
			{ kATInputCode_KeyLeft,		0, kATInputTrigger_Left		| kATInputTriggerMode_Relative | (4 << kATInputTriggerSpeed_Shift) | (6 << kATInputTriggerAccel_Shift)  },
			{ kATInputCode_KeyRight,	0, kATInputTrigger_Right	| kATInputTriggerMode_Relative | (4 << kATInputTriggerSpeed_Shift) | (6 << kATInputTriggerAccel_Shift)  },
			{ kATInputCode_KeyUp,		0, kATInputTrigger_Up		| kATInputTriggerMode_Relative | (4 << kATInputTriggerSpeed_Shift) | (6 << kATInputTriggerAccel_Shift)  },
			{ kATInputCode_KeyDown,		0, kATInputTrigger_Down		| kATInputTriggerMode_Relative | (4 << kATInputTriggerSpeed_Shift) | (6 << kATInputTriggerAccel_Shift)  },
			{ kATInputCode_KeyLControl, 0, kATInputTrigger_Button0 },
			{ kATInputCode_KeyLShift, 0, kATInputTrigger_Button0+1 },
			{ kATInputCode_Key0, 0, kATInputTrigger_5200_0 },
			{ kATInputCode_Key1, 0, kATInputTrigger_5200_1 },
			{ kATInputCode_Key2, 0, kATInputTrigger_5200_2 },
			{ kATInputCode_Key3, 0, kATInputTrigger_5200_3 },
			{ kATInputCode_Key4, 0, kATInputTrigger_5200_4 },
			{ kATInputCode_Key5, 0, kATInputTrigger_5200_5 },
			{ kATInputCode_Key6, 0, kATInputTrigger_5200_6 },
			{ kATInputCode_Key7, 0, kATInputTrigger_5200_7 },
			{ kATInputCode_Key8, 0, kATInputTrigger_5200_8 },
			{ kATInputCode_Key9, 0, kATInputTrigger_5200_9 },
			{ kATInputCode_KeyOemMinus, 0, kATInputTrigger_5200_Star },
			{ kATInputCode_KeyOemPlus, 0, kATInputTrigger_5200_Pound },
			{ kATInputCode_KeyF2, 0, kATInputTrigger_5200_Start },
			{ kATInputCode_KeyF3, 0, kATInputTrigger_5200_Pause },
			{ kATInputCode_KeyF4, 0, kATInputTrigger_5200_Reset },
		}
	},
	{
		true, false, L"Xbox 360 Controller -> Joystick (port 1)", -1,
		{
			{ kATInputControllerType_Joystick, 0 },
			{ kATInputControllerType_Console, 0 },
		},
		{
			// Joystick
			{ kATInputCode_JoyPOVLeft,		0, kATInputTrigger_Left },
			{ kATInputCode_JoyPOVRight,		0, kATInputTrigger_Right },
			{ kATInputCode_JoyPOVUp,		0, kATInputTrigger_Up },
			{ kATInputCode_JoyPOVDown,		0, kATInputTrigger_Down },
			{ kATInputCode_JoyStick1Left,	0, kATInputTrigger_Left },
			{ kATInputCode_JoyStick1Right,	0, kATInputTrigger_Right },
			{ kATInputCode_JoyStick1Up,		0, kATInputTrigger_Up },
			{ kATInputCode_JoyStick1Down,	0, kATInputTrigger_Down },
			{ kATInputCode_JoyButton0,		0, kATInputTrigger_Button0 },
			{ kATInputCode_JoyButton0+1,	0, kATInputTrigger_Button0 | kATInputTriggerMode_AutoFire | (5 << kATInputTriggerSpeed_Shift) },

			// Console
			{ kATInputCode_JoyButton0+2,	1, kATInputTrigger_Option },
			{ kATInputCode_JoyButton0+4,	1, kATInputTrigger_Turbo },
			{ kATInputCode_JoyButton0+6,	1, kATInputTrigger_Select },
			{ kATInputCode_JoyButton0+7,	1, kATInputTrigger_Start },
		}
	},
	{
		true, false, L"Xbox 360 Controller -> Paddle A", -1,
		{
			{ kATInputControllerType_Paddle, 0 },
			{ kATInputControllerType_Console, 0 },
		},
		{
			// Paddle
			{ kATInputCode_JoyButton0,		0, kATInputTrigger_Button0 },
			{ kATInputCode_JoyButton0+1,	0, kATInputTrigger_Button0 | kATInputTriggerMode_AutoFire | (5 << kATInputTriggerSpeed_Shift) },
			{ kATInputCode_JoyHoriz1,		0, kATInputTrigger_Axis0 | kATInputTriggerMode_Relative | (5 << kATInputTriggerSpeed_Shift) },
			{ kATInputCode_JoyPOVLeft,		0, kATInputTrigger_Left | kATInputTriggerMode_Relative | (5 << kATInputTriggerSpeed_Shift) },
			{ kATInputCode_JoyPOVRight,		0, kATInputTrigger_Right | kATInputTriggerMode_Relative | (5 << kATInputTriggerSpeed_Shift) },
			{ kATInputCode_JoyHoriz3,		0, kATInputTrigger_Axis0+1 },
			{ kATInputCode_JoyVert3,		0, kATInputTrigger_Axis0+2 },

			// Console
			{ kATInputCode_JoyButton0+2,	1, kATInputTrigger_Option },
			{ kATInputCode_JoyButton0+4,	1, kATInputTrigger_Turbo },
			{ kATInputCode_JoyButton0+6,	1, kATInputTrigger_Select },
			{ kATInputCode_JoyButton0+7,	1, kATInputTrigger_Start },
		}
	},
	{
		true, false, L"Xbox 360 Controller -> 5200 Controller (relative; port 1)", -1,
		{
			{ kATInputControllerType_5200Controller, 0 },
		},
		{
			{ kATInputCode_JoyHoriz1,		0, kATInputTrigger_Right | kATInputTriggerMode_Relative | (5 << kATInputTriggerSpeed_Shift)  },
			{ kATInputCode_JoyVert1,		0, kATInputTrigger_Down  | kATInputTriggerMode_Relative | (5 << kATInputTriggerSpeed_Shift)  },
			{ kATInputCode_JoyPOVLeft,		0, kATInputTrigger_Left  | kATInputTriggerMode_Relative | (4 << kATInputTriggerSpeed_Shift) | (6 << kATInputTriggerAccel_Shift)  },
			{ kATInputCode_JoyPOVRight,		0, kATInputTrigger_Right | kATInputTriggerMode_Relative | (4 << kATInputTriggerSpeed_Shift) | (6 << kATInputTriggerAccel_Shift)  },
			{ kATInputCode_JoyPOVUp,		0, kATInputTrigger_Up    | kATInputTriggerMode_Relative | (4 << kATInputTriggerSpeed_Shift) | (6 << kATInputTriggerAccel_Shift)  },
			{ kATInputCode_JoyPOVDown,		0, kATInputTrigger_Down  | kATInputTriggerMode_Relative | (4 << kATInputTriggerSpeed_Shift) | (6 << kATInputTriggerAccel_Shift)  },
			{ kATInputCode_JoyButton0+0,	0, kATInputTrigger_Button0 },
			{ kATInputCode_JoyButton0+1,	0, kATInputTrigger_Button0+1 },
			{ kATInputCode_JoyButton0+7,	0, kATInputTrigger_5200_Start },
			{ kATInputCode_JoyButton0+3,	0, kATInputTrigger_5200_Pause },
			{ kATInputCode_JoyButton0+6,	0, kATInputTrigger_5200_Reset },
		}
	},
	{
		true, false, L"Xbox 360 Controller -> User interface control", -1,
		{
			{ kATInputControllerType_Console, 0 },
		},
		{
			{ kATInputCode_JoyPOVLeft,		0, kATInputTrigger_UILeft },
			{ kATInputCode_JoyPOVRight,		0, kATInputTrigger_UIRight },
			{ kATInputCode_JoyPOVUp,		0, kATInputTrigger_UIUp },
			{ kATInputCode_JoyPOVDown,		0, kATInputTrigger_UIDown },
			{ kATInputCode_JoyButton0+0,	0, kATInputTrigger_UIAccept },
			{ kATInputCode_JoyButton0+1,	0, kATInputTrigger_UIReject },
			{ kATInputCode_JoyButton0+2,	0, kATInputTrigger_UIOption },
			{ kATInputCode_JoyButton0+3,	0, kATInputTrigger_UIMenu },
			{ kATInputCode_JoyButton0+4,	0, kATInputTrigger_UISwitchLeft },
			{ kATInputCode_JoyButton0+5,	0, kATInputTrigger_UISwitchRight },
			{ kATInputCode_JoyStick2Down,	0, kATInputTrigger_UILeftShift },
			{ kATInputCode_JoyStick2Up,		0, kATInputTrigger_UIRightShift },
		}
	},
	{
		false, false, L"Mouse -> Light Gun (XG-1)", -1,
		{
			{ kATInputControllerType_LightGun, 0 },
		},
		{
			{ kATInputCode_MouseBeamX,		0, kATInputTrigger_Axis0 },
			{ kATInputCode_MouseBeamY,		0, kATInputTrigger_Axis0+1 },
			{ kATInputCode_MouseLMB,		0, kATInputTrigger_Button0 },
			{ kATInputCode_None,			0, kATInputTrigger_Button0+2 | kATInputTriggerMode_Inverted },
		}
	},
	{
		false, false, L"Mouse -> Light Pen (CX-70/CX-75)", -1,
		{
			{ kATInputControllerType_LightPen, 0 },
		},
		{
			{ kATInputCode_MouseBeamX,		0, kATInputTrigger_Axis0 },
			{ kATInputCode_MouseBeamY,		0, kATInputTrigger_Axis0+1 },
			{ kATInputCode_MouseLMB,		0, kATInputTrigger_Button0 },
			{ kATInputCode_None,			0, kATInputTrigger_Button0+2 | kATInputTriggerMode_Inverted },
		}
	},
	{
		false, false, L"Keyboard -> Driving Controller (CX-20)", -1,
		{
			{ kATInputControllerType_Driving, 0 },
		},
		{
			{ kATInputCode_KeyLeft,			0, kATInputTrigger_Left | kATInputTriggerMode_Relative | (5 << kATInputTriggerSpeed_Shift) },
			{ kATInputCode_KeyRight,		0, kATInputTrigger_Right | kATInputTriggerMode_Relative | (5 << kATInputTriggerSpeed_Shift) },
			{ kATInputCode_KeyLControl,		0, kATInputTrigger_Button0 },
		}
	},
};

uint32 ATInputManager::GetPresetMapDefCount() const {
	return vdcountof(kPresetMapDefs);
}

const ATInputManager::PresetMapDef *ATInputManager::GetPresetMapDef(uint32 index) const {
	if (index >= std::size(kPresetMapDefs))
		return nullptr;

	return &kPresetMapDefs[index];
}

void ATInputManager::InitPresetMap(const PresetMapDef& def, ATInputMap **ppMap) const {
	vdrefptr<ATInputMap> imap(new ATInputMap);

	imap->SetName(def.mpName);

	if (def.mUnit >= 0)
		imap->SetSpecificInputUnit(def.mUnit);

	imap->AddControllers(def.mControllers);
	imap->AddMappings(def.mMappings);
	imap->SetQuickMap(def.mbDefaultQuick);

	*ppMap = imap.release();
}

bool ATInputManager::IsTriggerRestricted(const Trigger& trigger) const {
	if (!mbRestrictedMode)
		return false;

	// UI triggers always pass through restricted mode.
	if ((trigger.mId & 0xff00) == kATInputTrigger_UILeft)
		return false;

	// Triggers whose controller has been redirected to a capture proxy
	// (netplay) are allowed through for controller-port-writing trigger
	// classes: their writes land in the capture buffer rather than
	// the real hardware port, so they can't cause the "double fire"
	// that restricted mode exists to prevent. This lets the full
	// ATInputManager pipeline (custom key bindings, touch controls,
	// secondary gamepads, etc.) feed local port-1 input into netplay
	// instead of being silenced.
	//
	// Console (0x02xx), Keyboard (0x03xx), Flag (0x09xx) and similar
	// trigger classes bypass the controller port -- e.g. the Console
	// controller writes directly to GTIA via IATInputConsoleCallback
	// -- so redirecting the controller port does NOT divert those
	// writes. They must remain restricted, and netplay feeds console
	// and keyboard state through its separate OnLocalConsoleSwitch /
	// OnLocalKeyDown paths.
	if (trigger.mpController && trigger.mpController->IsRedirected()) {
		const uint32 cls = trigger.mId & kATInputTrigger_ClassMask;
		// Allow: Button0 (0x0000), directions (0x0100), 5200 keypad
		// (0x0400, writes to 5200 port), axis (0x0800, paddles etc).
		if (cls == 0x0000 || cls == 0x0100 || cls == 0x0400 || cls == 0x0800)
			return false;
	}

	return true;
}
