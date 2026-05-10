//	AltirraSDL - Android platform helpers (JNI implementation)
//	See android_platform.h for the API contract.
//
//	On non-Android builds this file compiles to a set of stubs so it can
//	live unconditionally in the CMake source list.

#include <stdafx.h>
#include "android_platform.h"

#ifdef __ANDROID__

#include <jni.h>
#include <string>
#include <android/log.h>
#include <SDL3/SDL.h>
#include <SDL3/SDL_system.h>

#define LOGTAG "AltirraSDL"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO,  LOGTAG, __VA_ARGS__)
#define LOGW(...) __android_log_print(ANDROID_LOG_WARN,  LOGTAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOGTAG, __VA_ARGS__)

namespace {

bool		g_insetsValid   = false;
ATSafeInsets	g_insets;
std::string	g_downloadsDir;
bool		g_downloadsLookupDone = false;
std::vector<ATAndroidVolume>	g_storageVolumes;
bool		g_volumesValid = false;

// Helper: scoped-delete local JNI refs without forgetting.
struct JLocal {
	JNIEnv *env;
	jobject obj;
	JLocal(JNIEnv *e, jobject o) : env(e), obj(o) {}
	~JLocal() { if (obj) env->DeleteLocalRef(obj); }
	operator jobject() const { return obj; }
	operator bool() const { return obj != nullptr; }
};

// Helper: catch + clear any pending JNI exception so we don't crash the
// VM on a later call.  Logs the exception class name for diagnosis.
bool CheckAndClearException(JNIEnv *env, const char *where) {
	if (!env->ExceptionCheck())
		return false;
	jthrowable ex = env->ExceptionOccurred();
	env->ExceptionClear();
	if (ex) {
		jclass exClass = env->GetObjectClass(ex);
		if (exClass) {
			jmethodID getName = env->GetMethodID(exClass,
				"getClass", "()Ljava/lang/Class;");
			(void)getName;
		}
		env->DeleteLocalRef(ex);
	}
	LOGW("JNI exception at %s (cleared)", where);
	return true;
}

// Fetch the SDL activity reference.  SDL3 returns a global ref owned by
// SDL; we must not delete it.
jobject GetActivity() {
	return (jobject)SDL_GetAndroidActivity();
}

JNIEnv *GetEnv() {
	return (JNIEnv *)SDL_GetAndroidJNIEnv();
}

// Query WindowInsets via:
//   Activity.getWindow().getDecorView().getRootWindowInsets()
//   .getSystemWindowInset{Top,Bottom,Left,Right}()
// On API 30+ these methods are deprecated in favor of
// WindowInsets.getInsets(WindowInsetsCompat.Type.systemBars()) but the
// deprecated getters still work and return equivalent values.
void QueryInsetsLocked() {
	g_insets = ATSafeInsets{};

	JNIEnv *env = GetEnv();
	jobject activity = GetActivity();
	if (!env || !activity) {
		LOGW("QueryInsets: no JNI env / activity");
		return;
	}

	// Activity.getWindow()
	jclass activityClass = env->GetObjectClass(activity);
	if (!activityClass) { CheckAndClearException(env, "GetObjectClass activity"); return; }
	JLocal _acls(env, activityClass);

	jmethodID getWindow = env->GetMethodID(activityClass,
		"getWindow", "()Landroid/view/Window;");
	if (!getWindow) { CheckAndClearException(env, "getWindow mid"); return; }

	JLocal window(env, env->CallObjectMethod(activity, getWindow));
	if (CheckAndClearException(env, "Activity.getWindow") || !window) return;

	// Window.getDecorView()
	jclass windowClass = env->GetObjectClass(window);
	JLocal _wcls(env, windowClass);
	jmethodID getDecorView = env->GetMethodID(windowClass,
		"getDecorView", "()Landroid/view/View;");
	if (!getDecorView) { CheckAndClearException(env, "getDecorView mid"); return; }

	JLocal decor(env, env->CallObjectMethod(window, getDecorView));
	if (CheckAndClearException(env, "Window.getDecorView") || !decor) return;

	// View.getRootWindowInsets()
	jclass viewClass = env->GetObjectClass(decor);
	JLocal _vcls(env, viewClass);
	jmethodID getRootInsets = env->GetMethodID(viewClass,
		"getRootWindowInsets", "()Landroid/view/WindowInsets;");
	if (!getRootInsets) { CheckAndClearException(env, "getRootWindowInsets mid"); return; }

	JLocal insets(env, env->CallObjectMethod(decor, getRootInsets));
	if (CheckAndClearException(env, "View.getRootWindowInsets")) return;

	if (!insets) {
		// View not yet attached; the caller will re-invalidate on next
		// resize and we will try again.
		LOGI("QueryInsets: root insets null (view not attached yet)");
		return;
	}

	jclass insetsClass = env->GetObjectClass(insets);
	JLocal _icls(env, insetsClass);

	auto getIntMethod = [&](const char *name) -> int {
		jmethodID mid = env->GetMethodID(insetsClass, name, "()I");
		if (!mid) {
			CheckAndClearException(env, name);
			return 0;
		}
		jint v = env->CallIntMethod(insets, mid);
		if (CheckAndClearException(env, name))
			return 0;
		return v;
	};

	// The deprecated-but-still-working getters give us the system bar
	// insets in pixels.  We take the max of these and the display cutout
	// so a camera notch doesn't punch through our UI.
	int top    = getIntMethod("getSystemWindowInsetTop");
	int bottom = getIntMethod("getSystemWindowInsetBottom");
	int left   = getIntMethod("getSystemWindowInsetLeft");
	int right  = getIntMethod("getSystemWindowInsetRight");

	// DisplayCutout — optional, only available API 28+
	{
		jmethodID getCutout = env->GetMethodID(insetsClass,
			"getDisplayCutout", "()Landroid/view/DisplayCutout;");
		if (getCutout) {
			JLocal cutout(env, env->CallObjectMethod(insets, getCutout));
			CheckAndClearException(env, "getDisplayCutout");
			if (cutout) {
				jclass cutoutClass = env->GetObjectClass(cutout);
				JLocal _ccls(env, cutoutClass);
				auto getCutoutInt = [&](const char *name) -> int {
					jmethodID m = env->GetMethodID(cutoutClass, name, "()I");
					if (!m) { CheckAndClearException(env, name); return 0; }
					jint v = env->CallIntMethod(cutout, m);
					if (CheckAndClearException(env, name)) return 0;
					return v;
				};
				int ct = getCutoutInt("getSafeInsetTop");
				int cb = getCutoutInt("getSafeInsetBottom");
				int cl = getCutoutInt("getSafeInsetLeft");
				int cr = getCutoutInt("getSafeInsetRight");
				if (ct > top)    top    = ct;
				if (cb > bottom) bottom = cb;
				if (cl > left)   left   = cl;
				if (cr > right)  right  = cr;
			}
		} else {
			CheckAndClearException(env, "getDisplayCutout mid");
		}
	}

	g_insets.top    = top;
	g_insets.bottom = bottom;
	g_insets.left   = left;
	g_insets.right  = right;

	LOGI("QueryInsets: top=%d bottom=%d left=%d right=%d",
		top, bottom, left, right);
}

void QueryDownloadsDir() {
	g_downloadsLookupDone = true;
	g_downloadsDir.clear();

	JNIEnv *env = GetEnv();
	if (!env) return;

	jclass envClass = env->FindClass("android/os/Environment");
	if (!envClass) { CheckAndClearException(env, "FindClass Environment"); return; }
	JLocal _ecls(env, envClass);

	// Environment.DIRECTORY_DOWNLOADS (public static final String)
	jfieldID dlField = env->GetStaticFieldID(envClass,
		"DIRECTORY_DOWNLOADS", "Ljava/lang/String;");
	if (!dlField) { CheckAndClearException(env, "DIRECTORY_DOWNLOADS fid"); return; }
	JLocal dlName(env, env->GetStaticObjectField(envClass, dlField));
	if (!dlName) return;

	// Environment.getExternalStoragePublicDirectory(String)
	jmethodID getDir = env->GetStaticMethodID(envClass,
		"getExternalStoragePublicDirectory",
		"(Ljava/lang/String;)Ljava/io/File;");
	if (!getDir) { CheckAndClearException(env, "getExternalStoragePublicDirectory mid"); return; }

	JLocal file(env, env->CallStaticObjectMethod(envClass, getDir, (jstring)dlName.obj));
	if (CheckAndClearException(env, "getExternalStoragePublicDirectory") || !file) return;

	// File.getAbsolutePath()
	jclass fileClass = env->GetObjectClass(file);
	JLocal _fcls(env, fileClass);
	jmethodID getPath = env->GetMethodID(fileClass,
		"getAbsolutePath", "()Ljava/lang/String;");
	if (!getPath) { CheckAndClearException(env, "getAbsolutePath mid"); return; }

	JLocal pathStr(env, env->CallObjectMethod(file, getPath));
	if (CheckAndClearException(env, "File.getAbsolutePath") || !pathStr) return;

	const char *c = env->GetStringUTFChars((jstring)pathStr.obj, nullptr);
	if (c) {
		g_downloadsDir = c;
		env->ReleaseStringUTFChars((jstring)pathStr.obj, c);
		LOGI("Downloads dir = %s", g_downloadsDir.c_str());
	}
}

void QueryStorageVolumes() {
	g_volumesValid = true;
	g_storageVolumes.clear();

	JNIEnv *env = GetEnv();
	jobject activity = GetActivity();
	if (!env || !activity) return;

	jclass activityClass = env->GetObjectClass(activity);
	if (!activityClass) { CheckAndClearException(env, "vol: GetObjectClass"); return; }
	JLocal _acls(env, activityClass);

	jmethodID mid = env->GetMethodID(activityClass,
		"getStorageVolumes", "()Ljava/lang/String;");
	if (!mid) {
		CheckAndClearException(env, "getStorageVolumes mid");
		LOGW("AltirraActivity.getStorageVolumes() missing");
		return;
	}

	JLocal resultObj(env, env->CallObjectMethod(activity, mid));
	if (CheckAndClearException(env, "getStorageVolumes") || !resultObj)
		return;

	const char *raw = env->GetStringUTFChars((jstring)resultObj.obj, nullptr);
	if (!raw) return;

	std::string data(raw);
	env->ReleaseStringUTFChars((jstring)resultObj.obj, raw);

	// Parse tab-separated lines: path\tlabel\tremovable\n
	size_t pos = 0;
	while (pos < data.size()) {
		size_t eol = data.find('\n', pos);
		if (eol == std::string::npos) eol = data.size();
		std::string line = data.substr(pos, eol - pos);
		pos = eol + 1;

		if (line.empty()) continue;

		size_t tab1 = line.find('\t');
		if (tab1 == std::string::npos) continue;
		size_t tab2 = line.find('\t', tab1 + 1);
		if (tab2 == std::string::npos) continue;

		ATAndroidVolume vol;
		vol.path = line.substr(0, tab1);
		vol.label = line.substr(tab1 + 1, tab2 - tab1 - 1);
		vol.removable = (line.substr(tab2 + 1) == "1");
		g_storageVolumes.push_back(std::move(vol));

		LOGI("Volume: path=%s label=%s removable=%d",
			g_storageVolumes.back().path.c_str(),
			g_storageVolumes.back().label.c_str(),
			g_storageVolumes.back().removable ? 1 : 0);
	}
}

} // namespace

