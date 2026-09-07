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
#include <vd2/system/vdstl_algorithm.h>
#include <at/atcore/propertyset.h>
#include "printerfx80.h"
#include "printerfontfx80.h"

void ATCreateDevicePrinterFX80(const ATPropertySet& pset, IATDevice **dev) {
	vdrefptr<ATDevicePrinterFX80> p(new ATDevicePrinterFX80);

	*dev = p.release();
}

extern const ATDeviceDefinition g_ATDeviceDefPrinterFX80 = { "fx80", "fx80", L"Epson FX-80/FX-80+ 80-Column Printer", ATCreateDevicePrinterFX80, 0, "parallel" };

constinit const uint16 ATDevicePrinterFX80::kIntlToUnicodeTable[32] {
0x00E0,		// 00 Latin small letter A with grave
0x00E8,		// 01 Latin small letter E with grave
0x00F9,		// 02 Latin small letter U with grave
0x00F2,		// 03 Latin small letter O with grave
0x00EC,		// 04 Latin small letter I with grave
0x00B0,		// 05 Degree sign
0x00A3,		// 06 Pound sign
0x00A1,		// 07 Inverted exclamation mark
0x00BF,		// 08 Inverted question mark
0x00D1,		// 09 Latin capital letter N with tilde
0x00F1,		// 0A Latin small letter N with tilde
0x00A4,		// 0B Currency sign
0x20A7,		// 0C Peseta sign
0x00C5,		// 0D Latin capital letter A with ring above
0x0227,		// 0E Latin small letter A with dot above
0x00E7,		// 0F Latin small letter C with cedilla
0x00A7,		// 10 Section sign
0x00DF,		// 11 Latin small letter sharp S
0x00C6,		// 12 Latin capital letter Ae
0x00E6,		// 13 Latin small letter Ae
0x00D8,		// 14 Latin capital letter O with stroke
0x00F8,		// 15 Latin small letter O with stroke
0x00A8,		// 16 Diaeresis
0x00C4,		// 17 Latin capital letter A with diaeresis
0x00D6,		// 18 Latin capital letter O with diaeresis
0x00DC,		// 19 Latin capital letter U with diaeresis
0x00E4,		// 1A Latin small letter A with diaeresis
0x00F6,		// 1B Latin small letter O with diaeresis
0x00FC,		// 1C Latin small letter U with diaeresis
0x00C9,		// 1D Latin capital letter E with acute
0x00E9,		// 1E Latin small letter E with acute
0x00A5,		// 1F Yen sign
};

ATDevicePrinterFX80::ATDevicePrinterFX80() {
	SetSaveStateAgnostic();
}

ATDevicePrinterFX80::~ATDevicePrinterFX80() {
}

void ATDevicePrinterFX80::GetDeviceInfo(ATDeviceInfo& info) {
	info.mpDef = &g_ATDeviceDefPrinterFX80;
}

void ATDevicePrinterFX80::Init() {
	RecreateGraphicsOutput();
}

void ATDevicePrinterFX80::RecreateGraphicsOutput() {
	mpGraphicsOutput = nullptr;

	ATPrinterGraphicsSpec spec {};
	spec.mPageWidthMM = 215.9f;				// 8.5" wide paper
	spec.mPageHeightMM = mbPaperHeightSetting ? 25.4f * 12.0f : 25.4f * 11.0f;

	// The default vertical border isn't symmetric with the horizontal border
	// because the printer has an option for a 1" perforation skip, so we
	// should default to half an inch.
	spec.mPageVBorderMM = 0.5f * 25.4f;
	spec.mLeftMarginMM = kPaperLeftMarginMM<float>;
	spec.mDotRadiusMM = 0.22f;				// guess for dot radius
	spec.mVerticalDotPitchMM = 25.4f / 72.0f;	// 1/72"
	spec.mbBit0Top = false;
	spec.mNumPins = 9;
	spec.mBaselinePin = 2;
	mpGraphicsOutput = GetService<IATPrinterOutputManager>()->CreatePrinterGraphicalOutput(g_ATDeviceDefPrinterFX80.mpName, spec);
	IATPrinterGraphicalOutput::CharColumn charColumns[24];

	for(int uniCharSet = 0; uniCharSet < +UniCharSetIndex::Count; ++uniCharSet) {
		double pitch = 0;
		double advance = 0;
		bool prop = false;
		bool emphasized = false;

		switch(uniCharSet % +UniCharSetIndex::PitchCount) {
			case +UniCharSetIndex::Normal:
				pitch = kMMPerHorizUnit<double> * 6.0;
				advance = kMMPerHorizUnit<double> * kWidthUnitsPerCharPica;
				break;

			case +UniCharSetIndex::Proportional:
				// MERGE NOTE: test19 never enabled proportional metrics.
				prop = true;
				[[fallthrough]];
			case +UniCharSetIndex::Emphasized:
				pitch = kMMPerHorizUnit<double> * 6.0;
				advance = kMMPerHorizUnit<double> * kWidthUnitsPerCharPica;
				emphasized = true;
				break;

			case +UniCharSetIndex::Elite:
				pitch = kMMPerHorizUnit<double> * 4.0;
				advance = kMMPerHorizUnit<double> * kWidthUnitsPerCharElite;
				break;

			case +UniCharSetIndex::Compressed:
				pitch = kMMPerHorizUnit<double> * 3.0;
				advance = kMMPerHorizUnit<double> * kWidthUnitsPerCharCompressed;
				break;

			case +UniCharSetIndex::EliteCompressed:
				pitch = kMMPerHorizUnit<double> * 3.0;
				advance = kMMPerHorizUnit<double> * kWidthUnitsPerCharCompressedElite;
				break;
		}

		bool expanded = false;
		if (uniCharSet >= +UniCharSetIndex::Expanded) {
			expanded = true;
			pitch *= 2;
			advance *= 2;
		}

		for(int ch = 0; ch < 256; ++ch) {
			uint32 uniChar = ch & 127;

			if (uniChar < 32)
				uniChar = kIntlToUnicodeTable[uniChar];
			else if (uniChar == 127)
				uniChar = 0x0030;

			int startCol = g_ATPrinterFontFX80.mPropStartStop[ch][0];
			int stopCol = g_ATPrinterFontFX80.mPropStartStop[ch][1];
			const auto& dotColumns = g_ATPrinterFontFX80.mFont[ch];
			int numCharCols = 0;
			// Keep dot positions identical to the raw-dot rendering path.
			int xOffset = 0;
			if (!prop) {
				startCol = 0;
				stopCol = 11;
			}
			uint16 prevDots = 0;

			for(int col = startCol; col <= stopCol; ++col) {
				uint16 dots = dotColumns[col];

				if (expanded || emphasized) {
					uint16 newDots = dots | prevDots;
					prevDots = dots;
					dots = newDots;
				}

				if (dots) {
					double x = pitch * (col - xOffset);
					charColumns[numCharCols].mXOffset = (float)x;
					charColumns[numCharCols].mDots = dots;
					++numCharCols;

					if (emphasized) {
						charColumns[numCharCols].mXOffset = (float)(x + pitch * 0.5);
						charColumns[numCharCols].mDots = dots;
						++numCharCols;
					}
				}
			}

			mpGraphicsOutput->DefineChar(
				prop ? (advance / 12.0) * (stopCol + 1 - startCol) : advance,
				vdspan(charColumns, numCharCols),
				uniChar
			);
		}
	}
}

