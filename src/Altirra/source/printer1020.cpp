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
	ATDevicePrinterBase::GetSettings(settings);

	VDStringA name;
	for(int i=0; i<4; ++i) {
		if (mPenColors[i] >= 0) {
			name.sprintf("pencolor%d", i);

			settings.SetUint32(name.c_str(), mPenColors[i]);
		}
	}

	if (mbAccurateBresenham)
		settings.SetBool("accurate_bresenham", true);
}

bool ATDevicePrinter1020::SetSettings(const ATPropertySet& settings) {
	ATDevicePrinterBase::SetSettings(settings);

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

	mbAccurateBresenham = settings.GetBool("accurate_bresenham");

	return true;
}

void ATDevicePrinter1020::Shutdown() {
	if (mpScheduler)
		mpScheduler->UnsetEvent(mpEventDrawingAction);

	ATDevicePrinterBase::Shutdown();
}

void ATDevicePrinter1020::ColdReset() {
	ATDevicePrinterBase::ColdReset();

	ResetState();

	mLastDrawPosX = ConvertPointFToMM(0.0f, 0.0f).x;

	ResetDrawingEngine();
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
	spec.mPageWidthMM = kPaperWidthMM<float>;	// 4.5" wide paper (based on CGP-115 service manual)
	spec.mPageVBorderMM = kMarginMM<float>;		// vertical border
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
				mLineState = LineState::StartOfLine;

				switch(ch) {
					case 0x07:	// ESC CTRL G: graphics mode
						mbGraphicsMode = true;
						mCommandState = CommandState::WaitForCommand;
						break;
					case 0x10:	// ESC CTRL P: 20 char mode and return to text mode
						mCharSize = 4;
						ReturnToTextMode();
						break;
					case 0x13:	// ESC CTRL S: 80 char mode and return to text mode
						mCharSize = 1;
						ReturnToTextMode();
						break;
					case 0x0E:	// ESC CTRL N: 40 char mode and return to text mode
						mCharSize = 2;
						ReturnToTextMode();
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

				continue;
			}

			case LineState::ProcessLine:
				if (ch == 0x9B) {
					mLineState = LineState::StartOfLine;
					if (!mbGraphicsMode) {
						FillFIFO(1);
						PrintChar(ch, false);
						AddDrainFIFOAction(1);
						continue;
					}
				}
				break;
		}

		switch(mCommandState) {
			case CommandState::WaitForCommand:
				mGraphicsCommand = ch;
				mPendingCmdFIFOChars = 1;

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
						AddMoveAction(ConvertPointToMM(mBaseX, -mY));

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
						FillFIFO(1);
						AddDrainFIFOAction(1);
						break;
				}
				break;

			case CommandState::PrintText:
				if (ch == 0x1B)
					mCommandState = CommandState::PrintTextEscape;
				else if (ch == 0x9B)
					mCommandState = CommandState::WaitForCommand;
				else {
					FillFIFO(1);
					PrintChar(ch, mbGraphicsMode);
					AddDrainFIFOAction(1);
				}
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

				if (ch == ' ' || ch == 0x09) {
					++mPendingCmdFIFOChars;
					break;
				}

				// Minus signs may be interspersed with spaces/tabs in any order, they do not have
				// to be adjacent to digits. Also, multiple minus signs are the same as one.
				if (ch == '-') {
					mArgSign = -1;
					++mPendingCmdFIFOChars;
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

					if (mArg1 >= -999 && mArg1 <= 999) {
						++mPendingCmdFIFOChars;
						break;
					}

					// overflow -- force to 0 and ignore rest of argument
					mArg1 = 0;
				}

				mCommandState = CommandState::ArgEnd;
				[[fallthrough]];
			case CommandState::ArgEnd: {
				bool isEndOfCommand = false;

				++mPendingCmdFIFOChars;

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

				// Fill the FIFO by the number of chars sent to the print MCU
				FillFIFO(mPendingCmdFIFOChars);

				// Execute the command.
				bool allowContinuation = false;

				switch(mGraphicsCommand) {
					case 'A':	// A		Return to text mode
						ReturnToTextMode();
						break;

					case 'S':	// S<0-63>	Set text scale
						mArg1 &= 0xFF;
						mCharSize = mArg1 >= 64 ? 2 : mArg1 + 1;	// OOB -> S1, not S0
						break;

					case 'L':	// L<0-15>	Set line style
						mArg1 &= 0xFF;
						mLineStyle = mArg1 >= 16 ? 0 : mArg1;
						break;

					case 'C': {	// C0-3		Set color
						// Pen colors greater than 3 are forced to 0.
						//
						// On the actual 1020, an LSB >= 132 curiously causes it to begin changing
						// pens endlessly. We currently don't have a way to emulate this until
						// accurate timing (and sound) is implemented.

						mArg1 &= 0xFF;

						const uint32 newPenIndex = mArg1 > 3 ? 0 : mArg1;

						// The 1020 needs to return to the left margin and click the head against
						// the left stop in order to rotate the pen carriage; two clicks are needed
						// for each pen change.

						if (mPenIndex != newPenIndex) {
							while(mPenIndex != newPenIndex) {
								AddMoveAction(vddouble2(0, 0));
								AddMoveAction(ConvertPointFToMM(0, 0));
								AddMoveAction(vddouble2(0, 0));
								AddMoveAction(ConvertPointFToMM(0, 0));

								mPenIndex = (mPenIndex + 1) & 3;
								AddPenChangeAction(mPenIndex);
							}

							AddMoveAction(ConvertPointToMM(mX, 0));
						}
						break;
					}

					case 'Q':	// Q<0-3>	Set text orientation
						mArg1 &= 0xFF;
						mCharRotation = mArg1 > 3 ? 0 : mArg1;
						break;

					case 'D':	// Dx,y[;x,y...]	Draw line to absolute points
						DrawToAbsolute(mBaseX + mArg2, mArg1, false);
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

							DrawToAbsolute(mX + mArg2, mY + mArg1, false);
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

						AddMoveAction(ConvertPointToMM(mX, mArg1));

						allowContinuation = true;
						break;

					case 'M':	// Mx,y		Move to absolute point
						if (mX != mBaseX + mArg2 || mY != mArg1) {
							AddMoveAction(ConvertPointToMM(mBaseX + mArg2, mArg1 - mY));
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

							// reset line state to solid -- confirmed on a 1020 that this affects later
							// draws too
							mLineStyle = 0;

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
								DrawToAbsolute(x + stepx, y + stepy, false);

								x += stepx;
								y += stepy;

								// draw a tick
								MoveToAbsolute(x + tickx, y + ticky);

								DrawToAbsolute(x - tickx, y - ticky, false);

								MoveToAbsolute(x, y);
							}
						}
						break;

					default:
						break;
				}

				// insert FIFO fence at this point
				AddDrainFIFOAction(mPendingCmdFIFOChars);
				mPendingCmdFIFOChars = 0;

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

	ProcessNextDrawingAction();
}

