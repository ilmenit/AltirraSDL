//	AltirraSDL - Android platform helpers
//	Thin JNI wrappers for things SDL3 doesn't expose directly:
//	  - System UI safe-area insets (status bar, navigation bar,
//	    display cutout) from WindowInsets.
//	  - Runtime storage permission request.
//	  - Public Downloads directory lookup via Environment.
//
//	All functions are no-ops / return defaults on non-Android builds.

#pragma once

#include <string>
#include <vector>

struct ATAndroidVolume {
	std::string path;
	std::string label;
	bool removable;
};

struct ATSafeInsets {
	int top = 0;
	int bottom = 0;
	int left = 0;
	int right = 0;
};

// Returns the current system UI insets in pixels.  Cheap to call every
// frame — the result is cached and only re-queried when invalidated.
ATSafeInsets ATAndroid_GetSafeInsets();

// Call from the native main thread on window resize / orientation change
// so the next GetSafeInsets() call re-queries the Java side.
void ATAndroid_InvalidateSafeInsets();

// Ask Android for storage read permission at runtime.  Fires the system
// dialog if not already granted.  Non-blocking — the result is delivered
// via the activity's onRequestPermissionsResult which we ignore; the
// file browser simply re-checks readability on the next open.
//
// Safe to call multiple times.  Returns true if the call was dispatched
// to the Java side (i.e. Android build and activity is up), false if
// this is a non-Android build or the JNI call failed.
bool ATAndroid_RequestStoragePermission();

// Returns true if READ_EXTERNAL_STORAGE (API <= 32) or
// MANAGE_EXTERNAL_STORAGE / All files access (API 30+) is currently
// granted to the app.
bool ATAndroid_HasStoragePermission();

// Opens the Android Settings screen where the user can toggle
// "All files access" (MANAGE_EXTERNAL_STORAGE) for Altirra.  On
// pre-API-30 devices falls back to the ordinary runtime dialog.
// Returns true if the intent was dispatched.
bool ATAndroid_OpenManageStorageSettings();

// Fire a short haptic pulse on the phone.  Uses the Android Vibrator
// system service via JNI (Context.VIBRATOR_SERVICE /
// VibratorManager on API 31+, Vibrator on older versions).  The
// duration is in milliseconds — caller-tuned for different events
// (e.g. 15ms for a fire button tap, 5ms for a direction change).
// No-op on non-Android builds and if the device has no vibrator.
void ATAndroid_Vibrate(int durationMs);

// Returns the public Downloads directory, e.g.
// "/storage/emulated/0/Download".  Uses
// Environment.getExternalStoragePublicDirectory(DIRECTORY_DOWNLOADS)
// on the Java side so we get the correct path regardless of device
// OEM customization.  Returns an empty string on failure or non-Android.
const char *ATAndroid_GetPublicDownloadsDir();

// Returns the list of mounted storage volumes (internal, SD card, USB).
// Uses StorageManager.getStorageVolumes() on the Java side.  The result
// is cached; call ATAndroid_InvalidateStorageVolumes() to force a
// re-query (e.g. on app resume after a hot-plug).
const std::vector<ATAndroidVolume>& ATAndroid_GetStorageVolumes();

// Invalidate the cached volume list so the next
// ATAndroid_GetStorageVolumes() call re-queries the Java side.
void ATAndroid_InvalidateStorageVolumes();

// Toggle Android immersive mode — hide / show the status bar and
// navigation bar so the emulator canvas occupies the entire physical
// screen.  Swiping from the top or bottom edge transiently reveals
// the bars while immersive is on.  No-op on non-Android builds.
//
// On API 30+ uses WindowInsetsController + setDecorFitsSystemWindows;
// on older devices falls back to setSystemUiVisibility with
// IMMERSIVE_STICKY.  After toggling, the safe-area insets reported by
// ATAndroid_GetSafeInsets() will change on the next layout pass —
// callers should invalidate via ATAndroid_InvalidateSafeInsets() so
// the UI re-queries them.
void ATAndroid_SetImmersiveMode(bool enabled);

// Mark the Android activity for a true exit: when SDLActivity's main
// thread runner calls finish() after native main() returns, the
// AltirraActivity.finish() override redirects to finishAndRemoveTask()
// and kills the OS process.  Used by the "Exit Emulator" menu item so
// the user gets a deterministic shutdown — without this, finish()
// leaves the task chip in recents and tapping it warm-starts a new
// activity, looking like the app failed to quit.  Call before pushing
// SDL_EVENT_QUIT so the flag is in place by the time main() returns.
// No-op on non-Android builds.
void ATAndroid_RequestQuitAndRemoveTask();
