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
#include <vd2/system/constexpr.h>
#include <vd2/system/math.h>
#include <at/atcore/logging.h>
#include <at/atcore/deviceport.h>
#include <at/atcore/enumparseimpl.h>
#include <at/atcore/scheduler.h>
#include "inputcontroller.h"
#include "inputdefs.h"
#include "inputmanager.h"
#include <at/ataudio/pokey.h>
#include "antic.h"
#include "gtia.h"
#include "pia.h"

class ATAnticEmulator;

///////////////////////////////////////////////////////////////////////////

AT_DEFINE_ENUM_TABLE_BEGIN(ATLightPenNoiseMode)
	{ ATLightPenNoiseMode::None, "none" },
	{ ATLightPenNoiseMode::Low, "low" },
	{ ATLightPenNoiseMode::High, "high" },
AT_DEFINE_ENUM_TABLE_END(ATLightPenNoiseMode, ATLightPenNoiseMode::None)

// This accounts for the discrepancy between the emulator's ANTIC counters
// and the real ANTIC counters, as well as delays in the light pen latch.
constexpr int ATLightPenPort::kHorizOffset = 12;

ATLightPenPort::ATLightPenPort() {
}

void ATLightPenPort::Init(ATAnticEmulator& antic, ATGTIAEmulator& gtia) {
	mpAntic = &antic;
	mpGTIA = &gtia;
}

bool ATLightPenPort::GetImmediateUpdateEnabled() const {
	return mbImmediateUpdate;
}

void ATLightPenPort::SetImmediateUpdateEnabled(bool enable) {
	mbImmediateUpdate = enable;
}

ATLightPenNoiseMode ATLightPenPort::GetNoiseMode() const {
	return mNoiseMode;
}

void ATLightPenPort::SetNoiseMode(ATLightPenNoiseMode mode) {
	mNoiseMode = mode;
}

void ATLightPenPort::ApplyCorrection(int dx, int dy) {
	auto& adjust = mAdjust[mbLastWasPen];

	adjust.x += dx;
	adjust.y += dy;

	if (adjust.x < -114)
		adjust.x += 228 * ((adjust.x - 57) / 228);
	else
		adjust.x -= 228 * ((adjust.x + 57) / 228);
}

void ATLightPenPort::TriggerCorrection(bool phase, int x, int y) {
	mTriggerCorrectionEvent.InvokeAll(phase, x, y);
}

void ATLightPenPort::SetIgnorePort34(bool ignore) {
	mTriggerStateMask = ignore ? 0x03 : 0x0F;
}

void ATLightPenPort::SetPosition(bool pen, int x, int y) {
	mbLastWasPen = pen;

	if (mbImmediateUpdate) {
		SetAnticPenPosition(x, y);
	}

	mbOddPhase = (x & 1) != 0;
	mPenBeamX = x;
	mPenBeamY = y;
}

void ATLightPenPort::SetPortTriggerState(int index, bool state) {
	uint8 newState = mTriggerState;

	if (state)
		newState |= 1 << index;
	else
		newState &= ~(1 << index);

	if (newState == mTriggerState)
		return;

	mTriggerState = newState;

	if ((newState & mTriggerStateMask) && mpAntic && !mbImmediateUpdate) {
		int beamX = mpAntic->GetBeamX() * 2 + kHorizOffset + (mbOddPhase ? 1 : 0);
		int beamY = mpAntic->GetBeamY();

		// if the current timing is close enough, correct to the pen position
		if (abs(beamY - mPenBeamY) <= 2) {
			beamX = mPenBeamX;
			beamY = mPenBeamY;
		}

		SetAnticPenPosition(beamX, beamY);
	}
}

void ATLightPenPort::SetAnticPenPosition(int x, int y) {
	int yn = (int)mpAntic->GetScanlineCount();

	int xc = x;
	int yc = y;

	yc += xc / 228;
	xc %= 228;

	if (xc < 0) {
		xc += 228;
		--yc;
	}

	yc %= yn;
		
	if (yc < 0)
		yc += yn;

	mpAntic->SetLightPenPosition(xc, yc);
}

///////////////////////////////////////////////////////////////////////////

ATPortInputController::ATPortInputController() {
}

ATPortInputController::~ATPortInputController() {
	Detach();
}

void ATPortInputController::Attach(IATDevicePortManager& portMgr, int portIndex, int multiIndex) {
	portMgr.AllocControllerPort(portIndex, ~mpControllerPort);

	mPortIndex = portIndex;
	mMultiMask = multiIndex < 0 ? 0 : 1 << multiIndex;

	mpControllerPort->SetOnDirOutputChanged(0, [this] { UpdateOutput(); }, false);

	OnAttach();
}

void ATPortInputController::Detach() {
	// Ensure a redirect is torn down before releasing refs, so we restore
	// the real port's neutral state rather than leaving stale input held.
	if (mpRealControllerPort)
		SetRedirectPort(nullptr);

	if (mpControllerPort) {
		OnDetach();
		mpControllerPort = nullptr;
	}

	mPortIndex = -1;
}

void ATPortInputController::SetRedirectPort(IATDeviceControllerPort *proxy) {
	if (proxy) {
		if (mpRealControllerPort)
			return;  // already redirected
		if (!mpControllerPort)
			return;

		// Park the real port in a neutral state so it stops driving
		// hardware while we hold the redirect.
		mpControllerPort->SetDirInput(0x0F);
		mpControllerPort->SetTriggerDown(false);
		mpControllerPort->ResetPotPosition(false);
		mpControllerPort->ResetPotPosition(true);

		mpRealControllerPort = mpControllerPort;
		mpControllerPort = proxy;
	} else {
		if (!mpRealControllerPort)
			return;  // not redirected

		// Swap the real port back in. We don't try to restore the
		// previously-held stick/trigger state on the real port:
		// ATInputManager::ReleaseButtons (called by netplay EndSession)
		// drops any held inputs, and the next user interaction will
		// push fresh state through the full controller pipeline.
		mpControllerPort = mpRealControllerPort;
		mpRealControllerPort = nullptr;
	}
}

void ATPortInputController::SelectMultiJoy(uint8 mask) {
	if (mMultiMask)
		mpControllerPort->SetEnabled((mMultiMask & mask) != 0);
}

void ATPortInputController::SetPortOutput(uint32 portBits) {
	mpControllerPort->SetDirInput((uint8)(~portBits & 15));
	mpControllerPort->SetTriggerDown((portBits & 0x100) != 0);
}

void ATPortInputController::ResetPotPositions() {
	mpControllerPort->ResetPotPosition(false);
	mpControllerPort->ResetPotPosition(true);
}

void ATPortInputController::SetPotPosition(bool second, uint8 pos) {
	mpControllerPort->SetPotPosition(second, pos);
}

void ATPortInputController::SetPotHiPosition(bool second, int pos, bool grounded) {
	mpControllerPort->SetPotHiresPosition(second, pos, grounded);
}

void ATPortInputController::SetOutputMonitorMask(uint8 mask) {
	// The real port owns the dir-output-changed callback (registered
	// in Attach), so its monitor mask is what determines when the
	// callback fires. Route the mask to the real port while redirected.
	IATDeviceControllerPort *port = mpRealControllerPort
		? (IATDeviceControllerPort *)mpRealControllerPort
		: mpControllerPort.get();
	if (port)
		port->SetDirOutputMask(mask);
}

void ATPortInputController::UpdateOutput() {
	// When redirected, the callback that invoked us was fired by the
	// real port reacting to real hardware; read from that same port
	// so mPortOutputState reflects actual bus state rather than the
	// proxy's stub 0x0F.
	IATDeviceControllerPort *port = mpRealControllerPort
		? (IATDeviceControllerPort *)mpRealControllerPort
		: mpControllerPort.get();
	if (!port)
		return;
	mPortOutputState = port->GetCurrentDirOutput();
	OnPortOutputChanged(mPortOutputState);
}

///////////////////////////////////////////////////////////////////////////

