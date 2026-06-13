//	Altirra - Atari 800/800XL emulator
//	Copyright (C) 2009-2024 Avery Lee
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
#include <at/atcore/configvar.h>
#include <at/atcore/propertyset.h>
#include "printer1020.h"
#include "printer1020font.h"

ATConfigVarRGBColor g_ATCVDevice1020Pen0("devices.1020.pen0", 0x000000);	// black
ATConfigVarRGBColor g_ATCVDevice1020Pen1("devices.1020.pen1", 0x181FF0);	// blue
ATConfigVarRGBColor g_ATCVDevice1020Pen2("devices.1020.pen2", 0x0B9C2F);	// green
ATConfigVarRGBColor g_ATCVDevice1020Pen3("devices.1020.pen3", 0xC91B12);	// red

void ATCreateDevicePrinter1020(const ATPropertySet& pset, IATDevice **dev) {
	vdrefptr<ATDevicePrinter1020> p(new ATDevicePrinter1020);

	*dev = p.release();
}

extern const ATDeviceDefinition g_ATDeviceDefPrinter1020 = { "1020", "1020", L"1020 Color Printer", ATCreateDevicePrinter1020 };

ATDevicePrinter1020::ATDevicePrinter1020()
	: ATDevicePrinterBase(true, false, false, true)
{
	SetSaveStateAgnostic();

	for(auto& v : mPenColors)
		v = -1;
}

ATDevicePrinter1020::~ATDevicePrinter1020() {
}

void ATDevicePrinter1020::GetDeviceInfo(ATDeviceInfo& info) {
	info.mpDef = &g_ATDeviceDefPrinter1020;
}

void ATDevicePrinter1020::GetSettings(ATPropertySet& settings) {
	settings.Clear();

	VDStringA name;
	for(int i=0; i<4; ++i) {
		if (mPenColors[i] >= 0) {
			name.sprintf("pencolor%d", i);

			settings.SetUint32(name.c_str(), mPenColors[i]);
		}
	}
}

bool ATDevicePrinter1020::SetSettings(const ATPropertySet& settings) {
	VDStringA name;
	for(int i=0; i<4; ++i) {
		name.sprintf("pencolor%d", i);

		uint32 c = 0;
		if (settings.TryGetUint32(name.c_str(), c))
			mPenColors[i] = c & 0xFFFFFF;
		else
			mPenColors[i] = -1;
	}

	ReconvertPens();

	return true;
}

void ATDevicePrinter1020::ColdReset() {
	ATDevicePrinterBase::ColdReset();

	ResetState();
}

void ATDevicePrinter1020::OnCreatedGraphicalOutput() {
	mpPrinterGraphicalOutput->SetOnClear(
		[this] {
			mBaseX = 0;
			mX = 0;
			mY = 0;
		}
	);

	ReconvertPens();
}

ATPrinterGraphicsSpec ATDevicePrinter1020::GetGraphicsSpec() const {
	ATPrinterGraphicsSpec spec {};
	spec.mPageWidthMM = 114.5f;				// 4.5" wide paper (based on CGP-115 service manual)
	spec.mPageVBorderMM = 8.0f;				// vertical border
	spec.mDotRadiusMM = 0.090f;				// guess for dot radius
	spec.mVerticalDotPitchMM = 0.403175f;	// 0.0159" vertical pitch (guess)
	spec.mbBit0Top = true;
	spec.mNumPins = 9;

	return spec;
}

bool ATDevicePrinter1020::IsSupportedDeviceId(uint8 id) const {
	// The source for P4: being supported is the Atari 8-bit FAQ.
	return id == 0x40 || id == 0x43;
}

bool ATDevicePrinter1020::IsSupportedOrientation(uint8 aux1) const {
	return true;
}

uint8 ATDevicePrinter1020::GetWidthForOrientation(uint8 aux1) const {
	return 40;
}

void ATDevicePrinter1020::GetStatusFrameInternal(uint8 frame[4]) {
	ResetState();
}