void ATDevicePrinterFX80::Shutdown() {
	mpGraphicsOutput = nullptr;
}

void ATDevicePrinterFX80::GetSettings(ATPropertySet& settings) {
	settings.Clear();

	if (mbAutoLF)
		settings.SetBool("auto_lf", true);

	if (mbSlashedZero)
		settings.SetBool("slashed_zero", true);

	if (mbCompressedSetting)
		settings.SetBool("compressed_on", true);

	if (mbEmphasizeSetting)
		settings.SetBool("emphasize_on", true);

	if (mbSkipPerforationSetting)
		settings.SetBool("skip_on", true);

	if (mIntlModeSetting != 0)
		settings.SetInt32("intl_mode", mIntlModeSetting);

	if (mbPaperHeightSetting)
		settings.SetBool("paper_ht12", true);
}

bool ATDevicePrinterFX80::SetSettings(const ATPropertySet& settings) {
	mbAutoLF = settings.GetBool("auto_lf", false);
	mbSlashedZero = settings.GetBool("slashed_zero", false);
	mbSkipPerforationSetting = settings.GetBool("skip_on", false);
	mbCompressedSetting = settings.GetBool("compressed_on", false);
	mbEmphasizeSetting = settings.GetBool("emphasize_on", false);
	mIntlModeSetting = settings.GetInt32("intl_mode", 0) & 7;

	// If the paper height setting changes, recreate the graphics output.
	const bool paperHeightSetting = settings.GetBool("paper_ht12", false);

	if (mbPaperHeightSetting != paperHeightSetting) {
		mbPaperHeightSetting = paperHeightSetting;

		if (mpGraphicsOutput)
			RecreateGraphicsOutput();
	}

	return true;
}

void ATDevicePrinterFX80::ColdReset() {
	ResetState();
}

bool ATDevicePrinterFX80::WantUnicode() const {
	return false;
}

void ATDevicePrinterFX80::WriteRaw(const uint8 *buf, size_t len) {
	while(len--) {
		uint8 ch = *buf++;

		switch(mState) {
			case State::None:
				ProcessChar(ch);
				break;

			case State::Graphics:
				ProcessGraphics(ch);
				break;

			case State::Esc:
				ProcessEsc(ch);
				break;

			case State::CommandArgs:
				mCommandArgBuf[mPendingCommandCharIndex++] = ch;
				if (mPendingCommandCharIndex >= mPendingCommandChars) {
					mState = State::None;
					
					auto cmd = mpPendingCommand;
					mpPendingCommand = nullptr;

					(this->*cmd)();
				}
				break;
		}
	}
}

void ATDevicePrinterFX80::ResetState() {
	mState = State::None;
	mLeftMarginUnits = 0;
	mRightMarginUnits = 0;
	mLineSpacingUnits = kDefaultLineSpacingUnits;

	mEighthBitAndMask = 0xFF;
	mEighthBitXorMask = 0x00;
	mbIntlCharsEnabled = false;
	mbItalicIntlCharsEnabled = false;
	mIntlCharMode = mIntlModeSetting;

	mbExpandedCurrentLine = false;
	mbExpandedAlways = false;
	mbCompressed = mbCompressedSetting;
	mbElite = false;
	mbProportional = false;
	mbEmphasized = mbEmphasizeSetting;
	mbDoubleStrike = false;
	mbUnderline = false;
	mbItalic = false;
	mbSuperscript = false;
	mbSubscript = false;
	mbUserCharsEnabled = false;

	mXPos = 0;
	mYPos = 0;
	mPrintXPos = 0;

	// Default to 11" or 12" paper depending on J5 jumper.
	mFormHeightUnits = mbPaperHeightSetting ? 216 * 12 : 216 * 11;

	// Default skip 1" perforation if enabled by SW2-3.
	mPerforationSkipDistance = mbSkipPerforationSetting ? 216 : 0;

	mNumCustomHTabs = -1;
	mCustomVTabChannel = 0;
	mCustomVTabChannelsSet = 0;

	UpdateActiveState();

	mRedefinableGraphicsModes[0] = 0;	// ESC K - default single
	mRedefinableGraphicsModes[1] = 1;	// ESC L - default low-speed double
	mRedefinableGraphicsModes[2] = 2;	// ESC Y - default high-speed double
	mRedefinableGraphicsModes[3] = 3;	// ESC Z - default quadruple
}

void ATDevicePrinterFX80::ProcessChar(uint8 ch) {
	uint8 controlCh = ch;

	// if the italic control chars setting is disabled, ignore bit 7 when
	// testing for control chars
	if (!mbItalicIntlCharsEnabled)
		controlCh &= 0x7F;

	switch(controlCh) {
		case 0x07:	// BEL	sound beep
			return;

		case 0x08:	// BS	backspace
			ProcessCmdBackspace();
			return;

		case 0x09:	// HT	horizontal tab
			ProcessCmdHorizontalTab();
			return;

		case 0x0A:	// LF	line feed
			PrintLF();
			return;

		case 0x0B:	// VT	vertical tab
			ProcessCmdVerticalTab();
			return;

		case 0x0C:	// FF	form feed
			ProcessCmdFormFeed();
			return;

		case 0x0D:	// CR	carriage return
			if (mbAutoLF)
				PrintLF();
			else
				PrintCR();
			return;

		case 0x0E:	// SO	expanded mode on (1-line)
			mbExpandedCurrentLine = true;
			UpdateActiveState();
			return;

		case 0x0F:	// SI	compressed on
			if (!mbCompressed) {
				FlushPrintBuffer();
				mbCompressed = true;
				UpdateActiveState();
			}
			return;

		case 0x11:	// DC1	active state
			return;

		case 0x12:	// DC2	compressed off
			if (mbCompressed) {
				FlushPrintBuffer();
				mbCompressed = false;
				UpdateActiveState();
			}
			return;

		case 0x13:	// DC3	deactivate printer
			return;

		case 0x14:	// DC4	expanded mode off (1-line)
			mbExpandedCurrentLine = false;
			UpdateActiveState();
			return;

		case 0x18:	// CAN	cancel text in line buffer
			ClearPrintBuffer();
			return;

		case 0x1B:	// ESC	escape
			mState = State::Esc;
			return;

		case 0x7F:	// DEL	delete
			if (mBufferedCharCount)
				--mBufferedCharCount;
			return;

		default:
			// if intl chars disabled, ignore chars in $00-1F not mapped to a
			// control char
			if (controlCh < 0x20 && !mbIntlCharsEnabled)
				return;
			break;
	}

	BufferChar(ch);
}

void ATDevicePrinterFX80::ProcessGraphics(uint8 ch) {
	if (!mbGraphicsDiscard) {
		if (mbGraphics9Pin && !(mGraphicsBytesLeft & 1)) {
			mGraphicsPending9PinByte = ch;
		} else {
			// check if the graphics extends beyond right column -- we need to
			// discard everything past that
			const sint32 newXPos = mXPos + mGraphicsXStep;
			if (newXPos > kMaxWidthUnits - mRightMarginUnits)
				mbGraphicsDiscard = true;
			else {
				// print new column
				uint32 pins = 0;
				
				if (mbGraphics9Pin)
					pins = ((uint32)mGraphicsPending9PinByte << 1) + (ch >> 7);
				else
					pins = (uint32)ch << 1;

				if (mbGraphicsNoAdjacentDots)
					pins &= ~mGraphicsLastPins;

				mGraphicsLastPins = pins;

				if (mpGraphicsOutput)
					mpGraphicsOutput->Print(kPaperLeftMarginMM<double> + kMMPerHorizUnit<double> * (double)mXPos, pins);

				mXPos = newXPos;
			}
		}
	}

	if (!--mGraphicsBytesLeft) {
		mState = State::None;
	}
}