ATMouseController::ATMouseController(bool amigaMode)
	: mPortBits(0)
	, mTargetX(0)
	, mTargetY(0)
	, mAccumX(0)
	, mAccumY(0)
	, mpUpdateXEvent(NULL)
	, mpUpdateYEvent(NULL)
	, mpScheduler(NULL)
	, mbAmigaMode(amigaMode)
{
	mbButtonState[0] = false;
	mbButtonState[1] = false;
}

ATMouseController::~ATMouseController() {
}

void ATMouseController::Init(ATScheduler *scheduler) {
	mpScheduler = scheduler;
}

void ATMouseController::SetPosition(int x, int y) {
	mTargetX = x;
	mTargetY = y;
}

void ATMouseController::AddDelta(int dx, int dy) {
	mTargetX += dx;
	mTargetY += dy;
}

void ATMouseController::SetButtonState(int button, bool state) {
	mbButtonState[button] = state;
}

void ATMouseController::SetDigitalTrigger(uint32 trigger, bool state) {
	switch(trigger) {
		case kATInputTrigger_Button0:
			if (mbButtonState[0] != state) {
				mbButtonState[0] = state;

				uint32 newBits = (mPortBits & ~0x100) + (state ? 0x100 : 0);

				if (mPortBits != newBits) {
					mPortBits = newBits;
					SetPortOutput(mPortBits);
				}
			}
			break;
		case kATInputTrigger_Button0 + 1:
			if (mbButtonState[1] != state) {
				mbButtonState[1] = state;

				SetPotPosition(false, state ? 0 : 229);
			}
			break;
	}
}

void ATMouseController::ApplyImpulse(uint32 trigger, int ds) {
	ds <<= 5;

	switch(trigger) {
		case kATInputTrigger_Left:
			mTargetX -= ds;
			EnableUpdate();
			break;

		case kATInputTrigger_Axis0:
		case kATInputTrigger_Right:
			mTargetX += ds;
			EnableUpdate();
			break;

		case kATInputTrigger_Up:
			mTargetY -= ds;
			EnableUpdate();
			break;

		case kATInputTrigger_Axis0+1:
		case kATInputTrigger_Down:
			mTargetY += ds;
			EnableUpdate();
			break;
	}
}

void ATMouseController::OnScheduledEvent(uint32 id) {
	if (id == 1) {
		mpUpdateXEvent = nullptr;
		Update(true, false);
		EnableUpdate();
	} else if (id == 2) {
		mpUpdateYEvent = nullptr;
		Update(false, true);
		EnableUpdate();
	}
}

void ATMouseController::EnableUpdate() {
	if (!mpUpdateXEvent) {
		const uint32 dx = (mAccumX << 16) - mTargetX;

		if (dx & UINT32_C(0xffff0000)) {
			uint32 adx = dx & UINT32_C(0x80000000) ? 0-dx : dx;

			mpUpdateXEvent = mpScheduler->AddEvent(std::max<sint32>(1, std::min<uint32>(256, 0x1000000 / adx)), this, 1);
		}
	}

	if (!mpUpdateYEvent) {
		const uint32 dy = (mAccumY << 16) - mTargetY;

		if (dy & UINT32_C(0xffff0000)) {
			uint32 ady = dy & UINT32_C(0x80000000) ? 0-dy : dy;

			mpUpdateYEvent = mpScheduler->AddEvent(std::max<sint32>(1, std::min<uint32>(256, 0x1000000 / ady)), this, 2);
		}
	}
}

void ATMouseController::Update(bool doX, bool doY) {
	bool changed = false;

	if (doX) {
		const uint32 posX = mTargetX >> 16;
		if (mAccumX != posX) {
			if ((sint16)(mAccumX - posX) < 0)
				++mAccumX;
			else
				--mAccumX;

			mAccumX &= 0xffff;

			changed = true;
		}
	}

	if (doY) {
		const int posY = mTargetY >> 16;
		if (mAccumY != posY) {
			if ((sint16)(mAccumY - posY) < 0)
				++mAccumY;
			else
				--mAccumY;

			mAccumY &= 0xffff;

			changed = true;
		}
	}

	if (changed)
		UpdatePort();
}

void ATMouseController::UpdatePort() {
	static const uint8 kSTTabX[4] = { 0x00, 0x02, 0x03, 0x01 };
	static const uint8 kSTTabY[4] = { 0x00, 0x08, 0x0c, 0x04 };
	static const uint8 kAMTabX[4] = { 0x00, 0x02, 0x0A, 0x08 };
	static const uint8 kAMTabY[4] = { 0x00, 0x01, 0x05, 0x04 };

	uint32 val;
		
	if (mbAmigaMode)
		val = kAMTabX[mAccumX & 3] + kAMTabY[mAccumY & 3];
	else
		val = kSTTabX[mAccumX & 3] + kSTTabY[mAccumY & 3];

	uint32 newPortBits = (mPortBits & ~15) + val;

	if (mPortBits != newPortBits) {
		mPortBits = newPortBits;
		SetPortOutput(mPortBits);
	}
}

void ATMouseController::OnAttach() {
}

void ATMouseController::OnDetach() {
	ResetPotPositions();

	if (mpScheduler) {
		mpScheduler->UnsetEvent(mpUpdateXEvent);
		mpScheduler->UnsetEvent(mpUpdateYEvent);
	}
}

///////////////////////////////////////////////////////////////////////////

ATTrackballController::ATTrackballController()
	: mPortBits(0)
	, mTargetX(0)
	, mTargetY(0)
	, mAccumX(0)
	, mAccumY(0)
	, mpUpdateEvent(NULL)
	, mpScheduler(NULL)
{
	mbButtonState = false;
}

ATTrackballController::~ATTrackballController() {
}

void ATTrackballController::Init(ATScheduler *scheduler) {
	mpScheduler = scheduler;
}

void ATTrackballController::SetPosition(int x, int y) {
	mTargetX = x;
	mTargetY = y;
}

void ATTrackballController::AddDelta(int dx, int dy) {
	mTargetX += dx;
	mTargetY += dy;
}

void ATTrackballController::SetButtonState(int button, bool state) {
	mbButtonState = state;
}

void ATTrackballController::SetDigitalTrigger(uint32 trigger, bool state) {
	switch(trigger) {
		case kATInputTrigger_Button0:
			if (mbButtonState != state) {
				mbButtonState = state;

				uint32 newBits = (mPortBits & ~0x100) + (state ? 0x100 : 0);

				if (mPortBits != newBits) {
					mPortBits = newBits;
					SetPortOutput(mPortBits);
				}
			}
			break;
	}
}

void ATTrackballController::ApplyImpulse(uint32 trigger, int ds) {
	ds <<= 5;

	switch(trigger) {
		case kATInputTrigger_Left:
			mTargetX -= ds;
			break;

		case kATInputTrigger_Axis0:
		case kATInputTrigger_Right:
			mTargetX += ds;
			break;

		case kATInputTrigger_Up:
			mTargetY -= ds;
			break;

		case kATInputTrigger_Axis0+1:
		case kATInputTrigger_Down:
			mTargetY += ds;
			break;
	}
}

void ATTrackballController::OnScheduledEvent(uint32 id) {
	mpUpdateEvent = mpScheduler->AddEvent(7, this, 1);
	Update();
}

void ATTrackballController::Update() {
	bool changed = false;
	uint8 dirBits = mPortBits & 5;

	const uint16 posX = mTargetX >> 16;
	if (mAccumX != posX) {
		if ((sint16)(mAccumX - posX) < 0) {
			dirBits &= 0xFE;
			++mAccumX;
		} else {
			dirBits |= 0x01;
			--mAccumX;
		}

		changed = true;
	}

	const uint16 posY = mTargetY >> 16;
	if (mAccumY != posY) {
		if ((sint16)(mAccumY - posY) < 0) {
			++mAccumY;
			dirBits &= 0xFB;
		} else {
			--mAccumY;
			dirBits |= 0x04;
		}

		changed = true;
	}

	if (changed) {
		uint32 val = dirBits + ((mAccumX << 1) & 0x02) + ((mAccumY << 3) & 0x08);

		uint32 newPortBits = (mPortBits & ~15) + val;

		if (mPortBits != newPortBits) {
			mPortBits = newPortBits;
			SetPortOutput(mPortBits);
		}
	}
}

