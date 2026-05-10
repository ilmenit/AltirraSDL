//	AltirraSDL - User data wipe ("Reset Altirra")
//
//	Recursively removes every file Altirra writes inside ATGetConfigDir():
//	settings.ini, imgui.ini, gamelibrary.json, thumbnails/, custom_art/,
//	netplay_cache/, last_crash.txt, quicksave.atstate2, lobby.ini, etc.
//
//	The "wipe and exit" entry point is what the About dialogs call when
//	the user confirms — exit happens via std::_Exit(0) so the normal
//	shutdown flush doesn't re-create settings.ini from the in-memory
//	registry between the wipe and the process exit.
//
//	Android note: this does NOT disable Android Auto Backup.  The user
//	wanted a clean slate today; a future backup of the (now-empty) data
//	dir naturally propagates the wipe to other devices via Google Drive.

#pragma once

// Delete every file/directory under ATGetConfigDir(), leaving the
// directory itself.  Returns true on full success, false if any
// individual path failed.  Idempotent — running on an empty dir is a
// no-op.
bool ATWipeAllUserData();

// ATWipeAllUserData() + immediate process exit (std::_Exit) so the
// shutdown path doesn't flush the registry back to disk.  Never
// returns.
[[noreturn]] void ATWipeAndExit();
