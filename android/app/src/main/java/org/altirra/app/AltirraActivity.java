package org.altirra.app;

import android.Manifest;
import android.content.Context;
import android.content.Intent;
import android.content.pm.PackageManager;
import android.net.Uri;
import android.os.Build;
import android.os.Environment;
import android.os.storage.StorageManager;
import android.os.storage.StorageVolume;
import android.provider.Settings;
import android.view.View;
import android.view.Window;
import android.view.WindowInsets;
import android.view.WindowInsetsController;

import java.io.File;
import java.util.List;

import org.libsdl.app.SDLActivity;

/**
 * Altirra Android activity.
 *
 * Extends SDL3's SDLActivity which handles:
 * - GL surface creation and lifecycle
 * - Native library loading (libmain.so via libSDL3.so)
 * - Touch event routing to SDL
 * - Gamepad/keyboard input
 * - Audio device management
 *
 * We override only what's needed for Altirra-specific behavior
 * (runtime storage permission request for ROM/disk image access).
 *
 * Uses the platform APIs (Activity.checkSelfPermission /
 * requestPermissions) directly to avoid a dependency on AndroidX.
 */
public class AltirraActivity extends SDLActivity {

    private static final int PERM_REQUEST_STORAGE = 0x4154;  // "AT"

    /**
     * When true, the next call to {@link #finish()} (typically from
     * SDLActivity's SDLMain after our native main loop returns) is
     * redirected to {@link #finishAndRemoveTask()} so the recents
     * entry disappears, and we kill the OS process so Android cannot
     * warm-start a new activity into our half-torn-down state.
     *
     * Set from native via {@link #requestQuitAndRemoveTask()} when the
     * user picks "Exit Emulator" from the hamburger menu — that path
     * promises a true exit, not a normal back-stack pop.  Other
     * finish() callers (broken-libraries dialog, system back stack
     * navigation, etc.) leave this false and get the standard
     * Android behaviour.
     */
    private volatile boolean mQuitAndRemoveTask = false;

    @Override
    protected String[] getLibraries() {
        return new String[]{
            "SDL3",
            "main"  // Our native library (AltirraSDL compiled as libmain.so)
        };
    }

    @Override
    protected String getMainFunction() {
        return "main";
    }

    /**
     * Called from native via JNI by the "Exit Emulator" menu handler
     * before it asks the SDL event loop to shut down.  Marks the
     * activity so the eventual finish() call removes the task from
     * recents and kills the process.
     */
    public void requestQuitAndRemoveTask() {
        mQuitAndRemoveTask = true;
    }

    @Override
    public void finish() {
        if (mQuitAndRemoveTask) {
            // True exit path requested by the user via Exit Emulator.
            // finishAndRemoveTask() drops the recents entry so tapping
            // the task chip cannot warm-start the app — without this,
            // Android keeps the chip and re-launches into a new
            // activity, which looks like the app "didn't really quit".
            // We deliberately don't follow up with Process.killProcess:
            // finishAndRemoveTask is asynchronous, killing the process
            // immediately afterwards races the task-removal commit and
            // can leave a stale chip in recents.  Android cleans up the
            // process on its own once all activities are finished.
            finishAndRemoveTask();
        } else {
            super.finish();
        }
    }

    /**
     * Called from native via JNI to fire the runtime storage permission
     * dialog.  No-op if permission is already granted or the platform
     * doesn't require it (API >= 33 where scoped storage is used).
     *
     * The result is NOT reported back to native — native code polls
     * {@link #hasStoragePermission()} after the dialog would have been
     * dismissed (typically on the next file-browser open).
     */
    public void requestStoragePermission() {
        // API 33+ uses scoped storage / MediaStore / SAF.  The legacy
        // READ_EXTERNAL_STORAGE permission is declared with
        // maxSdkVersion="32" in the manifest so there is nothing to
        // request at runtime on newer devices — actual access to ROM
        // files there is mediated via SAF from the file browser.
        if (Build.VERSION.SDK_INT >= 33) {
            return;
        }

        if (Build.VERSION.SDK_INT < Build.VERSION_CODES.M) {
            // API < 23 grants all manifest permissions at install time;
            // no runtime request needed.  (minSdk is 24, but guard anyway.)
            return;
        }

        if (checkSelfPermission(Manifest.permission.READ_EXTERNAL_STORAGE)
                == PackageManager.PERMISSION_GRANTED) {
            return;
        }

        requestPermissions(
            new String[] { Manifest.permission.READ_EXTERNAL_STORAGE },
            PERM_REQUEST_STORAGE);
    }

    /**
     * Returns true if the app can read public external storage
     * (Downloads, /sdcard/**).
     *
     * On API 30+ (Android 11+) this is true iff the user has granted
     * MANAGE_EXTERNAL_STORAGE ("All files access") via the system
     * Settings page.  Our app needs this because users pick arbitrary
     * ROM / disk-image / cassette files from anywhere.
     *
     * Below API 30 we fall back to READ_EXTERNAL_STORAGE.
     */
    public boolean hasStoragePermission() {
        if (Build.VERSION.SDK_INT >= 30) {
            return Environment.isExternalStorageManager();
        }
        if (Build.VERSION.SDK_INT < Build.VERSION_CODES.M) {
            return true;
        }
        return checkSelfPermission(Manifest.permission.READ_EXTERNAL_STORAGE)
            == PackageManager.PERMISSION_GRANTED;
    }