void ATTrackballController::OnAttach() {
	if (!mpUpdateEvent)
		mpUpdateEvent = mpScheduler->AddEvent(32, this, 1);
}

void ATTrackballController::OnDetach() {
	if (mpUpdateEvent) {
		mpScheduler->RemoveEvent(mpUpdateEvent);
		mpUpdateEvent = NULL;
		mpScheduler = NULL;
	}
}

///////////////////////////////////////////////////////////////////////////

ATPaddleController::ATPaddleController() {
}

ATPaddleController::~ATPaddleController() {
}

void ATPaddleController::SetHalf(bool second) {
	mbSecond = second;
}

void ATPaddleController::AddDelta(int delta) {
	int newPos = mRawPos;

	newPos -= delta * 113;

	if (newPos < (1 << 16) + 0x8000)
		newPos = (1 << 16) + 0x8000;

	if (newPos > (229 << 16) + 0x8000)
		newPos = (229 << 16) + 0x8000;

	SetRawHiPos(newPos);
}

void ATPaddleController::SetTrigger(bool enable) {
	const uint32 newbits = (enable ? mbSecond ? 0x08 : 0x04 : 0x00);

	if (mPortBits != newbits) {
		mPortBits = newbits;
		SetPortOutput(newbits);
	}
}

void ATPaddleController::SetDigitalTrigger(uint32 trigger, bool state) {
	if (trigger == kATInputTrigger_Button0)
		SetTrigger(state);
	else if (trigger == kATInputTrigger_Axis0) {
		const int pos = state ? (1 << 16) + 0x8000 : (228 << 16) + 0x8000;

		SetRawHiPos(pos);
	} else if (trigger == kATInputTrigger_Left) {
		if (mbLeft != state) {
			mbLeft = state;

			if (mbRight == state)
				SetRawHiPos(114 << 16);
			else
				SetRawHiPos(state ? 1 << 16 : 227 << 16);
		}
	} else if (trigger == kATInputTrigger_Right) {
		if (mbRight != state) {
			mbRight = state;

			if (mbLeft == state)
				SetRawHiPos(114 << 16);
			else
				SetRawHiPos(state ? 227 << 16 : 1 << 16);
		}
	}
}

void ATPaddleController::ApplyAnalogInput(uint32 trigger, int ds) {
	switch(trigger) {
		case kATInputTrigger_Left:
			ds = -ds;
			[[fallthrough]];
		case kATInputTrigger_Axis0:
		case kATInputTrigger_Right:
			{
				int oldPos = mRawPos;
				mRawPos = (114 << 16) + 0x8000 - ds * 114;

				if (mRawPos < (1 << 16) + 0x8000)
					mRawPos = (1 << 16) + 0x8000;

				if (mRawPos > (229 << 16) + 0x8000)
					mRawPos = (229 << 16) + 0x8000;

				int newPos = mRawPos;

				if (newPos != oldPos)
					SetPotHiPosition(mbSecond, newPos);
			}
			break;
		case kATInputTrigger_Axis0+1:
			mRotX[mRotIndex] = ds;
			break;
		case kATInputTrigger_Axis0+2:
			mRotY[mRotIndex] = ds;
			break;
	}
}

void ATPaddleController::ApplyImpulse(uint32 trigger, int ds) {
	switch(trigger) {
		case kATInputTrigger_Axis0:
		case kATInputTrigger_Right:
			AddDelta(ds);
			break;

		case kATInputTrigger_Left:
			AddDelta(-ds);
			break;
	}
}

void ATPaddleController::Tick() {
	mRotIndex = (mRotIndex + 1) & 3;

	float x = (float)((mRotX[0] + mRotX[1] + mRotX[2] + mRotX[3] + 2) >> 2);
	float y = (float)((mRotY[0] + mRotY[1] + mRotY[2] + mRotY[3] + 2) >> 2);

	AddDelta(VDRoundToInt32((y * mRotXLast - x * mRotYLast) * (1.0f / 200000.0f)));

	mRotXLast = x;
	mRotYLast = y;
}

void ATPaddleController::OnDetach() {
	SetPotPosition(mbSecond, 229);
}

void ATPaddleController::SetRawHiPos(int hiPos) {


	if (mRawPos != hiPos) {
		mRawPos = hiPos;

		if (mpControllerPort->IsPotNoiseEnabled()) {
			// The method to the madness below:
			//
			// Paddle pot noise comes from dirt or worn out portions between
			// the wiper and the track. This means that the noise is semi-
			// deterministic per position. We quantize to ~57 positions,
			// compute noise per position, and linearly interpolate between
			// the two adjacent positions.
			//
			// The noise is squared to bias it toward higher values, or rotating
			// the paddle knob left.

			const uint32 noiseHash = (uint32)hiPos;
			const uint32 noiseIndex = noiseHash >> 18;

			const auto h = [](uint32 v) {
				v *= 0xacf0fd51;		// randomly selected value
				v ^= v >> 16;

				return (float)(v & 0xFFF) / 4095.0f;
			};

			float v0 = h(noiseIndex);
			float v1 = h(noiseIndex + 1);
			float v = v0 + (v1 - v0) * (float)(noiseHash & 0x3FFFF) / 262144.0f;

			v *= v;
			v *= (65536.0f * 40.0f);

			hiPos += VDRoundToInt32(v);
		}

		SetPotHiPosition(mbSecond, hiPos, false);
	}
}

///////////////////////////////////////////////////////////////////////////

ATTabletController::ATTabletController(int styUpPos, bool invertY)
	: mPortBits(0)
	, mbStylusUp(false)
	, mbInvertY(invertY)
	, mStylusUpPos(styUpPos)
{
	mRawPos[0] = 0;
	mRawPos[1] = 0;
}

ATTabletController::~ATTabletController() {
}

void ATTabletController::SetDigitalTrigger(uint32 trigger, bool state) {
	uint32 newBits = mPortBits;

	if (trigger == kATInputTrigger_Button0) {
		if (state)
			newBits |= 1;
		else
			newBits &= ~1;
	} else if (trigger == kATInputTrigger_Button0+1) {
		if (state)
			newBits |= 4;
		else
			newBits &= ~4;
	} else if (trigger == kATInputTrigger_Button0+2) {
		if (state)
			newBits |= 8;
		else
			newBits &= ~8;
	} else if (trigger == kATInputTrigger_Button0+3) {
		mbStylusUp = state;

		SetPos(0, mRawPos[0]);
		SetPos(1, mRawPos[1]);
	}

	if (mPortBits != newBits) {
		mPortBits = newBits;
		SetPortOutput(newBits);
	}
}

void ATTabletController::ApplyAnalogInput(uint32 trigger, int ds) {
	switch(trigger) {
		case kATInputTrigger_Axis0+1:
			if (mbInvertY)
				ds = -ds;
			// fall through
		case kATInputTrigger_Axis0:
			SetPos(trigger - kATInputTrigger_Axis0, ds);
			break;
	}
}

void ATTabletController::ApplyImpulse(uint32 trigger, int ds) {
	switch(trigger) {
		case kATInputTrigger_Left:
			ds = -ds;
		case kATInputTrigger_Axis0:
		case kATInputTrigger_Right:
			AddDelta(0, ds);
			break;

		case kATInputTrigger_Up:
			ds = -ds;
		case kATInputTrigger_Axis0+1:
		case kATInputTrigger_Down:
			if (mbInvertY)
				ds = -ds;

			AddDelta(1, ds);
			break;
	}
}

void ATTabletController::AddDelta(int axis, int delta) {
	int pos = mRawPos[axis] + delta;

	SetPos(axis, pos);
}

void ATTabletController::SetPos(int axis, int pos) {
	if (pos < -0x10000)
		pos = -0x10000;

	if (pos > 0x10000)
		pos = 0x10000;

	int oldPos = (mRawPos[axis] * 114 + 0x728000) >> 16;
	int newPos = mbStylusUp ? mStylusUpPos : (pos * 114 + 0x728000) >> 16;

	mRawPos[axis] = pos;

	if (newPos != oldPos)
		SetPotPosition(axis != 0, newPos);
}

///////////////////////////////////////////////////////////////////////////

ATJoystickController::ATJoystickController()
	: mPortBits(0)
{
}