void ATDevicePrinter1020::HandleFrameInternal(uint8 orientation, uint8 *buf, uint32 len, bool graphics) {
	// Commands that must be at the start of a line:
	//	ESC CTRL G		Switch to graphics mode
	//	ESC CTRL P		20 character text mode
	//	ESC CTRL S		80 character text mode
	//	ESC CTRL N		40 character text mode
	//
	// Commands that can be used at the start of a line or in text:
	//	ESC CTRL W		Enable international characters
	//	ESC CTRL X		Disable international characters
	//
	// Graphics commands:
	//	S<0-63>			Set text scale
	//	H				Return to home position
	//	C<0-3>			Set color
	//	L<0-15>			Set line style
	//	Dx,y[;x,y...]	Draw line to absolute points
	//	I				Initialize (reset home origin)
	//	Jx,y			Draw line to relative point
	//	Mx,y			Move to absolute point
	//	Rx,y			Move to relative point
	//	Xa,d,n			Draw axis a (0=Y, 2=X), d distance between marks, n marks
	//	Q<0-3>			Set text orientation
	//	P<text>			Print text from graphics mode
	//	A				Return to text mode

	while(len--) {
		uint8 ch = *buf++;

		// Ignore everything in the frame past an EOL.
		if (ch == 0x9B)
			len = 0;

		// Two independent state machines are run for line parsing and for command parsing
		// due to a sticky edge case. It is possible for a command to span lines when
		// EOL is accepted as an argument separator before the last argument. When this
		// happens, ESC commands can be processed at the start of the next line in the
		// middle of the command.
		//
		// Example:
		//	M240
		//	ESC CTRL+N -120
		//
		// Instead of moving to (240,-120), the ESC CTRL+N is processed and forces a return
		// to text mode, printing "-120" instead.

		switch(mLineState) {
			case LineState::StartOfLine:
				if (ch == 0x1B) {
					mLineState = LineState::Escape;
					continue;
				}

				if (ch == 0x9B) {
					if (!mbGraphicsMode) {
						PrintChar(ch, false);
						continue;
					}
				}

				mLineState = LineState::ProcessLine;

				// Force print text mode if we're in text mode. In graphics
				// mode, we must NOT force WaitForCommand because we may be
				// in the middle of parsing a command.
				if (!mbGraphicsMode)
					mCommandState = CommandState::PrintText;
				break;

			case LineState::Escape: {
				bool returnToTextMode = false;

				mLineState = LineState::StartOfLine;

				switch(ch) {
					case 0x07:	// ESC CTRL G: graphics mode
						mbGraphicsMode = true;
						mCommandState = CommandState::WaitForCommand;
						break;
					case 0x10:	// ESC CTRL P: 20 char mode and return to text mode
						mCharSize = 4;
						returnToTextMode = true;
						break;
					case 0x13:	// ESC CTRL S: 80 char mode and return to text mode
						mCharSize = 1;
						returnToTextMode = true;
						break;
					case 0x0E:	// ESC CTRL N: 40 char mode and return to text mode
						mCharSize = 2;
						returnToTextMode = true;
						break;
					case 0x17:	// ESC CTRL W: enable international
						mbIntCharsEnabled = true;
						break;
					case 0x18:	// ESC CTRL X: disable international
						mbIntCharsEnabled = false;
						break;

					case 0x1B:	// ESC ESC: Stay in ESC mode (needed by BIOPLOT)
						mLineState = LineState::Escape;
						break;

					default:
						break;
				}

				if (returnToTextMode) {
					// return to text mode
					mbGraphicsMode = false;
					mCommandState = CommandState::PrintText;
					mX = 0;
					mY = 0;
					mBaseX = 0;
					mLineStyle = 0;
				}
				continue;
			}

			case LineState::ProcessLine:
				if (ch == 0x9B) {
					mLineState = LineState::StartOfLine;
					if (!mbGraphicsMode) {
						PrintChar(ch, false);
						continue;
					}
				}
				break;
		}

		switch(mCommandState) {
			case CommandState::WaitForCommand:
				mGraphicsCommand = ch;

				switch(ch) {
					case 'A':	// A		Return to text mode
						// This needs to act as though it takes an argument; it
						// discards everything to asterisk or EOL, and text
						// after asterisk can be printed.
						mArgsLeft = 1;
						mArgCount = 1;
						mCommandState = CommandState::ArgStart;
						break;

					case 'H':
						// return to home position
						if (mpPrinterGraphicalOutput)
							mpPrinterGraphicalOutput->FeedPaper(ConvertPointToMM(0, -mY).y);

						mX = mBaseX;
						mY = 0;
						break;

					case 'I':	// Initialize (set home origin)
						mBaseX = mX;
						mY = 0;
						break;

					case 'S':	// S<0-63>	Set text scale
					case 'L':	// L<0-15>	Set line style
					case 'C':	// C0-3		Set color
					case 'Q':	// Q<0-3>	Set text orientation
						mArgsLeft = 1;
						mArgCount = 1;
						mCommandState = CommandState::ArgStart;
						break;

					case 'D':	// Dx,y[;x,y...]	Draw line to absolute points
					case 'J':	// Jx,y		Draw line to relative points
					case 'R':	// Rx,y		Move to relative point
					case 'M':	// Mx,y		Move to absolute point
						mArgsLeft = 2;
						mArgCount = 2;
						mCommandState = CommandState::ArgStart;
						break;

					case 'X':	// Xa,d,n	Draw axis a (0=Y, 2=X), d distance between marks, n marks
						mArgsLeft = 3;
						mArgCount = 3;
						mCommandState = CommandState::ArgStart;
						break;

					case 'P':	// P<text>	Print text while in graphics mode
						mCommandState = CommandState::PrintText;
						break;
				}
				break;

			case CommandState::PrintText:
				if (ch == 0x1B)
					mCommandState = CommandState::PrintTextEscape;
				else if (ch == 0x9B)
					mCommandState = CommandState::WaitForCommand;
				else
					PrintChar(ch, mbGraphicsMode);
				break;

			case CommandState::PrintTextEscape:
				mCommandState = CommandState::PrintText;

				switch(ch) {
					case 0x17:	// ESC CTRL W: enable international
						mbIntCharsEnabled = true;
						break;

					case 0x18:	// ESC CTRL X: disable international
						mbIntCharsEnabled = false;
						break;

					case 0x9B:	// ESC EOL: end line (ESC cannot splice new lines onto P command)
						mCommandState = CommandState::WaitForCommand;
						break;
				}

				break;


			case CommandState::ArgStart:
				mArg3 = mArg2;
				mArg2 = mArg1;
				mArg1 = 0;
				mArgSign = +1;

				mCommandState = CommandState::ArgStart2;
				[[fallthrough]];
			case CommandState::ArgStart2:
				// The precise behavior of numeric argument parsing is as follows:
				//
				//	- Zero or more spaces, tabs, or minus signs are parsed. Plus signs
				//	  are not allowed.
				//	- Zero or more decimal digits are parsed.
				//
				// Upon reaching an unrecognized character, the remaining characters are
				// skipped until either a comma, semicolon, asterisk, or EOL are reached.
				// If no digits are parsed, the value is 0.
				//
				// Unlike the VIC-1520, there is no special handling for exponential
				// numbers in the Atari 1020. It simply stops parsing at the E. This
				// means that 5E-20 is interpreted as 5.
				//
				// There is no limit on the number of digits that may be specified, but
				// if the value is greater than 999 in magnitude, it is considered invalid
				// and flushed to 0. Therefore, 0000999 is valid, but 1000 is not.
				//
				// All numeric values are parsed this way into a 16-bit signed, two's
				// complement integer. All non-position arguments use the least significant
				// byte.
				//

				if (ch == ' ' || ch == 0x09)
					break;

				// Minus signs may be interspersed with spaces/tabs in any order, they do not have
				// to be adjacent to digits. Also, multiple minus signs are the same as one.
				if (ch == '-') {
					mArgSign = -1;
					break;
				}
				
				mCommandState = CommandState::ArgDigit;
				[[fallthrough]];
			case CommandState::ArgDigit:
				if (ch >= 0x30 && ch < 0x3A) {
					// The 1020 diagnostic test does something goofy: it uses 1000 to mean 0. We need
					// to emulate this or the right half of the dash chart goes wild. Testing on a 1020
					// shows that 0 is substituted for any overflow, it's not wrapped.
					mArg1 = (mArg1 * 10 + (int)(ch - 0x30) * mArgSign);

					if (mArg1 >= -999 && mArg1 <= 999)
						break;

					// overflow -- force to 0 and ignore rest of argument
					mArg1 = 0;
				}

				mCommandState = CommandState::ArgEnd;
				[[fallthrough]];
			case CommandState::ArgEnd: {
				bool isEndOfCommand = false;

				if (ch == 0x9B || ch == '*' || ch == ';' || ch == ',') {
					// EOL, asterisk, semicolon, or comma ends the current argument. These are all
					// treated identically for internal arguments; they only have a distinction when
					// ending the last argument expected by a command. Yes, this means that commands
					// can cross EOLs.
					if (--mArgsLeft) {
						mCommandState = CommandState::ArgStart;
						break;
					}

					if (ch == 0x9B || ch == '*')
						isEndOfCommand = true;
				} else {
					// Any other character -- skip.
					//
					// This is crucial for BASIC being able to dump fp numbers to the plotter
					// and have it work sensibly, as many test programs do. Decimals are a no
					// brainer -- but more subtly, scientific notation needs to be handled
					// too.
					break;
				}

				// Execute the command.
				bool allowContinuation = false;

				switch(mGraphicsCommand) {
					case 'A':	// A		Return to text mode
						mbGraphicsMode = false;
						mX = 0;
						mBaseX = 0;
						mLineStyle = 0;
						break;

					case 'S':	// S<0-63>	Set text scale
						mArg1 &= 0xFF;
						mCharSize = mArg1 >= 64 ? 2 : mArg1 + 1;	// OOB -> S1, not S0
						break;

					case 'L':	// L<0-15>	Set line style
						mArg1 &= 0xFF;
						mLineStyle = mArg1 >= 16 ? 0 : mArg1;
						break;

					case 'C':	// C0-3		Set color
						// Pen colors greater than 3 are forced to 0.
						//
						// On the actual 1020, an LSB >= 132 curiously causes it to begin changing
						// pens endlessly. We currently don't have a way to emulate this until
						// accurate timing (and sound) is implemented.

						mArg1 &= 0xFF;
						mPenIndex = mArg1 > 3 ? 0 : mArg1;
						break;

					case 'Q':	// Q<0-3>	Set text orientation
						mArg1 &= 0xFF;
						mCharRotation = mArg1 > 3 ? 0 : mArg1;
						break;

					case 'D':	// Dx,y[;x,y...]	Draw line to absolute points
						DrawToAbsolute(mBaseX + mArg2, mArg1);
						allowContinuation = true;
						break;

					case 'J':	// Jx,y		Draw line to relative points
						{
							sint32 x2 = mX + mArg2;
							sint32 y2 = mY + mArg1;

							if (x2 != (sint16)x2 || y2 != (sint16)y2) {
								// printer forces reset on signed overflow
								ResetState();
								return;
							}

							DrawToAbsolute(mX + mArg2, mY + mArg1);
						}
						allowContinuation = true;
						break;

					case 'R':	// Rx,y		Move to relative point
						mX += mArg2;
						mY += mArg1;

						if ((sint16)mX != mX || (sint16)mY != mY) {
							// printer forces reset on signed overflow
							ResetState();
							return;
						}

						if (mpPrinterGraphicalOutput)
							mpPrinterGraphicalOutput->FeedPaper(ConvertVectorToMM(0, mArg1).y);

						allowContinuation = true;
						break;

					case 'M':	// Mx,y		Move to absolute point
						if (mY != mArg1) {
							if (mpPrinterGraphicalOutput)
								mpPrinterGraphicalOutput->FeedPaper(ConvertVectorToMM(0, mArg1 - mY).y);
						}

						mX = mBaseX + mArg2;
						mY = mArg1;
						allowContinuation = true;
						break;

					case 'X':	// Xa,b,c	Draw axis with orientation a, tick spacing b, tick count c
						// The 1020 diagnostic test rather abuses the heck out of this command:
						//
						// X0,20,256	(expected to draw one vertical tick at +20 step)
						// X0,1000,-1	(expected to draw one vertical tick)
						// X1,-1000,0	(expected to draw one horizontal tick)
						// X1,-20,12	(expected to draw 12 horizontal ticks at -20 step)
						// X2,20,-256	(expected to draw 1 horizontal tick at +20 step)
						//
						// Testing on actual device reveals:
						// - Values outside of +/-999 treated as 0
						// - Any count outside of 1-255 is treated as 1
						// - Distances of 0 _do_ work

						{
							// Compute normal axis direction.
							//
							// The manual specifies that any non-zero value is interpreted as horizontal
							// axis, and the 1020 diagnostic uses both 1 and 2 for horizontal axis. To be
							// precise, it's actually the LSB non-zero.
							const sint32 dx = mArg3 & 0xFF ? 1 : 0;
							const sint32 dy = 1 - dx;

							// scale by distance
							const sint32 stepx = dx * mArg2;
							const sint32 stepy = dy * mArg2;

							// draw ticks
							//
							sint32 n = mArg1;
							if (n < 1 || n > 255)
								n = 1;

							sint32 x = mX;
							sint32 y = mY;

							const sint32 tickx =  2 * dy;
							const sint32 ticky = -2 * dx;
							for(sint32 i = 0; i < n; ++i) {
								// draw a line segment
								if (!DrawToAbsolute(x + stepx, y + stepy))
									break;

								x += stepx;
								y += stepy;

								// draw a tick
								if (!MoveToAbsolute(x + tickx, y + ticky))
									break;

								if (!DrawToAbsolute(x - tickx, y - ticky))
									break;

								if (!MoveToAbsolute(x, y))
									break;
							}
						}
						break;

					default:
						break;
				}

				// if the command is continuable and the argument was ended with a comma
				// or semicolon, parse the next set of args and repeat the command; otherwise,
				// return to command mode
				if (allowContinuation && !isEndOfCommand) {
					mArgsLeft = mArgCount;
					mCommandState = CommandState::ArgStart;
					break;
				}

				mCommandState = CommandState::WaitForCommand;
				break;
			}
		}

		if (ch == 0x9B)
			break;
	}
}