void ATDevicePrinter1020::OnScheduledEvent(uint32 id) {
	if (id == kEventId_DrawingAction) {
		mpEventDrawingAction = nullptr;

		ProcessNextDrawingAction();
	}

	return ATDevicePrinterBase::OnScheduledEvent(id);
}

void ATDevicePrinter1020::ResetState() {
	mLineState = LineState::StartOfLine;
	mCommandState = CommandState::WaitForCommand;
	mbGraphicsMode = false;
	mX = 0;
	mY = 0;
	mBaseX = 0;

	mPenIndex = 0;
	AddPenChangeAction(0);

	mLineStyle = 0;
	mCharSize = 2;	// 40 col
	mCharRotation = 0;
	mbIntCharsEnabled = false;

	// do not reset the drawing engine here -- status commands don't cancel
	// drawing in progress
}

void ATDevicePrinter1020::ResetDrawingEngine() {
	EndAsyncPrinting();
	mFIFOLevel = 0;
	mPendingCmdFIFOChars = 0;

	mpScheduler->UnsetEvent(mpEventDrawingAction);
	mNextDrawingActionIndex = 0;
	mDrawingActions.clear();
	mbDrawingActionPending = false;
}

void ATDevicePrinter1020::ReturnToTextMode() {
	mbGraphicsMode = false;
	mCommandState = CommandState::PrintText;
	mX = 0;
	mY = 0;
	mBaseX = 0;
	mLineStyle = 0;

	// print an EOL; the 1020 advances with the current line height
	// after returning to text mode
	PrintChar(0x9B, false);
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
	//
	// Note that the orientation is always left-to-right in text mode.

	if (!graphical && (ch == 0x9B || mX + advanceWidth * size > 480)) {
		// When EOL wrap occurs, the 1020 does a vertical feed first and then
		// return left.
		MoveToAbsolute(mX - vx * 12, -vy * 12);
		mY = 0;
		MoveToAbsolute(mBaseX - vx * 12, 0);
		mBaseX = mX;

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
			sint32 v = (code & 7);

			// transform to absolute space
			sint32 x2 = x + h * hx + v * vx;
			sint32 y2 = y + h * hy + v * vy;

			if (code & ATPrinterFont1020::kMoveBit) {
				MoveToAbsolute(x2, y2);
			} else {
				// The 1020 always draws characters with solid lines, regardless
				// of the line style state. However, it does not reset the line
				// style state when doing so.
				DrawToAbsolute(x2, y2, true);
			}

			if (code & ATPrinterFont1020::kEndBit)
				break;
		}
	}

	// move to next position
	MoveToAbsolute(bx + advanceWidth * hx, by + advanceWidth * hy);
}