ATJoystickController::~ATJoystickController() {
}

void ATJoystickController::SetDigitalTrigger(uint32 trigger, bool state) {
	uint32 mask = 0;

	switch(trigger) {
		case kATInputTrigger_Button0:
			mask = 0x100;
			break;

		case kATInputTrigger_Up:
			mask = 0x01;
			break;

		case kATInputTrigger_Down:
			mask = 0x02;
			break;

		case kATInputTrigger_Left:
			mask = 0x04;
			break;

		case kATInputTrigger_Right:
			mask = 0x08;
			break;

		default:
			return;
	}

	uint32 bit = state ? mask : 0;

	if ((mPortBits ^ bit) & mask) {
		mPortBits ^= mask;

		UpdatePortOutput();
	}
}

void ATJoystickController::UpdatePortOutput() {
	uint32 v = mPortBits;

	if ((v & 0x0c) == 0x0c)
		v &= ~0x0c;

	if ((v & 0x03) == 0x03)
		v &= ~0x03;

	SetPortOutput(v);
}

///////////////////////////////////////////////////////////////////////////

ATDrivingController::ATDrivingController() {
}

ATDrivingController::~ATDrivingController() {
}

void ATDrivingController::SetDigitalTrigger(uint32 trigger, bool state) {
	if (trigger == kATInputTrigger_Button0) {
		const uint32 mask = 0x100;
		uint32 bit = state ? mask : 0;

		if ((mPortBits ^ bit) & mask) {
			mPortBits ^= mask;

			SetPortOutput(mPortBits);
		}
	}
}

void ATDrivingController::ApplyImpulse(uint32 trigger, int ds) {
	switch(trigger) {
		case kATInputTrigger_Axis0:
		case kATInputTrigger_Right:
			AddDelta(ds);
			break;

		case kATInputTrigger_Left:
			AddDelta(-ds);
			break;
	}
}

void ATDrivingController::AddDelta(int delta) {
	// The driving controller does 16 clicks per rotation. We set this to
	// achieve about the same rotational speed as the paddle.
	mRawPos -= delta * 0x20000;

	static const uint8 kRotaryEncode[4] = { 0, 1, 3, 2 };

	uint32 dirBits = kRotaryEncode[mRawPos >> 30];
	uint32 change = (dirBits ^ mPortBits) & 3;
	if (change) {
		mPortBits ^= change;
		SetPortOutput(mPortBits);
	}
}

///////////////////////////////////////////////////////////////////////////

ATConsoleController::ATConsoleController(ATInputManager *im)
	: mpParent(im)
{
}

ATConsoleController::~ATConsoleController() {
}

void ATConsoleController::SetDigitalTrigger(uint32 trigger, bool state) {
	IATInputConsoleCallback *cb = mpParent->GetConsoleCallback();

	if (cb)
		cb->SetConsoleTrigger(trigger, state);
}

///////////////////////////////////////////////////////////////////////////

ATInputStateController::ATInputStateController(ATInputManager *im, uint32 flagBase)
	: mpParent(im)
	, mFlagBase(flagBase)
{
}

ATInputStateController::~ATInputStateController() {
}

void ATInputStateController::SetDigitalTrigger(uint32 trigger, bool state) {
	switch(trigger) {
		case kATInputTrigger_Flag0:
			mpParent->ActivateFlag(mFlagBase + 0, state);
			break;

		case kATInputTrigger_Flag0+1:
			mpParent->ActivateFlag(mFlagBase + 1, state);
			break;
	}
}

///////////////////////////////////////////////////////////////////////////

AT5200ControllerController::AT5200ControllerController(int index, bool trackball, ATPokeyEmulator& pokey)
	: mpPokey(&pokey)
	, mbActive(false)
	, mbPotsEnabled(false)
	, mbTrackball(trackball)
	, mIndex(index)
{
	mJitterLFSR = (index + 1) * 0x10001;

	memset(mbKeyMatrix, 0, sizeof mbKeyMatrix);

	for(int i=0; i<2; ++i) {
		mPot[i] = 114 << 16;
		mJitter[i] = 0;
	}
}

AT5200ControllerController::~AT5200ControllerController() {
}

bool AT5200ControllerController::Select5200Controller(int index, bool potsEnabled) {
	bool active = (index == mIndex);

	if (active != mbActive) {
		mbActive = active;

		if (active) {
			mpPokey->SetKeyMatrix(mbKeyMatrix);
			UpdateTopButtonState();
		} else {
			mpPokey->SetShiftKeyState(false, false);
			mpPokey->SetControlKeyState(false);
			mpPokey->SetBreakKeyState(false, false);
			mpPokey->SetKeyMatrix(nullptr);
		}

	}

	if (mbPotsEnabled != potsEnabled) {
		mbPotsEnabled = potsEnabled;

		UpdatePot(0);
		UpdatePot(1);
	}

	return active;
}

void AT5200ControllerController::SetDigitalTrigger(uint32 trigger, bool state) {
	switch(trigger) {
		case kATInputTrigger_Up:
			if (mbUp != state) {
				mbUp = state;
				mbImpulseMode = false;

				if (mbDown == state)
					SetPot(1, 114 << 16, true);
				else
					SetPot(1, state ? 1 << 16 : 227 << 16, true);
			}
			break;

		case kATInputTrigger_Down:
			if (mbDown != state) {
				mbDown = state;
				mbImpulseMode = false;

				if (mbUp == state)
					SetPot(1, 114 << 16, true);
				else
					SetPot(1, state ? 227 << 16 : 1 << 16, true);
			}
			break;

		case kATInputTrigger_Left:
			if (mbLeft != state) {
				mbLeft = state;
				mbImpulseMode = false;

				if (mbRight == state)
					SetPot(0, 114 << 16, true);
				else
					SetPot(0, state ? 1 << 16 : 227 << 16, true);
			}
			break;

		case kATInputTrigger_Right:
			if (mbRight != state) {
				mbRight = state;
				mbImpulseMode = false;

				if (mbLeft == state)
					SetPot(0, 114 << 16, true);
				else
					SetPot(0, state ? 227 << 16 : 1 << 16, true);
			}
			break;

		case kATInputTrigger_Button0:
			SetPortOutput(state ? 0x100 : 0);
			break;

		case kATInputTrigger_Button0+1:
			mbTopButton = state;
			UpdateTopButtonState();
			break;

		case kATInputTrigger_5200_0:
			SetKeyState(0x02, state);
			break;

		case kATInputTrigger_5200_1:
			SetKeyState(0x0F, state);
			break;

		case kATInputTrigger_5200_2:
			SetKeyState(0x0E, state);
			break;

		case kATInputTrigger_5200_3:
			SetKeyState(0x0D, state);
			break;

		case kATInputTrigger_5200_4:
			SetKeyState(0x0B, state);
			break;

		case kATInputTrigger_5200_5:
			SetKeyState(0x0A, state);
			break;

		case kATInputTrigger_5200_6:
			SetKeyState(0x09, state);
			break;

		case kATInputTrigger_5200_7:
			SetKeyState(0x07, state);
			break;

		case kATInputTrigger_5200_8:
			SetKeyState(0x06, state);
			break;

		case kATInputTrigger_5200_9:
			SetKeyState(0x05, state);
			break;

		case kATInputTrigger_5200_Pound:
			SetKeyState(0x01, state);
			break;

		case kATInputTrigger_5200_Star:
			SetKeyState(0x03, state);
			break;

		case kATInputTrigger_5200_Reset:
			SetKeyState(0x04, state);
			break;

		case kATInputTrigger_5200_Pause:
			SetKeyState(0x08, state);
			break;

		case kATInputTrigger_5200_Start:
			SetKeyState(0x0C, state);
			break;
	}
}