ATSafeInsets ATAndroid_GetSafeInsets() {
	if (!g_insetsValid) {
		QueryInsetsLocked();
		g_insetsValid = true;
	}
	return g_insets;
}

void ATAndroid_InvalidateSafeInsets() {
	g_insetsValid = false;
}

bool ATAndroid_RequestStoragePermission() {
	JNIEnv *env = GetEnv();
	jobject activity = GetActivity();
	if (!env || !activity) return false;

	jclass activityClass = env->GetObjectClass(activity);
	if (!activityClass) { CheckAndClearException(env, "perm: GetObjectClass"); return false; }
	JLocal _acls(env, activityClass);

	jmethodID mid = env->GetMethodID(activityClass,
		"requestStoragePermission", "()V");
	if (!mid) {
		// Method missing (old activity class).  Swallow the exception
		// and fall back to the SDK permission API directly.
		CheckAndClearException(env, "requestStoragePermission mid");
		LOGW("AltirraActivity.requestStoragePermission() missing");
		return false;
	}

	env->CallVoidMethod(activity, mid);
	if (CheckAndClearException(env, "requestStoragePermission")) return false;
	LOGI("Storage permission dialog requested");
	return true;
}

bool ATAndroid_HasStoragePermission() {
	JNIEnv *env = GetEnv();
	jobject activity = GetActivity();
	if (!env || !activity) return false;

	jclass activityClass = env->GetObjectClass(activity);
	if (!activityClass) { CheckAndClearException(env, "hasPerm: GetObjectClass"); return false; }
	JLocal _acls(env, activityClass);

	jmethodID mid = env->GetMethodID(activityClass,
		"hasStoragePermission", "()Z");
	if (!mid) {
		CheckAndClearException(env, "hasStoragePermission mid");
		return false;
	}
	jboolean r = env->CallBooleanMethod(activity, mid);
	if (CheckAndClearException(env, "hasStoragePermission")) return false;
	return r == JNI_TRUE;
}