void ATDevicePrinterFX80::ProcessEsc(uint8 ch) {
	mState = State::None;

	switch(ch) {
		case 0x21:	// ESC !	Master select
			BeginCommand(1, &ATDevicePrinterFX80::ProcessCmdMasterSelect);
			break;

		case 0x23:	// ESC #	Accept eighth bit as-is
			mEighthBitAndMask = 0xFF;
			mEighthBitXorMask = 0x00;
			break;

		case 0x25:	// ESC %	Character set select
			BeginCommand(2, &ATDevicePrinterFX80::ProcessCmdCharacterSelect);
			break;

		case 0x26:	// ESC &	Define characters in user RAM
			BeginCommand(15, &ATDevicePrinterFX80::ProcessCmdDefineCharacters);
			break;

		case 0x2A:	// ESC *	Graphics mode
			BeginCommand(3, &ATDevicePrinterFX80::ProcessCmdGraphics);
			break;

		case 0x2D:	// ESC -	Underline mode
			BeginCommand(1, &ATDevicePrinterFX80::ProcessCmdUnderlineMode);
			break;

		case 0x2F:	// ESC /	Select vertical tab channel
			BeginCommand(3, &ATDevicePrinterFX80::ProcessCmdSelectVerticalTabChannel);
			break;

		case 0x30:	// ESC 0	Set line spacing to 1/8 inch (9-dot)
			mLineSpacingUnits = 9*3;
			break;

		case 0x31:	// ESC 1	Set line spacing to 7/72 inch (7-dot)
			mLineSpacingUnits = 7*3;
			break;

		case 0x32:	// ESC 2	Set line spacing to 1/6 inch (12-dot)
			mLineSpacingUnits = 12*3;
			break;

		case 0x33:	// ESC 3	Set line spacing to n/216 inch
			BeginCommand(1, &ATDevicePrinterFX80::ProcessCmdLineSpacingFine);
			break;	

		case 0x34:	// ESC 4	Turn italic mode on
			mbItalic = true;
			break;

		case 0x35:	// ESC 5	Turn italic mode off
			mbItalic = false;
			break;

		case 0x36:	// ESC 6	Turn italic int'l chars on
			mbItalicIntlCharsEnabled = true;
			break;

		case 0x37:	// ESC 7	Turn italic int'l chars off
			mbItalicIntlCharsEnabled = false;
			break;

		case 0x38:	// ESC 8	Disable paper-out sensor
			break;

		case 0x39:	// ESC 9	Enable paper-out sensor
			break;

		case 0x3A:	// ESC :	Copy ROM characters to RAM
			BeginCommand(3, &ATDevicePrinterFX80::ProcessCmdCopyROMChars);
			break;

		case 0x3C:	// ESC <	Turn on 1-line unidirectional mode
			break;

		case 0x3D:	// ESC =	Set eighth bit to 0
			mEighthBitAndMask = 0x7F;
			mEighthBitXorMask = 0x00;
			break;

		case 0x3E:	// ESC >	Set eighth bit to 1
			mEighthBitAndMask = 0x7F;
			mEighthBitXorMask = 0x80;
			break;

		case 0x3F:	// ESC ?	Redefine alternate graphics code
			BeginCommand(2, &ATDevicePrinterFX80::ProcessCmdRedefineAlternateGraphicsCode);
			break;

		case 0x40:	// ESC @	Reset code
			ResetState();
			break;

		case 0x41:	// ESC A	Set line spacing to n/72 inch
			BeginCommand(1, &ATDevicePrinterFX80::ProcessCmdLineSpacingCoarse);
			break;

		case 0x42:	// ESC B	Set vertical tabs
			ProcessCmdSetVerticalTabs();
			break;

		case 0x43:	// ESC C	Set form length
			BeginCommand(1, &ATDevicePrinterFX80::ProcessCmdSetFormLength);
			break;

		case 0x44:	// ESC D	Set horizontal tabs
			ProcessCmdSetHorizontalTabs();
			break;

		case 0x45:	// ESC E	Emphasized mode on
			if (!mbEmphasized) {
				mbEmphasized = true;
				UpdateActiveState();
			}
			break;

		case 0x46:	// ESC F	Emphasized mode off
			if (mbEmphasized) {
				mbEmphasized = false;
				UpdateActiveState();
			}
			break;

		case 0x47:	// ESC G	Double-strike mode on
			if (!mbDoubleStrike) {
				mbDoubleStrike = true;
				UpdateActiveState();
			}
			break;

		case 0x48:	// ESC H	Double-strike mode off
			if (mbDoubleStrike) {
				mbDoubleStrike = false;
				UpdateActiveState();
			}
			break;

		case 0x49:	// ESC I	Turn on/off intl chars
			BeginCommand(1, &ATDevicePrinterFX80::ProcessCmdIntlChars);
			break;

		case 0x4A:	// ESC J	Immediate temporary line feed
			FlushPrintBuffer();
			// MERGE NOTE: test18 called the handler before consuming ESC J's
			// argument. Queue it so the requested feed distance is used.
			BeginCommand(1, &ATDevicePrinterFX80::ProcessCmdImmediateTempLF);
			break;

		case 0x4B:	// ESC K	Single-density graphics (redefinable)
			ProcessCmdGraphics(mRedefinableGraphicsModes[0]);
			break;

		case 0x4C:	// ESC L	Low-speed double density graphics (redefinable)
			ProcessCmdGraphics(mRedefinableGraphicsModes[1]);
			break;

		case 0x4D:	// ESC M	Elite mode on
			if (!mbElite) {
				mbElite = true;
				UpdateActiveState();
			}
			break;

		case 0x4E:	// ESC N	Skip over perforation on
			BeginCommand(1, &ATDevicePrinterFX80::ProcessCmdSkipOverPerforationOn);
			break;

		case 0x4F:	// ESC O	Skip over perforation off
			ProcessCmdSkipOverPerforationOff();
			break;

		case 0x50:	// ESC P	Elite mode off
			if (mbElite) {
				mbElite = false;
				UpdateActiveState();
			}
			break;

		case 0x51:	// ESC Q	Set right margin
			BeginCommand(1, &ATDevicePrinterFX80::ProcessCmdRightMargin);
			break;

		case 0x52:	// ESC R	Select international character set
			BeginCommand(1, &ATDevicePrinterFX80::ProcessCmdSelectIntlCharSet);
			break;
		
		case 0x53:	// ESC S	Turn on superscript/subscript mode
			BeginCommand(1, &ATDevicePrinterFX80::ProcessCmdSuperSubScript);
			break;

		case 0x54:	// ESC T	Turn off superscript/subscript mode
			mbSuperscript = false;
			mbSubscript = false;
			UpdateActiveState();
			break;

		case 0x55:	// ESC U	Turn on/off unidirectional mode
			BeginCommand(1, &ATDevicePrinterFX80::ProcessCmdUnidirectionalMode);
			break;

		case 0x57:	// ESC W	Turn on/off expanded mode
			BeginCommand(1, &ATDevicePrinterFX80::ProcessCmdExpanded);
			break;

		case 0x59:	// ESC Y	High-speed double density graphics (redefinable)
			ProcessCmdGraphics(mRedefinableGraphicsModes[2]);
			break;

		case 0x5A:	// ESC Z	Quadruple density graphics (redefinable)
			ProcessCmdGraphics(mRedefinableGraphicsModes[3]);
			break;

		case 0x5E:	// ESC ^	9-pin graphics
			BeginCommand(3, &ATDevicePrinterFX80::ProcessCmdGraphics9Pin);
			break;

		case 0x62:	// ESC b	Set vertical tabs for channel n
			BeginCommand(1, &ATDevicePrinterFX80::ProcessCmdSetVerticalTabChannel);
			// MERGE NOTE: keep ESC b from falling through to the new ESC i case.
			break;

		case 0x69:	// ESC i	Turn immediate mode on/off
			FlushPrintBuffer();
			BeginCommand(1, &ATDevicePrinterFX80::ProcessCmdImmediateMode);
			break;

		case 0x6A:	// ESC j	Reverse feed n/216"
			BeginCommand(1, &ATDevicePrinterFX80::ProcessCmdReverseFeed);
			break;

		case 0x6C:	// ESC l	Set right margin
			BeginCommand(1, &ATDevicePrinterFX80::ProcessCmdLeftMargin);
			return;

		case 0x70:	// ESC p	Turn on/off proportional mode
			BeginCommand(1, &ATDevicePrinterFX80::ProcessCmdProportional);
			break;

		case 0x73:	// ESC s	Select print speed
			BeginCommand(1, &ATDevicePrinterFX80::ProcessCmdSelectPrintSpeed);
			break;

		default:
			// Any unrecognized escape chars are ignored and eaten
			// without being printed.
			break;
	}
}