void ATDevicePrinter1020::MoveToAbsolute(sint32 x, sint32 y) {
	if (mX != x || mY != y) {
		AddMoveAction(ConvertPointToMM(std::clamp<sint32>(x, 0, 480), y - mY));

		mX = x;
		mY = y;
	}
}

void ATDevicePrinter1020::DrawToAbsolute(sint32 x2, sint32 y2, bool forceSolid) {
	vddouble2 raw1 { (double)mX, 0.0f };
	vddouble2 raw2 { (double)x2, (double)(y2 - mY) };

	sint32 x = mX;
	const sint32 vecx = x2 - mX;
	const sint32 vecy = y2 - mY;

	mX = x2;
	mY = y2;

	// Null draws do not do a pen down on the 1020.
	if (!vecx && !vecy)
		return;

	if (!mpPrinterGraphicalOutput)
		return;

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

	const sint32 dx = abs(vecx);
	const sint32 dy = abs(vecy);
	sint32 stepsLeft = std::max<sint32>(abs(dx), abs(dy)) + 1;

	if (mbAccurateBresenham) {
		// Reverse engineered behavior from the SP-400 firmware:
		//
		// - Discriminant starts as dx >> 1, but dx subtraction occurs before minor axis
		//   check.
		// - Minor axis steps on d < 0.

		const sint32 stepx = vecx < 0 ? -1 : vecx > 0 ? +1 : 0;
		const sint32 stepy = vecy < 0 ? -1 : vecy > 0 ? +1 : 0;
		const bool xMajor = dx >= dy;
		const sint32 dmajor = xMajor ? dx : dy;
		const sint32 dminor = xMajor ? dy : dx;

		sint32 stepCount = xMajor ? dx : dy;
		bool prevInRange = x >= 0 && x <= 480;
		int dashLen = mLineStyle;
		bool penDown = false;

		if (!dashLen) {
			dashLen = 0x7FFFFFFF;
			penDown = true;
		}

		sint32 error = dmajor >> 1;
		sint32 y = 0;
		bool lastPenDown = false;

		while(stepCount-- > 0) {
			sint32 xn = x;
			sint32 yn = y;

			error -= dminor;

			if (error < 0) {
				error += dmajor;

				if (xMajor)
					yn += stepy;
				else
					xn += stepx;
			}

			if (xMajor)
				xn += stepx;
			else
				yn += stepy;

			const bool nextInRange = xn >= 0 && xn <= 480;

			if (prevInRange && nextInRange && penDown) {
				AddMoveAction(ConvertPointFToMM(x, y));

				if (!lastPenDown) {
					lastPenDown = true;
					AddPenDownAction();
				}

				AddDrawAction(ConvertPointFToMM(xn, yn));

				yn = 0;
			} else {
				if (lastPenDown) {
					lastPenDown = false;
					AddPenUpAction();
				}
			}

			if (!--dashLen) {
				dashLen = mLineStyle;
				penDown = !penDown;
			}

			x = xn;
			y = yn;
			prevInRange = nextInRange;
		}

		if (lastPenDown) {
			lastPenDown = false;
			AddPenUpAction();
		}

		if (y)
			AddMoveAction(ConvertPointFToMM(x, y));
	} else if (mLineStyle && !forceSolid) {
		const vddouble2 step = (raw2 - raw1) / (double)stepsLeft;
		const int dashLen = mLineStyle;

		while(stepsLeft > 0) {
			if (stepsLeft <= dashLen)
				break;

			stepsLeft -= dashLen;

			int drawSteps = std::min<sint32>(stepsLeft, dashLen);
			stepsLeft -= drawSteps;

			raw1 += step * dashLen;
			raw2 = raw1 + step * drawSteps;

			DrawClippedLine(raw1, raw2);

			raw1 = raw2;
			raw1.y = 0;
		}

		if (stepsLeft) {
			// check if we have an undrawn part at the end -- we still need to feed paper
			double finalStep = (double)stepsLeft;

			AddMoveAction(ConvertPointFToMM(std::clamp<double>(raw1.x + finalStep * step.x, 0.0, 480.0), finalStep * step.y));
		}
	} else {
		DrawClippedLine(raw1, raw2);
	}
}

