//	AltirraSDL - Dear ImGui debugger history pane (formatting)
//	Line text formatting, timestamp decoding, register formatting,
//	preview node, copy-to-clipboard.

#include <stdafx.h>
#include <SDL3/SDL.h>
#include <vd2/system/vdtypes.h>
#include <vd2/system/VDString.h>
#include <vd2/system/unknown.h>
#include <at/atcore/wraptime.h>
#include <at/atcpu/history.h>
#include <at/atcpu/execstate.h>
#include <at/atdebugger/historytree.h>
#include <at/atdebugger/target.h>
#include <at/atio/cassetteimage.h>
#include "ui_dbg_history.h"
#include "debugger.h"
#include "disasm.h"
#include "simulator.h"
#include "cassette.h"
#include "cpu.h"

extern ATSimulator g_sim;

// =========================================================================
// Fast formatting helpers (from Windows uihistoryview.cpp)
// =========================================================================

namespace {
	const char kHexDig[] = "0123456789ABCDEF";

	void FastFormat02X(char *s, uint8 v) {
		s[0] = kHexDig[v >> 4];
		s[1] = kHexDig[v & 15];
	}

	void FastFormat3U(char *s, uint32 v) {
		if (v < 10) {
			s[0] = ' ';
			s[1] = ' ';
			s[2] = (char)('0' + v);
		} else {
			uint32 d0 = v % 10; v /= 10;
			uint32 d1 = v % 10; v /= 10;

			if (v)
				s[0] = (char)('0' + v);
			else
				s[0] = ' ';

			s[1] = (char)('0' + d1);
			s[2] = (char)('0' + d0);
		}
	}

	int FastFormat10D(char *s, uint32 v) {
		for (int i = 9; i >= 0; --i) {
			s[i] = (char)('0' + (v % 10));
			v /= 10;

			if (!v)
				return i;
		}

		return 0;
	}
}

// =========================================================================
// History entry access
// =========================================================================

bool ATImGuiHistoryPaneImpl::GetLineHistoryIndex(const ATHTLineIterator& it, uint32& index) const {
	if (it.mpNode->mNodeType != kATHTNodeType_Insn)
		return false;

	if (it.mLineIndex >= it.mpNode->mVisibleLines)
		return false;

	if (it.mpNode->mbFiltered)
		index = mFilteredInsnLookup[it.mpNode->mInsn.mOffset + it.mLineIndex];
	else
		index = it.mpNode->mInsn.mOffset + it.mLineIndex;

	return true;
}

const ATCPUHistoryEntry *ATImGuiHistoryPaneImpl::GetLineHistoryEntry(const ATHTLineIterator& it) const {
	ATHTNode *node = it.mpNode;

	if (!node)
		return nullptr;

	switch (node->mNodeType) {
		case kATHTNodeType_Insn:
		case kATHTNodeType_Interrupt:
			if (it.mLineIndex >= node->mVisibleLines)
				return nullptr;

			if (node->mbFiltered)
				return &mInsnBuffer[mFilteredInsnLookup[node->mInsn.mOffset + it.mLineIndex]];

			return &mInsnBuffer[node->mInsn.mOffset + it.mLineIndex];

		case kATHTNodeType_InsnPreview:
			return &mPreviewNodeHEnt;

		default:
			return nullptr;
	}
}

const ATCPUHistoryEntry *ATImGuiHistoryPaneImpl::GetSelectedLineHistoryEntry() const {
	return GetLineHistoryEntry(mSelectedLine);
}

uint32 ATImGuiHistoryPaneImpl::GetHistoryAddress(const ATCPUHistoryEntry& he) const {
	uint32 addr = he.mPC;

	if ((bool)mbShowGlobalPCAddress) {
		if (mDisasmMode == kATDebugDisasmMode_6502 || mDisasmMode == kATDebugDisasmMode_65C02)
			addr += he.mGlobalPCBase;
		else if (mDisasmMode == kATDebugDisasmMode_65C816)
			addr += (uint32)he.mK << 16;
	}

	return addr;
}

