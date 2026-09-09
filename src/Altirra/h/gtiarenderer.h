//	Altirra - Atari 800/800XL emulator
//	Copyright (C) 2008-2009 Avery Lee
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

#ifndef f_AT_GTIARENDER_H
#define f_AT_GTIARENDER_H

#include <vd2/system/memory.h>
#include <vd2/system/vdstl.h>
#include <vd2/system/vdstl_vectorview.h>

class ATSaveStateReader;
class IATSerializable;
enum class ATGTIADefectMode : uint8;

namespace ATGTIA {
	const uint8 PF0		= 0x01;
	const uint8 PF1		= 0x02;
	const uint8 PF01	= 0x03;
	const uint8 PF2		= 0x04;
	const uint8 PF3		= 0x08;
	const uint8 PF23	= 0x0c;
	const uint8 PF		= 0x0f;
	const uint8 P0		= 0x10;
	const uint8 P1		= 0x20;
	const uint8 P01		= 0x30;
	const uint8 P2		= 0x40;
	const uint8 P3		= 0x80;
	const uint8 P23		= 0xc0;
	const uint8 P0123	= 0xf0;

	enum : uint8 {
		kColorP0		= 0,
		kColorP1,
		kColorP2,
		kColorP3,
		kColorPF0,
		kColorPF1,
		kColorPF2,
		kColorPF3,
		kColorBAK,
		kColorBlack,
		kColorP0P1,
		kColorP2P3,
		kColorPF0P0,
		kColorPF0P1,
		kColorPF0P0P1,
		kColorPF1P0,
		kColorPF1P1,
		kColorPF1P0P1,
		kColorPF2P2,
		kColorPF2P3,
		kColorPF2P2P3,
		kColorPF3P2,
		kColorPF3P3,
		kColorPF3P2P3
	};

	static constexpr size_t kColorCount = kColorPF3P2P3 + 1;
};

struct ATGTIAColorRegisters {
	uint8 mCOLPM[4];
	uint8 mCOLPF[4];
	uint8 mCOLBK;
};

class ATGTIARenderer : public VDAlignedObject<16> {
	ATGTIARenderer(const ATGTIARenderer&);
	ATGTIARenderer& operator=(const ATGTIARenderer&);
public:
	ATGTIARenderer();
	~ATGTIARenderer();

	void SetAnalysisMode(bool analysisMode);
	void SetVBlank(bool vblank);
	void SetCTIAMode();
	void SetSECAMMode(bool secam) { mbSECAMMode = secam; }
	void SetDefectMode(ATGTIADefectMode defectMode) { mDefectMode = defectMode; }

	void ColdReset();

	void BeginScanline(uint8 *dst, const uint8 *mergeBuffer, const uint8 *anticBuffer, bool hires);
	void RenderScanline(int x2, bool pfgraphics, bool pmgraphics, bool mixed);
	uint32 SenseScanline(int hpx1, int hpx2, const uint8 *weights) const;
	void EndScanline();

	void AddRegisterChange(uint8 pos, uint8 addr, uint8 value);
	void SetRegisterImmediate(uint8 addr, uint8 value);

	void LoadState(ATSaveStateReader& reader);
	void ResetState();

	void SaveState(IATSerializable **ser) const;
	void LoadState(IATSerializable *ser);

protected:
	struct RegisterChange {
		uint8 mPos;
		uint8 mReg;
		uint8 mValue;
		uint8 mPad;
	};

	template<class T> void ExchangeState(T& io);
	void UpdateRegisters(const RegisterChange *changes, int count);
	void RenderBlank(int x1);
	void RenderLores(int x1, int x2);
	void RenderLoresFast(int x1, int x2);
	void RenderMode8(int x1, int x2);
	void RenderMode8Fast(int x1, int x2);
	void RenderMode9DefectType1(int x1, int x2);
	void RenderMode9DefectType2(int x1, int x2);
	void RenderMode9(int x1, int x2);
	void RenderMode9Fast(int x1, int x2);
	void RenderMode9Transition1(int x1);
	void RenderMode9Transition2(int x1);

	static const uint8 kMode10Lookup[16];
	static const uint8 kMode10PFMask[16];

	void RenderMode10DefectType1(int x1, int x2);
	void RenderMode10DefectType2(int x1, int x2);
	void RenderMode10(int x1, int x2);
	void RenderMode10Fast(int x1, int x2);
	void RenderMode10Transition2(int x1);
	void RenderMode10Transition3(int x1);

	static const uint8 kMode11Lookup[16][2][2];

	void RenderMode11DefectType1(int x1, int x2);
	void RenderMode11DefectType2(int x1, int x2);
	void RenderMode11(int x1, int x2);
	void RenderMode11Fast(int x1, int x2);
	void UpdatePriorityTable();

	const uint8 *mpMergeBuffer;
	const uint8 *mpAnticBuffer;

	uint8 *mpDst;
	int mX;
	int mRCIndex;
	int mRCCount;

	ATGTIADefectMode mDefectMode {};
	bool mbHiresMode;
	bool mbGTIAEnableTransition;
	bool mbGTIATransitionFromHiresMode;
	uint8 mTransitionPhase;
	bool mbVBlank;
	bool mbSECAMMode;
	bool mbDefectType2Active;
	uint8 mPRIOR;

	const uint8 *mpPriTable;
	const uint8 *mpColorTable;

	typedef vdfastvector<RegisterChange> RegisterChanges;
	RegisterChanges mRegisterChanges;

	VDALIGN(16) uint8	mColorTable[24];
	uint8	mPriorityTables[32][256];
};

#endif	// f_AT_GTIARENDER_H