void ATDevicePrinter1020::DrawClippedLine(const vddouble2& oraw1, const vddouble2& oraw2) {
	// The 1020 allows lines to be drawn with endpoints outside of the 0-480
	// horizontal range and draws the portion with 0-480. We need to emulate
	// this. Since our default is to draw continuous lines instead of Bresenham
	// lines, we need to clip to the edges. Only horizontal clipping is needed
	// as there is no vertical clipping.
	vddouble2 raw1 = oraw1;
	vddouble2 raw2 = oraw2;

	// entry clipping
	const vddouble2 rawd = raw2 - raw1;

	do {
		if (rawd.x == 0.0) {
			if (raw1.x < 0.0 || raw1.x > 480.0)
				break;
		} else {
			double dydx = rawd.y / rawd.x;

			if (raw1.x < 0.0) {
				if (raw2.x < 0.0)
					break;

				const double clipx = -raw1.x;
				raw1.y += clipx * dydx;
				raw1.x = 0;
			} else if (raw1.x > 480.0) {
				if (raw2.x > 480.0)
					break;

				const double clipx = 480.0f - raw1.x;
				raw1.y += clipx * dydx;
				raw1.x = 480.0;
			}

			// exit clipping
			if (raw2.x < 0.0) {
				const double clipx = -raw2.x;

				raw2.y += clipx * dydx;
				raw2.x = 0.0;
			} else if (raw2.x > 480.0) {
				const double clipx = 480.0 - raw2.x;

				raw2.y += clipx * dydx;
				raw2.x = 480.0;
			}
		}

		vddouble2 pt1 = ConvertPointFToMM(raw1.x, raw1.y);
		vddouble2 pt2 = ConvertPointFToMM(raw2.x, raw2.y - raw1.y);

		AddMoveAction(pt1);

		if (fabs(pt1.x - pt2.x) > 1e-5 || fabs(pt2.y) > 1e-5) {
			AddPenDownAction();
			AddDrawAction(pt2);
			AddPenUpAction();
		}

		if (raw2.y != oraw2.y)
			AddMoveAction(ConvertPointFToMM(raw2.x, oraw2.y) - vddouble2(0, pt2.y));

		return;
	} while(false);

	// We didn't draw anything -- do vertical feed
	AddMoveAction(ConvertPointFToMM(std::clamp<double>(raw2.x, 0, 480), raw2.y));
}

vddouble2 ATDevicePrinter1020::ConvertPointToMM(sint32 x, sint32 y) const {
	return ConvertPointFToMM((double)x, (double)y);
}

vddouble2 ATDevicePrinter1020::ConvertPointFToMM(double x, double y) const {
	return vddouble2 { x * kUnitsToMM<double> + kMarginMM<double>, y * -kUnitsToMM<double> };
}

vddouble2 ATDevicePrinter1020::ConvertVectorToMM(sint32 x, sint32 y) const {
	return ConvertVectorFToMM((double)x, (double)y);
}

