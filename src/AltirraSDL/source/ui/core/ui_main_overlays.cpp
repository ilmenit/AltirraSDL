//	AltirraSDL - Exit confirm + drag-drop overlay (split from ui_main.cpp Phase 3g)

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
#include "ui_confirm_dialog.h"
#include "simulator.h"
#include "uitypes.h"
#include "diskinterface.h"
#include "cassette.h"
#include "cartridge.h"
#include "mediamanager.h"
#include <at/atio/cassetteimage.h>
#include <at/atio/image.h>
#include "uiaccessors.h"
#include "debugger.h"

extern ATSimulator g_sim;

// =========================================================================
// Exit confirmation dialog
//
// Delegates to the reusable ATUIConfirm helper so the user gets full
// keyboard navigation (Left/Right/Tab, Enter, Escape) with visual focus
// highlight — matching Windows MessageBox behavior.
// =========================================================================

// Persistent message string — outlives the call stack of ATUIRenderExitConfirm
// because the helper captures `.message` as a const char*.
static VDStringA g_exitConfirmMsgUtf8;

// Build the dirty-storage message, matching Windows ATUIConfirmDiscardAllStorageGetMessage().
// Returns UTF-8 string ready for ImGui, using "  " indent instead of "\t" (tabs
// don't render as indentation in ImGui).
static VDStringA BuildDirtyStorageMessage(ATSimulator &sim) {
	vdfastvector<ATStorageId> dirtyIds;
	sim.GetDirtyStorage(dirtyIds, ~(uint32)0);

	vdfastvector<ATDebuggerStorageId> dbgDirtyIds;
	IATDebugger *dbg = ATGetDebugger();
	if (dbg)
		dbg->GetDirtyStorage(dbgDirtyIds);

	if (dirtyIds.empty() && dbgDirtyIds.empty())
		return VDStringA();

	std::sort(dirtyIds.begin(), dirtyIds.end());
	std::sort(dbgDirtyIds.begin(), dbgDirtyIds.end());

	VDStringA msg;
	msg = "The following modified items have not been saved:\n\n";
	msg += "  Contents of emulation memory\n";

	for (const ATStorageId id : dirtyIds) {
		const uint32 type = id & kATStorageId_TypeMask;
		const uint32 unit = id & kATStorageId_UnitMask;

		switch (type) {
			case kATStorageId_Cartridge:
				msg += "  Cartridge";
				if (unit)
					msg.append_sprintf(" %u", unit + 1);
				break;

			case kATStorageId_Disk:
				msg.append_sprintf("  Disk (D%u:)", unit + 1);
				break;

			case kATStorageId_Tape:
				msg += "  Tape";
				break;

			case kATStorageId_Firmware:
				switch (unit) {
					case 0: msg += "  IDE main firmware"; break;
					case 1: msg += "  IDE SDX firmware"; break;
					case 2: msg += "  Ultimate1MB firmware"; break;
					case 3: msg += "  Rapidus flash firmware"; break;
					case 4: msg += "  Rapidus PBI firmware"; break;
				}
				break;
		}
		msg += '\n';
	}

	for (const ATDebuggerStorageId id : dbgDirtyIds) {
		switch (id) {
			case kATDebuggerStorageId_CustomSymbols:
				msg += "  Debugger: Custom Symbols\n";
				break;
			default:
				break;
		}
	}

	msg += "\nAre you sure you want to exit?";
	return msg;
}

void ATUIRenderExitConfirm(ATSimulator &sim, ATUIState &state) {
	// Only queue the helper once per invocation cycle.  showExitConfirm
	// is set true by the close handler; we flip it false immediately
	// after queuing so the per-frame render call doesn't requeue.
	if (!state.showExitConfirm)
		return;

	state.showExitConfirm = false;

	// Guard against the user mashing Close while a confirmation is
	// already up — don't stack duplicate exit prompts.
	if (ATUIIsConfirmDialogActive())
		return;

	g_exitConfirmMsgUtf8 = BuildDirtyStorageMessage(sim);

	// Windows Altirra always confirms on exit:
	// - If dirty storage: lists dirty items + memory warning
	// - If nothing dirty: still warns about emulation memory loss
	if (g_exitConfirmMsgUtf8.empty())
		g_exitConfirmMsgUtf8 = "Any unsaved work in emulation memory will be lost.\n\nAre you sure you want to exit?";

	if (ATUIDiskExplorerHasUnsavedChanges()) {
		VDStringA message("Modified disk images in Explorer have not been saved. "
			"Cancel and use File > Save disk image as... in each Explorer to keep those changes.\n\n");
		message += g_exitConfirmMsgUtf8;
		g_exitConfirmMsgUtf8 = message;
	}

	ATUIConfirmOptions opts;
	opts.title        = "Confirm Exit";
	opts.message      = g_exitConfirmMsgUtf8.c_str();
	opts.confirmLabel = "Exit";
	opts.cancelLabel  = "Cancel";
	opts.destructive  = true;        // default focus on Cancel
	opts.onConfirm    = [&state]() {
		state.exitConfirmed = true;
		SDL_Event quit{};
		quit.type = SDL_EVENT_QUIT;
		SDL_PushEvent(&quit);
	};
	ATUIShowConfirm(std::move(opts));
}

