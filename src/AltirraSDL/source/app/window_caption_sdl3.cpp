// Window title updater for the SDL3 build.
//
// Mirrors what Windows Altirra's ATUIWindowCaptionUpdater renders into
// the main window title: program/version, current profile name, and a
// summary of the active hardware configuration (machine type, video
// standard, memory size, U1MB / VBXE / Rapidus / BASIC, extended CPU).
//
// Updates only when the simulator config-change counter or the active
// profile id changes, so calling it every frame is cheap.

#include <stdafx.h>
#include <SDL3/SDL.h>

#include <vd2/system/VDString.h>
#include <vd2/system/text.h>

#include "simulator.h"
#include "settings.h"
#include "versioninfo.h"
#include "constants.h"
#include "cpu.h"

extern ATSimulator g_sim;
extern SDL_Window *g_pWindow;

namespace {
	void AppendSegment(VDStringW& info, const wchar_t *s) {
		if (!info.empty())
			info += L" / ";
		info += s;
	}
}

void ATUpdateWindowCaption() {
	if (!g_pWindow)
		return;

	static uint32 s_lastConfigCounter = ~uint32(0);
	static uint32 s_lastProfileId     = ~uint32(0);

	const uint32 cc  = g_sim.GetConfigChangeCounter();
	const uint32 pid = ATSettingsGetCurrentProfileId();
	if (cc == s_lastConfigCounter && pid == s_lastProfileId)
		return;
	s_lastConfigCounter = cc;
	s_lastProfileId     = pid;

	// Build "AltirraSDL/<OS>/<arch> <version>..." instead of the
	// shared AT_FULL_VERSION_STR (which says "Altirra/x64 ...").
#if defined(__linux__) && defined(__ANDROID__)
	#define AT_SDL_OS_STR L"/Android"
#elif defined(__linux__)
	#define AT_SDL_OS_STR L"/Linux"
#elif defined(__APPLE__)
	#define AT_SDL_OS_STR L"/macOS"
#elif defined(__FreeBSD__)
	#define AT_SDL_OS_STR L"/FreeBSD"
#elif defined(_WIN32)
	#define AT_SDL_OS_STR L"/Windows"
#else
	#define AT_SDL_OS_STR L""
#endif

	VDStringW title(L"AltirraSDL" AT_SDL_OS_STR AT_PROGRAM_PLATFORM_STR
		L" " AT_VERSION_STR AT_VERSION_DEBUG_STR AT_VERSION_PRERELEASE_STR);

	const VDStringW profileName = ATSettingsProfileGetName(pid);
	if (!profileName.empty()) {
		title += L" [";
		title += profileName;
		title += L"]";
	}

	VDStringW info;

	switch (g_sim.GetHardwareMode()) {
		case kATHardwareMode_800:    AppendSegment(info, L"800");    break;
		case kATHardwareMode_800XL:  AppendSegment(info, L"XL");     break;
		case kATHardwareMode_130XE:  AppendSegment(info, L"XE");     break;
		case kATHardwareMode_1200XL: AppendSegment(info, L"1200XL"); break;
		case kATHardwareMode_XEGS:   AppendSegment(info, L"XEGS");   break;
		case kATHardwareMode_1400XL: AppendSegment(info, L"1400XL"); break;
		case kATHardwareMode_5200:   AppendSegment(info, L"5200");   break;
		default: break;
	}

	switch (g_sim.GetVideoStandard()) {
		case kATVideoStandard_NTSC:   AppendSegment(info, L"NTSC");    break;
		case kATVideoStandard_PAL:    AppendSegment(info, L"PAL");     break;
		case kATVideoStandard_SECAM:  AppendSegment(info, L"SECAM");   break;
		case kATVideoStandard_NTSC50: AppendSegment(info, L"NTSC-50"); break;
		case kATVideoStandard_PAL60:  AppendSegment(info, L"PAL-60");  break;
		default: break;
	}

	switch (g_sim.GetMemoryMode()) {
		case kATMemoryMode_8K:         AppendSegment(info, L"8K");          break;
		case kATMemoryMode_16K:        AppendSegment(info, L"16K");         break;
		case kATMemoryMode_24K:        AppendSegment(info, L"24K");         break;
		case kATMemoryMode_32K:        AppendSegment(info, L"32K");         break;
		case kATMemoryMode_40K:        AppendSegment(info, L"40K");         break;
		case kATMemoryMode_48K:        AppendSegment(info, L"48K");         break;
		case kATMemoryMode_52K:        AppendSegment(info, L"52K");         break;
		case kATMemoryMode_64K:        AppendSegment(info, L"64K");         break;
		case kATMemoryMode_128K:       AppendSegment(info, L"128K");        break;
		case kATMemoryMode_256K:       AppendSegment(info, L"256K Rambo");  break;
		case kATMemoryMode_320K:       AppendSegment(info, L"320K Rambo");  break;
		case kATMemoryMode_320K_Compy: AppendSegment(info, L"320K Compy");  break;
		case kATMemoryMode_576K:       AppendSegment(info, L"576K");        break;
		case kATMemoryMode_576K_Compy: AppendSegment(info, L"576K Compy");  break;
		case kATMemoryMode_1088K:      AppendSegment(info, L"1088K");       break;
		default: break;
	}

	if (g_sim.IsUltimate1MBEnabled()) AppendSegment(info, L"U1MB");
	if (g_sim.GetVBXE())              AppendSegment(info, L"VBXE");
	if (g_sim.IsRapidusEnabled())     AppendSegment(info, L"Rapidus");
	if (g_sim.IsBASICEnabled())       AppendSegment(info, L"BASIC");

	switch (g_sim.GetCPUMode()) {
		case kATCPUMode_65C02:
			AppendSegment(info, L"65C02");
			break;
		case kATCPUMode_65C816: {
			VDStringW cpu(L"65C816");
			const uint32 sub = g_sim.GetCPUSubCycles();
			if (sub > 1)
				cpu.append_sprintf(L" @ %.3gMHz", (double)sub * 1.79);
			AppendSegment(info, cpu.c_str());
			break;
		}
		default:
			break;
	}

	if (!info.empty()) {
		title += L" - ";
		title += info;
	}

	const VDStringA u8 = VDTextWToU8(title);
	SDL_SetWindowTitle(g_pWindow, u8.c_str());
}