void ATDevicePrinterFX80::BeginCommand(uint8 argBytes, void (ATDevicePrinterFX80::*fn)()) {
	mpPendingCommand = fn;
	mPendingCommandCharIndex = 0;
	mPendingCommandChars = argBytes;
	mState = State::CommandArgs;
}

std::optional<bool> ATDevicePrinterFX80::ParseBool() const {
	// Ignore 8th bit, then check for either binary or ASCII 0/1.
	// Most commands ignore any unrecognized values.
	switch(mCommandArgBuf[0] & 0x7F) {
		case 0x00:
		case 0x30:
			return false;

		case 0x01:
		case 0x31:
			return true;

		default:
			return std::nullopt;
	}
}

void ATDevicePrinterFX80::ProcessCmdBackspace() {
	// Undocumented: BS is ignored in proportional mode, since character
	// width varies
	if (mbProportional)
		return;

	// get current char width (char doesn't matter)
	const sint32 width = GetCharWidth(0x20);

	// ignore if it would backspace beyond left margin
	if (mLeftMarginUnits + width > mXPos)
		return;

	// print buffered characters
	FlushPrintBuffer();

	// back up
	mXPos -= width;
}

void ATDevicePrinterFX80::ProcessCmdCharacterSelect() {
	const uint8 font = mCommandArgBuf[0] & 0x7F;
	const uint8 area = mCommandArgBuf[1] & 0x7F;

	if (area)
		return;

	if (font >= 2)
		return;

	const bool useUserChars = (font == 1);

	if (mbUserCharsEnabled != useUserChars) {
		FlushPrintBuffer();
		mbUserCharsEnabled = useUserChars;
	}
}

void ATDevicePrinterFX80::ProcessCmdCopyROMChars() {
	// validate all three unused parameters as 7-bit zeroes
	for(int i=0; i<3; ++i) {
		if (mCommandArgBuf[i] & 0x7F)
			return;
	}

	memcpy(mUserFontData, g_ATPrinterFontFX80.mFont, sizeof(mUserFontData));
	memcpy(mUserFontStartStop, g_ATPrinterFontFX80.mPropStartStop, sizeof(mUserFontStartStop));
}

void ATDevicePrinterFX80::ProcessCmdDefineCharacters() {
	const uint8 area = mCommandArgBuf[0];
	const uint8 startChar = mCommandArgBuf[1];
	const uint8 endChar = mCommandArgBuf[2];

	// memory area must be 0
	if ((area & 0x7F) != 0)
		return;

	// start char must be <= end char
	if (startChar > endChar)
		return;

	// break out start/end columns from control byte
	uint8 startColumn = (mCommandArgBuf[3] >> 4) & 7;
	uint8 endColumn = mCommandArgBuf[3] & 15;

	// validate minimum width of 5 columns and max 12 columns; force full
	// columns on violation
	if (endColumn >= 12 || startColumn > endColumn || endColumn - startColumn < 4) {
		startColumn = 0;
		endColumn = 11;
	}

	// set metadata
	mUserFontStartStop[startChar][0] = startColumn;
	mUserFontStartStop[startChar][1] = endColumn;

	// copy over columns, applying descender shift if needed -- note that we are
	// leaving column 12 blank, as it always is
	const bool descender = !(mCommandArgBuf[3] & 0x80);

	for(int i=0; i<11; ++i) {
		const uint16 pins = mCommandArgBuf[4 + i];

		if (descender)
			mUserFontData[startChar][i] = pins;
		else
			mUserFontData[startChar][i] = pins << 1;
	}

	// if there are more chars, loop for more
	if (startChar < endChar) {
		BeginCommand(15, &ATDevicePrinterFX80::ProcessCmdDefineCharacters);
		++mCommandArgBuf[1];
		mPendingCommandCharIndex = 3;
	}
}

void ATDevicePrinterFX80::ProcessCmdExpanded() {
	const auto enable = ParseBool();

	if (!enable.has_value())
		return;

	mbExpandedCurrentLine = mbExpandedAlways = enable.value();
	UpdateActiveState();
}

void ATDevicePrinterFX80::ProcessCmdFormFeed() {
	FeedPaper(mFormHeightUnits - mYPos);
}

void ATDevicePrinterFX80::ProcessCmdGraphics(uint8 mode) {
	BeginCommand(3, &ATDevicePrinterFX80::ProcessCmdGraphics);
	mPendingCommandCharIndex = 1;
	mCommandArgBuf[0] = mode;
}

