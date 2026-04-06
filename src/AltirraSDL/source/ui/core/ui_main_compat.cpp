//	AltirraSDL - Compatibility Warning (split from ui_main.cpp Phase 3g)

#include <stdafx.h>
#include <SDL3/SDL.h>
#include <imgui.h>
#include <vd2/system/vdtypes.h>
#include <vd2/system/VDString.h>
#include <vd2/system/text.h>
#include <vd2/system/error.h>
#include <vd2/system/file.h>
#include <vd2/system/filesys.h>
#include "ui_main.h"
#include "ui_main_internal.h"
#include "simulator.h"
#include "uitypes.h"
#include "compatengine.h"
#include "compatdb.h"
#include "uicompat.h"
#include "logging.h"

extern ATSimulator g_sim;

// =========================================================================
// Compatibility Warning — SDL3 replacement for Windows IDD_COMPATIBILITY
// =========================================================================

// Pending compat check flag — set after boot, consumed by render loop.
// Declared in ui_main_internal.h so the deferred-action queue in
// ui_main.cpp can push this flag.
bool g_compatCheckPending = false;

// Compat warning dialog state.  Type promoted out of an anonymous
// struct in the original ui_main.cpp so it can be extern'd via the
// internal header (Phase 3g).
ATUICompatWarningState g_compatWarningState;

// SDL3 implementation of ATUICompatGetKnownTagDisplayName
// (Windows version is in uicompat.cpp which is excluded from SDL3 build)
const wchar_t *ATUICompatGetKnownTagDisplayName(ATCompatKnownTag knownTag) {
	static constexpr const wchar_t *kKnownTagNames[] = {
		L"Requires BASIC",
		L"Requires Atari BASIC revision A",
		L"Requires Atari BASIC revision B",
		L"Requires Atari BASIC revision C",
		L"Requires BASIC disabled",
		L"Requires OS-A",
		L"Requires OS-B",
		L"Requires XL/XE OS",
		L"Requires accurate disk timing",
		L"Requires no additional CIO devices",
		L"Requires no expanded memory",
		L"Requires CTIA",
		L"Incompatible with Ultimate1MB",
		L"Requires 6502 undocumented opcodes",
		L"Incompatible with 65C816 24-bit addressing",
		L"Requires writable disk",
		L"Incompatible with floating data bus",
		L"Cart: Use 5200 8K mapper",
		L"Cart: Use 5200 one-chip 16K mapper",
		L"Cart: Use 5200 two-chip 16K mapper",
		L"Cart: Use 5200 32K mapper",
		L"Requires 60Hz (NTSC ANTIC)",
		L"Requires 50Hz (PAL ANTIC)",
	};

	const size_t index = (size_t)knownTag - 1;
	if (index < sizeof(kKnownTagNames) / sizeof(kKnownTagNames[0]))
		return kKnownTagNames[index];

	return L"<Unknown tag>";
}

// SDL3 linker symbol for ATUIShowDialogCompatWarning.
// In the SDL3 build, compat checking is done directly in ATUIPollDeferredActions
// rather than through this function, but we provide it as a symbol since
// uicompat.h declares it and compatengine.cpp may reference it.
ATUICompatAction ATUIShowDialogCompatWarning(VDGUIHandle, const ATCompatDBTitle *title,
	const ATCompatKnownTag *tags, size_t numTags)
{
	g_compatWarningState.pTitle = title;
	g_compatWarningState.tags.assign(tags, tags + numTags);
	g_compatWarningState.ignoreThistitle = ATCompatIsTitleMuted(title);
	g_compatWarningState.ignoreAll = ATCompatIsAllMuted();
	g_compatCheckPending = true;
	return kATUICompatAction_Ignore;
}

void ATUICheckCompatibility(ATSimulator &, ATUIState &state) {
	// Compat check already happened in ATUIPollDeferredActions.
	// This just picks up the pending flag and shows the dialog.
	state.showCompatWarning = true;
}

void ATUIRenderCompatWarning(ATSimulator &sim, ATUIState &state) {
	if (!state.showCompatWarning)
		return;

	// Helper to apply mute settings from checkboxes
	auto applyMuteSettings = [&]() {
		auto &s = g_compatWarningState;
		if (s.ignoreAll)
			ATCompatSetAllMuted(true);
		else if (s.ignoreThistitle)
			ATCompatSetTitleMuted(s.pTitle, true);
	};

	ImGui::SetNextWindowSize(ImVec2(480, 320), ImGuiCond_Appearing);
	ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));

	bool wasOpen = state.showCompatWarning;
	if (!ImGui::Begin("Compatibility Warning", &state.showCompatWarning,
			ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings)) {
		// Window collapsed — if user closed via X, resume emulation
		if (wasOpen && !state.showCompatWarning) {
			applyMuteSettings();
			sim.Resume();
		}
		ImGui::End();
		return;
	}

	// Detect X-button or ESC close (showCompatWarning toggled to false by ImGui)
	if (!state.showCompatWarning || ATUICheckEscClose()) {
		state.showCompatWarning = false;
		applyMuteSettings();
		sim.Resume();
		ImGui::End();
		return;
	}

	auto &s = g_compatWarningState;

	// Title text — mName is a UTF-8 byte string from the compat DB
	if (s.pTitle) {
		ImGui::TextWrapped("The title \"%s\" being booted has compatibility issues "
			"with current settings:", s.pTitle->mName.c_str());
	}

	ImGui::Spacing();

	// List of issues
	for (size_t i = 0; i < s.tags.size(); ++i) {
		VDStringA tagName = VDTextWToU8(VDStringW(ATUICompatGetKnownTagDisplayName(s.tags[i])));
		ImGui::BulletText("%s", tagName.c_str());
	}

	ImGui::Spacing();
	ImGui::TextWrapped("Do you want to automatically adjust emulation settings "
		"for better compatibility?");

	ImGui::Spacing();
	ImGui::Separator();
	ImGui::Spacing();

	// Action buttons — emulation is paused while this dialog is open
	if (ImGui::Button("Auto-adjust settings and reboot", ImVec2(-1, 0))) {
		applyMuteSettings();
		ATCompatAdjust(nullptr, s.tags.data(), s.tags.size());
		sim.ColdReset();
		sim.Resume();
		state.showCompatWarning = false;
	}

	if (ImGui::Button("Pause emulation to adjust manually", ImVec2(-1, 0))) {
		applyMuteSettings();
		// Leave paused — user wants to manually adjust settings
		state.showCompatWarning = false;
	}

	if (ImGui::Button("Boot anyway", ImVec2(-1, 0))) {
		applyMuteSettings();
		sim.Resume();
		state.showCompatWarning = false;
	}

	ImGui::Spacing();

	// Mute options
	ImGui::Checkbox("Turn off compatibility checks for this title", &s.ignoreThistitle);
	ImGui::Checkbox("Turn off all compatibility warnings", &s.ignoreAll);

	ImGui::End();
}

