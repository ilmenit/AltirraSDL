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
#include "netplay/netplay_glue.h"
#include "settings.h"

#ifdef ALTIRRA_NETPLAY_ENABLED

#include "ui/netplay/ui_netplay.h"
#include "ui/netplay/ui_netplay_state.h"
#include "ui/emotes/emote_netplay.h"

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

		// Emoticons toggle: mirrors the Configure System → Online Play
		// "Send communication icons" checkbox and, when enabled, shows
		// the on-screen picker button during an active session in every
		// mode (Desktop, Gaming Mode, touch).  Receive stays on its own
		// Configure System page; the menu surfaces Send because it is
		// the direction tied to the visible on-screen button.
		{
			bool showEmotes = ATEmoteNetplay::GetSendEnabled();
			if (ImGui::MenuItem("Show Emoticons", nullptr, showEmotes)) {
				ATEmoteNetplay::SetSendEnabled(!showEmotes);
				ATSaveSettings(kATSettingsCategory_Environment);
			}
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
#else // !ALTIRRA_NETPLAY_ENABLED

void ATUIRenderOnlineMenu() {}

#endif // ALTIRRA_NETPLAY_ENABLED