void ATDevicePrinterFX80::ProcessCmdGraphics() {
	// The FX-80 manual says that the MSB is masked to 3 bits. However, the
	// FX-80+ takes 7.
	const uint32 n = mCommandArgBuf[1] + 256*(mCommandArgBuf[2] & 0x7F);
	mGraphicsBytesLeft = n;

	// flush any pending text
	FlushPrintBuffer();

	// zero bytes is a no-op
	if (!n)
		return;

	// check if the mode is valid
	const uint8 mode = mCommandArgBuf[0] & 0x7F;
	mbGraphicsDiscard = true;
	mbGraphics9Pin = false;

	if (mode < 8) {
		mbGraphicsDiscard = false;

		switch(mode) {
			case 0:		// single density
				mGraphicsXStep = 12;	// 5760/12 = 480 dots per line (60 dpi)
				mbGraphicsNoAdjacentDots = false;
				break;
			case 1:
				mGraphicsXStep = 6;		// 5760/6 = 960 dots per line (120 dpi)
				mbGraphicsNoAdjacentDots = false;
				break;
			case 2:
				mGraphicsXStep = 6;		// 5760/6 = 960 dots per line (120 dpi)
				mbGraphicsNoAdjacentDots = true;
				break;
			case 3:
				mGraphicsXStep = 3;		// 5760/3 = 1920 dots per line (240 dpi)
				mbGraphicsNoAdjacentDots = true;
				break;
			case 4:
				mGraphicsXStep = 9;		// 5760/9 = 640 dots per line (80 dpi)
				mbGraphicsNoAdjacentDots = false;
				break;
			case 5:
				mGraphicsXStep = 10;	// 5760/6 = 576 dots per line (72 dpi)
				mbGraphicsNoAdjacentDots = false;
				break;
			case 6:
				mGraphicsXStep = 8;		// 5760/8 = 720 dots per line (90 dpi)
				mbGraphicsNoAdjacentDots = false;
				break;
			case 7:
				mGraphicsXStep = 5;		// 5760/6 = 1152 dots per line (144 dpi)
				mbGraphicsNoAdjacentDots = false;
				break;
		}
	}

	mState = State::Graphics;
}

void ATDevicePrinterFX80::ProcessCmdGraphics9Pin() {
	const uint8 mode = mCommandArgBuf[0] & 0x7F;

	// modes 0-1 valid for FX-80; modes 0-3 valid for FX-80+

	if (mode >= 4)
		mCommandArgBuf[0] = 0x7F;

	ProcessCmdGraphics();

	mbGraphics9Pin = true;
	mGraphicsBytesLeft *= 2;
}

void ATDevicePrinterFX80::ProcessCmdHorizontalTab() {
	// Check if custom tabs have been set.
	if (mNumCustomHTabs >= 0) {
		// find first tab beyond current horizontal position
		for(int i=0; i<mNumCustomHTabs; ++i) {
			if (mCustomHTabs[i] > mXPos) {
				FlushPrintBuffer();

				if (mCustomHTabs[i] <= kMaxWidthUnits - mRightMarginUnits)
					mXPos = mCustomHTabs[i];
				break;
			}
		}
	} else {
		// Add spaces until the horizontal position is a multiple of 8. This
		// applies even if a wrap is needed.
		//
		// There is one problem here. Only two characters are required between
		// the margins, so it's possible that the printer will NEVER be able
		// to complete a tab operation. This is a problem when we are running
		// in immediate mode. We cut off the tab operation after a while
		// if it looks like it'll go on forever.
		for(int i=0; i<100; ++i) {
			BufferChar(0x20);

			if (!(mBufferedCharCount & 7))
				break;
		}
	}
}

void ATDevicePrinterFX80::ProcessCmdImmediateMode() {
	// Nothing to do -- the argument is currently ignored.
}

void ATDevicePrinterFX80::ProcessCmdImmediateTempLF() {
	FeedPaper(mCommandArgBuf[0]);
}

void ATDevicePrinterFX80::ProcessCmdIntlChars() {
	const auto enable = ParseBool();

	if (enable.has_value())
		mbIntlCharsEnabled = enable.value();
}

void ATDevicePrinterFX80::ProcessCmdLeftMargin() {
	const sint32 charCount = mCommandArgBuf[0];
	const sint32 width = GetCharWidth(0x20);
	const sint32 leftMarginUnits = charCount * width;

	// Enforce required minimum of 144 units (two pica chars) between left
	// and right margin; ignore left margin requests exceeding this.
	if (leftMarginUnits + 144 + mRightMarginUnits > kMaxWidthUnits)
		return;

	// change left margin
	mLeftMarginUnits = leftMarginUnits;

	// turn off custom horizontal tabs
	mNumCustomHTabs = -1;

	// cancel the print buffer
	ClearPrintBuffer();
}

void ATDevicePrinterFX80::ProcessCmdLineSpacingCoarse() {
	if (mCommandArgBuf[0] >= 86)
		return;

	mLineSpacingUnits = mCommandArgBuf[0] * 3;
}

void ATDevicePrinterFX80::ProcessCmdLineSpacingFine() {
	mLineSpacingUnits = mCommandArgBuf[0];
}

void ATDevicePrinterFX80::ProcessCmdMasterSelect() {
	// flush print buffer even if nothing changes
	FlushPrintBuffer();

	// Master select code bits are as follows:
	//
	//	1 - elite
	//	4 - compressed
	//	8 - emphasized
	//	16 - double-strike
	//	32 - expanded
	//
	// Proportional mode cannot be enabled via master select and is disabled.

	const uint8 mode = mCommandArgBuf[0];

	// MERGE NOTE: honor the master-select contract above even after ESC p.
	mbProportional = false;
	mbElite = (mode & 0x01) != 0;
	mbCompressed = (mode & 0x04) != 0;
	mbEmphasized = (mode & 0x08) != 0;
	mbDoubleStrike = (mode & 0x10) != 0;
	mbExpandedAlways = mbExpandedCurrentLine = (mode & 0x20) != 0;

	UpdateActiveState();
}

void ATDevicePrinterFX80::ProcessCmdProportional() {
	const auto mode = ParseBool();

	if (!mode.has_value())
		return;

	mbProportional = mode.value();

	UpdateActiveState();
}

void ATDevicePrinterFX80::ProcessCmdRedefineAlternateGraphicsCode() {
	const uint8 ch = mCommandArgBuf[0];
	const uint8 mode = mCommandArgBuf[1];

	if (mode >= 8)
		return;

	switch(ch) {
		case 'K': mRedefinableGraphicsModes[0] = mode; break;
		case 'L': mRedefinableGraphicsModes[1] = mode; break;
		case 'Y': mRedefinableGraphicsModes[2] = mode; break;
		case 'Z': mRedefinableGraphicsModes[3] = mode; break;

		default:
			break;
	}
}

void ATDevicePrinterFX80::ProcessCmdReverseFeed() {
	const sint32 increment = mCommandArgBuf[0];

	FlushPrintBuffer();

	if (increment)
		FeedPaper(-increment);
}

void ATDevicePrinterFX80::ProcessCmdRightMargin() {
	const sint32 charCount = mCommandArgBuf[0];
	const sint32 width = GetCharWidth(0x20);
	const sint32 rightMarginUnits = charCount * width;

	// Enforce required minimum of 144 units (two pica chars) between left
	// and right margin; ignore right margin requests exceeding this.
	if (mLeftMarginUnits + 144 + rightMarginUnits > kMaxWidthUnits)
		return;

	// change left margin
	mRightMarginUnits = rightMarginUnits;

	// cancel the print buffer
	ClearPrintBuffer();
}

void ATDevicePrinterFX80::ProcessCmdSelectIntlCharSet() {
	const uint8 mode = mCommandArgBuf[0] & 0x7F;

	if (mode >= 9)
		return;

	mIntlCharMode = mode;
}

void ATDevicePrinterFX80::ProcessCmdSelectPrintSpeed() {
	const auto mode = ParseBool();

	if (mode.has_value() && mbSlowPrintSpeed != mode.value()) {
		FlushPrintBuffer();

		mbSlowPrintSpeed = mode.value();
	}
}

