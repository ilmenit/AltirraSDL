//	AltirraSDL - About / Change Log / Command-Line Help dialogs
//	Three small self-contained help dialogs that live on the Help menu.
//	Split out of ui_main.cpp (Phase 2f).

#include <stdafx.h>
#include <imgui.h>
#include "ui_main.h"
#include "ui_main_internal.h"

// ATUICheckEscClose lives in ui_main.cpp. Forward-declared locally.
bool ATUICheckEscClose();

void ATUIRenderCommandLineHelpDialog(ATUIState &state) {
	ImGui::SetNextWindowSize(ImVec2(580, 520), ImGuiCond_Appearing);
	ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
	if (!ImGui::Begin("Command-Line Help", &state.showCommandLineHelp, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings)) {
		ImGui::End();
		return;
	}

	if (ATUICheckEscClose()) {
		state.showCommandLineHelp = false;
		ImGui::End();
		return;
	}

	ImGui::TextWrapped("Usage: AltirraSDL [options] [image-file ...]");
	ImGui::Separator();

	if (ImGui::BeginChild("##cmdhelp_scroll", ImVec2(0, -ImGui::GetFrameHeightWithSpacing() - 4), ImGuiChildFlags_None)) {
		ImGui::TextWrapped(
			"Positional arguments are loaded as boot images (ATR, XEX, BIN, ROM, CAR, CAS, WAV, ATX).\n"
			"Drag-and-drop onto the window also loads an image.\n"
			"Settings are stored in ~/.config/altirra/settings.ini\n");

		if (ImGui::CollapsingHeader("Display & Video", ImGuiTreeNodeFlags_DefaultOpen)) {
			ImGui::TextWrapped(
				"  --f                   Start in fullscreen\n"
				"  --ntsc / --pal / --secam / --ntsc50 / --pal60\n"
				"                        Set video standard\n"
				"  --artifact <mode>     Artifacting: none, ntsc, ntschi, pal, palhi\n"
				"  --vsync / --novsync   Enable/disable VSync");
		}

		if (ImGui::CollapsingHeader("Hardware")) {
			ImGui::TextWrapped(
				"  --hardware <mode>     800, 800xl, 1200xl, 130xe, xegs, 1400xl, 5200\n"
				"  --kernel <name>       default, osa, osb, xl, xegs, 1200xl, 5200,\n"
				"                        lle, llexl, hle, 5200lle\n"
				"  --kernelref <ref>     Select kernel by firmware reference string\n"
				"  --basicref <ref>      Select BASIC by firmware reference string\n"
				"  --memsize <size>      8K..1088K (e.g. 64K, 128K, 320KCOMPY)\n"
				"  --axlonmemsize <size> none, 64K..4096K\n"
				"  --highbanks <n>       na, 0, 1, 3, 15, 63\n"
				"  --stereo / --nostereo Enable/disable dual POKEY\n"
				"  --basic / --nobasic   Enable/disable BASIC ROM");
		}

		if (ImGui::CollapsingHeader("Acceleration")) {
			ImGui::TextWrapped(
				"  --burstio / --noburstio   Burst disk I/O\n"
				"  --siopatch / --siopatchsafe / --nosiopatch\n"
				"                        SIO patch mode\n"
				"  --fastboot / --nofastboot\n"
				"                        Fast boot (skip OS memory test)\n"
				"  --accuratedisk / --noaccuratedisk\n"
				"                        Accurate disk timing");
		}

		if (ImGui::CollapsingHeader("Cassette")) {
			ImGui::TextWrapped(
				"  --casautoboot / --nocasautoboot\n"
				"                        Auto-boot cassette images\n"
				"  --casautobasicboot / --nocasautobasicboot\n"
				"                        Auto-boot with BASIC\n"
				"  --tape <file>         Mount cassette image\n"
				"  --tapepos <time>      Set initial tape position (HH:MM:SS.sss)");
		}

		if (ImGui::CollapsingHeader("Media Loading")) {
			ImGui::TextWrapped(
				"  --cart <file>         Load cartridge image\n"
				"  --disk <file>         Mount disk image (repeatable: D1:, D2:, ...)\n"
				"  --run <file>          Load program (EXE/XEX)\n"
				"  --runbas <file>       Load BASIC program\n"
				"  --bootro / --bootrw / --bootvrw / --bootvrwsafe\n"
				"                        Set write mode for loaded images\n"
				"  --cartmapper <id>     Force cartridge mapper ID\n"
				"  --nocartchecksum      Ignore cartridge checksum");
		}

		if (ImGui::CollapsingHeader("Devices")) {
			ImGui::TextWrapped(
				"  --soundboard <base>   Add SoundBoard (d2c0, d500, d600)\n"
				"  --nosoundboard        Remove SoundBoard\n"
				"  --slightsid / --noslightsid\n"
				"  --covox / --nocovox\n"
				"  --cleardevices        Remove all devices\n"
				"  --adddevice <tag[,params]>\n"
				"                        Add device (e.g. --adddevice soundboard)\n"
				"  --setdevice <tag[,params]>\n"
				"                        Add or reconfigure device\n"
				"  --removedevice <tag>  Remove device\n"
				"  --pclink <mode,path>  Mount PCLink directory (mode: ro, rw)\n"
				"  --nopclink            Remove PCLink device\n"
				"  --hdpath <path>       Mount H: device (read-only)\n"
				"  --hdpathrw <path>     Mount H: device (read-write)\n"
				"  --nohdpath            Remove H: device");
		}

		if (ImGui::CollapsingHeader("Debugger")) {
			ImGui::TextWrapped(
				"  --debug               Open debugger console\n"
				"  --debugcmd <cmd>      Queue debugger command (repeatable)\n"
				"  --debugbrkrun / --nodebugbrkrun\n"
				"                        Break on EXE run address\n"
				"  --autotest            Register autotest debugger commands");
		}

		if (ImGui::CollapsingHeader("Other")) {
			ImGui::TextWrapped(
				"  --type <text>         Type text into emulator after boot\n"
				"                        (~ = Enter, ` = quote)\n"
				"  --rawkeys / --norawkeys\n"
				"                        Raw keyboard mode\n"
				"  --diskemu <mode>      Disk emulation mode for all drives\n"
				"  --nocheats            Disable cheat engine\n"
				"  --cheats <file>       Load cheat file");
		}
	}
	ImGui::EndChild();

	ImGui::Spacing();
	float buttonWidth = 80.0f;
	ImGui::SetCursorPosX(ImGui::GetWindowWidth() - buttonWidth - ImGui::GetStyle().WindowPadding.x);
	if (ImGui::Button("OK", ImVec2(buttonWidth, 0)))
		state.showCommandLineHelp = false;

	ImGui::End();
}