    /**
     * Opens the system Settings screen where the user can grant
     * "All files access" (MANAGE_EXTERNAL_STORAGE) to Altirra.
     * On pre-30 devices this falls back to the ordinary runtime
     * permission dialog.
     *
     * Called from native (JNI) when the user taps a "Grant access"
     * button in the mobile file browser's permission prompt.
     */
    public void openManageStoragePermissionSettings() {
        if (Build.VERSION.SDK_INT >= 30) {
            try {
                Intent intent = new Intent(
                    Settings.ACTION_MANAGE_APP_ALL_FILES_ACCESS_PERMISSION);
                intent.setData(Uri.parse("package:" + getPackageName()));
                intent.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK);
                startActivity(intent);
            } catch (Exception e) {
                // Fallback: generic "All files access" list screen
                try {
                    Intent intent = new Intent(
                        Settings.ACTION_MANAGE_ALL_FILES_ACCESS_PERMISSION);
                    intent.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK);
                    startActivity(intent);
                } catch (Exception e2) {
                    // Give up silently — user will have to find it in Settings.
                }
            }
        } else {
            requestStoragePermission();
        }
    }

    /**
     * Returns a simple text encoding of mounted storage volumes.
     * Each volume is one line: "path\tlabel\tremovable\n"
     * where removable is "1" or "0".
     *
     * Uses StorageManager.getStorageVolumes() (API 24+, our minSdk).
     * On API 30+ we get the directory directly from StorageVolume;
     * below that we derive it from the volume UUID or use the primary
     * external storage path.
     *
     * Called from native (JNI) to populate the file browser shortcut
     * bar with SD card / USB drive entries.
     */
    /**
     * Toggle Android's immersive mode.  When enabled, the system status
     * bar and navigation bar are hidden and the emulator canvas
     * occupies the entire physical screen.  Swiping from the top or
     * bottom edge transiently reveals them, then they auto-hide again
     * (BEHAVIOR_SHOW_TRANSIENT_BARS_BY_SWIPE on API 30+, IMMERSIVE_STICKY
     * on older devices).
     *
     * Called from native via JNI when the user toggles the "Full
     * Screen" setting in Gaming Mode.  Must be invoked on the UI
     * thread; we marshal via runOnUiThread so the JNI caller can be on
     * any thread.
     */
    public void setImmersiveMode(final boolean enabled) {
        runOnUiThread(new Runnable() {
            @Override
            public void run() {
                applyImmersiveMode(enabled);
            }
        });
    }

    private void applyImmersiveMode(boolean enabled) {
        Window window = getWindow();
        if (window == null) return;

        if (Build.VERSION.SDK_INT >= 30) {
            // API 30+ uses WindowInsetsController and decor-fits-system-
            // windows = false to draw under the bars.  This is the
            // modern, non-deprecated path.
            window.setDecorFitsSystemWindows(!enabled);
            WindowInsetsController controller = window.getInsetsController();
            if (controller != null) {
                if (enabled) {
                    controller.hide(WindowInsets.Type.statusBars()
                                  | WindowInsets.Type.navigationBars());
                    controller.setSystemBarsBehavior(
                        WindowInsetsController.BEHAVIOR_SHOW_TRANSIENT_BARS_BY_SWIPE);
                } else {
                    controller.show(WindowInsets.Type.statusBars()
                                  | WindowInsets.Type.navigationBars());
                }
            }
        } else {
            // Pre-30: legacy setSystemUiVisibility flags.  Sticky-immersive
            // means the bars come back transiently on a swipe gesture
            // and auto-hide.  Same UX as the modern API above.
            View decor = window.getDecorView();
            if (decor == null) return;
            int flags;
            if (enabled) {
                flags = View.SYSTEM_UI_FLAG_LAYOUT_STABLE
                      | View.SYSTEM_UI_FLAG_LAYOUT_HIDE_NAVIGATION
                      | View.SYSTEM_UI_FLAG_LAYOUT_FULLSCREEN
                      | View.SYSTEM_UI_FLAG_HIDE_NAVIGATION
                      | View.SYSTEM_UI_FLAG_FULLSCREEN
                      | View.SYSTEM_UI_FLAG_IMMERSIVE_STICKY;
            } else {
                flags = View.SYSTEM_UI_FLAG_LAYOUT_STABLE
                      | View.SYSTEM_UI_FLAG_LAYOUT_FULLSCREEN;
            }
            decor.setSystemUiVisibility(flags);
        }
    }

    public String getStorageVolumes() {
        StorageManager sm = (StorageManager) getSystemService(Context.STORAGE_SERVICE);
        if (sm == null) return "";

        List<StorageVolume> volumes = sm.getStorageVolumes();
        StringBuilder sb = new StringBuilder();

        for (StorageVolume vol : volumes) {
            // Skip volumes that aren't mounted.  Accept both
            // read-write and read-only since the emulator only reads.
            String state = vol.getState();
            if (state == null
                    || (!state.equals(Environment.MEDIA_MOUNTED)
                        && !state.equals(Environment.MEDIA_MOUNTED_READ_ONLY)))
                continue;

            String path = null;

            if (Build.VERSION.SDK_INT >= 30) {
                // API 30+: StorageVolume.getDirectory() returns the
                // mount point directly.
                File dir = vol.getDirectory();
                if (dir != null)
                    path = dir.getAbsolutePath();
            }

            if (path == null) {
                // Fallback for API 24-29: primary volume is
                // Environment.getExternalStorageDirectory(); secondary
                // volumes are at /storage/<uuid>.
                if (vol.isPrimary()) {
                    path = Environment.getExternalStorageDirectory()
                            .getAbsolutePath();
                } else {
                    String uuid = vol.getUuid();
                    if (uuid != null)
                        path = "/storage/" + uuid;
                }
            }

            if (path == null) continue;

            String label = vol.getDescription(this);
            if (label == null) label = path;

            sb.append(path);
            sb.append('\t');
            sb.append(label);
            sb.append('\t');
            sb.append(vol.isRemovable() ? '1' : '0');
            sb.append('\n');
        }

        return sb.toString();
    }
}
