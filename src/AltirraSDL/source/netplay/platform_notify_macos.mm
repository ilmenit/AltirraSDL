// Altirra SDL3 netplay - macOS notification backend
//
// Uses UNUserNotificationCenter (modern API, available 10.14+).
// Requests notification permission on first init; if the user denies,
// subsequent Post() calls no-op quietly.

#include <stdafx.h>

#define ALTIRRA_NETPLAY_NOTIFY_MACOS 1
#include "platform_notify.h"

#if defined(__APPLE__)

#import <Foundation/Foundation.h>
#import <UserNotifications/UserNotifications.h>

namespace ATNetplay {

namespace {
bool g_ok = false;
}

int PlatformNotifyInit(const char* appName) {
	(void)appName;
	if (g_ok) return 1;
	UNUserNotificationCenter* center = [UNUserNotificationCenter currentNotificationCenter];
	UNAuthorizationOptions opts = UNAuthorizationOptionAlert |
	                              UNAuthorizationOptionSound;
	[center requestAuthorizationWithOptions:opts
		completionHandler:^(BOOL granted, NSError* err) {
			(void)granted; (void)err;
		}];
	g_ok = true;
	return 1;
}

void PlatformNotifyShutdown() {
	g_ok = false;
}

int PlatformNotifyPost(const char* title, const char* body) {
	if (!g_ok) return 0;

	// Build under MRR so this file compiles without -fobjc-arc.  The
	// content object is retained by UNNotificationRequest, so we can
	// release our own alloc/init reference immediately after.
	@autoreleasepool {
		UNMutableNotificationContent* c = [[UNMutableNotificationContent alloc] init];
		c.title = [NSString stringWithUTF8String:(title ? title : "AltirraSDL")];
		c.body  = [NSString stringWithUTF8String:(body ? body : "")];

		NSString* reqId = [[NSUUID UUID] UUIDString];
		UNNotificationRequest* req = [UNNotificationRequest
			requestWithIdentifier:reqId content:c trigger:nil];
		[[UNUserNotificationCenter currentNotificationCenter]
			addNotificationRequest:req
			 withCompletionHandler:^(NSError* err) { (void)err; }];
		[c release];
	}
	return 1;
}

} // namespace ATNetplay

#endif // __APPLE__