void ATDevicePrinterFX80::ProcessCmdSelectVerticalTabChannel() {
	const uint8 channel = mCommandArgBuf[0];

	if (channel >= 8)
		return;


	mCustomVTabChannel = channel;
}

void ATDevicePrinterFX80::ProcessCmdSetFormLength() {
	const uint8 lines = mCommandArgBuf[0];

	// ESC c 0 n sets the form length in inches. Note that this must be 0;
	// $80 will not work.
	if (lines == 0) {
		BeginCommand(1, &ATDevicePrinterFX80::ProcessCmdSetFormLengthInches);
		return;
	}

	ProcessCmdSetFormLength(mLineSpacingUnits * (lines & 0x7F));
}

void ATDevicePrinterFX80::ProcessCmdSetFormLengthInches() {
	const uint8 lines = mCommandArgBuf[0] & 0x7F;

	// reject inputs over 22 lines (per docs)
	if (lines >= 23)
		return;

	ProcessCmdSetFormLength(lines * 216);
}

void ATDevicePrinterFX80::ProcessCmdSetFormLength(sint32 units) {
	// zero is not valid
	if (!units)
		return;

	mFormHeightUnits = units;

	// vertical pos is reset and perforation skip turned off
	mYPos = 0;

	// disable all custom vertical tabs
	mCustomVTabChannelsSet = 0;
}

void ATDevicePrinterFX80::ProcessCmdSetHorizontalTabs() {
	// reset tab table
	mNumCustomHTabs = 0;

	BeginCommand(1, &ATDevicePrinterFX80::ProcessCmdSetHorizontalTabsNext);
}

void ATDevicePrinterFX80::ProcessCmdSetHorizontalTabsNext() {
	const uint8 charPos = mCommandArgBuf[0];

	// 0 or a value less than previous ends the list
	if (!charPos || charPos < mCustomHTabCharLast)
		return;

	mCustomHTabCharLast = charPos;

	const uint32 charWidth = mbProportional ? 72 : GetCharWidth(0x20);
	const uint32 tabPos = charWidth * charPos;

	// store up to 32 tabs; discard afterward but keep reading the list
	if (mNumCustomHTabs < (int)vdcountof(mCustomHTabs))
		mCustomHTabs[mNumCustomHTabs++] = tabPos;

	BeginCommand(1, &ATDevicePrinterFX80::ProcessCmdSetHorizontalTabsNext);
}

void ATDevicePrinterFX80::ProcessCmdSetVerticalTabs() {
	mCommandArgBuf[0] = 0;

	ProcessCmdSetVerticalTabChannel();
}

void ATDevicePrinterFX80::ProcessCmdSetVerticalTabChannel() {
	const uint8 channel = mCommandArgBuf[0];

	if (channel >= 8)
		return;

	// mark the tab channel as set, and clear it
	mCustomVTabChannelsSet |= 1 << channel;

	memset(&mCustomVTabs[channel][0], 0, sizeof mCustomVTabs[channel]);

	// begin reading tabs
	BeginCommand(3, &ATDevicePrinterFX80::ProcessCmdSetVerticalTabChannelNext);
	mCommandArgBuf[1] = 0;
	mPendingCommandCharIndex = 2;
}

void ATDevicePrinterFX80::ProcessCmdSetVerticalTabChannelNext() {
	uint8 channel = mCommandArgBuf[0];
	uint8 nextIndex = mCommandArgBuf[1];
	uint8 vtLine = mCommandArgBuf[2];

	VDASSERT(channel < 8 && nextIndex < 16);

	channel &= 7;
	nextIndex &= 15;

	// 0 ends the list
	if (vtLine == 0)
		return;

	const uint16 vtPos = (uint32)vtLine * mLineSpacingUnits;

	// if there's room in the vtab list, and the new tab is below the last one,
	// add it -- otherwise, begin skipping
	if (nextIndex < 16) {
		const bool tabOrderOk = !nextIndex || vtPos > mCustomVTabs[channel][nextIndex - 1];

		if (tabOrderOk && (sint32)vtPos < mFormHeightUnits) {
			mCustomVTabs[channel][nextIndex] = vtPos;
			++mCommandArgBuf[1];
		} else {
			// force skipping of remaining vtabs
			mCommandArgBuf[1] = 16;
		}
	}

	BeginCommand(3, &ATDevicePrinterFX80::ProcessCmdSetVerticalTabChannelNext);
	mPendingCommandCharIndex = 2;
}

void ATDevicePrinterFX80::ProcessCmdSkipOverPerforationOn() {
	const sint32 lines = mCommandArgBuf[0] & 0x7F;

	// zero is not valid
	if (!lines)
		return;

	// convert to units
	const sint32 units = lines * mLineSpacingUnits;

	// ignore request if >= page height
	if (units >= mFormHeightUnits)
		return;

	mPerforationSkipDistance = units;
}

void ATDevicePrinterFX80::ProcessCmdSkipOverPerforationOff() {
	mPerforationSkipDistance = 0;
}

void ATDevicePrinterFX80::ProcessCmdSuperSubScript() {
	const auto mode = ParseBool();

	if (!mode.has_value())
		return;

	mbSuperscript = (mode.value() == false);
	mbSubscript = (mode.value() == true);

	UpdateActiveState();
}

void ATDevicePrinterFX80::ProcessCmdUnderlineMode() {
	const auto mode = ParseBool();

	if (!mode.has_value())
		return;

	mbUnderline = mode.value();
	UpdateActiveState();
}

void ATDevicePrinterFX80::ProcessCmdUnidirectionalMode() {
	// currently ignored -- does not flush print buffer anyway
}

void ATDevicePrinterFX80::ProcessCmdVerticalTab() {
	FlushPrintBuffer();
	mXPos = 0;

	// check if custom tabs have been enabled for the current vtab channel
	if (!(mCustomVTabChannelsSet & (1 << mCustomVTabChannel))) {
		// no -- just do a line feed
		PrintLF();
		return;
	}

	// search for the next vertical tab >= current vpos
	const auto& vtChannel = mCustomVTabs[mCustomVTabChannel];

	for(const uint16& vtPos : vtChannel) {
		if (vtPos > mYPos) {
			FeedPaper(vtPos - mYPos);
			return;
		}
	}

	// no vtab found -- do form feed
	ProcessCmdFormFeed();
}