// =========================================================================
// Drag-and-drop overlay — highlight drop target during file drag
// =========================================================================

void ATUIRenderDragDropOverlay() {
	if (!g_dragDropState.active)
		return;

	ImDrawList *fg = ImGui::GetForegroundDrawList();
	float cx = g_dragDropState.x;
	float cy = g_dragDropState.y;

	// Check which drop target the cursor is over
	struct DropTarget {
		ImVec2 pos, size;
		const char *label;
	};
	DropTarget target = {};

	ImVec2 p, s;
	if (ATUIXEXExplorerGetDropRect(p, s, cx, cy)
		&& cx >= p.x && cy >= p.y && cx <= p.x + s.x && cy <= p.y + s.y) {
		target = {p, s, "Open in XEX Explorer"};
	} else if (ATUICartridgeExplorerGetDropRect(p, s, cx, cy)
		&& cx >= p.x && cy >= p.y && cx <= p.x + s.x && cy <= p.y + s.y) {
		target = {p, s, "Open in Cart Explorer"};
	} else if (ATUIDiskExplorerGetDropRect(p, s, cx, cy)
		&& cx >= p.x && cy >= p.y && cx <= p.x + s.x && cy <= p.y + s.y) {
		target = {p, s, "Open/import in Disk Explorer"};
	} else if (ATUIFirmwareManagerGetDropRect(p, s)
		&& cx >= p.x && cy >= p.y && cx <= p.x + s.x && cy <= p.y + s.y) {
		target = {p, s, "Add firmware"};
	} else {
		// Default: entire viewport = boot image
		ImVec2 vp = ImGui::GetMainViewport()->Pos;
		ImVec2 vs = ImGui::GetMainViewport()->Size;
		target = {vp, vs, "Boot image"};
	}

	// Draw highlight border around target window
	ImU32 borderColor = IM_COL32(80, 160, 255, 200);
	ImU32 fillColor = IM_COL32(80, 160, 255, 30);
	float thickness = 3.0f;
	ImVec2 tl = target.pos;
	ImVec2 br = ImVec2(tl.x + target.size.x, tl.y + target.size.y);
	fg->AddRectFilled(tl, br, fillColor);
	fg->AddRect(tl, br, borderColor, 0.0f, 0, thickness);

	// Draw label tooltip near cursor
	ImVec2 textSize = ImGui::CalcTextSize(target.label);
	ImVec2 padding = ImVec2(8, 4);
	ImVec2 labelPos = ImVec2(cx + 16, cy + 16);

	// Keep label within viewport
	ImVec2 vpMax = ImVec2(ImGui::GetMainViewport()->Pos.x + ImGui::GetMainViewport()->Size.x,
		ImGui::GetMainViewport()->Pos.y + ImGui::GetMainViewport()->Size.y);
	if (labelPos.x + textSize.x + padding.x * 2 > vpMax.x)
		labelPos.x = cx - textSize.x - padding.x * 2 - 4;
	if (labelPos.y + textSize.y + padding.y * 2 > vpMax.y)
		labelPos.y = cy - textSize.y - padding.y * 2 - 4;

	ImVec2 bgMin = labelPos;
	ImVec2 bgMax = ImVec2(labelPos.x + textSize.x + padding.x * 2,
		labelPos.y + textSize.y + padding.y * 2);
	fg->AddRectFilled(bgMin, bgMax, IM_COL32(30, 30, 30, 220), 4.0f);
	fg->AddRect(bgMin, bgMax, borderColor, 4.0f);
	fg->AddText(ImVec2(labelPos.x + padding.x, labelPos.y + padding.y),
		IM_COL32(255, 255, 255, 255), target.label);
}