void AT5200ControllerController::ApplyImpulse(uint32 trigger, int ds) {
	switch(trigger) {
		case kATInputTrigger_Left:
			ApplyImpulse(kATInputTrigger_Axis0, -ds);
			break;

		case kATInputTrigger_Right:
			ApplyImpulse(kATInputTrigger_Axis0, ds);
			break;

		case kATInputTrigger_Up:
			ApplyImpulse(kATInputTrigger_Axis0+1, -ds);
			break;

		case kATInputTrigger_Down:
			ApplyImpulse(kATInputTrigger_Axis0+1, ds);
			break;

		case kATInputTrigger_Axis0:
		case kATInputTrigger_Axis0+1:
			{
				const int index = (int)(trigger - kATInputTrigger_Axis0);

				if (mbTrackball) {
					mbImpulseMode = true;

					mImpulseAccum[index] += (float)ds / 32768.0f;
				} else {
					SetPot(index, mPot[index] + ds * 113, false);
				}
			}
			break;
	}
}

void AT5200ControllerController::ApplyAnalogInput(uint32 trigger, int ds) {
	switch(trigger) {
		case kATInputTrigger_Left:
			ds = -ds;
			[[fallthrough]];
		case kATInputTrigger_Axis0:
		case kATInputTrigger_Right:
			mbImpulseMode = false;
			SetPot(0, ds * 113 + (114 << 16) + 0x8000, false);
			break;

		case kATInputTrigger_Up:
			ds = -ds;
			[[fallthrough]];
		case kATInputTrigger_Axis0+1:
		case kATInputTrigger_Down:
			mbImpulseMode = false;
			SetPot(1, ds * 113 + (114 << 16) + 0x8000, false);
			break;
	}
}

void AT5200ControllerController::Tick() {
	if (mbImpulseMode) {
		for(int i=0; i<2; ++i) {
			float v = mImpulseAccum[i];

			if (v < -1.0f)
				v = -1.0f;

			if (v > 1.0f)
				v = 1.0f;

			mImpulseAccum2[i] += VDRoundToInt(v * (float)(114 << 16));
			mImpulseAccum[i] = 0;

			// We have a little bit of a cheat here. 5200 games commonly truncate LSBs, which causes
			// small deltas to get lost. To work around this issue, we accumulate and only send deltas
			// once they exceed +/-4 on the POT counter.
			int idx = (mImpulseAccum2[i] + (1 << 17)) >> 18;
			mImpulseAccum2[i] -= idx << 18;

			SetPot(i, (idx * 4 + 114 + (idx < 0 ? -2 : idx > 0 ? +2 : 0)) << 16, false);
		}
	}

	static constexpr uint32 kLFSRAdvanceTab[16] = {
		0x00000000,0x14600000,0x28C00000,0x3CA00000,
		0x51800000,0x45E00000,0x79400000,0x6D200000,
		0xA3000000,0xB7600000,0x8BC00000,0x9FA00000,
		0xF2800000,0xE6E00000,0xDA400000,0xCE200000
	};

	mJitterLFSR = (mJitterLFSR >> 4) ^ kLFSRAdvanceTab[mJitterLFSR & 15];

	for(int i=0; i<2; ++i) {
		UpdatePot(i);

		if (mJitter[i])
			--mJitter[i];
	}
}

void AT5200ControllerController::OnAttach() {
	UpdatePot(0);
	UpdatePot(1);
}

void AT5200ControllerController::OnDetach() {
	ResetPotPositions();
}

void AT5200ControllerController::SetKeyState(uint8 index, bool state) {
	index += index;

	if (mbKeyMatrix[index] != state) {
		mbKeyMatrix[index] = state;
		mbKeyMatrix[index+1] = state;
		mbKeyMatrix[index+0x20] = state;
		mbKeyMatrix[index+0x21] = state;

		if (mbActive)
			mpPokey->SetKeyMatrix(mbKeyMatrix);
	}
}

void AT5200ControllerController::UpdateTopButtonState() {
	if (!mbActive)
		return;

	mpPokey->SetShiftKeyState(mbTopButton, false);
	mpPokey->SetControlKeyState(mbTopButton);
	mpPokey->SetBreakKeyState(mbTopButton, false);
}

void AT5200ControllerController::SetPot(int index, int pos, bool forceJitter) {
	// 5200 Galaxian has an awful bug in its calibration routine. It was meant to
	// use separate calibration variables for controllers 1 and 2, but due to
	// the following sequence:
	//
	//		LDX $D8
	//		AND #$01
	//		BNE $BD86
	//
	// ...it actually splits even and odd pot positions. This prevents the
	// calibration from working if the controller is only pushed to three positions
	// where center and right have different even/odd polarity. To fix this, we
	// apply a tiny jitter to the controller over a couple of frames.

	int& pot = mPot[index];
	int delta = abs(pot - pos);

	if (pot != pos) {
		pot = pos;

		// force jittering if we're getting large deltas, as this typically
		// means that we're getting digital input as analog.
		if (forceJitter || delta > (64 << 16))
			mJitter[index] = 5;

		UpdatePot(index);
	}
}

void AT5200ControllerController::UpdatePot(int index) {
	// Note that we must not return $E4, which is used by many games to detect
	// either a trackball or a disconnected controller. In particular, Vanguard
	// breaks.

	int& pot = mPot[index];

	static constexpr sint8 kJitterTable[4] = { -1, 0, 1, 0 };
	int jitter = mJitter[index] ? kJitterTable[(mJitterLFSR >> (index * 2)) & 3] : 0;

	if (mbTrackball) {
		// Pengo barfs if the trackball returns values that are more than 80
		// units from the center position.
		pot = std::clamp(pot, 38 << 16, 189 << 16);

		SetPotPosition(index != 0, mbPotsEnabled ? (pot >> 16) + jitter : 114);
	} else {
		pot = std::clamp(pot, 1 << 16, 227 << 16);

		SetPotPosition(index != 0, mbPotsEnabled ? (pot >> 16) + jitter : 228);
	}
}

//////////////////////////////////////////////////////////////////////////

ATLightPenController::ATLightPenController(Type type)
	: mType(type)
{
}

ATLightPenController::~ATLightPenController() {
}

void ATLightPenController::Init(ATScheduler *fastScheduler, ATLightPenPort *lpp) {
	mpScheduler = fastScheduler;
	mpLightPen = lpp;

	SetLightSensingEnabled(mbPenDown);
}

void ATLightPenController::SetDigitalTrigger(uint32 trigger, bool state) {
	switch(trigger) {
		case kATInputTrigger_Button0:
			if (mbTriggerPressed != state) {
				mbTriggerPressed = state;

				auto pos = GetAnticLightPos(ATLightPenNoiseMode::None);

				mpLightPen->TriggerCorrection(state, pos.x, pos.y);
			}

			UpdatePortState();
			break;

		case kATInputTrigger_Button0+2:
			if (mbPenDown != state) {
				mbPenDown = state;

				mpScheduler->UnsetEvent(mpLPDeassertEvent);

				SetLightSensingEnabled(state && !mpLightPen->GetImmediateUpdateEnabled());
			}
			break;

		default:
			return;
	}
}

void ATLightPenController::ApplyImpulse(uint32 trigger, int ds) {
	if (!ds)
		return;

	switch(trigger) {
		case kATInputTrigger_Left:
			ds = -ds;
			// fall through
		case kATInputTrigger_Axis0:
		case kATInputTrigger_Right:
			ApplyAnalogInput(kATInputTrigger_Axis0, mPosX + ds);
			break;

		case kATInputTrigger_Up:
			ds = -ds;
			// fall through
		case kATInputTrigger_Axis0+1:
		case kATInputTrigger_Down:
			ApplyAnalogInput(kATInputTrigger_Axis0+1, mPosY + ds);
			break;
	}
}

void ATLightPenController::ApplyAnalogInput(uint32 trigger, int ds) {
	// The center of the screen is at (128, 64), while the full visible area
	// is from (34, 4)-(222, 124). This gives a range of 188x120, with roughly
	// square pixels (PAL). We ignore the NTSC aspect and map a 188x188 area,
	// discarding about 36% of the vertical area to make a squarish mapping.

	switch(trigger) {
		case kATInputTrigger_Axis0:
			mPosX = ds;
			if (mPosX < -0x10000)
				mPosX = -0x10000;
			else if (mPosX > 0x10000)
				mPosX = 0x10000;
			break;

		case kATInputTrigger_Axis0+1:
			mPosY = ds;
			if (mPosY < -0xa367)
				mPosY = -0xa367;
			else if (mPosY > 0xa367)
				mPosY = 0xa367;
			break;
	}
}