void ATDevicePrinter1020::ResetState() {
	mLineState = LineState::StartOfLine;
	mCommandState = CommandState::WaitForCommand;
	mbGraphicsMode = false;
	mX = 0;
	mY = 0;
	mBaseX = 0;
	mPenIndex = 0;
	mLineStyle = 0;
	mCharSize = 2;	// 40 col
	mbIntCharsEnabled = false;
}

void ATDevicePrinter1020::PrintChar(uint8 ch, bool graphical) {
	// if international chars are disabled, discard 00-1F and blank 60/7B/7D-7F.
	if (!mbIntCharsEnabled && ch != 0x9B) {
		uint8 ch2 = ch & 0x7F;
		if (ch2 < 0x20)
			return;

		if (ch2 == 0x60 || ch2 == 0x7B || ch2 >= 0x7D)
			ch = 0x20;
	}

	// compute the advance width
	const sint32 size = mCharSize;
	const sint32 advanceWidth = 6;

	// compute transformation matrix
	const sint8 kRotTab[5] { 1, 0, -1, 0, 1 };
	sint32 hx = size*kRotTab[mCharRotation];
	sint32 hy = size*kRotTab[mCharRotation + 1];
	sint32 vx = -hy;
	sint32 vy = hx;

	// Check for EOL, or if there isn't enough width, push EOL. We only do this
	// in text mode because the 1020 diagnostic test draws a gigantic size 64
	// character mid-line such that the right side spacing would extend beyond
	// the right edge, so the assumption is that the P command in graphics mode
	// does not line wrap.
	if (!graphical && (ch == 0x9B || mX + advanceWidth * size > 480)) {
		MoveToAbsolute(mBaseX - vx * 12, -vy * 12);
		mBaseX = mX;
		mY = 0;

		if (ch == 0x9B)
			return;
	}

	// save off baseline
	sint32 bx = mX;
	sint32 by = mY;

	// compute glyph origin at baseline
	sint32 x = bx;
	sint32 y = by;

	// look up character data
	const uint8 *dat = &g_ATPrinterFont1020.mpFontData[g_ATPrinterFont1020.mCharOffsets[ch & 0x7F]];

	if (!(dat[0] & ATPrinterFont1020::kEndBit)) {
		for(;;) {
			const uint8 code = *dat++;
			
			// unpack glyph space point
			sint32 h = (code >> 4) & 7;
			sint32 v = code & 7;

			// transform to absolute space
			sint32 x2 = x + h * hx + v * vx;
			sint32 y2 = y + h * hy + v * vy;

			if (code & ATPrinterFont1020::kMoveBit) {
				if (!MoveToAbsolute(x2, y2))
					break;
			} else {
				if (!DrawToAbsolute(x2, y2))
					break;
			}

			if (code & ATPrinterFont1020::kEndBit)
				break;
		}
	}

	// move to next position
	MoveToAbsolute(bx + advanceWidth * hx, by + advanceWidth * hy);
}

