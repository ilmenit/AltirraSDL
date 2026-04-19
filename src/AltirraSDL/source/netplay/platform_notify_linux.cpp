// Altirra SDL3 netplay - Linux notification backend
//
// Uses libnotify via dlopen() so there is no link-time dependency.  If
// libnotify-4.so is missing at runtime, Post() simply returns 0 and
// the common code falls back to just SDL_FlashWindow + chime.
//
// We never block: notify_notification_show() dispatches to D-Bus
// asynchronously.  The one free-threaded concern is libnotify's global
// init state; we serialise by only calling init/uninit from the main
// thread (netplay UI invokes us from there).

#include <stdafx.h>

#define ALTIRRA_NETPLAY_NOTIFY_LINUX 1
#include "platform_notify.h"

#include <dlfcn.h>
#include <cstdio>
#include <cstring>

namespace ATNetplay {

namespace {

// Subset of libnotify-4 ABI used here.  GLib's `gboolean` is `int`.
typedef int gboolean;

struct NotifyBackend {
	void*     libHandle = nullptr;
	bool      initOk    = false;
	// libnotify
	gboolean (*notify_init)(const char* app)             = nullptr;
	void     (*notify_uninit)()                          = nullptr;
	void*    (*notify_notification_new)(const char*, const char*, const char*) = nullptr;
	gboolean (*notify_notification_show)(void*, void**)  = nullptr;
	void     (*g_object_unref)(void*)                    = nullptr;
};
NotifyBackend g;

void* LoadSym(void* h, const char* name) {
	dlerror();  // clear
	void* s = dlsym(h, name);
	if (!s) { (void)dlerror(); }
	return s;
}

bool LoadLibnotify() {
	if (g.libHandle) return true;
	const char* candidates[] = {
		"libnotify.so.4",
		"libnotify.so",
	};
	void* h = nullptr;
	for (const char* c : candidates) {
		h = dlopen(c, RTLD_NOW | RTLD_LOCAL);
		if (h) break;
	}
	if (!h) return false;

	g.libHandle = h;
	g.notify_init              = (gboolean(*)(const char*))LoadSym(h, "notify_init");
	g.notify_uninit            = (void(*)())LoadSym(h, "notify_uninit");
	g.notify_notification_new  = (void*(*)(const char*, const char*, const char*))LoadSym(h, "notify_notification_new");
	g.notify_notification_show = (gboolean(*)(void*, void**))LoadSym(h, "notify_notification_show");

	// g_object_unref lives in libgobject-2.0.  Many distros publish it
	// into libnotify's resolution graph so dlsym(RTLD_DEFAULT,...)
	// finds it after dlopen'ing libnotify; if not, open it directly.
	g.g_object_unref = (void(*)(void*))dlsym(RTLD_DEFAULT, "g_object_unref");
	if (!g.g_object_unref) {
		void* glib = dlopen("libgobject-2.0.so.0", RTLD_NOW | RTLD_LOCAL);
		if (glib) g.g_object_unref = (void(*)(void*))dlsym(glib, "g_object_unref");
	}

	if (!g.notify_init || !g.notify_notification_new
	    || !g.notify_notification_show || !g.notify_uninit) {
		dlclose(h);
		g = NotifyBackend{};
		return false;
	}
	return true;
}

} // anonymous

int PlatformNotifyInit(const char* appName) {
	if (g.initOk) return 1;
	if (!LoadLibnotify()) return 0;
	if (!g.notify_init(appName)) {
		return 0;
	}
	g.initOk = true;
	return 1;
}

void PlatformNotifyShutdown() {
	if (g.initOk && g.notify_uninit) {
		g.notify_uninit();
	}
	if (g.libHandle) {
		// Leave libnotify loaded — unloading it at shutdown can race
		// with GLib's pending callbacks.  Just drop our reference.
	}
	g = NotifyBackend{};
}

int PlatformNotifyPost(const char* title, const char* body) {
	if (!g.initOk) return 0;
	// Icon: "dialog-information" is part of freedesktop icon spec;
	// every DE ships it.
	void* n = g.notify_notification_new(title, body, "dialog-information");
	if (!n) return 0;
	g.notify_notification_show(n, nullptr);
	if (g.g_object_unref) g.g_object_unref(n);
	return 1;
}

} // namespace ATNetplay