const char *ATAndroid_GetPublicDownloadsDir() {
	if (!g_downloadsLookupDone)
		QueryDownloadsDir();
	return g_downloadsDir.empty() ? "" : g_downloadsDir.c_str();
}

const std::vector<ATAndroidVolume>& ATAndroid_GetStorageVolumes() {
	if (!g_volumesValid)
		QueryStorageVolumes();
	return g_storageVolumes;
}

void ATAndroid_InvalidateStorageVolumes() {
	g_volumesValid = false;
}

// --- Vibrator cache ---
// Getting a reference to the Vibrator service is moderately expensive,
// so we cache a global reference the first time we succeed.
namespace {

jobject  g_vibratorRef      = nullptr;  // global JNI ref
jmethodID g_vibratorVibrate = nullptr;  // (J)V  — long milliseconds overload
bool      g_vibratorLookupDone = false;

void EnsureVibrator() {
	if (g_vibratorLookupDone) return;
	g_vibratorLookupDone = true;

	JNIEnv *env = GetEnv();
	jobject activity = GetActivity();
	if (!env || !activity) return;

	jclass activityClass = env->GetObjectClass(activity);
	if (!activityClass) { CheckAndClearException(env, "vib: GetObjectClass"); return; }
	JLocal _acls(env, activityClass);

	jmethodID getSvc = env->GetMethodID(activityClass,
		"getSystemService",
		"(Ljava/lang/String;)Ljava/lang/Object;");
	if (!getSvc) { CheckAndClearException(env, "vib: getSystemService mid"); return; }

	jstring name = env->NewStringUTF("vibrator");
	jobject vibLocal = env->CallObjectMethod(activity, getSvc, name);
	env->DeleteLocalRef(name);
	if (CheckAndClearException(env, "vib: getSystemService(vibrator)") || !vibLocal)
		return;

	jclass vibClass = env->GetObjectClass(vibLocal);
	if (!vibClass) {
		CheckAndClearException(env, "vib: GetObjectClass vibrator");
		env->DeleteLocalRef(vibLocal);
		return;
	}

	// Vibrator.vibrate(long) is deprecated on API 26+ but still works.
	// Skipping VibrationEffect to avoid API-level branching.
	jmethodID vibrate = env->GetMethodID(vibClass, "vibrate", "(J)V");
	if (!vibrate) {
		CheckAndClearException(env, "vib: vibrate(J) mid");
		env->DeleteLocalRef(vibLocal);
		env->DeleteLocalRef(vibClass);
		return;
	}

	g_vibratorVibrate = vibrate;
	g_vibratorRef = env->NewGlobalRef(vibLocal);
	env->DeleteLocalRef(vibLocal);
	env->DeleteLocalRef(vibClass);
	LOGI("Vibrator service cached");
}

} // namespace