bool ATDevicePrinter1020::MoveToAbsolute(sint32 x, sint32 y) {
	mX = x;

	if (mY != y) {
		if (mpPrinterGraphicalOutput)
			mpPrinterGraphicalOutput->FeedPaper(ConvertVectorToMM(0, y - mY).y);

		mY = y;
	}

	return true;
}

bool ATDevicePrinter1020::DrawToAbsolute(sint32 x, sint32 y) {
	vdfloat2 raw1 { (float)mX, 0.0f };
	vdfloat2 raw2 { (float)x, (float)(y - mY) };
	sint32 stepsLeft = std::max<sint32>(abs(x - mX), abs(y - mY)) + 1;

	mX = x;
	mY = y;

	if (!mpPrinterGraphicalOutput)
		return true;

	// The VIC-1520's dashed line algorithm, which we assume is the same as the Atari 1020's,
	// is as follows:
	//
	// - The line dashing is counted in steps. Since the plotter uses a Bresenham, this
	//   means that the line dashes are counted along the major axis and not along the line
	//   length.
	//
	// - If dashing is off (line style 0), then the pen is always down.
	//
	// - If dashing is on, then every L steps the pen is toggled whenever the counter is 0,
	//   and the counter is reset. Then, the counter is decremented.
	//
	// - Pen up/down occurs before a step.
	//
	// The various plotters seem to differ in line offsets across lines. The VIC-1520 manual
	// shows line patterns consistently starting on, while the 1020 shows them consistently
	// starting off. The CGP-115 appears to carry over the remainder to the next line.

	if (mLineStyle) {
		const vdfloat2 step = (raw2 - raw1) / (float)stepsLeft;
		const int dashLen = mLineStyle;

		while(stepsLeft > 0) {
			stepsLeft -= dashLen;

			if (stepsLeft <= 0) {
				// check if we have an undrawn part at the end -- we still need to feed paper
				sint32 dy = stepsLeft + dashLen;

				if (dy)
					mpPrinterGraphicalOutput->FeedPaper(ConvertVectorFToMM(0.0f, dy * step.y).y);

				break;
			}

			int drawSteps = std::min<sint32>(stepsLeft, dashLen);

			raw1 += step * dashLen;
			raw2 = raw1 + step * drawSteps;

			DrawClippedLine(raw1, raw2);

			raw1 = raw2;
			raw1.y = 0;
			stepsLeft -= dashLen;
		}
	} else {
		DrawClippedLine(raw1, raw2);
	}

	return true;
}