vddouble2 ATDevicePrinter1020::ConvertVectorFToMM(double x, double y) const {
	return vddouble2 { x * kUnitsToMM<double>, y * -kUnitsToMM<double> };
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

void ATDevicePrinter1020::FillFIFO(uint32 numChars) {
	if (!numChars)
		return;

	if (mFIFOLevel <= kFIFOThreshold && mFIFOLevel + numChars > kFIFOThreshold)
		BeginAsyncPrinting();

	mFIFOLevel += numChars;

	if (mFIFOLevel > 1000) {
		VDFAIL("1020 FIFO possible overflow");
	}
}

void ATDevicePrinter1020::DrainFIFO(uint32 numChars) {
	if (!numChars)
		return;

	if (numChars > mFIFOLevel) {
		VDFAIL("1020 FIFO underflow");
		numChars = mFIFOLevel;
	}

	if (mFIFOLevel > kFIFOThreshold && mFIFOLevel <= kFIFOThreshold + numChars)
		EndAsyncPrinting();

	mFIFOLevel -= numChars;
}

void ATDevicePrinter1020::AddDrainFIFOAction(uint32 numChars) {
	if (!numChars)
		return;

	DrawingAction& action = AddAction();

	action.mType = DrawingActionType::DrainFIFO;
	action.mLength = numChars;
}

void ATDevicePrinter1020::AddWaitAction(uint32 cycles) {
	if (!cycles)
		return;

	DrawingAction& action = AddAction();

	action.mType = DrawingActionType::Wait;
	action.mLength = cycles;
}

void ATDevicePrinter1020::AddMoveAction(const vddouble2& rawPos) {
	DrawingAction& action = AddAction();

	action.mType = DrawingActionType::Move;
	action.mDstPoint = rawPos;
}

void ATDevicePrinter1020::AddDrawAction(const vddouble2& rawPos) {
	DrawingAction& action = AddAction();

	action.mType = DrawingActionType::Draw;
	action.mDstPoint = rawPos;
	action.mPenIndex = (uint8)mPenIndex;
}

void ATDevicePrinter1020::AddPenDownAction() {
	if (!mbSoundEnabled || !mbAccurateTimingEnabled)
		return;

	DrawingAction& action = AddAction();

	action.mType = DrawingActionType::PenDown;
}

void ATDevicePrinter1020::AddPenUpAction() {
	if (!mbSoundEnabled || !mbAccurateTimingEnabled)
		return;

	DrawingAction& action = AddAction();

	action.mType = DrawingActionType::PenUp;
}

void ATDevicePrinter1020::AddPenChangeAction(uint32 penIndex) {
	DrawingAction& action = AddAction();

	action.mType = DrawingActionType::PenChange;
	action.mPenIndex = (uint8)penIndex;
}

ATDevicePrinter1020::DrawingAction& ATDevicePrinter1020::AddAction() {
	if (!mpEventDrawingAction)
		mpEventDrawingAction = mpScheduler->AddEvent(1, this, kEventId_DrawingAction);

	const size_t n = mDrawingActions.size();
	if (mNextDrawingActionIndex >= n || (mNextDrawingActionIndex >= 16 && n >= 2*mNextDrawingActionIndex)) {
		mDrawingActions.erase(mDrawingActions.begin(), mDrawingActions.begin() + mNextDrawingActionIndex);
		mNextDrawingActionIndex = 0;
	}

	return mDrawingActions.emplace_back();
}

void ATDevicePrinter1020::ProcessNextDrawingAction() {
	if (mpEventDrawingAction)
		return;

	while(mNextDrawingActionIndex < mDrawingActions.size()) {
		DrawingAction& action = mDrawingActions[mNextDrawingActionIndex];

		if (mbDrawingActionPending) {
			if (mpPrinterGraphicalOutput) {
				switch(action.mType) {
					case DrawingActionType::Wait:
						break;

					case DrawingActionType::Move:
					case DrawingActionType::Draw: {
						if (!mbAccurateTimingEnabled) {
							mLastDrawPosX += mDrawMoveVec.x;

							if (mpPrinterGraphicalOutput)
								mpPrinterGraphicalOutput->MoveVector(vddouble2(mLastDrawPosX, mDrawMoveVec.y));
						} else {
							// if there were span cycles, queue a move
							if (mDrawCurrentSpanCycles > 0) {
								// compute movement for this span
								const vddouble2 spanVec = mDrawMoveVec * ((double)mDrawCurrentSpanCycles / (double)mDrawTotalCycles);

								// update tracked horizontal position
								mLastDrawPosX += spanVec.x;

								// move head
								if (mpPrinterGraphicalOutput)
									mpPrinterGraphicalOutput->MoveVector(vddouble2(mLastDrawPosX, spanVec.y));

								// update cycle accounting
								mDrawRemainingCycles -= mDrawCurrentSpanCycles;
								mDrawCurrentSpanCycles = 0;
							}

							// check if there are cycles left
							if (mDrawRemainingCycles > 0) {
								// queue update for next span
								const sint32 nextSpanCycles = std::min<sint32>(mDrawRemainingCycles, 20000);

								mDrawCurrentSpanCycles = nextSpanCycles;

								mpScheduler->SetEvent(nextSpanCycles, this, kEventId_DrawingAction, mpEventDrawingAction);
								return;
							}
						}

						// force X position to ensure accurate alignment
						mLastDrawPosX = action.mDstPoint.x;

						if (mpPrinterGraphicalOutput) {
							mpPrinterGraphicalOutput->MoveVector(vddouble2(mLastDrawPosX, 0.0f));

							if (action.mType == DrawingActionType::Draw) {
								mpPrinterGraphicalOutput->AddVector(
									vddouble2(mLastDrawStartPosX, -mDrawMoveVec.y),
									vddouble2(mLastDrawPosX, 0),
									mPrintPenColors[action.mPenIndex]
								);
							}
						}
						mbDrawingActionPending = false;

						break;
					}

					default:
						VDFAIL("Invalid drawing action type for timed evaluation");
						mbDrawingActionPending = false;
						break;
				}
			}

			if (!mbDrawingActionPending)
				++mNextDrawingActionIndex;
			continue;
		} else {
			switch(action.mType) {
				case DrawingActionType::Wait:
					if (mbAccurateTimingEnabled)
						mpScheduler->SetEvent(action.mLength, this, kEventId_DrawingAction, mpEventDrawingAction);
					break;

				case DrawingActionType::Move:
				case DrawingActionType::Draw: {
					// compute delta vector to move
					vddouble2 deltaVec { action.mDstPoint.x - mLastDrawPosX, action.mDstPoint.y };

					mLastDrawStartPosX = mLastDrawPosX;
					mDrawMoveVec = deltaVec;
					mDrawCurrentSpanCycles = 0;

					// compute major axis distance, since the Bresenham does diagonal steps
					const double lineDistance = std::max(fabs(deltaVec.x), fabs(deltaVec.y));

					// check if any real movement is occurring
					if (lineDistance < 1e-5) {
						++mNextDrawingActionIndex;
						break;
					}

					// play sound if head direction has reversed
					if (deltaVec.x != 0) {
						if (mbSoundEnabled && mbAccurateTimingEnabled && mLastHeadDirection * deltaVec.x < 0)
							mPrinterSoundSource.ScheduleSound(kATAudioSampleId_Printer1020HeadReverse, false, 0, 0, 1.0f);

						mLastHeadDirection = deltaVec.x;
					}

					// compute cycles to move
					const float cyclesF = lineDistance * 20000.0f;
					const sint32 cycles = VDRoundToInt32(cyclesF);

					if (cycles > 0 && mbSoundEnabled && mbAccurateTimingEnabled) {
						const double cyclesToSecs = mpScheduler->GetRate().AsInverseDouble();

						if (deltaVec.x != 0)
							mPrinterSoundSource.ScheduleSound(kATAudioSampleId_Printer1020HeadMove, true, 0, cycles * cyclesToSecs, 1.0f);

						if (deltaVec.y != 0)
							mPrinterSoundSource.ScheduleSound(kATAudioSampleId_Printer1020PaperFeed, true, 0, cycles * cyclesToSecs, 1.0f);
					}

					mDrawTotalCycles = cycles;
					mDrawRemainingCycles = cycles;
					mbDrawingActionPending = true;
					break;
				}

				case DrawingActionType::PenDown:
					if (mbSoundEnabled && mbAccurateTimingEnabled)
						mPrinterSoundSource.ScheduleSound(kATAudioSampleId_Printer1020PenDown, false, 0, 0, 1.0f);

					++mNextDrawingActionIndex;
					break;

				case DrawingActionType::PenUp:
					if (mbSoundEnabled && mbAccurateTimingEnabled)
						mPrinterSoundSource.ScheduleSound(kATAudioSampleId_Printer1020PenUp, false, 0, 0, 1.0f);

					++mNextDrawingActionIndex;
					break;

				case DrawingActionType::PenChange:
					if (mpPrinterGraphicalOutput)
						mpPrinterGraphicalOutput->ChangePenColor(mPrintPenColors[action.mPenIndex]);

					++mNextDrawingActionIndex;
					break;

				case DrawingActionType::DrainFIFO:
					DrainFIFO(action.mLength);
					++mNextDrawingActionIndex;
					break;

				default:
					VDFAIL("Invalid drawing action type");
					++mNextDrawingActionIndex;
					break;

			}
		}

		if (mpEventDrawingAction)
			return;
	}

	// There are no more drawing actions, so the print MCU is idle. This means
	// that the FIFO level should be empty. If it isn't, we screwed up the
	// accounting.
	if (mFIFOLevel) {
		VDFAIL("FIFO level should be zero when the printer is idle.");

		mFIFOLevel = 0;
	}
}
