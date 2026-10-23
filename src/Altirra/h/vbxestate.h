//	Altirra - Atari 800/800XL/5200 emulator
//	Copyright (C) 2009-2023 Avery Lee
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

#ifndef f_AT_VBXESTATE_H
#define f_AT_VBXESTATE_H

#include <at/atcore/snapshotimpl.h>

class ATSaveStateVbxe final : public ATSnapExchangeObject<ATSaveStateVbxe, "ATSaveStateVbxe"> {
public:
	template<ATExchanger T>
	void Exchange(T& ex);

	vdrefptr<ATSaveStateMemoryBuffer> mpMemory;

	// architectural write registers
	// MERGE NOTE: retain the test24 fork presence field for snapshot compatibility.
	bool mbFeatureControlPresent = false;
	uint8 mVideoControl = 0;			// $Dx40 VIDEO_CONTROL
	uint32 mXdlAddr = 0;				// $Dx41-3 XDL_ADR0-2
	uint8 mCsel = 0;					// $Dx44 CSEL
	uint8 mPsel = 0;					// $Dx45 PSEL
	uint8 mColMask = 0;					// $Dx49 COLMASK
	uint8 mFeatureControl = 0;			// $Dx4B FEATURE_CONTROL (FX 1.27+)
	uint32 mBlitListAddr = 0;			// $Dx50-2 BL_ADR0-2
	uint8 mIrqControl = 0;				// $Dx54 IRQ_CONTROL
	uint8 mPri[4] {};					// $Dx55-58 P0-3
	uint8 mMemacBControl = 0;			// $Dx5D MEMAC_B_CONTROL
	uint8 mMemacControl = 0;			// $Dx5E MEMAC_CONTROL
	uint8 mMemacBankSel = 0;			// $Dx5F MEMAC_BANK_SEL

	// architectural read registers
	uint8 mArchColDetect = 0;			// $Dx4A COLDETECT
	uint8 mArchBltCollisionCode = 0;	// $Dx50 BLT_COLLISION_CODE
	uint8 mArchBlitterBusy = 0;			// $Dx53 BLITTER_BUSY
	uint8 mArchIrqStatus = 0;			// $Dx54 IRQ_STATUS

	// internal state
	uint32 mXdlAddrActive = 0;
	uint8 mXdlRepeat = 0;				// XDLC_RPTL
	uint8 mIntPfPalette = 0;
	uint8 mIntOvPalette = 0;
	uint32 mIntOvAddr = 0;
	uint32 mIntOvStep = 0;
	uint8 mIntOvMode = 0;				// XDLC_TMON, XDLC_GMON, XDLC_HR, and XDLC_LR bits
	uint8 mIntOvWidth = 0;
	uint8 mIntOvHscroll = 0;
	uint8 mIntOvVscroll = 0;
	uint8 mIntOvTextRow = 0;
	uint8 mIntOvChbase = 0;
	uint8 mIntOvPriority = 0;
	bool mbIntMapEnabled = false;
	uint32 mIntMapAddr = 0;
	uint32 mIntMapStep = 0;
	uint8 mIntMapHscroll = 0;
	uint8 mIntMapVscroll = 0;
	uint8 mIntMapWidth = 0;
	uint8 mIntMapHeight = 0;
	uint8 mIntMapRow = 0;

	uint32 mIntBlitListAddr = 0;			// next blit control block fetch address
	uint32 mIntBlitCyclesPending = 0;		// number of cycles blitter is behind from current cycle
	uint16 mIntBlitHeightLeft = 0;			// number of rows remaining for blitter to run, or 0 if next blit needs to be read in
	uint8 mIntBlitYZoomCounter = 0;			// current sub-row counter, which counts from 0 up to Y zoom
	uint16 mIntBlitStopDelay = 0;			// if near the end of the blit, the number of cycles until the blit stops

	uint8 mIntBlitState[21] {};				// current blit state, in the form of the blit control block
	bool mbIntBlitRayCast = false;
	uint32 mIntBlitRayCastSrcX = 0;
	uint32 mIntBlitRayCastSrcY = 0;
	uint32 mIntBlitRayCastSrcStepX = 0;

	bool mbIntBlitCollisionLatched = false;	// true if blitter has encountered a collision

	// MERGE NOTE: FX1.27 can suspend between BCB containers or texture pixels.
	bool mbIntBlitContainersPending = false;
	bool mbIntBlitRaycast = false;
	uint32 mIntBlitRaycastCoords[6] {}; // X,Y,initial X,Y,step X,Y
	uint8 mIntBlitRaycastTextureBits[2] {};

	uint8 mGtiaPal[9] {};
	uint8 mGtiaPrior {};

	vdfastvector<uint8> mGtiaRegisterChanges;

	uint32 mPalette[1024] {};
};

#endif
