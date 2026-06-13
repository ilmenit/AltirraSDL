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
	void ColdReset() override;

	void OnCreatedGraphicalOutput() override;
	ATPrinterGraphicsSpec GetGraphicsSpec() const override;
	bool IsSupportedDeviceId(uint8 id) const override;
	bool IsSupportedOrientation(uint8 aux1) const override;
	uint8 GetWidthForOrientation(uint8 aux1) const override;
	void GetStatusFrameInternal(uint8 frame[4]) override;
	void HandleFrameInternal(uint8 orientation, uint8 *buf, uint32 len, bool graphics) override;

private:
	void ResetState();
	void PrintChar(uint8 ch, bool graphical);
	bool MoveToAbsolute(sint32 x, sint32 y);
	bool DrawToAbsolute(sint32 x, sint32 y);
	void DrawClippedLine(const vdfloat2& raw1, const vdfloat2& raw2);
	vdfloat2 ConvertPointToMM(sint32 x, sint32 y) const;
	vdfloat2 ConvertPointFToMM(float x, float y) const;
	vdfloat2 ConvertVectorToMM(sint32 x, sint32 y) const;
	vdfloat2 ConvertVectorFToMM(float x, float y) const;

	void ReconvertPens();

	// 0.200mm/step per VIC-1520 manual and CGP-115 service manual
	static constexpr float kUnitsToMM = 0.20f;

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

	sint32 mPenColors[4] {};
	uint32 mPrintPenColors[4] {};
};

#endif
