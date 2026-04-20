//	AltirraSDL - Online Play menu (top-level menu entry)
//
//	Mirrors Windows Altirra's convention of a top-level menu for
//	feature clusters.  Items:
//	  - Browse Hosted Games… → Opens the Online Browser
//	  - Host a Game…         → If a cart is booted, goes straight to
//	                           HostSetup.  Otherwise opens Browser so
//	                           the user can boot something first.
//	  - Preferences…         → Preferences → Netplay
//	  - Stop Hosting         → Shown only while a netplay session is
//	                           active.

#include <stdafx.h>

#include <imgui.h>

#include "ui_menus_internal.h"
#include "ui/netplay/ui_netplay.h"
#include "ui/netplay/ui_netplay_state.h"
#include "netplay/netplay_glue.h"

void ATUIRenderOnlineMenu() {
	bool inSession = ATNetplayGlue::IsActive();

	if (ImGui::MenuItem("Browse Hosted Games...")) {
		ATNetplayUI_OpenBrowser();
	}

	if (ImGui::MenuItem("Host Games...")) {
		ATNetplayUI_OpenMyHostedGames();
	}

	ImGui::Separator();

	// Session HUD toggle: controls the top-right "LIVE / FRAME / Peer"
	// overlay.  Always visible so the user can find it, but it only
	// does anything while a session is active.
	{
		auto& st = ATNetplayUI::GetState();
		bool showHud = st.prefs.showSessionHUD;
		if (ImGui::MenuItem("Show Session HUD", nullptr, showHud)) {
			st.prefs.showSessionHUD = !showHud;
			ATNetplayUI::SaveToRegistry();
		}
	}

	if (ImGui::MenuItem("Preferences...")) {
		ATNetplayUI_OpenPrefs();
	}

	if (inSession) {
		ImGui::Separator();
		if (ImGui::MenuItem("Stop Hosting")) {
			ATNetplayUI_EndSession();
		}
	}
}
