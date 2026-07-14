//	Altirra - Atari 800/800XL emulator
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

#ifndef f_AT_PRINTER1020_H
#define f_AT_PRINTER1020_H

#include "printer.h"

class ATDevicePrinter1020 final : public ATDevicePrinterBase {
public:
	ATDevicePrinter1020();
	~ATDevicePrinter1020();

	void GetDeviceInfo(ATDeviceInfo& info) override;
	void GetSettings(ATPropertySet& settings) override;
	bool SetSettings(const ATPropertySet& settings) override;
	void Shutdown() override;
	void ColdReset() override;

	void OnCreatedGraphicalOutput() override;
	ATPrinterGraphicsSpec GetGraphicsSpec() const override;
	bool IsSupportedDeviceId(uint8 id) const override;
	bool IsSupportedOrientation(uint8 aux1) const override;
	uint8 GetWidthForOrientation(uint8 aux1) const override;
	void GetStatusFrameInternal(uint8 frame[4]) override;
	void HandleFrameInternal(uint8 orientation, uint8 *buf, uint32 len, bool graphics) override;

public:
	void OnScheduledEvent(uint32 id) override;

private:
	void ResetState();
	void ResetDrawingEngine();
	void ReturnToTextMode();
	void PrintChar(uint8 ch, bool graphical);
	void MoveToAbsolute(sint32 x, sint32 y);
	void DrawToAbsolute(sint32 x, sint32 y, bool forceSolid);
	void DrawClippedLine(const vddouble2& raw1, const vddouble2& raw2);
	vddouble2 ConvertPointToMM(sint32 x, sint32 y) const;
	vddouble2 ConvertPointFToMM(double x, double y) const;
	vddouble2 ConvertVectorToMM(sint32 x, sint32 y) const;
	vddouble2 ConvertVectorFToMM(double x, double y) const;

	void ReconvertPens();

	enum class DrawingActionType : uint8 {
		Wait,
		Move,
		Draw,
		PenDown,
		PenUp,
		PenChange,
		DrainFIFO
	};

	struct DrawingAction {
		DrawingActionType mType {};
		uint8 mPenIndex = 0;
		uint32 mLength = 0;
		vddouble2 mDstPoint { 0, 0 };
	};

	// How many characters to allow buffering before a Write command must be
	// stalled due to the FIFO between the serial MCUs and print MCUs becoming
	// full. The FIFO is actually 8 characters but print MCU signals early to
	// make sure to avoid overflows. Note that only characters passed to the
	// print MCU matter, not characters processed directly by the serial MCU.
	static constexpr uint32 kFIFOThreshold = 5;

	void FillFIFO(uint32 numChars);
	void DrainFIFO(uint32 numChars);

	void AddDrainFIFOAction(uint32 numChars);
	void AddWaitAction(uint32 cycles);
	void AddMoveAction(const vddouble2& rawPos);
	void AddDrawAction(const vddouble2& rawPos);
	void AddPenDownAction();
	void AddPenUpAction();
	void AddPenChangeAction(uint32 penIndex);
	DrawingAction& AddAction();
	void ProcessNextDrawingAction();

	static constexpr auto kEventId_DrawingAction = kEventId_SubclassFirst;

	// The 1020 steps about 3.75" from margin to margin. This is very close
	// to the value of 0.200 mm/step given by the VIC-1520 manual and CGP-115
	// service manual.
	template<typename T>
	static constexpr T kUnitsToMM = T(0.20);

	// 1020 paper width is 4.5" (confirmed by physical device and CGP-115
	// manual)
	template<typename T>
	static constexpr T kPaperWidthMM = T(4.5 * 25.4);

	template<typename T>
	static constexpr T kMarginMM = T((kPaperWidthMM<T> - kUnitsToMM<T>*480) * 0.5);

	enum class LineState : uint8 {
		StartOfLine,
		Escape,
		ProcessLine,
	} mLineState = LineState::StartOfLine;

	enum class CommandState : uint8 {
		WaitForCommand,
		PrintText,
		PrintTextEscape,
		ArgStart,
		ArgStart2,
		ArgDigit,
		ArgEnd
	} mCommandState = CommandState::WaitForCommand;

	bool mbGraphicsMode = false;
	uint8 mGraphicsCommand = 0;
	sint32 mBaseX = 0;
	sint32 mX = 0;
	sint32 mY = 0;
	uint32 mPenIndex = 0;
	uint8 mLineStyle = 0;
	uint8 mCharSize = 0;
	uint8 mCharRotation = 0;
	int mArgSign = 0;
	sint32 mArg1 = 0;
	sint32 mArg2 = 0;
	sint32 mArg3 = 0;
	uint8 mArgsLeft = 0;
	uint8 mArgCount = 0;
	bool mbIntCharsEnabled = false;
	bool mbAccurateBresenham = false;

	uint32 mFIFOLevel = 0;

	// Number of FIFO characters being tracked for the current command.
	uint32 mPendingCmdFIFOChars = 0;

	sint32 mPenColors[4] {};
	uint32 mPrintPenColors[4] {};

	ATEvent *mpEventDrawingAction = nullptr;
	vdvector<DrawingAction> mDrawingActions;
	uint32 mNextDrawingActionIndex = 0;

	bool mbDrawingActionPending = false;
	double mLastDrawStartPosX = 0;
	double mLastDrawPosX = 0;
	vddouble2 mDrawMoveVec { 0.0f, 0.0f };
	sint32 mDrawRemainingCycles = 0;
	sint32 mDrawTotalCycles = 0;
	sint32 mDrawCurrentSpanCycles = 0;
	float mLastHeadDirection = 0;
};

#endif