void ATLightPenController::UpdatePortState() {
	uint32 newBits = 0;
	uint32 mask = 0;

	switch(mType) {
		case Type::LightPen:
			mask = 1;
			newBits = mbTriggerPressed ? 1 : 0;
			break;

		case Type::LightPenStack:
			mask = 4;
			newBits = mbTriggerPressed ? 0 : 4;
			break;

		case Type::LightGun:
			mask = 1;
			newBits = mbTriggerPressed ? 0 : 1;
			break;
	}

	if ((mPortBits ^ newBits) & mask) {
		mPortBits ^= mask;
		SetPortOutput(mPortBits);
	}
}

void ATLightPenController::SetLightSensingEnabled(bool enabled) {
	if (!mpLightPen)
		return;

	if (enabled) {
		if (mpLightSensor)
			return;

		static constexpr uint8 kWeights[3*3] {
			1, 2, 1,
			2, 4, 2,
			1, 2, 1,
		};

		mpLightSensor = mpLightPen->GetGTIA().RegisterLightSensor(3, 3, kWeights, 0.010f,
			[this](int y) {
				if (!mpLightPen->GetImmediateUpdateEnabled())
					AssertLPInput(y);
			}
		);
	} else {
		if (!mpLightSensor)
			return;

		mpLightPen->GetGTIA().UnregisterLightSensor(mpLightSensor);
		mpLightSensor = nullptr;
	}
}

void ATLightPenController::AssertLPInput(std::optional<int> forcedY) {
	auto pos = GetAnticLightPos(mpLightPen->GetNoiseMode());
	pos += mpLightPen->GetAdjust(mType != Type::LightGun);

	if (forcedY.has_value())
		pos.y = forcedY.value();

	mpLightPen->SetPosition(mType != Type::LightGun, pos.x, pos.y);

	DeassertLPInput();

	mPortBits |= 0x100;
	SetPortOutput(mPortBits);

	// Measured CX-75 light pulse width is about 32-35 color clocks.
	mpScheduler->SetEvent(17, this, 2, mpLPDeassertEvent);
}

void ATLightPenController::DeassertLPInput() {
	mpScheduler->UnsetEvent(mpLPDeassertEvent);

	if (mPortBits & 0x100) {
		mPortBits &= ~0x100;
		SetPortOutput(mPortBits);
	}
}

void ATLightPenController::GetPointers(vdfastvector<ATInputPointerInfo>& pointers) const {
	ATInputPointerInfo& pointer = pointers.emplace_back();
	pointer.mPos = GetBeamPointerPos();
	pointer.mRadius = -1.0f;
	pointer.mbPressed = mbPenDown;
	pointer.mCoordSpace = ATInputPointerCoordinateSpace::Beam;
	pointer.mbPrimary = true;
}

void ATLightPenController::Tick() {
	// X range is [17, 111].
	// Y range is [4, 123].

	if (mbPenDown) {
		if (mpLightPen->GetImmediateUpdateEnabled()) {
			// AtariGraphics needs to see the trigger down when the PENH/V
			// registers change in order to detect which port is being used
			// for the light pen.
			AssertLPInput();
			return;
		} else {
			if (mpLightSensor) {
				// This needs to use the uncorrected beam pos X because it corresponds to the
				// position that we're sensing on screen, not the position reported to ANTIC.
				// Light pen/gun delays don't matter.
				//
				// We do need to use inherent Y corrections since the light sensor will
				// unilaterally determine the PENV value by the line on which it triggers,
				// and so if we don't apply it here, it will have no effect. This means that
				// the light sensor itself is offset vertically by both the inherent and
				// explicit adjustments, but not horizontally. Barnyard Blaster, in particular,
				// expects the reported position to be noticeably lower than the scanned
				// position, which can't be explained by a larger sensor area.
				auto [x, y] = GetRawBeamPos();
				auto [x2, y2] = GetAnticLightPos(ATLightPenNoiseMode::None);

				y = y2;

				const auto [adjX, adjY] = mpLightPen->GetAdjust(mType != Type::LightGun);
				y += adjY;

				mpLightPen->GetGTIA().SetLightSensorPosition(mpLightSensor, x*2 - 1, y - 1);
			}
		}
	}

	DeassertLPInput();
}

void ATLightPenController::OnScheduledEvent(uint32 id) {
	mpLPDeassertEvent = nullptr;

	DeassertLPInput();
}

void ATLightPenController::OnAttach() {
	UpdatePortState();
}

void ATLightPenController::OnDetach() {
	if (mpScheduler) {
		mpScheduler->UnsetEvent(mpLPDeassertEvent);
	}

	SetLightSensingEnabled(false);
}

// Return the light pen's cursor position as a beam position (color clocks).
vdint2 ATLightPenController::GetRawBeamPos() const {
	const sint32 hiX = mPosX*94;
	const sint32 hiY = mPosY*188;

	return (vdint2 { hiX, hiY } + 0x808000) >> 16;
}

// Return the ANTIC light pen position as a beam position (color clocks).
vdint2 ATLightPenController::GetAnticLightPos(ATLightPenNoiseMode noiseMode) const {
	auto pos = GetRawBeamPos();

	if (mType == Type::LightGun) {
		// This is based on the average expected offset by some light gun
		// based XEGS games. There are a few sources of offsets:
		//
		//	- The internal horizontal counter latched into PENH starts at
		//	  ~7 when missile DMA occurs.
		//	- Internal delays from ANTIC and GTIA processing to video out.
		//	- Delays within TV processing.
		//	- Delays in latching H/V counters into PENH/V.
		//	- Area of pickup for the light sensor.

		pos.x += 43;
		pos.y += 3;
	} else {
		// This is based on actual measurements of a CX-75 with a 1702
		// monitor. A pixel at the center (hardware hpos/vpos=$80) gives
		// PENH=~$96-9C and PENV=$40.

		pos.x += 24;
		pos.y += 1;
	}
	
	// This is a hand-wavy algorithm to emulate some position-dependent horizontal
	// noise that appears to be related to phosphors.
	const sint32 hiY = mPosY*188;

	switch(noiseMode) {
		case ATLightPenNoiseMode::None:
			break;

		case ATLightPenNoiseMode::Low:
			if (hiY & 0x1000)
				pos.x += (hiY & 0xFFFF) / 0x8000;
			break;

		case ATLightPenNoiseMode::High:
			if (hiY & 0x1000)
				pos.x += (hiY & 0xFFFF) / 0x3333;
			break;
	}

	return pos;
}

vdfloat2 ATLightPenController::GetBeamPointerPos() const {
	const sint32 hiX = mPosX*94;
	const sint32 hiY = mPosY*188;

	float x = (float)hiX * 0x1p-16 + 128;
	float y = (float)hiY * 0x1p-16 + 128;

	return vdfloat2 { x, y };
}

//////////////////////////////////////////////////////////////////////////

ATKeypadController::ATKeypadController()
	: mPortBits(0x1F)
{
}

ATKeypadController::~ATKeypadController() {
}

void ATKeypadController::SetDigitalTrigger(uint32 trigger, bool state) {
	// DPRTA
	// 00000	-
	// 00001	+/ENTER
	// 00010	.
	// 00011	0
	// 00100	3
	// 00101	2
	// 00110	1
	// 00111	Y
	// 01000	9
	// 01001	8
	// 01010	7
	// 01011	N
	// 01100	6
	// 01101	5
	// 01110	4
	// 01111	DEL
	// 10000
	// 10001
	// 10010
	// 10011	ESC
	// 10100
	// 10101
	// 10110
	// 10111
	// 11000
	// 11001
	// 11010
	// 11011
	// 11100
	// 11101
	// 11110
	// 11111

	static const uint8 kButtonLookup[17]={
		0x06, 0x05, 0x04,	// 1 2 3
		0x0E, 0x0D, 0x0C,	// 4 5 6
		0x0A, 0x09, 0x08,	// 7 8 9
		0x03,	// 0
		0x02,	// .
		0x01,	// +/ENTER
		0x00,	// -
		0x07,	// Y
		0x0B,	// N
		0x0F,	// DEL
		0x13	// ESC
	};

	trigger -= kATInputTrigger_Button0;

	if (trigger >= 17)
		return;

	if (state) {
		uint8 code = kButtonLookup[trigger];
		mPortBits = 0x100 + (code & 0x0F);

		SetPotPosition(true, code & 0x10 ? 229 : 0);
	} else
		mPortBits &= ~0x100;

	SetPortOutput(mPortBits);
}