void ATUIRenderChangeLogDialog(ATUIState &state) {
	ImGui::SetNextWindowSize(ImVec2(520, 400), ImGuiCond_Appearing);
	ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
	if (!ImGui::Begin("Change Log", &state.showChangeLog, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings)) {
		ImGui::End();
		return;
	}

	if (ATUICheckEscClose()) {
		state.showChangeLog = false;
		ImGui::End();
		return;
	}

	ImGui::TextWrapped(
		"AltirraSDL - Cross-Platform Frontend\n\n"
		"This is the SDL3 + Dear ImGui cross-platform port of Altirra.\n"
		"It aims for full feature parity with the Windows version.\n\n"
		"Current status:\n"
		"  - Full emulation core (CPU, ANTIC, GTIA, POKEY, PIA)\n"
		"  - All hardware modes (800/800XL/1200XL/130XE/1400XL/XEGS/5200)\n"
		"  - Disk, cassette, and cartridge support\n"
		"  - Keyboard, gamepad, and mouse input\n"
		"  - Audio output via SDL3\n"
		"  - Settings persistence\n"
		"  - Profile management\n"
		"  - State save/load\n\n"
		"For the full Altirra change log, see the Windows version documentation.");

	ImGui::Spacing();
	float buttonWidth = 80.0f;
	ImGui::SetCursorPosX(ImGui::GetWindowWidth() - buttonWidth - ImGui::GetStyle().WindowPadding.x);
	if (ImGui::Button("OK", ImVec2(buttonWidth, 0)))
		state.showChangeLog = false;

	ImGui::End();
}

void ATUIRenderAboutDialog(ATUIState &state) {
	ImGui::SetNextWindowSize(ImVec2(420, 220), ImGuiCond_Appearing);
	ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
	if (!ImGui::Begin("About AltirraSDL", &state.showAboutDialog, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings)) {
		ImGui::End();
		return;
	}

	if (ATUICheckEscClose()) {
		state.showAboutDialog = false;
		ImGui::End();
		return;
	}

	ImGui::Text("AltirraSDL");
	ImGui::Separator();
	ImGui::TextWrapped(
		"Atari 800/800XL/5200 emulator\n"
		"Based on Altirra by Avery Lee\n"
		"SDL3 + Dear ImGui cross-platform frontend\n"
		"SDL Port by Jakub 'Ilmenit' Debski\n\n"
		"Licensed under GNU GPL v2+");

	ImGui::Spacing();
	float buttonWidth = 80.0f;
	ImGui::SetCursorPosX(ImGui::GetWindowWidth() - buttonWidth - ImGui::GetStyle().WindowPadding.x);
	if (ImGui::Button("OK", ImVec2(buttonWidth, 0)))
		state.showAboutDialog = false;

	ImGui::End();
}