// =========================================================================
// Timestamp model
// =========================================================================

double ATImGuiHistoryPaneImpl::DecodeTapeSample(uint32 cycle) {
	auto& cassette = g_sim.GetCassette();
	const bool isRunning = cassette.IsMotorRunning();

	auto& sch = *g_sim.GetScheduler();
	uint32 referenceCycle = isRunning ? sch.GetTick() : (uint32)cassette.GetLastStopCycle();
	double referencePos = isRunning ? cassette.GetSamplePos() : cassette.GetLastStopSamplePos();
	sint32 referenceOffset = isRunning ? cassette.GetSampleCycleOffset() : 0;

	if (!isRunning && ATWrapTime{cycle} >= referenceCycle)
		return referencePos;

	const sint32 tsDelta = (sint32)(cycle - sch.GetTick());

	return referencePos + ((double)(tsDelta + referenceOffset)) / (double)kATCassetteCyclesPerDataSample;
}

double ATImGuiHistoryPaneImpl::DecodeTapeSeconds(uint32 cycle) {
	auto& cassette = g_sim.GetCassette();
	const bool isRunning = cassette.IsMotorRunning();

	auto& sch = *g_sim.GetScheduler();
	uint32 referenceCycle = isRunning ? sch.GetTick() : (uint32)cassette.GetLastStopCycle();
	double referencePos = (isRunning ? cassette.GetSamplePos() : cassette.GetLastStopSamplePos()) / (double)kATCassetteDataSampleRate;
	sint32 referenceOffset = isRunning ? cassette.GetSampleCycleOffset() : 0;

	if (!isRunning && ATWrapTime{cycle} >= referenceCycle)
		return referencePos;

	const sint32 tsDelta = (sint32)(cycle - referenceCycle);

	return referencePos + (double)(tsDelta + referenceOffset) / (kATCassetteDataSampleRateD * kATCassetteCyclesPerDataSample);
}

uint32 ATImGuiHistoryPaneImpl::ConvertRawTimestamp(uint32 rawCycle) {
	IATDebugTargetHistory *history = vdpoly_cast<IATDebugTargetHistory *>(ATGetDebugger()->GetTarget());
	return history ? history->ConvertRawTimestamp(rawCycle) : rawCycle;
}

float ATImGuiHistoryPaneImpl::ConvertRawTimestampDeltaF(sint32 rawCycleDelta) {
	IATDebugTargetHistory *history = vdpoly_cast<IATDebugTargetHistory *>(ATGetDebugger()->GetTarget());
	if (!history)
		return (float)rawCycleDelta;
	return (float)((double)rawCycleDelta / history->GetTimestampFrequency());
}

ATCPUBeamPosition ATImGuiHistoryPaneImpl::DecodeBeamPosition(uint32 cycle) {
	return g_sim.GetTimestampDecoder().GetBeamPosition(cycle);
}

bool ATImGuiHistoryPaneImpl::IsInterruptPositionVBI(uint32 cycle) {
	return g_sim.GetTimestampDecoder().IsInterruptPositionVBI(cycle);
}

// =========================================================================
// Preview node
// =========================================================================