void ATKeypadController::OnAttach() {
}

void ATKeypadController::OnDetach() {
	ResetPotPositions();
}

//////////////////////////////////////////////////////////////////////////

ATKeyboardController::ATKeyboardController()
	: mKeyState(0)
{
}

ATKeyboardController::~ATKeyboardController() {
}

void ATKeyboardController::SetDigitalTrigger(uint32 trigger, bool state) {
	trigger -= kATInputTrigger_Button0;

	if (trigger >= 12)
		return;

	const uint32 bit = (1 << trigger);
	
	if (state)
		mKeyState |= bit;
	else
		mKeyState &= ~bit;

	UpdatePortOutput();
}

void ATKeyboardController::OnAttach() {
	// The four inputs are used as row select lines, so enable output monitoring.
	SetOutputMonitorMask(15);
}

void ATKeyboardController::OnDetach() {
	ResetPotPositions();
}

void ATKeyboardController::OnPortOutputChanged(uint8 outputState) {
	UpdatePortOutput();
}

void ATKeyboardController::UpdatePortOutput() {
	// The keyboard controller uses a keyboard matrix as follows:
	//
	// [1]-[2]-[3]------- pin 1 (up)
	//  |   |   |
	// [4]-[5]-[6]------- pin 2 (down)
	//  |   |   |
	// [7]-[8]-[9]------- pin 3 (left)
	//  |   |   |
	// [*]-[0]-[#]------- pin 4 (right)
	//  |   |   |
	//  |   |   +-------- pin 6 (trigger)
	//  |   |
	//  |   +------------ pin 9 (paddle A)
	//  |
	//  +---------------- pin 5 (paddle B)
	//
	// Thus, it is a simple row/column matrix. The switches are
	// designed to pull down the paddle or trigger lines to ground
	// (there are pull-ups on the paddle lines). There are no diodes,
	// so holding down more than one button or pulling down more than
	// one of the row lines can give phantoms.

	// Check for no rows or no columns.
	uint8 colState = 0;

	uint8 activeRows = ~mPortOutputState & 15;
	if (mKeyState && activeRows) {
		// Check for exactly one row and column.
		if (!(mKeyState & (mKeyState - 1)) && !(activeRows & (activeRows - 1))) {
			switch(activeRows) {
				case 1:
					colState = mKeyState & 7;
					break;

				case 2:
					colState = (mKeyState >> 3) & 7;
					break;

				case 4:
					colState = (mKeyState >> 6) & 7;
					break;

				case 8:
					colState = (mKeyState >> 9) & 7;
					break;
			}
		} else {
			// Ugh. Form a unified bitmask with rows and columns, then run over the
			// matrix until we have the transitive closure.
			uint8 mergedMask = activeRows << 3;

			static const uint8 kButtonConnections[12]={
				0x09, 0x0A, 0x0C,
				0x11, 0x12, 0x14,
				0x21, 0x22, 0x24,
				0x41, 0x42, 0x44
			};

			bool changed;
			do {
				changed = false;

				for(int i=0; i<12; ++i) {
					if (!(mKeyState & (1 << i)))
						continue;

					// check if some but not all lines connected by this button
					// are asserted
					const uint8 conn = kButtonConnections[i];
					if ((mergedMask & conn) && (~mergedMask & conn)) {
						// assert all lines and mark a change
						mergedMask |= conn;
						changed = true;
					}
				}
			} while(changed);

			colState = mergedMask & 7;
		}
	}

	// When buttons are depressed, the pot lines are grounded and are
	// guaranteed to never charge up to threshold. When they aren't,
	// pull-up resistors charge the caps. The value in POT0/1 that
	// results is low but non-zero. Also, the more buttons that are
	// held down, the faster the charge rate and the lower the POT
	// values, due to additional current being supplied by the PIA
	// outputs. Approximate readings in slow and fast pot modes (130XE):
	//
	//	Buttons		Slow	Fast
	//	   1		  2		 AA
	//	   2		  1		 85
	//	   3		  1		 63
	//
	// If columns are independent, they vary independently. If they
	// are bridged, then both columns get the average effect. The
	// trigger column is not involved. However, read timing does in
	// fast mode due to the caps being charged and discharged. For now, we
	// approximate it.

	uint32 column1Keys = mKeyState & 0x249;
	uint32 column2Keys = mKeyState & 0x492;
	int floorPos1 = 0x20000 - 0x5316 * std::min(2, VDCountBits(column1Keys));
	int floorPos2 = 0x20000 - 0x5316 * std::min(2, VDCountBits(column2Keys));

	// check if columns are connected
	if (column1Keys & (column2Keys >> 1)) {
		const int avg = (floorPos1 + floorPos2) >> 1;

		floorPos1 = avg;
		floorPos2 = avg;
	}

	SetPotHiPosition(false, (colState & 2) ? 255 << 16 : floorPos2, (colState & 2) != 0);
	SetPotHiPosition(true, (colState & 1) ? 255 << 16 : floorPos1, (colState & 1) != 0);
	SetPortOutput(colState & 4 ? 0x100 : 0);
}

///////////////////////////////////////////////////////////////////////////////

void ATPowerPadController::SwitchLine::Clear() {
	mSwitches[0] = 0;
	mSwitches[1] = 0;
	mSwitches[2] = 0;
	mSwitches[3] = 0;
}

void ATPowerPadController::SwitchLine::Set(int index) {
	VDASSERT((unsigned)index < 120);
	mSwitches[index >> 5] |= UINT32_C(1) << (index & 31);
}

void ATPowerPadController::SwitchLine::SetRange(int start, int end) {
	VDASSERT(start >= 0 && end <= 120 && start < end);

	const uint32 firstMask = UINT32_C(0xFFFFFFFF) << (start & 31);
	const uint32 lastMask = ~(UINT32_C(0xFFFFFFFE) << ((end - 1) & 31));
	const int firstIndex = start >> 5;
	const int lastIndex = (end - 1) >> 5;

	if (firstIndex == lastIndex) {
		mSwitches[firstIndex] |= (firstMask & lastMask);
	} else {
		mSwitches[firstIndex] |= firstMask;

		for(int i = firstIndex + 1; i < lastIndex; ++i)
			mSwitches[i] = ~UINT32_C(0);

		mSwitches[lastIndex] |= lastMask;
	}
}

bool ATPowerPadController::SwitchLine::Any() const {
	return (mSwitches[0] | mSwitches[1] | mSwitches[2] | mSwitches[3]) != 0;
}

int ATPowerPadController::SwitchLine::FindNext(int start) const {
	VDASSERT((unsigned)start < 120);

	int wordIndex = start >> 5;
	uint32 mask = UINT32_C(0xFFFFFFFF) << (start & 31);

	while(wordIndex < 4) {
		uint32 v = mSwitches[wordIndex] & mask;

		if (v) {
			return VDFindLowestSetBitFast(v) + (wordIndex << 5);
		}

		++wordIndex;
		mask = UINT32_C(0xFFFFFFFF);
	}

	return -1;
}

ATPowerPadController::ATPowerPadController() = default;
ATPowerPadController::~ATPowerPadController() = default;

void ATPowerPadController::ColdReset() {
	mbSense = false;
	mScanX = 0;
	mScanY = 0;

	// we don't reset the matrix it is a property of the inputs and not device state
}