void ATDevicePrinterFX80::UpdateActiveState() {
	// Elite has priority over Compressed on the FX-80. However, they can be
	// combined on the FX-80+.
	//
	// Elite overrides Proportional and Emphasized.
	//
	// Proportional implies Emphasized.
	//
	// Emphasized overrides Compressed.
	//
	// Super/subscript implies Double Strike.

	mActiveCharAttr = {};
	mActiveUniCharSet = {};

	// - Normal
	// - Elite
	// - Proportional
	// - Emphasized
	// - Compressed
	// - Elite Compressed
	if (mbElite) {
		if (mbCompressed) {
			mActiveCharAttr |= CharAttr::Compressed | CharAttr::Elite;
			mActiveUniCharSet = UniCharSetIndex::EliteCompressed;
		} else {
			mActiveCharAttr |= CharAttr::Elite;
			mActiveUniCharSet = UniCharSetIndex::Elite;
		}
	} else if (mbProportional) {
		mActiveCharAttr |= CharAttr::Proportional | CharAttr::Emphasized;
		mActiveUniCharSet = UniCharSetIndex::Proportional;
	} else if (mbEmphasized) {
		mActiveCharAttr |= CharAttr::Emphasized;
		mActiveUniCharSet = UniCharSetIndex::Emphasized;
	} else if (mbCompressed) {
		mActiveCharAttr |= CharAttr::Compressed;
		mActiveUniCharSet = UniCharSetIndex::Compressed;
	}

	// Superscript and subscript rely on double strike to work.
	if (mbSuperscript) {
		mActiveCharAttr |= CharAttr::Superscript | CharAttr::DoubleStrike;
	} else if (mbSubscript) {
		mActiveCharAttr |= CharAttr::Subscript | CharAttr::DoubleStrike;
	} else if (mbDoubleStrike) {
		mActiveCharAttr |= CharAttr::DoubleStrike;
	}

	if (mbExpandedCurrentLine) {
		mActiveCharAttr |= CharAttr::Expanded;
		mActiveUniCharSet += UniCharSetIndex::Expanded;
	}

	if (mbUnderline)
		mActiveCharAttr |= CharAttr::Underline;
}

void ATDevicePrinterFX80::BufferChar(uint8 ch) {
	if (mBufferedCharCount >= kMaxCharsBuffered) {
		VDFAIL("Char buffer overflow");
		return;
	}

	ch = TransformPrintCharacter(ch);

	// If the character doesn't fit, we need to flush the line and start a new
	// one. However, we then need to recompute the char width, because the
	// expanded state can reset (!).
	sint32 charWidth = GetCharWidth(ch);
	if (mXPos + charWidth > kMaxWidthUnits - mRightMarginUnits) {
		PrintLF();
		charWidth = GetCharWidth(ch);
	}

	if (mBufferedCharCount == 0)
		mPrintXPos = mXPos;

	mCharBuffer[mBufferedCharCount++] = BufferedChar { mActiveCharAttr, ch };
	mXPos += charWidth;
}

sint32 ATDevicePrinterFX80::GetCharWidth(uint8 ch) const {
	return GetCharWidth(ch, mActiveCharAttr);
}

sint32 ATDevicePrinterFX80::GetCharWidth(uint8 ch, CharAttr attr) const {
	sint32 width = kWidthUnitsPerCharPica;

	if (+(attr & CharAttr::Proportional)) {
		const uint8 (&startStop)[2] = mbUserCharsEnabled
			? mUserFontStartStop[ch]
			: g_ATPrinterFontFX80.mPropStartStop[ch];

		const auto [start, stop] = startStop;
		width = ((stop + 1) - start) * (kWidthUnitsPerCharPica / 12);
	} else {
		switch(+(attr & (CharAttr::Elite | CharAttr::Compressed))) {
			case +CharAttr::None:
			default:
				break;

			case +CharAttr::Elite:
				width = kWidthUnitsPerCharElite;
				break;

			case +CharAttr::Compressed:
				width = kWidthUnitsPerCharCompressed;
				break;

			case +(CharAttr::Compressed | CharAttr::Elite):
				width = kWidthUnitsPerCharCompressedElite;
				break;
		}
	}

	if (+(attr & CharAttr::Expanded))
		width *= 2;

	return width;
}

void ATDevicePrinterFX80::ClearPrintBuffer() {
	// - The print buffer is emptied without being printed.
	// - The X position is reset to the left margin.

	mBufferedCharCount = 0;
	mXPos = mLeftMarginUnits;
}

void ATDevicePrinterFX80::FlushPrintBuffer() {
	if (!mpGraphicsOutput) {
		mBufferedCharCount = 0;
		return;
	}

	CharAttr allCharAttrs = CharAttr::None;
	sint32 xpos = 0;

	for(int pass = 0; pass < 3; ++pass) {
		const CharAttr requiredAttrBit
			= pass == 1 ? CharAttr::DoubleStrike
			: pass == 2 ? CharAttr::Underline
			: CharAttr::None;

		const bool doingUnderline = (pass == 2);

		xpos = mPrintXPos;

		for(const BufferedChar& ch : vdspan(mCharBuffer, mBufferedCharCount)) {
			const uint16 *charDat = mbUserCharsEnabled
				? &mUserFontData[ch.mChar][0]
				: &g_ATPrinterFontFX80.mFont[ch.mChar][0];

			// compute paper position for left edge of character
			double paperXPos = kPaperLeftMarginMM<double> + kMMPerHorizUnit<double> * (double)xpos;

			// compute paper x-step for each dot column, based on character width
			// (12 columns / char)
			const sint32 charWidth = GetCharWidth(ch.mChar, ch.mAttributes);
			double paperDXPos = 0;
			int startColumn = 0;
			int stopColumn = 11;
				
			if (+(ch.mAttributes & CharAttr::Proportional)) {
				// proportional is always pica or pica expanded
				paperDXPos = +(ch.mAttributes & CharAttr::Expanded) ? kMMPerHorizUnit<double> * 12 : kMMPerHorizUnit<double> * 6;

				const uint8 (&startStop)[2] = mbUserCharsEnabled
					? mUserFontStartStop[ch.mChar]
					: g_ATPrinterFontFX80.mPropStartStop[ch.mChar];

				startColumn = startStop[0];
				stopColumn = startStop[1];
			} else {
				// The dot column spacing for the sub-pica modes are different
				// than the width. Elite is only 16% narrower in character
				// spacing but a full 33% narrower in column spacing. Similarly,
				// compressed characters are half width even though the spacing
				// is 58% of pica.

				if (+(ch.mAttributes & CharAttr::Compressed))
					paperDXPos = kMMPerHorizUnit<double> * 3;
				else if (+(ch.mAttributes & CharAttr::Elite))
					paperDXPos = kMMPerHorizUnit<double> * 4;
				else
					paperDXPos = kMMPerHorizUnit<double> * 6;

				if (+(ch.mAttributes & CharAttr::Expanded))
					paperDXPos *= 2;
			}

			// for the second and third passes, skip chars that don't have the required attribute bit
			if ((ch.mAttributes & requiredAttrBit) == requiredAttrBit) {
				// if we have no underline, double strike, or super/subscript, we can print a char directly instead of dots
				if (!mbUserCharsEnabled && pass == 0 && !(ch.mAttributes & (CharAttr::DoubleStrike | CharAttr::Subscript | CharAttr::Superscript))) {
					uint32 pch = ch.mChar;

					if (+(ch.mAttributes & CharAttr::Proportional))
						pch += (uint32)+UniCharSetIndex::Proportional << 8;
					else if (+(ch.mAttributes & CharAttr::Emphasized))
						pch += (uint32)+UniCharSetIndex::Emphasized << 8;
					else {
						if (+(ch.mAttributes & CharAttr::Elite))
							pch += (uint32)+UniCharSetIndex::Elite << 8;

						if (+(ch.mAttributes & CharAttr::Compressed))
							pch += (uint32)+UniCharSetIndex::Compressed << 8;
					}

					if (+(ch.mAttributes & CharAttr::Expanded))
						pch += (uint32)+UniCharSetIndex::Expanded << 8;

					mpGraphicsOutput->PrintChar(paperXPos, pch);
				} else {
					uint32 prevPins = 0;
					uint32 underlinePins = 0;
					uint32 activeUnderlinePins = 0;

					if (doingUnderline) {
						underlinePins = 1;
						activeUnderlinePins = mbSlowPrintSpeed && (xpos & 1) ? 0 : 1;
					}

					for(int i = startColumn; i <= stopColumn; ++i) {
						uint32 pins = 0;

						// if not at half speed, underline only every other pin
						if (doingUnderline) {
							pins = activeUnderlinePins;

							if (!mbSlowPrintSpeed)
								activeUnderlinePins ^= underlinePins;
						} else {
							pins = charDat[i];

							if (+(ch.mAttributes & (CharAttr::Emphasized | CharAttr::Expanded))) {
								const uint32 newPins = pins | prevPins;

								prevPins = pins;
								pins = newPins;
							}

							if (+(ch.mAttributes & (CharAttr::Subscript | CharAttr::Superscript))) {
								// select even/odd pins
								if (!pass)
									pins <<= 1;

								// compress pins
								pins = (pins & 0x100)
									+ ((pins & 0x40) << 1)
									+ ((pins & 0x10) << 2)
									+ ((pins & 0x4) << 3)
									+ ((pins & 0x1) << 4);

								// shift pins down for subscript
								if (+(ch.mAttributes & CharAttr::Subscript))
									pins >>= 4;
							}
						}

						if (pins) {
							mpGraphicsOutput->Print(paperXPos + paperDXPos * (double)i, pins);

							if (+(ch.mAttributes & CharAttr::Emphasized))
								mpGraphicsOutput->Print(paperXPos + paperDXPos * ((double)i + 0.5), pins);
						}
					}
				}
			}

			xpos += charWidth;
			allCharAttrs |= ch.mAttributes;
		}

		// check if we need to do subsequent passes and how much to feed paper
		if (pass == 0) {
			// if no double-strike or underline, we're done
			if (!(allCharAttrs & (CharAttr::DoubleStrike | CharAttr::Underline)))
				break;

			// If we still need to do double-strike, feed paper up by 1/216"; the
			// FX-80 can't do half a dot so a third will have to do. Otherwise, if
			// we still have underline to do, feed paper down two dots (6/216").
			if (+(allCharAttrs & CharAttr::DoubleStrike)) {
				mpGraphicsOutput->FeedPaper(-kMMPerVertUnit<double>);
			} else {
				mpGraphicsOutput->FeedPaper(6 * kMMPerVertUnit<double>);
				pass = 1;
			}
		} else if (pass == 1) {
			// if no underline, feed paper and we're done
			if (!(allCharAttrs & CharAttr::Underline)) {
				mpGraphicsOutput->FeedPaper(kMMPerVertUnit<double>);
				break;
			} else {
				// undo -1/216" feed and then do 6/216" (two dots) for underline
				mpGraphicsOutput->FeedPaper(7 * kMMPerVertUnit<double>);
			}
		} else {
			// undo 6/216" from underline
			mpGraphicsOutput->FeedPaper(-6 * kMMPerVertUnit<double>);
		}
	}

	mPrintXPos = xpos;
	mBufferedCharCount = 0;
}