bool ATImGuiHistoryPaneImpl::UpdatePreviewNode(ATCPUHistoryEntry& he) {
	IATDebugger *dbg = ATGetDebugger();
	if (!dbg)
		return false;

	IATDebugTarget *target = dbg->GetTarget();
	if (!target)
		return false;

	const auto dmode = target->GetDisasmMode();

	ATCPUExecState state;
	target->GetExecState(state);

	const bool isZ80 = dmode == kATDebugDisasmMode_Z80;
	const bool is6809 = dmode == kATDebugDisasmMode_6809;
	const bool is65xx = !isZ80 && !is6809;

	if (!(is65xx ? state.m6502.mbAtInsnStep : (isZ80 ? state.mZ80.mbAtInsnStep : state.m6809.mbAtInsnStep)))
		return false;

	ATCPUHistoryEntry hentLast {};

	if (is65xx) {
		const ATCPUExecState6502& s = state.m6502;

		hentLast.mA = s.mA;
		hentLast.mX = s.mX;
		hentLast.mY = s.mY;
		hentLast.mS = s.mS;
		hentLast.mPC = s.mPC;
		hentLast.mP = s.mP;
		hentLast.mbEmulation = s.mbEmulationFlag;
		hentLast.mExt.mSH = s.mSH;
		hentLast.mExt.mAH = s.mAH;
		hentLast.mExt.mXH = s.mXH;
		hentLast.mExt.mYH = s.mYH;
		hentLast.mB = s.mB;
		hentLast.mK = s.mK;
		hentLast.mD = s.mDP;
		uint32 addr24 = (uint32)s.mPC + ((uint32)s.mK << 16);
		hentLast.mOpcode[0] = target->DebugReadByte(addr24);
		hentLast.mOpcode[1] = target->DebugReadByte((addr24 + 1) & 0xffffff);
		hentLast.mOpcode[2] = target->DebugReadByte((addr24 + 2) & 0xffffff);
		hentLast.mOpcode[3] = target->DebugReadByte((addr24 + 3) & 0xffffff);
	} else if (isZ80) {
		const ATCPUExecStateZ80& s = state.mZ80;

		hentLast.mZ80_A = s.mA;
		hentLast.mZ80_F = s.mF;
		hentLast.mZ80_B = s.mB;
		hentLast.mZ80_C = s.mC;
		hentLast.mZ80_D = s.mD;
		hentLast.mExt.mZ80_E = s.mE;
		hentLast.mExt.mZ80_H = s.mH;
		hentLast.mExt.mZ80_L = s.mL;
		hentLast.mZ80_SP = s.mSP;
		hentLast.mPC = s.mPC;
		hentLast.mbEmulation = true;
		hentLast.mB = 0;
		hentLast.mK = 0;
		hentLast.mD = 0;

		uint32 addr16 = s.mPC;
		hentLast.mOpcode[0] = target->DebugReadByte(addr16);
		hentLast.mOpcode[1] = target->DebugReadByte((addr16 + 1) & 0xffff);
		hentLast.mOpcode[2] = target->DebugReadByte((addr16 + 2) & 0xffff);
		hentLast.mOpcode[3] = target->DebugReadByte((addr16 + 3) & 0xffff);
	} else {
		// 6809
		const ATCPUExecState6809& s = state.m6809;

		hentLast.mA = s.mA;
		hentLast.mExt.mAH = s.mB;
		hentLast.mExt.mXH = (uint8)(s.mX >> 8);
		hentLast.mX = (uint8)s.mX;
		hentLast.mExt.mYH = (uint8)(s.mY >> 8);
		hentLast.mY = (uint8)s.mY;
		hentLast.mExt.mSH = (uint8)(s.mS >> 8);
		hentLast.mS = (uint8)s.mS;
		hentLast.mD = s.mU;
		hentLast.mK = s.mDP;
		hentLast.mP = s.mCC;
		hentLast.mPC = s.mPC;
		hentLast.mbEmulation = true;

		uint32 addr16 = s.mPC;
		hentLast.mOpcode[0] = target->DebugReadByte(addr16);
		hentLast.mOpcode[1] = target->DebugReadByte((addr16 + 1) & 0xffff);
		hentLast.mOpcode[2] = target->DebugReadByte((addr16 + 2) & 0xffff);
		hentLast.mOpcode[3] = target->DebugReadByte((addr16 + 3) & 0xffff);
	}

	he = hentLast;
	return true;
}

// =========================================================================
// GetLineText -- faithful port of ATUIHistoryView::GetLineText()
// =========================================================================

