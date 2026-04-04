//	AltirraSDL - Dear ImGui verifier settings dialog
//	Replaces Win32 ATUIVerifierDialog (uiverifier.cpp).
//	Shows 11 checkboxes for CPU verification flags.

#include <stdafx.h>
#include <imgui.h>
#include <vd2/system/vdtypes.h>
#include "simulator.h"
#include "verifier.h"

extern ATSimulator g_sim;

namespace {

struct VerifierFlagInfo {
	uint32 mFlag;
	const char *mpLabel;
};

static const VerifierFlagInfo kVerifierFlags[] = {
	{ kATVerifierFlag_UndocumentedKernelEntry,      "Undocumented OS entry" },
	{ kATVerifierFlag_RecursiveNMI,                  "Recursive NMI execution" },
	{ kATVerifierFlag_InterruptRegs,                 "Interrupt handler register corruption" },
	{ kATVerifierFlag_64KWrap,                       "Address indexing across 64K boundary" },
	{ kATVerifierFlag_AbnormalDMA,                   "Abnormal playfield DMA" },
	{ kATVerifierFlag_CallingConventionViolations,   "OS calling convention violations" },
	{ kATVerifierFlag_LoadingOverDisplayList,        "Loading over active display list" },
	{ kATVerifierFlag_AddressZero,                   "Loading absolute address zero" },
	{ kATVerifierFlag_NonCanonicalHardwareAddress,   "Non-canonical hardware address" },
	{ kATVerifierFlag_StackWrap,                     "Stack overflow/underflow" },
	{ kATVerifierFlag_StackInZP816,                  "65C816: Stack pointer changed to page zero" },
};

struct VerifierDialogState {
	bool mbOpen = false;
	uint32 mFlags = 0;
};

VerifierDialogState g_verifierDialog;

} // anonymous namespace

void ATUIShowDialogVerifier() {
	auto& d = g_verifierDialog;
	d.mbOpen = true;
	d.mFlags = 0;

	ATCPUVerifier *ver = g_sim.GetVerifier();
	if (ver)
		d.mFlags = ver->GetFlags();
}

void ATUIRenderVerifierDialog() {
	auto& d = g_verifierDialog;
	if (!d.mbOpen)
		return;

	ImGui::SetNextWindowSize(ImVec2(420, 380), ImGuiCond_Appearing);
	ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(),
		ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));

	if (!ImGui::Begin("Verifier", &d.mbOpen,
			ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoDocking)) {
		ImGui::End();
		return;
	}

	ImGui::TextUnformatted("Select verification checks to enable:");
	ImGui::Separator();

	for (const auto& entry : kVerifierFlags) {
		bool checked = (d.mFlags & entry.mFlag) != 0;
		if (ImGui::Checkbox(entry.mpLabel, &checked)) {
			if (checked)
				d.mFlags |= entry.mFlag;
			else
				d.mFlags &= ~entry.mFlag;
		}
	}

	ImGui::Separator();

	if (ImGui::Button("OK", ImVec2(80, 0))) {
		if (d.mFlags) {
			g_sim.SetVerifierEnabled(true);
			ATCPUVerifier *ver = g_sim.GetVerifier();
			if (ver)
				ver->SetFlags(d.mFlags);
		} else {
			g_sim.SetVerifierEnabled(false);
		}
		d.mbOpen = false;
	}

	ImGui::SameLine();

	if (ImGui::Button("Cancel", ImVec2(80, 0))) {
		d.mbOpen = false;
	}

	ImGui::End();
}