void ATAndroid_Vibrate(int durationMs) {
	if (durationMs <= 0) return;
	EnsureVibrator();
	if (!g_vibratorRef || !g_vibratorVibrate) return;

	JNIEnv *env = GetEnv();
	if (!env) return;

	env->CallVoidMethod(g_vibratorRef, g_vibratorVibrate, (jlong)durationMs);
	CheckAndClearException(env, "vib: vibrate call");
}

bool ATAndroid_OpenManageStorageSettings() {
	JNIEnv *env = GetEnv();
	jobject activity = GetActivity();
	if (!env || !activity) return false;

	jclass activityClass = env->GetObjectClass(activity);
	if (!activityClass) { CheckAndClearException(env, "openMgr: GetObjectClass"); return false; }
	JLocal _acls(env, activityClass);

	jmethodID mid = env->GetMethodID(activityClass,
		"openManageStoragePermissionSettings", "()V");
	if (!mid) {
		CheckAndClearException(env, "openManageStoragePermissionSettings mid");
		return false;
	}
	env->CallVoidMethod(activity, mid);
	if (CheckAndClearException(env, "openManageStoragePermissionSettings")) return false;
	LOGI("Opened 'All files access' settings page");
	return true;
}

void ATAndroid_SetImmersiveMode(bool enabled) {
	JNIEnv *env = GetEnv();
	jobject activity = GetActivity();
	if (!env || !activity) return;

	jclass activityClass = env->GetObjectClass(activity);
	if (!activityClass) {
		CheckAndClearException(env, "imm: GetObjectClass");
		return;
	}
	JLocal _acls(env, activityClass);

	jmethodID mid = env->GetMethodID(activityClass,
		"setImmersiveMode", "(Z)V");
	if (!mid) {
		CheckAndClearException(env, "setImmersiveMode mid");
		LOGW("AltirraActivity.setImmersiveMode missing");
		return;
	}
	env->CallVoidMethod(activity, mid, (jboolean)(enabled ? JNI_TRUE : JNI_FALSE));
	if (CheckAndClearException(env, "setImmersiveMode call")) return;

	// The system bar visibility change will trigger a window-insets
	// callback; flush our cached insets so the next GetSafeInsets()
	// pulls the updated values.
	ATAndroid_InvalidateSafeInsets();
	LOGI("Immersive mode %s", enabled ? "enabled" : "disabled");
}