const char *ATImGuiHistoryPaneImpl::GetLineText(const ATHTLineIterator& it) {
	ATHTNode *node = it.mpNode;
	const char *s = nullptr;

	switch (node->mNodeType) {
		case kATHTNodeType_Repeat:
			if (node->mRepeat.mSize == 1)
				mTempLine.sprintf("Last insn repeated %u times", node->mRepeat.mCount);
			else
				mTempLine.sprintf("Last %u insns repeated %u times", node->mRepeat.mSize, node->mRepeat.mCount);
			s = mTempLine.c_str();
			break;

		case kATHTNodeType_Interrupt:
			{
				const ATCPUHistoryEntry *hent = GetLineHistoryEntry(it);
				if (!hent) {
					s = "??? interrupt";
					break;
				}

				uint32 ts = ConvertRawTimestamp(hent->mCycle);

				if (!mbDecodeAnticNMI) {
					if (hent->mbNMI)
						mTempLine = hent->mbIRQ ? "FIRQ interrupt" : "NMI interrupt";
					else
						mTempLine = "IRQ interrupt";
				} else {
					mTempLine = hent->mbNMI ? (IsInterruptPositionVBI(ts) ? "NMI interrupt (VBI)" : "NMI interrupt (DLI)") : "IRQ interrupt";
				}
				s = mTempLine.c_str();
			}
			break;

		case kATHTNodeType_Insn:
		case kATHTNodeType_InsnPreview:
			{
				const bool is65C816 = mDisasmMode == kATDebugDisasmMode_65C816;
				const ATCPUHistoryEntry *hent = GetLineHistoryEntry(it);
				if (!hent) {
					s = "???";
					break;
				}

				// Timestamp prefix
				if (node->mNodeType == kATHTNodeType_Insn) {
					switch (mTimestampMode) {
						case HistTimestampMode::Beam: {
							uint32 ts = ConvertRawTimestamp(hent->mCycle);
							const auto beamPos = DecodeBeamPosition(ts);

							if (mSubCycles >= 10) {
								mTempLine.sprintf("%3d:%3d:%3d.%2u | "
									, beamPos.mFrame, beamPos.mY, beamPos.mX, hent->mSubCycle);
							} else if (mSubCycles > 1) {
								mTempLine.sprintf("%3d:%3d:%3d.%u | "
									, beamPos.mFrame, beamPos.mY, beamPos.mX, hent->mSubCycle);
							} else {
								char tsbuf[] = "0000000000:  0:  0 | ";

								const int offset = FastFormat10D(tsbuf, beamPos.mFrame);
								FastFormat3U(tsbuf + 11, beamPos.mY);
								FastFormat3U(tsbuf + 15, beamPos.mX);

								mTempLine.assign(tsbuf + offset, tsbuf + sizeof(tsbuf) - 1);
							}
							break;
						}

						case HistTimestampMode::Microseconds:
							mTempLine.sprintf("T%+.6f | ", ConvertRawTimestampDeltaF((sint32)(hent->mCycle - mTimeBaseCycles)));
							break;

						case HistTimestampMode::Cycles:
							mTempLine.sprintf("T%+-4d | ", (sint32)(hent->mCycle - mTimeBaseCycles));
							break;

						case HistTimestampMode::UnhaltedCycles:
							mTempLine.sprintf("T%+-4d | ", (sint32)(hent->mUnhaltedCycle - mTimeBaseUnhaltedCycles));
							break;

						case HistTimestampMode::TapePositionSeconds:
						case HistTimestampMode::TapePositionSamples: {
							const uint32 ts = ConvertRawTimestamp(hent->mCycle);

							if (mTimestampMode == HistTimestampMode::TapePositionSeconds) {
								const double tapeSecs = DecodeTapeSeconds(ts);

								if (tapeSecs >= 0)
									mTempLine.sprintf("%8.5f | ", tapeSecs);
								else
									mTempLine = "N/A      | ";
							} else {
								const double tapeSample = DecodeTapeSample(ts);

								if (tapeSample >= 0)
									mTempLine.sprintf("%8.1f | ", tapeSample);
								else
									mTempLine = "N/A      | ";
							}
							break;
						}
					}
				} else {
					// Preview "NEXT" prefix
					uint32 padToColumn = 5;

					mTempLine = "NEXT ";

					switch (mTimestampMode) {
						case HistTimestampMode::Beam: {
							uint32 ts = ConvertRawTimestamp(hent->mCycle);
							const auto beamPos = DecodeBeamPosition(ts);

							if (mSubCycles >= 10)
								padToColumn = 14;
							else if (mSubCycles > 1)
								padToColumn = 13;
							else
								padToColumn = 12;

							if (beamPos.mFrame >= 1000000)
								padToColumn += 4;
							else if (beamPos.mFrame >= 100000)
								padToColumn += 3;
							else if (beamPos.mFrame >= 10000)
								padToColumn += 2;
							else if (beamPos.mFrame >= 1000)
								padToColumn += 1;
							break;
						}

						case HistTimestampMode::Microseconds:
							padToColumn = 8;
							break;

						case HistTimestampMode::Cycles:
						case HistTimestampMode::UnhaltedCycles:
							padToColumn = 6;
							break;

						case HistTimestampMode::TapePositionSeconds:
						case HistTimestampMode::TapePositionSamples:
							padToColumn = 9;
							break;
					}

					if (mTempLine.size() < padToColumn)
						mTempLine.resize(padToColumn, ' ');

					mTempLine += "| ";
				}

				// Registers
				if ((bool)mbShowRegisters) {
					if (mDisasmMode == kATDebugDisasmMode_8048) {
						mTempLine.append_sprintf("A=%02X PSW=%02X R0=%02X R1=%02X"
							, hent->mA, hent->mP, hent->mExt.m8048_R0, hent->mExt.m8048_R1);

						if ((bool)mbShowSpecialRegisters) {
							mTempLine.append_sprintf(" P1=%02X P2=%02X", hent->m8048_P1, hent->m8048_P2);
						}
					} else if (mDisasmMode == kATDebugDisasmMode_8051) {
						mTempLine.append_sprintf("A=%02X PSW=%02X R0=%02X R1=%02X"
							, hent->mA, hent->mP, hent->mExt.m8051_R0, hent->mExt.m8051_R1);

						if ((bool)mbShowSpecialRegisters) {
							mTempLine.append_sprintf(" R2=%02X R3=%02X R4=%02X R5=%02X DPTR=%04X P1=%02X P2=%02X SP=%02X"
								, hent->mExt.m8051_R2, hent->mExt.m8051_R3
								, hent->m8051_R4, hent->m8051_R5
								, hent->m8051_DPTR
								, hent->m8051_P1, hent->m8051_P2
								, hent->mS);
						}
					} else if (mDisasmMode == kATDebugDisasmMode_Z80) {
						mTempLine.append_sprintf("A=%02X BC=%02X%02X DE=%02X%02X HL=%02X%02X"
							, hent->mZ80_A
							, hent->mZ80_B, hent->mZ80_C
							, hent->mZ80_D, hent->mExt.mZ80_E
							, hent->mExt.mZ80_H, hent->mExt.mZ80_L);

						if ((bool)mbShowSpecialRegisters) {
							mTempLine.append_sprintf(" SP=%04X F=%02X", hent->mZ80_SP, hent->mZ80_F);
						}
					} else if (mDisasmMode == kATDebugDisasmMode_6809) {
						mTempLine.append_sprintf("A=%02X B=%02X X=%02X%02X Y=%02X%02X"
							, hent->mA, hent->mExt.mAH
							, hent->mExt.mXH, hent->mX
							, hent->mExt.mYH, hent->mY);

						if ((bool)mbShowSpecialRegisters) {
							mTempLine.append_sprintf(" U=%02X%02X S=%02X%02X DP=%02X CC=%02X"
								, (uint8)(hent->mD >> 8), (uint8)hent->mD
								, hent->mExt.mSH, hent->mS
								, hent->mK, hent->mP);
						}
					} else if (is65C816) {
						if (!hent->mbEmulation) {
							if (hent->mP & 0x20) {  // AT6502::kFlagM
								mTempLine.append_sprintf("C=%02X%02X", hent->mExt.mAH, hent->mA);
							} else {
								mTempLine.append_sprintf("A=%02X%02X", hent->mExt.mAH, hent->mA);
							}

							if (hent->mP & 0x10) {  // AT6502::kFlagX
								mTempLine.append_sprintf(" X=--%02X Y=--%02X", hent->mX, hent->mY);
							} else {
								mTempLine.append_sprintf(" X=%02X%02X Y=%02X%02X"
									, hent->mExt.mXH, hent->mX
									, hent->mExt.mYH, hent->mY);
							}

							if ((bool)mbShowSpecialRegisters) {
								mTempLine.append_sprintf(" S=%02X%02X B=%02X D=%04X P=%02X"
									, hent->mExt.mSH, hent->mS
									, hent->mB, hent->mD, hent->mP);
							}
						} else {
							mTempLine.append_sprintf("A=%02X:%02X X=%02X Y=%02X"
								, hent->mExt.mAH, hent->mA, hent->mX, hent->mY);

							if ((bool)mbShowSpecialRegisters) {
								mTempLine.append_sprintf(" S=%02X B=%02X D=%04X P=%02X"
									, hent->mS, hent->mB, hent->mD, hent->mP);
							}
						}
					} else {
						// Standard 6502/65C02
						char axybuf[] = "A=-- X=-- Y=-- S=-- P=--";

						FastFormat02X(axybuf + 2, hent->mA);
						FastFormat02X(axybuf + 7, hent->mX);
						FastFormat02X(axybuf + 12, hent->mY);

						if (!(bool)mbShowSpecialRegisters) {
							mTempLine.append(axybuf, axybuf + 14);
						} else {
							FastFormat02X(axybuf + 17, hent->mS);
							FastFormat02X(axybuf + 22, hent->mP);
							mTempLine.append(axybuf, axybuf + 24);
						}
					}
				}

				// Flags
				if ((bool)mbShowFlags) {
					if (mDisasmMode == kATDebugDisasmMode_8048) {
						mTempLine.append_sprintf(" (%c%c%c/RB%c)"
							, (hent->mP & 0x80) ? 'C' : ' '
							, (hent->mP & 0x40) ? 'A' : ' '
							, (hent->mP & 0x20) ? 'F' : ' '
							, (hent->mP & 0x10) ? '1' : '0');
					} else if (mDisasmMode == kATDebugDisasmMode_Z80) {
						mTempLine.append_sprintf(" (%c%c-%c-%c%c%c)"
							, (hent->mZ80_F & 0x80) ? 'S' : ' '
							, (hent->mZ80_F & 0x40) ? 'Z' : ' '
							, (hent->mZ80_F & 0x10) ? 'H' : ' '
							, (hent->mZ80_F & 0x04) ? 'P' : ' '
							, (hent->mZ80_F & 0x02) ? 'N' : ' '
							, (hent->mZ80_F & 0x01) ? 'C' : ' ');
					} else if (mDisasmMode == kATDebugDisasmMode_6809) {
						mTempLine.append_sprintf(" (%c%c%c%c%c%c%c%c)"
							, (hent->mP & 0x80) ? 'E' : ' '
							, (hent->mP & 0x40) ? 'F' : ' '
							, (hent->mP & 0x20) ? 'H' : ' '
							, (hent->mP & 0x10) ? 'I' : ' '
							, (hent->mP & 0x08) ? 'N' : ' '
							, (hent->mP & 0x04) ? 'Z' : ' '
							, (hent->mP & 0x02) ? 'V' : ' '
							, (hent->mP & 0x01) ? 'C' : ' ');
					} else if (is65C816 && !hent->mbEmulation) {
						mTempLine.append_sprintf(" (%c%c%c%c%c%c%c%c)"
							, (hent->mP & 0x80) ? 'N' : ' '
							, (hent->mP & 0x40) ? 'V' : ' '
							, (hent->mP & 0x20) ? 'M' : ' '
							, (hent->mP & 0x10) ? 'X' : ' '
							, (hent->mP & 0x08) ? 'D' : ' '
							, (hent->mP & 0x04) ? 'I' : ' '
							, (hent->mP & 0x02) ? 'Z' : ' '
							, (hent->mP & 0x01) ? 'C' : ' ');
					} else {
						mTempLine.append_sprintf(" (%c%c%c%c%c%c)"
							, (hent->mP & 0x80) ? 'N' : ' '
							, (hent->mP & 0x40) ? 'V' : ' '
							, (hent->mP & 0x08) ? 'D' : ' '
							, (hent->mP & 0x04) ? 'I' : ' '
							, (hent->mP & 0x02) ? 'Z' : ' '
							, (hent->mP & 0x01) ? 'C' : ' ');
					}
				}

				if ((bool)mbShowRegisters || (bool)mbShowFlags)
					mTempLine += " | ";

				// Disassembly (or HLE marker)
				if (hent->mbIRQ && hent->mbNMI && mDisasmMode != kATDebugDisasmMode_6809) {
					mTempLine.append_sprintf("%04X: -- High level emulation --", hent->mPC);
				} else {
					ATDisassembleInsn(mTempLine, nullptr, mDisasmMode, *hent,
						false,			// decodeReferences
						true,			// decodeRefsHistory
						(bool)mbShowPCAddress,
						(bool)mbShowCodeBytes,
						(bool)mbShowLabels,
						false,			// lowercaseOps
						false,			// wideOpcode
						(bool)mbShowLabelNamespaces,
						true,			// showSymbols
						(bool)mbShowGlobalPCAddress);
				}

				s = mTempLine.c_str();
			}
			break;

		case kATHTNodeType_Label:
			s = node->mpLabel;
			break;
	}

	return s;
}