void ATDevicePrinter1020::DrawClippedLine(const vdfloat2& oraw1, const vdfloat2& oraw2) {
	// The 1020 allows lines to be drawn with endpoints outside of the 0-480
	// horizontal range and draws the portion with 0-480. We need to emulate
	// this. Since our default is to draw continuous lines instead of Bresenham
	// lines, we need to clip to the edges. Only horizontal clipping is needed
	// as there is no vertical clipping.
	vdfloat2 raw1 = oraw1;
	vdfloat2 raw2 = oraw2;

	// entry clipping
	const vdfloat2 rawd = raw2 - raw1;

	do {
		if (rawd.x == 0.0f) {
			if (raw1.x < 0.0f || raw1.x > 480.0f)
				break;
		} else {
			float dydx = rawd.y / rawd.x;

			if (raw1.x < 0.0f) {
				if (raw2.x < 0.0f)
					break;

				const float clipx = -raw1.x;
				raw1.y += clipx * dydx;
				raw1.x = 0;
			} else if (raw1.x > 480.0f) {
				if (raw2.x > 480.0f)
					break;

				const float clipx = 480.0f - raw1.x;
				raw1.y += clipx * dydx;
				raw1.x = 480.0f;
			}

			// exit clipping
			if (raw2.x < 0.0f) {
				const float clipx = -raw2.x;

				raw2.y += clipx * dydx;
				raw2.x = 0.0f;
			} else if (raw2.x > 480.0f) {
				const float clipx = 480.0f - raw2.x;

				raw2.y += clipx * dydx;
				raw2.x = 480.0f;
			}
		}

		vdfloat2 pt1 = ConvertPointFToMM(raw1.x, raw1.y);
		vdfloat2 pt2 = ConvertPointFToMM(raw2.x, raw2.y);

		mpPrinterGraphicalOutput->AddVector(pt1, pt2, mPrintPenColors[mPenIndex]);

		if (raw2.y != oraw2.y)
			mpPrinterGraphicalOutput->FeedPaper(ConvertPointFToMM(0.0f, oraw2.y).y - pt2.y);

		return;
	} while(false);

	// We didn't draw anything -- do vertical feed
	mpPrinterGraphicalOutput->FeedPaper(ConvertPointFToMM(0.0f, raw2.y).y);
}