void ATPowerPadController::SetDigitalTrigger(uint32 trigger, bool state) {
	switch(trigger) {
		case kATInputTrigger_Button0:
		case kATInputTrigger_Button0+1:
		case kATInputTrigger_Button0+2:
		case kATInputTrigger_Button0+3:
			{
				const int index = trigger - kATInputTrigger_Button0;
				auto& touch = mTouches[index];

				if (state)
					mTouchActiveMask |= 1 << index;
				else
					mTouchActiveMask &= ~(1 << index);

				if (mbSetShift) {
					CopyTouch(index, 0);
				} else {
					if (touch.mbActive != state) {
						touch.mbActive = state;

						InvalidateMatrix();
					}
				}
			}
			break;

		case kATInputTrigger_Button0+4:
			if (mbSetShift != state) {
				mbSetShift = state;

				// If some of the touches are already on when the shift state is changed,
				// we need to correct state: touches can only be activated in unshifted
				// state, and touches should align to touch 0 in shifted state.
				if (state) {
					for(int i=1; i<4; ++i) {
						if (mTouches[i].mbActive) {
							mTouches[i].mbActive = false;
							CopyTouch(i, 0);
							InvalidateMatrix();
						}
					}
				} else {
					for(int i=1; i<4; ++i) {
						if ((mTouchActiveMask & (1 << i)) && !mTouches[i].mbActive) {
							mTouches[i].mbActive = true;
							InvalidateMatrix();
						}
					}
				}
			}
			break;

		default:
			break;
	}
}

void ATPowerPadController::ApplyAnalogInput(uint32 trigger, int ds) {
	switch(trigger) {
		case kATInputTrigger_Axis0:
			SetPos(0, ds);
			break;

		case kATInputTrigger_Axis0+1:
			SetPos(1, ds);
			break;

		case kATInputTrigger_Axis0+2:
			SetPos(2, ds);
			break;

		default:
			break;
	}
}

void ATPowerPadController::ApplyImpulse(uint32 trigger, int ds) {
	switch(trigger) {
		case kATInputTrigger_Axis0:
		case kATInputTrigger_Right:
			AddDelta(0, ds);
			break;
		case kATInputTrigger_Left:
			AddDelta(0, -ds);
			break;

		case kATInputTrigger_Axis0+1:
		case kATInputTrigger_Down:
			AddDelta(1, ds);
			break;
		case kATInputTrigger_Up:
			AddDelta(1, -ds);
			break;

		case kATInputTrigger_Axis0+2:
		case kATInputTrigger_ScrollDown:
			AddDelta(2, ds);
			break;
		case kATInputTrigger_ScrollUp:
			AddDelta(2, -ds);
			break;

		default:
			break;
	}
}

void ATPowerPadController::GetPointers(vdfastvector<ATInputPointerInfo>& pointers) const {
	bool primary = true;

	for(auto& touch : mTouches) {
		if (primary || touch.mbActive) {
			auto& pointer = pointers.emplace_back();

			constexpr float switchToNormalizedScale = 1.0f / 60.0f;
			pointer.mPos = touch.mPos * vdfloat2 { -switchToNormalizedScale, switchToNormalizedScale } + vdfloat2{1.0f, -1.0f};
			pointer.mRadius = touch.mRadius * switchToNormalizedScale;
			pointer.mbPressed = touch.mbActive;
			pointer.mCoordSpace = ATInputPointerCoordinateSpace::Normalized;
			pointer.mbPrimary = primary;
		}

		primary = false;
	}
}

void ATPowerPadController::OnAttach() {
	// PA1 and PA2 are computer outputs.
	SetOutputMonitorMask(0x06);

	// Reset touch sizes.
	//
	// The switch grid is 120x120 over 12"x12", giving 10 switches per inch on each axis.
	// A fingerpress is about half an inch wide, so we render a disc with a radius of
	// 2.5 switches.
	for(auto& touch : mTouches)
		touch.mRadius = 2.5f;
}

void ATPowerPadController::OnPortOutputChanged(uint8 outputState) {
	const uint8 triggered = ~outputState & mPrevPortState;
	mPrevPortState = outputState;

	// PA1 is the clear signal, which restarts the scan.
	// PA2 is the clock, which advances the shift register.
	//
	// If both the clock and clear are triggered, we shift first under the assumption
	// that the scan can't occur immediately, so the shift would take place first.
	if (triggered & 4)
		mShiftRegister >>= 1;

	if (triggered & 2) {
		if (!mbMatrixValid)
			UpdateMatrix();

		// Find next position. There will always be one as (0,0) is always set.
		for(;;) {
			mScanY = mColumnArray[mScanX].FindNext(mScanY);

			if (mScanY >= 0)
				break;

			mScanY = 0;

			if (++mScanX >= 120)
				mScanX = 0;
			else {
				mScanX = mRowArray.FindNext(mScanX);
				if (mScanX < 0)
					mScanX = 0;
			}
		}

		// Assert sense signal and update shift register with found point. The serial
		// data is '01' followed by 7-bit Y and 7-bit X LSB-first, then infinite zeroes.
		// The data line is inverted, but so is our direction input, so it works out.
		mbSense = true;
		mShiftRegister = 2 + ((uint32)mScanY << 2) + ((uint32)mScanX << 9);

		// bump to next position
		if (++mScanY >= 120) {
			mScanY = 0;

			if (++mScanX >= 120)
				mScanX = 0;
		}
	}

	UpdatePortOutput();
}

void ATPowerPadController::AddDelta(int axis, sint32 delta) {
	SetPos(axis, mAxis[axis] + std::clamp(delta, -0x10000, 0x10000));
}

void ATPowerPadController::SetPos(int axis, sint32 pos) {
	pos = std::clamp(pos, -0x10000, 0x10000);

	if (mAxis[axis] == pos)
		return;

	mAxis[axis] = pos;

	// Convert generic pad position to PowerPad position.
	//
	// The switch grid is 120x120 over 12"x12", giving 10 switches per inch on each axis.
	// Note that (0,0) is the upper _right_ of the pad.

	switch(axis) {
		case 0:
			mTouches[0].mPos.x = (0x1p16f - (float)mAxis[0]) * (119.0f * 0x1p-17f);
			break;

		case 1:
			mTouches[0].mPos.y = ((float)mAxis[1] + 0x1p16f) * (119.0f * 0x1p-17f);
			break;

		case 2:
			mTouches[0].mRadius = 2.5f * (mAxis[2] + 0x1p16f) * 0x1p-16f;
			break;
	}

	if (mTouches[0].mbActive)
		InvalidateMatrix();
}

void ATPowerPadController::CopyTouch(int dst, int src) {
	if (src == dst)
		return;

	auto& dstTouch = mTouches[dst];
	const auto& srcTouch = mTouches[src];

	dstTouch.mPos = srcTouch.mPos;
	dstTouch.mRadius = srcTouch.mRadius;

	if (dstTouch.mbActive)
		InvalidateMatrix();
}

void ATPowerPadController::InvalidateMatrix() {
	mbMatrixValid = false;
}

void ATPowerPadController::UpdateMatrix() {
	// clear the switch matrix
	for(SwitchLine& sl : mColumnArray)
		sl.Clear();

	mRowArray.Clear();

	// (0,0) is always returned as set
	mColumnArray[0].Set(0);
	mRowArray.Set(0);

	// Render touches.
	for(const auto& touch : mTouches) {
		if (!touch.mbActive)
			continue;

		const float cx = touch.mPos.x;
		const float cy = touch.mPos.y;
		const float r = touch.mRadius;
		const float r2 = r*r;

		const int ix0 = std::max<int>(  0, VDCeilToInt(cx - r));
		const int ix1 = std::min<int>(120, VDCeilToInt(cx + r));

		for(int ix = ix0; ix < ix1; ++ix) {
			float dx = (float)ix - cx;
			const float desc = r2 - dx*dx;

			if (desc > 0) {
				float dy = sqrtf(desc);
				const int iy0 = std::max(  0, VDCeilToInt(cy - dy));
				const int iy1 = std::min(120, VDCeilToInt(cy + dy));

				if (iy0 < iy1) {
					mColumnArray[ix].SetRange(iy0, iy1);
					mRowArray.Set(ix);
				}
			}
		}
	}

	mbMatrixValid = true;
}

void ATPowerPadController::UpdatePortOutput() {
	// PA0 is data (pre-inverted).
	// PA3 is sense (active low).
	//
	// Note that the input to SetPortOutput() is _also_ inverted.

	SetPortOutput((mShiftRegister & 1) + (mbSense ? 0x08 : 0x00));
}
