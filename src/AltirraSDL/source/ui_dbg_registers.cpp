//	AltirraSDL - Dear ImGui debugger registers pane
//	Replaces Win32 ATRegistersWindow (uidbgregisters.cpp).
//	Displays CPU registers for all supported CPU types.

#include <stdafx.h>
#include <imgui.h>
#include <vd2/system/vdtypes.h>
#include <at/atcpu/execstate.h>
#include "ui_debugger.h"
#include "console.h"
#include "debugger.h"
#include "cpu.h"

// =========================================================================
// Registers pane
// =========================================================================

class ATImGuiRegistersPaneImpl final : public ATImGuiDebuggerPane {
public:
	ATImGuiRegistersPaneImpl();

	bool Render() override;
	void OnDebuggerSystemStateUpdate(const ATDebuggerSystemState& state) override;

private:
	VDStringA mFormattedState;
};

ATImGuiRegistersPaneImpl::ATImGuiRegistersPaneImpl()
	: ATImGuiDebuggerPane(kATUIPaneId_Registers, "Registers")
{
}

void ATImGuiRegistersPaneImpl::OnDebuggerSystemStateUpdate(const ATDebuggerSystemState& state) {
	ATImGuiDebuggerPane::OnDebuggerSystemStateUpdate(state);

	// Format registers exactly as Windows version does (uidbgregisters.cpp:101-238)
	mFormattedState.clear();

	mFormattedState.append_sprintf("Target: %s\n", state.mpDebugTarget->GetName());

	const auto dmode = state.mpDebugTarget->GetDisasmMode();
	if (dmode == kATDebugDisasmMode_8048) {
		const ATCPUExecState8048& s = state.mExecState.m8048;
		mFormattedState.append_sprintf("PC  = %04X\n", s.mPC);
		mFormattedState.append_sprintf("PSW = %02X\n", s.mPSW);
		mFormattedState.append_sprintf("A   = %02X\n", s.mA);
	} else if (dmode == kATDebugDisasmMode_Z80) {
		const ATCPUExecStateZ80& s = state.mExecState.mZ80;
		mFormattedState.append_sprintf("PC = %04X\n", s.mPC);
		mFormattedState.append_sprintf("SP = %04X\n", s.mSP);
		mFormattedState += '\n';
		mFormattedState.append_sprintf("A  = %02X\n", s.mA);
		mFormattedState.append_sprintf("F  = %02X (%c%c%c%c%c%c)\n"
			, s.mF
			, (s.mF & 0x80) ? 'S' : '-'
			, (s.mF & 0x40) ? 'Z' : '-'
			, (s.mF & 0x10) ? 'H' : '-'
			, (s.mF & 0x04) ? 'P' : '-'
			, (s.mF & 0x02) ? 'N' : '-'
			, (s.mF & 0x01) ? 'C' : '-'
		);
		mFormattedState.append_sprintf("BC = %02X%02X\n", s.mB, s.mC);
		mFormattedState.append_sprintf("DE = %02X%02X\n", s.mD, s.mE);
		mFormattedState.append_sprintf("HL = %02X%02X\n", s.mH, s.mL);
		mFormattedState.append_sprintf("IX = %04X\n", s.mIX);
		mFormattedState.append_sprintf("IY = %04X\n", s.mIY);
		mFormattedState += '\n';
		mFormattedState.append_sprintf("AF'= %02X%02X\n", s.mAltA, s.mAltF);
		mFormattedState.append_sprintf("BC'= %02X%02X\n", s.mAltB, s.mAltC);
		mFormattedState.append_sprintf("DE'= %02X%02X\n", s.mAltD, s.mAltE);
		mFormattedState.append_sprintf("HL'= %02X%02X\n", s.mAltH, s.mAltL);
		mFormattedState += '\n';
		mFormattedState.append_sprintf("I  = %02X\n", s.mI);
		mFormattedState.append_sprintf("R  = %02X\n", s.mR);
	} else if (dmode == kATDebugDisasmMode_6809) {
		const ATCPUExecState6809& s = state.mExecState.m6809;
		mFormattedState.append_sprintf("PC = %04X\n", s.mPC);
		mFormattedState.append_sprintf("A  = %02X\n", s.mA);
		mFormattedState.append_sprintf("B  = %02X\n", s.mB);
		mFormattedState.append_sprintf("X  = %02X\n", s.mX);
		mFormattedState.append_sprintf("Y  = %02X\n", s.mY);
		mFormattedState.append_sprintf("U  = %02X\n", s.mU);
		mFormattedState.append_sprintf("S  = %02X\n", s.mS);
		mFormattedState.append_sprintf("CC = %02X (%c%c%c%c%c%c%c%c)\n"
			, s.mCC
			, s.mCC & 0x80 ? 'E' : '-'
			, s.mCC & 0x40 ? 'F' : '-'
			, s.mCC & 0x20 ? 'H' : '-'
			, s.mCC & 0x10 ? 'I' : '-'
			, s.mCC & 0x08 ? 'N' : '-'
			, s.mCC & 0x04 ? 'Z' : '-'
			, s.mCC & 0x02 ? 'V' : '-'
			, s.mCC & 0x01 ? 'C' : '-'
		);
	} else if (dmode == kATDebugDisasmMode_65C816) {
		const ATCPUExecState6502& s = state.mExecState.m6502;

		if (s.mbEmulationFlag)
			mFormattedState += "Mode: Emulation\n";
		else {
			mFormattedState.append_sprintf("Mode: Native (M%d X%d)\n"
				, s.mP & AT6502::kFlagM ? 8 : 16
				, s.mP & AT6502::kFlagX ? 8 : 16
			);
		}

		mFormattedState.append_sprintf("PC = %02X:%04X (%04X)\n", s.mK, s.mPC, state.mPC);

		if (s.mbEmulationFlag || (s.mP & AT6502::kFlagM)) {
			mFormattedState.append_sprintf("A  = %02X\n", s.mA);
			mFormattedState.append_sprintf("B  = %02X\n", s.mAH);
		} else
			mFormattedState.append_sprintf("A  = %02X%02X\n", s.mAH, s.mA);

		if (s.mbEmulationFlag || (s.mP & AT6502::kFlagX)) {
			mFormattedState.append_sprintf("X  = %02X\n", s.mX);
			mFormattedState.append_sprintf("Y  = %02X\n", s.mY);
		} else {
			mFormattedState.append_sprintf("X  = %02X%02X\n", s.mXH, s.mX);
			mFormattedState.append_sprintf("Y  = %02X%02X\n", s.mYH, s.mY);
		}

		if (s.mbEmulationFlag)
			mFormattedState.append_sprintf("S  = %02X\n", s.mS);
		else
			mFormattedState.append_sprintf("S  = %02X%02X\n", s.mSH, s.mS);

		mFormattedState.append_sprintf("P  = %02X\n", s.mP);

		if (s.mbEmulationFlag) {
			mFormattedState.append_sprintf("     %c%c%c%c%c%c\n"
				, s.mP & 0x80 ? 'N' : '-'
				, s.mP & 0x40 ? 'V' : '-'
				, s.mP & 0x08 ? 'D' : '-'
				, s.mP & 0x04 ? 'I' : '-'
				, s.mP & 0x02 ? 'Z' : '-'
				, s.mP & 0x01 ? 'C' : '-'
			);
		} else {
			mFormattedState.append_sprintf("     %c%c%c%c%c%c%c%c\n"
				, s.mP & 0x80 ? 'N' : '-'
				, s.mP & 0x40 ? 'V' : '-'
				, s.mP & 0x20 ? 'M' : '-'
				, s.mP & 0x10 ? 'X' : '-'
				, s.mP & 0x08 ? 'D' : '-'
				, s.mP & 0x04 ? 'I' : '-'
				, s.mP & 0x02 ? 'Z' : '-'
				, s.mP & 0x01 ? 'C' : '-'
			);
		}

		mFormattedState.append_sprintf("E  = %d\n", s.mbEmulationFlag);
		mFormattedState.append_sprintf("D  = %04X\n", s.mDP);
		mFormattedState.append_sprintf("B  = %02X\n", s.mB);
	} else {
		// Standard 6502/65C02
		const ATCPUExecState6502& s = state.mExecState.m6502;

		mFormattedState.append_sprintf("PC = %04X (%04X)\n", state.mInsnPC, s.mPC);
		mFormattedState.append_sprintf("A  = %02X\n", s.mA);
		mFormattedState.append_sprintf("X  = %02X\n", s.mX);
		mFormattedState.append_sprintf("Y  = %02X\n", s.mY);
		mFormattedState.append_sprintf("S  = %02X\n", s.mS);
		mFormattedState.append_sprintf("P  = %02X\n", s.mP);
		mFormattedState.append_sprintf("     %c%c%c%c%c%c\n"
			, s.mP & 0x80 ? 'N' : '-'
			, s.mP & 0x40 ? 'V' : '-'
			, s.mP & 0x08 ? 'D' : '-'
			, s.mP & 0x04 ? 'I' : '-'
			, s.mP & 0x02 ? 'Z' : '-'
			, s.mP & 0x01 ? 'C' : '-'
		);
	}
}

bool ATImGuiRegistersPaneImpl::Render() {
	bool open = true;

	if (mbFocusRequested) {
		ImGui::SetNextWindowFocus();
		mbFocusRequested = false;
	}

	ImGui::SetNextWindowSize(ImVec2(200, 300), ImGuiCond_FirstUseEver);
	if (!ImGui::Begin(mTitle.c_str(), &open)) {
		ImGui::End();
		return open;
	}

	if (mbStateValid && !mFormattedState.empty()) {
		ImGui::TextUnformatted(mFormattedState.c_str(),
			mFormattedState.c_str() + mFormattedState.size());
	} else {
		ImGui::TextDisabled("(no state)");
	}

	ImGui::End();
	return open;
}

// =========================================================================
// Registration
// =========================================================================

void ATUIDebuggerEnsureRegistersPane() {
	if (!ATUIDebuggerGetPane(kATUIPaneId_Registers)) {
		auto *pane = new ATImGuiRegistersPaneImpl();
		ATUIDebuggerRegisterPane(pane);
		pane->Release();
	}
}