vdfloat2 ATDevicePrinter1020::ConvertPointToMM(sint32 x, sint32 y) const {
	return ConvertPointFToMM((float)x, (float)y);
}

vdfloat2 ATDevicePrinter1020::ConvertPointFToMM(float x, float y) const {
	return vdfloat2 { x * kUnitsToMM + 10.0f, y * -kUnitsToMM };
}

vdfloat2 ATDevicePrinter1020::ConvertVectorToMM(sint32 x, sint32 y) const {
	return ConvertVectorFToMM((float)x, (float)y);
}

vdfloat2 ATDevicePrinter1020::ConvertVectorFToMM(float x, float y) const {
	return vdfloat2 { x * kUnitsToMM, y * -kUnitsToMM };
}

void ATDevicePrinter1020::ReconvertPens() {
	if (mpPrinterGraphicalOutput) {
		const uint32 defaultColors[] {
			g_ATCVDevice1020Pen0,
			g_ATCVDevice1020Pen1,
			g_ATCVDevice1020Pen2,
			g_ATCVDevice1020Pen3,
		};

		for(int i=0; i<4; ++i) {
			mPrintPenColors[i] = mpPrinterGraphicalOutput->ConvertColor(mPenColors[i] >= 0 ? mPenColors[i] : defaultColors[i]);
		}
	}
}