// =========================================================================
// Copy to clipboard
// =========================================================================

void ATImGuiHistoryPaneImpl::CopyVisibleLines() {
	const ATHTNode *root = mHistoryTree.GetRootNode();
	const uint32 totalLines = root->mHeight > 0 ? root->mHeight - 1 : 0;

	if (totalLines == 0)
		return;

	VDStringA text;

	ATHTLineIterator it = mHistoryTree.GetLineFromPos(0);

	// Find minimum level for indentation normalization
	int minLevel = INT_MAX;
	for (ATHTLineIterator tmp = it; tmp; tmp = mHistoryTree.GetNextVisibleLine(tmp)) {
		int level = 0;
		for (ATHTNode *p = tmp.mpNode->mpParent; p; p = p->mpParent)
			++level;
		if (level < minLevel)
			minLevel = level;
	}

	for (; it; it = mHistoryTree.GetNextVisibleLine(it)) {
		int level = -minLevel;
		for (ATHTNode *p = it.mpNode; p; p = p->mpParent)
			++level;

		if (level > 0) {
			for (int i = 0; i < level; ++i) {
				text += ' ';
				text += ' ';
			}
		}

		text += it.mpNode->mpFirstChild ? (it.mpNode->mbExpanded ? '-' : '+') : ' ';
		text += ' ';

		const char *lineText = GetLineText(it);
		if (lineText)
			text += lineText;

		text += '\n';
	}

	if (!text.empty())
		SDL_SetClipboardText(text.c_str());
}