uint8 ATDevicePrinterFX80::TransformPrintCharacter(uint8 ch) const {
	// apply 8th bit set/reset
	ch = (ch & mEighthBitAndMask) ^ mEighthBitXorMask;

	// transform international characters
	if (mIntlCharMode) {
		static constexpr uint8 kIntlCharMap[9][12] {
			//  #     $     @     [     \     ]     ^     `     {     |     }     ~
			{ 0x23, 0x24, 0x40, 0x5B, 0x5C, 0x5D, 0x5E, 0x60, 0x7B, 0x7C, 0x7D, 0x7E },	// USA
			{ 0x23, 0x24, 0x00, 0x05, 0x0F, 0x10, 0x5E, 0x60, 0x1E, 0x02, 0x01, 0x16 },	// France
			{ 0x23, 0x24, 0x10, 0x17, 0x18, 0x19, 0x5E, 0x60, 0x1A, 0x1B, 0x1C, 0x11 },	// Germany
			{ 0x06, 0x24, 0x40, 0x5B, 0x5C, 0x5D, 0x5E, 0x60, 0x7B, 0x7C, 0x7D, 0x7E },	// UK
			{ 0x23, 0x24, 0x40, 0x12, 0x14, 0x0D, 0x5E, 0x60, 0x13, 0x15, 0x0E, 0x7E },	// Denmark
			{ 0x23, 0x0B, 0x1D, 0x17, 0x18, 0x0D, 0x19, 0x1E, 0x1A, 0x1B, 0x0E, 0x1C },	// Sweden
			{ 0x23, 0x24, 0x40, 0x05, 0x5C, 0x1E, 0x5E, 0x02, 0x00, 0x03, 0x01, 0x04 },	// Italy
			{ 0x0C, 0x24, 0x40, 0x07, 0x09, 0x08, 0x5E, 0x60, 0x16, 0x0A, 0x7D, 0x7E },	// Spain
			{ 0x23, 0x24, 0x40, 0x5B, 0x1F, 0x5D, 0x5E, 0x60, 0x7B, 0x7C, 0x7D, 0x7E },	// Japan
		};

		int charIdx = -1;

		switch(ch & 0x7F) {
			case 0x23: charIdx = 0; break;
			case 0x24: charIdx = 1; break;
			case 0x40: charIdx = 2; break;
			case 0x5B: charIdx = 3; break;
			case 0x5C: charIdx = 4; break;
			case 0x5D: charIdx = 5; break;
			case 0x5E: charIdx = 6; break;
			case 0x60: charIdx = 7; break;
			case 0x7B: charIdx = 8; break;
			case 0x7C: charIdx = 9; break;
			case 0x7D: charIdx = 10; break;
			case 0x7E: charIdx = 11; break;
			default:
				break;
		}

		if (charIdx >= 0)
			ch = kIntlCharMap[mIntlCharMode][charIdx];
	}

	// apply italics
	if (mbItalic)
		ch |= 0x80;

	// transform zero to slashed zero (at $7F in the font)
	if (mbSlashedZero && ((ch & 0x7F) == 0x30))
		ch = (ch & 0x80) + 0x7F;

	return ch;
}

void ATDevicePrinterFX80::PrintCR() {
	FlushPrintBuffer();
	mXPos = mLeftMarginUnits;
}

void ATDevicePrinterFX80::PrintLF() {
	PrintCR();
	FeedPaper();

	// Turn off 1-line expanded mode. This happens on an explicit LF, implicit
	// LF due to wrapping, and form feeds.
	if (mbExpandedCurrentLine) {
		mbExpandedCurrentLine = false;

		UpdateActiveState();
	}
}

void ATDevicePrinterFX80::FeedPaper() {
	FeedPaper(mLineSpacingUnits);
}

void ATDevicePrinterFX80::FeedPaper(sint32 units) {
	mYPos += units;

	if (mPerforationSkipDistance && mYPos + mPerforationSkipDistance >= mFormHeightUnits) {
		units += mPerforationSkipDistance;
		mYPos += mPerforationSkipDistance;
	}

	if ((uint32)mYPos >= (uint32)mFormHeightUnits) {
		mYPos %= mFormHeightUnits;

		if (mYPos < 0)
			mYPos += mFormHeightUnits;
	}

	if (mpGraphicsOutput)
		mpGraphicsOutput->FeedPaper(kMMPerVertUnit<double> * (double)units);
}