void ATAndroid_RequestQuitAndRemoveTask() {
	JNIEnv *env = GetEnv();
	jobject activity = GetActivity();
	if (!env || !activity) return;

	jclass activityClass = env->GetObjectClass(activity);
	if (!activityClass) {
		CheckAndClearException(env, "quit: GetObjectClass");
		return;
	}
	JLocal _acls(env, activityClass);

	// Sets a Java-side flag that the AltirraActivity.finish() override
	// reads when SDLActivity's main-thread runner calls finish() after
	// our native main() returns.  With the flag set, finish() routes
	// to finishAndRemoveTask() + Process.killProcess() so the recents
	// entry disappears and the OS process actually dies — without it,
	// "Exit Emulator" leaves the task in recents and clicking it warm-
	// starts a new activity, which looks like the app didn't really
	// quit.  Java handles the flag; we only schedule it here.
	jmethodID mid = env->GetMethodID(activityClass,
		"requestQuitAndRemoveTask", "()V");
	if (!mid) {
		CheckAndClearException(env, "requestQuitAndRemoveTask mid");
		LOGW("AltirraActivity.requestQuitAndRemoveTask missing");
		return;
	}
	env->CallVoidMethod(activity, mid);
	if (CheckAndClearException(env, "requestQuitAndRemoveTask call")) return;
	LOGI("Quit-and-remove-task requested");
}

#else // !__ANDROID__

ATSafeInsets ATAndroid_GetSafeInsets() { return ATSafeInsets{}; }
void ATAndroid_InvalidateSafeInsets() {}
bool ATAndroid_RequestStoragePermission() { return false; }
bool ATAndroid_HasStoragePermission() { return true; }
const char *ATAndroid_GetPublicDownloadsDir() { return ""; }
bool ATAndroid_OpenManageStorageSettings() { return false; }
void ATAndroid_Vibrate(int) {}
void ATAndroid_SetImmersiveMode(bool) {}
void ATAndroid_RequestQuitAndRemoveTask() {}

namespace {
std::vector<ATAndroidVolume> g_emptyVolumes;
}

const std::vector<ATAndroidVolume>& ATAndroid_GetStorageVolumes() {
	return g_emptyVolumes;
}
void ATAndroid_InvalidateStorageVolumes() {}

#endif
