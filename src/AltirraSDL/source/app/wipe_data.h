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
//	Android note: this wipes LOCAL data only.  Auto Backup is enabled
//	(see android:allowBackup="true" in AndroidManifest.xml, with curated
//	exclusions in res/xml/backup_rules.xml + data_extraction_rules.xml)
//	so the user's game library and settings can follow them to a new
//	phone.  Android does not let an app refuse a restore at install
//	time — uninstall+reinstall on the same device will pull the cloud
//	snapshot back too.  Users who want a fully clean slate must also
//	clear the cloud backup from Settings > System > Backup > Manage
//	backup > Altirra.  The Reset Altirra button intentionally does NOT
//	open that screen automatically — wiping someone's cloud copy is
//	more destructive than the local wipe and warrants a manual step.

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
