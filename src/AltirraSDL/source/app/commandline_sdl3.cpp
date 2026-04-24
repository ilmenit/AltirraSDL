//	Altirra - Atari 800/800XL/5200 emulator
//	SDL3 command-line processing — mirrors Windows uicommandline.cpp
//	Copyright (C) 2026 Avery Lee
//
//	This program is free software; you can redistribute it and/or modify
//	it under the terms of the GNU General Public License as published by
//	the Free Software Foundation; either version 2 of the License, or
//	(at your option) any later version.
//
//	This program is distributed in the hope that it will be useful,
//	but WITHOUT ANY WARRANTY; without even the implied warranty of
//	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//	GNU General Public License for more details.
//
//	You should have received a copy of the GNU General Public License along
//	with this program. If not, see <http://www.gnu.org/licenses/>.

#include <stdafx.h>
#include <string>
#include <vector>
#ifndef _MSC_VER
#include <strings.h>
#endif
#include <vd2/system/filesys.h>
#include <vd2/system/strutil.h>
#include <vd2/system/text.h>
#include <at/atcore/media.h>
#include <at/atcore/propertyset.h>
#include <at/atcore/enumparse.h>
#include <at/atio/cartridgeimage.h>
#include <at/atio/cartridgetypes.h>
#include <at/atio/image.h>
#include "mediamanager.h"
#include "cassette.h"
#include "cheatengine.h"
#include "console.h"
#include "constants.h"
#include "debugger.h"
#include "devicemanager.h"
#include "disk.h"
#include "firmwaremanager.h"
#include "gtia.h"
#include "hostdevice.h"
#include "options.h"
#include "settings.h"
#include "simulator.h"
#include "uiaccessors.h"
#include "uikeyboard.h"
#include "ui_main.h"
#include "logging.h"

extern ATSimulator g_sim;
extern ATUIKeyboardOptions g_kbdOpts;
extern ATOptions g_ATOptions;

void ATDebuggerInitAutotestCommands();

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

namespace {

void SetKernelType(ATFirmwareType type) {
	uint64 id = g_sim.GetFirmwareManager()->GetFirmwareOfType(type, false);
	g_sim.SetKernel(id ? id : kATFirmwareId_NoKernel);
}

// Simplified time argument parser for --tapepos.
// Supports: [[HH:]MM:]SS[.sss]
float ParseTimeArgument(const char *s) {
	float parts[3] = {};
	int nParts = 0;

	while (*s && nParts < 3) {
		char *end = nullptr;
		float val = strtof(s, &end);
		if (end == s)
			break;
		parts[nParts++] = val;
		s = end;
		if (*s == ':')
			++s;
	}

	float t = 0;
	if (nParts == 3) {
		t = parts[0] * 3600.0f + parts[1] * 60.0f + parts[2];
	} else if (nParts == 2) {
		t = parts[0] * 60.0f + parts[1];
	} else if (nParts == 1) {
		t = parts[0];
	}
	return t;
}

// Load an image directly (used by --cart, --disk, --run, --runbas, --tape,
// and positional arguments).  Replicates the Windows DoLoadStream() retry
// loop from main.cpp:1186-1369 and the SDL3 ATUIPollDeferredActions()
// equivalent in ui_main.cpp.
//
// Returns true if a cold reset should be suppressed (e.g. save-state load).
bool DoLoadDirect(const char *utf8path,
				  const ATMediaWriteMode *writeMode,
				  int cartmapper,
				  ATImageType loadType,
				  int loadIndex) {
	VDStringW wpath = VDTextU8ToW(VDStringSpanA(utf8path));

	vdfastvector<uint8> captureBuffer;

	ATCartLoadContext cartCtx {};
	cartCtx.mbReturnOnUnknownMapper = true;

	if (cartmapper > 0) {
		cartCtx.mbReturnOnUnknownMapper = false;
		cartCtx.mCartMapper = cartmapper;
	} else {
		cartCtx.mpCaptureBuffer = &captureBuffer;
		if (cartmapper < 0)
			cartCtx.mbIgnoreChecksum = true;
	}

	ATStateLoadContext stateCtx {};

	ATImageLoadContext ctx {};
	ctx.mLoadType = loadType;
	ctx.mLoadIndex = loadIndex;
	ctx.mpCartLoadContext = &cartCtx;
	ctx.mpStateLoadContext = &stateCtx;

	ATMediaLoadContext mctx;
	mctx.mOriginalPath = wpath;
	mctx.mImageName = wpath;
	mctx.mpStream = nullptr;
	mctx.mWriteMode = writeMode ? *writeMode : g_ATOptions.mDefaultWriteMode;
	mctx.mbStopOnModeIncompatibility = true;
	mctx.mbStopAfterImageLoaded = true;
	mctx.mbStopOnMemoryConflictBasic = true;
	mctx.mbStopOnIncompatibleDiskFormat = true;
	mctx.mpImageLoadContext = &ctx;

	int safetyCounter = 10;
	bool loadSuccess = false;

	for (;;) {
		try {
			if (g_sim.Load(mctx)) {
				loadSuccess = true;
				break;
			}
		} catch (const MyError& e) {
			LOG_ERROR("CmdLine", "Error loading '%s': %s", utf8path, VDTextWToU8(VDStringW(e.wc_str())).c_str());
			return false;
		}

		if (!--safetyCounter)
			break;

		if (mctx.mbStopAfterImageLoaded)
			mctx.mbStopAfterImageLoaded = false;

		if (mctx.mbMode5200Required) {
			mctx.mbMode5200Required = false;
			if (g_sim.GetHardwareMode() != kATHardwareMode_5200) {
				if (!ATUISwitchHardwareMode(nullptr, kATHardwareMode_5200, true))
					break;
			}
			continue;
		} else if (mctx.mbModeComputerRequired) {
			mctx.mbModeComputerRequired = false;
			if (g_sim.GetHardwareMode() == kATHardwareMode_5200) {
				if (!ATUISwitchHardwareMode(nullptr, kATHardwareMode_800XL, true))
					break;
			}
			continue;
		} else if (mctx.mbMemoryConflictBasic) {
			// Auto-disable BASIC on conflict (no modal dialog at startup)
			mctx.mbStopOnMemoryConflictBasic = false;
			g_sim.SetBASICEnabled(false);
			continue;
		} else if (mctx.mbIncompatibleDiskFormat) {
			// Auto-accept incompatible disk format
			mctx.mbIncompatibleDiskFormat = false;
			mctx.mbStopOnIncompatibleDiskFormat = false;
			continue;
		}

		if (ctx.mLoadType == kATImageType_Cartridge) {
			// Unknown cart mapper — can't show dialog before main loop
			LOG_INFO("CmdLine", "Unknown cartridge mapper for '%s' — " "use --cartmapper to specify", utf8path);
			break;
		}

		break;
	}

	if (loadSuccess) {
		ATAddMRU(wpath.c_str());

		if (ctx.mLoadType == kATImageType_SaveState ||
			ctx.mLoadType == kATImageType_SaveState2) {
			return true;  // suppress cold reset
		}
	}

	return false;
}

// Match a switch name (case-insensitive).  The arg should already have
// the leading "--" stripped.
bool MatchSwitch(const char *arg, const char *name) {
	return strcasecmp(arg, name) == 0;
}

// Check if argv[i] is a switch with the given name and consume its value
// argument from argv[i+1].  Returns nullptr if not matched or missing arg.
// If the next argument starts with "--" (is itself a switch), it is not
// consumed — the switch is treated as having an empty value, matching the
// Windows VDCommandLine::FindAndRemoveSwitch behavior.
const char *ConsumeArg(int argc, char **argv, int &i,
					   std::vector<bool> &consumed, const char *name) {
	if (!MatchSwitch(argv[i] + 2, name))
		return nullptr;
	consumed[i] = true;
	if (i + 1 >= argc) {
		LOG_INFO("CmdLine", "--%s requires an argument", name);
		return nullptr;
	}
	// Don't consume the next argument if it looks like a switch
	if (argv[i + 1][0] == '-' && argv[i + 1][1] == '-' && argv[i + 1][2] != '\0') {
		LOG_INFO("CmdLine", "--%s requires an argument (got switch %s)", name, argv[i + 1]);
		return nullptr;
	}
	consumed[++i] = true;
	return argv[i];
}

}  // anonymous namespace

// ---------------------------------------------------------------------------
// ATProcessCommandLineSDL3
// ---------------------------------------------------------------------------

bool ATProcessCommandLineSDL3(int argc, char **argv) {
	// State variables matching Windows ATUICommandLineProcessor
	bool coldResetPending = false;
	bool debugModeSuspend = false;
	bool haveUnloadedAllImages = false;
	bool hadBootImage = false;
	int imageCartMapper = 0;
	float initialTapePos = 0;
	std::string keysToType;
	const ATMediaWriteMode *bootImageWriteMode = nullptr;
	static const ATMediaWriteMode modeRO = kATMediaWriteMode_RO;
	static const ATMediaWriteMode modeRW = kATMediaWriteMode_RW;
	static const ATMediaWriteMode modeVRW = kATMediaWriteMode_VRW;
	static const ATMediaWriteMode modeVRWSafe = kATMediaWriteMode_VRWSafe;

	// Track which args have been consumed
	std::vector<bool> consumed(argc, false);
	consumed[0] = true;  // program name

	// Also skip args already consumed by main() (--test-mode was stripped
	// from argv before we get here, so nothing extra to do).

	// Pass 1: Process all named switches (order-independent)
	// This matches Windows FindAndRemoveSwitch semantics where each switch
	// is found regardless of its position among other arguments.

	int diskIndex = 0;

	for (int i = 1; i < argc; ++i) {
		if (consumed[i])
			continue;

		const char *arg = argv[i];

		// Only process arguments starting with "--"
		if (arg[0] != '-' || arg[1] != '-')
			continue;

		const char *sw = arg + 2;  // skip "--"
		const char *val = nullptr;

		// ---- Help ----
		if (MatchSwitch(sw, "help") || MatchSwitch(sw, "?")) {
			consumed[i] = true;
			LOG_INFO("CmdLine", "Usage: AltirraSDL [options] [image-file ...]\n\n" "Display:  --f  --ntsc --pal --secam --ntsc50 --pal60\n" "          --artifact <mode>  --vsync/--novsync\n" "Hardware: --hardware <mode>  --kernel <name>  --memsize <size>\n" "          --stereo/--nostereo  --basic/--nobasic\n" "Media:    --cart/--disk/--run/--runbas/--tape <file>\n" "          --bootro/--bootrw/--bootvrw/--bootvrwsafe\n" "Devices:  --adddevice/--setdevice/--removedevice <spec>\n" "          --cleardevices  --pclink <mode,path>  --hdpath <path>\n" "Debugger: --debug  --debugcmd <cmd>  --autotest\n" "Other:    --type <text>  --rawkeys  --diskemu <mode>\n\n" "Use Help > Command-Line Help in the menu for full details.");
			continue;
		}

		// ---- Autotest ----
		if (MatchSwitch(sw, "autotest")) {
			consumed[i] = true;
			ATDebuggerInitAutotestCommands();
			continue;
		}

		// ---- Fullscreen ----
		if (MatchSwitch(sw, "f")) {
			consumed[i] = true;
			ATSetFullscreen(true);
			continue;
		}

		// ---- Video standard ----
		if (MatchSwitch(sw, "ntsc")) {
			consumed[i] = true;
			g_sim.SetVideoStandard(kATVideoStandard_NTSC);
			continue;
		}
		if (MatchSwitch(sw, "pal")) {
			consumed[i] = true;
			g_sim.SetVideoStandard(kATVideoStandard_PAL);
			continue;
		}
		if (MatchSwitch(sw, "secam")) {
			consumed[i] = true;
			g_sim.SetVideoStandard(kATVideoStandard_SECAM);
			continue;
		}
		if (MatchSwitch(sw, "ntsc50")) {
			consumed[i] = true;
			g_sim.SetVideoStandard(kATVideoStandard_NTSC50);
			continue;
		}
		if (MatchSwitch(sw, "pal60")) {
			consumed[i] = true;
			g_sim.SetVideoStandard(kATVideoStandard_PAL60);
			continue;
		}

		// ---- Disk burst I/O ----
		if (MatchSwitch(sw, "burstio") || MatchSwitch(sw, "burstiopolled")) {
			consumed[i] = true;
			g_sim.SetDiskBurstTransfersEnabled(true);
			continue;
		}
		if (MatchSwitch(sw, "noburstio")) {
			consumed[i] = true;
			g_sim.SetDiskBurstTransfersEnabled(false);
			continue;
		}

		// ---- SIO patch ----
		if (MatchSwitch(sw, "siopatch")) {
			consumed[i] = true;
			g_sim.SetDiskSIOPatchEnabled(true);
			g_sim.SetDiskSIOOverrideDetectEnabled(false);
			g_sim.SetCassetteSIOPatchEnabled(true);
			continue;
		}
		if (MatchSwitch(sw, "siopatchsafe")) {
			consumed[i] = true;
			g_sim.SetDiskSIOPatchEnabled(true);
			g_sim.SetDiskSIOOverrideDetectEnabled(true);
			g_sim.SetCassetteSIOPatchEnabled(true);
			continue;
		}
		if (MatchSwitch(sw, "nosiopatch")) {
			consumed[i] = true;
			g_sim.SetDiskSIOPatchEnabled(false);
			g_sim.SetCassetteSIOPatchEnabled(false);
			continue;
		}

		// ---- Fast boot ----
		if (MatchSwitch(sw, "fastboot")) {
			consumed[i] = true;
			g_sim.SetFastBootEnabled(true);
			continue;
		}
		if (MatchSwitch(sw, "nofastboot")) {
			consumed[i] = true;
			g_sim.SetFastBootEnabled(false);
			continue;
		}

		// ---- Cassette ----
		if (MatchSwitch(sw, "casautoboot")) {
			consumed[i] = true;
			g_sim.SetCassetteAutoBootEnabled(true);
			continue;
		}
		if (MatchSwitch(sw, "nocasautoboot")) {
			consumed[i] = true;
			g_sim.SetCassetteAutoBootEnabled(false);
			continue;
		}
		if (MatchSwitch(sw, "casautobasicboot")) {
			consumed[i] = true;
			g_sim.SetCassetteAutoBasicBootEnabled(true);
			continue;
		}
		if (MatchSwitch(sw, "nocasautobasicboot")) {
			consumed[i] = true;
			g_sim.SetCassetteAutoBasicBootEnabled(false);
			continue;
		}

		// ---- Accurate disk ----
		if (MatchSwitch(sw, "accuratedisk")) {
			consumed[i] = true;
			g_sim.SetDiskAccurateTimingEnabled(true);
			continue;
		}
		if (MatchSwitch(sw, "noaccuratedisk")) {
			consumed[i] = true;
			g_sim.SetDiskAccurateTimingEnabled(false);
			continue;
		}

		// ---- Stereo (dual POKEY) ----
		if (MatchSwitch(sw, "stereo")) {
			consumed[i] = true;
			g_sim.SetDualPokeysEnabled(true);
			coldResetPending = true;
			continue;
		}
		if (MatchSwitch(sw, "nostereo")) {
			consumed[i] = true;
			g_sim.SetDualPokeysEnabled(false);
			coldResetPending = true;
			continue;
		}

		// ---- BASIC ----
		if (MatchSwitch(sw, "basic")) {
			consumed[i] = true;
			g_sim.SetBASICEnabled(true);
			coldResetPending = true;
			continue;
		}
		if (MatchSwitch(sw, "nobasic")) {
			consumed[i] = true;
			g_sim.SetBASICEnabled(false);
			coldResetPending = true;
			continue;
		}

		// ---- SoundBoard ----
		if ((val = ConsumeArg(argc, argv, i, consumed, "soundboard")) != nullptr) {
			uint32 base = 0;
			if (strcasecmp(val, "d2c0") == 0) base = 0xD2C0;
			else if (strcasecmp(val, "d500") == 0) base = 0xD500;
			else if (strcasecmp(val, "d600") == 0) base = 0xD600;
			else {
				LOG_INFO("CmdLine", "Invalid SoundBoard memory base: %s", val);
				continue;
			}

			ATPropertySet pset;
			pset.SetUint32("base", base);

			auto *dm = g_sim.GetDeviceManager();
			auto *dev = dm->GetDeviceByTag("soundboard");
			if (dev)
				dev->SetSettings(pset);
			else
				dm->AddDevice("soundboard", pset);

			coldResetPending = true;
			continue;
		}
		if (MatchSwitch(sw, "nosoundboard")) {
			consumed[i] = true;
			g_sim.GetDeviceManager()->RemoveDevice("soundboard");
			coldResetPending = true;
			continue;
		}

		// ---- SlightSID ----
		if (MatchSwitch(sw, "slightsid")) {
			consumed[i] = true;
			auto &dm = *g_sim.GetDeviceManager();
			if (!dm.GetDeviceByTag("slightsid"))
				dm.AddDevice("slightsid", ATPropertySet());
			continue;
		}
		if (MatchSwitch(sw, "noslightsid")) {
			consumed[i] = true;
			g_sim.GetDeviceManager()->RemoveDevice("slightsid");
			continue;
		}

		// ---- COVOX ----
		if (MatchSwitch(sw, "covox")) {
			consumed[i] = true;
			auto &dm = *g_sim.GetDeviceManager();
			if (!dm.GetDeviceByTag("covox"))
				dm.AddDevice("covox", ATPropertySet());
			continue;
		}
		if (MatchSwitch(sw, "nocovox")) {
			consumed[i] = true;
			g_sim.GetDeviceManager()->RemoveDevice("covox");
			continue;
		}

		// ---- Hardware mode ----
		if ((val = ConsumeArg(argc, argv, i, consumed, "hardware")) != nullptr) {
			if (strcasecmp(val, "800") == 0) g_sim.SetHardwareMode(kATHardwareMode_800);
			else if (strcasecmp(val, "800xl") == 0) g_sim.SetHardwareMode(kATHardwareMode_800XL);
			else if (strcasecmp(val, "1200xl") == 0) g_sim.SetHardwareMode(kATHardwareMode_1200XL);
			else if (strcasecmp(val, "130xe") == 0) g_sim.SetHardwareMode(kATHardwareMode_130XE);
			else if (strcasecmp(val, "xegs") == 0) g_sim.SetHardwareMode(kATHardwareMode_XEGS);
			else if (strcasecmp(val, "1400xl") == 0) g_sim.SetHardwareMode(kATHardwareMode_1400XL);
			else if (strcasecmp(val, "5200") == 0) g_sim.SetHardwareMode(kATHardwareMode_5200);
			else LOG_INFO("CmdLine", "Invalid hardware mode: %s", val);
			continue;
		}

		// ---- Kernel ----
		if ((val = ConsumeArg(argc, argv, i, consumed, "kernel")) != nullptr) {
			if (strcasecmp(val, "default") == 0) g_sim.SetKernel(0);
			else if (strcasecmp(val, "osa") == 0) SetKernelType(kATFirmwareType_Kernel800_OSA);
			else if (strcasecmp(val, "osb") == 0) SetKernelType(kATFirmwareType_Kernel800_OSB);
			else if (strcasecmp(val, "xl") == 0) SetKernelType(kATFirmwareType_KernelXL);
			else if (strcasecmp(val, "xegs") == 0) SetKernelType(kATFirmwareType_KernelXEGS);
			else if (strcasecmp(val, "1200xl") == 0) SetKernelType(kATFirmwareType_Kernel1200XL);
			else if (strcasecmp(val, "5200") == 0) SetKernelType(kATFirmwareType_Kernel5200);
			else if (strcasecmp(val, "lle") == 0) g_sim.SetKernel(kATFirmwareId_Kernel_LLE);
			else if (strcasecmp(val, "llexl") == 0) g_sim.SetKernel(kATFirmwareId_Kernel_LLEXL);
			else if (strcasecmp(val, "hle") == 0) g_sim.SetKernel(kATFirmwareId_Kernel_LLE);
			else if (strcasecmp(val, "5200lle") == 0) g_sim.SetKernel(kATFirmwareId_5200_LLE);
			else LOG_INFO("CmdLine", "Invalid kernel mode: %s", val);
			continue;
		}

		// ---- Kernel/BASIC by reference ----
		if ((val = ConsumeArg(argc, argv, i, consumed, "kernelref")) != nullptr) {
			VDStringW wval = VDTextU8ToW(VDStringSpanA(val));
			const auto id = g_sim.GetFirmwareManager()->GetFirmwareByRefString(wval.c_str(), ATIsKernelFirmwareType);
			if (!id)
				LOG_INFO("CmdLine", "No matching kernel for reference: %s", val);
			else
				g_sim.SetKernel(id);
			continue;
		}
		if ((val = ConsumeArg(argc, argv, i, consumed, "basicref")) != nullptr) {
			VDStringW wval = VDTextU8ToW(VDStringSpanA(val));
			const auto id = g_sim.GetFirmwareManager()->GetFirmwareByRefString(wval.c_str(),
				[](ATFirmwareType type) { return type == kATFirmwareType_Basic; });
			if (!id)
				LOG_INFO("CmdLine", "No matching BASIC for reference: %s", val);
			else
				g_sim.SetBasic(id);
			continue;
		}

		// ---- Memory size ----
		if ((val = ConsumeArg(argc, argv, i, consumed, "memsize")) != nullptr) {
			if (strcasecmp(val, "8K") == 0) g_sim.SetMemoryMode(kATMemoryMode_8K);
			else if (strcasecmp(val, "16K") == 0) g_sim.SetMemoryMode(kATMemoryMode_16K);
			else if (strcasecmp(val, "24K") == 0) g_sim.SetMemoryMode(kATMemoryMode_24K);
			else if (strcasecmp(val, "32K") == 0) g_sim.SetMemoryMode(kATMemoryMode_32K);
			else if (strcasecmp(val, "40K") == 0) g_sim.SetMemoryMode(kATMemoryMode_40K);
			else if (strcasecmp(val, "48K") == 0) g_sim.SetMemoryMode(kATMemoryMode_48K);
			else if (strcasecmp(val, "52K") == 0) g_sim.SetMemoryMode(kATMemoryMode_52K);
			else if (strcasecmp(val, "64K") == 0) g_sim.SetMemoryMode(kATMemoryMode_64K);
			else if (strcasecmp(val, "128K") == 0) g_sim.SetMemoryMode(kATMemoryMode_128K);
			else if (strcasecmp(val, "256K") == 0) g_sim.SetMemoryMode(kATMemoryMode_256K);
			else if (strcasecmp(val, "320K") == 0) g_sim.SetMemoryMode(kATMemoryMode_320K);
			else if (strcasecmp(val, "320KCOMPY") == 0) g_sim.SetMemoryMode(kATMemoryMode_320K_Compy);
			else if (strcasecmp(val, "576K") == 0) g_sim.SetMemoryMode(kATMemoryMode_576K);
			else if (strcasecmp(val, "576KCOMPY") == 0) g_sim.SetMemoryMode(kATMemoryMode_576K_Compy);
			else if (strcasecmp(val, "1088K") == 0) g_sim.SetMemoryMode(kATMemoryMode_1088K);
			else LOG_INFO("CmdLine", "Invalid memory mode: %s", val);
			continue;
		}

		// ---- Axlon memory ----
		if ((val = ConsumeArg(argc, argv, i, consumed, "axlonmemsize")) != nullptr) {
			if (strcasecmp(val, "none") == 0) g_sim.SetAxlonMemoryMode(0);
			else if (strcasecmp(val, "64K") == 0) g_sim.SetAxlonMemoryMode(2);
			else if (strcasecmp(val, "128K") == 0) g_sim.SetAxlonMemoryMode(3);
			else if (strcasecmp(val, "256K") == 0) g_sim.SetAxlonMemoryMode(4);
			else if (strcasecmp(val, "512K") == 0) g_sim.SetAxlonMemoryMode(5);
			else if (strcasecmp(val, "1024K") == 0) g_sim.SetAxlonMemoryMode(6);
			else if (strcasecmp(val, "2048K") == 0) g_sim.SetAxlonMemoryMode(7);
			else if (strcasecmp(val, "4096K") == 0) g_sim.SetAxlonMemoryMode(7);
			else LOG_INFO("CmdLine", "Invalid Axlon memory size: %s", val);
			continue;
		}

		// ---- High memory banks ----
		if ((val = ConsumeArg(argc, argv, i, consumed, "highbanks")) != nullptr) {
			if (strcasecmp(val, "na") == 0) g_sim.SetHighMemoryBanks(-1);
			else if (strcmp(val, "0") == 0) g_sim.SetHighMemoryBanks(0);
			else if (strcmp(val, "1") == 0) g_sim.SetHighMemoryBanks(1);
			else if (strcmp(val, "3") == 0) g_sim.SetHighMemoryBanks(3);
			else if (strcmp(val, "15") == 0) g_sim.SetHighMemoryBanks(15);
			else if (strcmp(val, "63") == 0) g_sim.SetHighMemoryBanks(63);
			else LOG_INFO("CmdLine", "Invalid high banks value: %s", val);
			continue;
		}

		// ---- Artifacting ----
		if ((val = ConsumeArg(argc, argv, i, consumed, "artifact")) != nullptr) {
			if (strcasecmp(val, "none") == 0) g_sim.GetGTIA().SetArtifactingMode(ATArtifactMode::None);
			else if (strcasecmp(val, "ntsc") == 0) g_sim.GetGTIA().SetArtifactingMode(ATArtifactMode::NTSC);
			else if (strcasecmp(val, "ntschi") == 0) g_sim.GetGTIA().SetArtifactingMode(ATArtifactMode::NTSCHi);
			else if (strcasecmp(val, "pal") == 0) g_sim.GetGTIA().SetArtifactingMode(ATArtifactMode::PAL);
			else if (strcasecmp(val, "palhi") == 0) g_sim.GetGTIA().SetArtifactingMode(ATArtifactMode::PALHi);
			else LOG_INFO("CmdLine", "Invalid artifact mode: %s", val);
			continue;
		}

		// ---- VSync ----
		if (MatchSwitch(sw, "vsync")) {
			consumed[i] = true;
			g_sim.GetGTIA().SetVsyncEnabled(true);
			continue;
		}
		if (MatchSwitch(sw, "novsync")) {
			consumed[i] = true;
			g_sim.GetGTIA().SetVsyncEnabled(false);
			continue;
		}

		// ---- Debugger ----
		if (MatchSwitch(sw, "debug")) {
			consumed[i] = true;
			ATShowConsole();
			continue;
		}
		if (MatchSwitch(sw, "debugbrkrun")) {
			consumed[i] = true;
			ATGetDebugger()->SetBreakOnEXERunAddrEnabled(true);
			continue;
		}
		if (MatchSwitch(sw, "nodebugbrkrun")) {
			consumed[i] = true;
			ATGetDebugger()->SetBreakOnEXERunAddrEnabled(false);
			continue;
		}
		if ((val = ConsumeArg(argc, argv, i, consumed, "debugcmd")) != nullptr) {
			debugModeSuspend = true;
			ATGetDebugger()->QueueCommand(val, false);
			continue;
		}

		// ---- Boot write mode ----
		if (MatchSwitch(sw, "bootro")) {
			consumed[i] = true;
			bootImageWriteMode = &modeRO;
			continue;
		}
		if (MatchSwitch(sw, "bootrw")) {
			consumed[i] = true;
			bootImageWriteMode = &modeRW;
			continue;
		}
		if (MatchSwitch(sw, "bootvrw")) {
			consumed[i] = true;
			bootImageWriteMode = &modeVRW;
			continue;
		}
		if (MatchSwitch(sw, "bootvrwsafe")) {
			consumed[i] = true;
			bootImageWriteMode = &modeVRWSafe;
			continue;
		}

		// ---- Type text ----
		if ((val = ConsumeArg(argc, argv, i, consumed, "type")) != nullptr) {
			keysToType += val;
			continue;
		}

		// ---- PCLink ----
		if (MatchSwitch(sw, "nopclink")) {
			consumed[i] = true;
			g_sim.GetDeviceManager()->RemoveDevice("pclink");
			continue;
		}
		if ((val = ConsumeArg(argc, argv, i, consumed, "pclink")) != nullptr) {
			// Format: mode,path  (mode = ro|rw)
			std::string valStr(val);
			auto commaPos = valStr.find(',');
			if (commaPos == std::string::npos) {
				LOG_INFO("CmdLine", "Invalid PCLink mount string: %s", val);
				continue;
			}

			std::string mode = valStr.substr(0, commaPos);
			std::string path = valStr.substr(commaPos + 1);

			bool write = false;
			if (strcasecmp(mode.c_str(), "rw") == 0)
				write = true;
			else if (strcasecmp(mode.c_str(), "ro") != 0) {
				LOG_INFO("CmdLine", "Invalid PCLink mount mode: %s", mode.c_str());
				continue;
			}

			ATPropertySet pset;
			pset.SetString("path", VDTextU8ToW(VDStringSpanA(path.c_str())).c_str());
			if (write)
				pset.SetBool("write", true);

			auto *dm = g_sim.GetDeviceManager();
			IATDevice *dev = dm->GetDeviceByTag("pclink");
			if (dev)
				dev->SetSettings(pset);
			else
				dm->AddDevice("pclink", pset);
			continue;
		}

		// ---- Host device (H:) ----
		if (MatchSwitch(sw, "nohdpath")) {
			consumed[i] = true;
			auto *dm = g_sim.GetDeviceManager();
			IATDevice *dev = dm->GetDeviceByTag("hostfs");
			if (dev) {
				dm->RemoveDevice(dev);
				coldResetPending = true;
			}
			continue;
		}
		if ((val = ConsumeArg(argc, argv, i, consumed, "hdpath")) != nullptr) {
			VDStringW wval = VDTextU8ToW(VDStringSpanA(val));
			auto *dm = g_sim.GetDeviceManager();
			IATDevice *dev = dm->GetDeviceByTag("hostfs");
			if (!dev)
				dev = dm->AddDevice("hostfs", ATPropertySet());
			IATHostDeviceEmulator *hd = vdpoly_cast<IATHostDeviceEmulator *>(dev);
			if (hd) {
				hd->SetReadOnly(true);
				hd->SetBasePath(0, wval.c_str());
				coldResetPending = true;
			}
			continue;
		}
		if ((val = ConsumeArg(argc, argv, i, consumed, "hdpathrw")) != nullptr) {
			VDStringW wval = VDTextU8ToW(VDStringSpanA(val));
			auto *dm = g_sim.GetDeviceManager();
			IATDevice *dev = dm->GetDeviceByTag("hostfs");
			if (!dev)
				dev = dm->AddDevice("hostfs", ATPropertySet());
			IATHostDeviceEmulator *hd = vdpoly_cast<IATHostDeviceEmulator *>(dev);
			if (hd) {
				hd->SetReadOnly(false);
				hd->SetBasePath(0, wval.c_str());
				coldResetPending = true;
			}
			continue;
		}

		// ---- Raw keys ----
		if (MatchSwitch(sw, "rawkeys")) {
			consumed[i] = true;
			g_kbdOpts.mbRawKeys = true;
			continue;
		}
		if (MatchSwitch(sw, "norawkeys")) {
			consumed[i] = true;
			g_kbdOpts.mbRawKeys = false;
			continue;
		}

		// ---- Cartridge mapper ----
		if (MatchSwitch(sw, "nocartchecksum")) {
			consumed[i] = true;
			imageCartMapper = -1;
			continue;
		}
		if ((val = ConsumeArg(argc, argv, i, consumed, "cartmapper")) != nullptr) {
			imageCartMapper = ATGetCartridgeModeForMapper(atoi(val));
			if (imageCartMapper <= 0 || imageCartMapper >= kATCartridgeModeCount) {
				LOG_INFO("CmdLine", "Unsupported or invalid cartridge mapper: %s", val);
				imageCartMapper = 0;
			}
			continue;
		}

		// ---- Cheat engine ----
		if (MatchSwitch(sw, "nocheats")) {
			consumed[i] = true;
			g_sim.SetCheatEngineEnabled(false);
			continue;
		}
		if ((val = ConsumeArg(argc, argv, i, consumed, "cheats")) != nullptr) {
			g_sim.SetCheatEngineEnabled(true);
			VDStringW wval = VDTextU8ToW(VDStringSpanA(val));
			g_sim.GetCheatEngine()->Load(wval.c_str());
			continue;
		}

		// ---- Disk emulation mode ----
		if ((val = ConsumeArg(argc, argv, i, consumed, "diskemu")) != nullptr) {
			auto result = ATParseEnum<ATDiskEmulationMode>(VDStringSpanA(val));
			if (!result.mValid) {
				LOG_INFO("CmdLine", "Unsupported disk emulation mode: %s", val);
			} else {
				for (int d = 0; d < 15; ++d)
					g_sim.GetDiskDrive(d).SetEmulationMode(result.mValue);
			}
			continue;
		}

		// ---- Media loading (type-specific) ----
		if ((val = ConsumeArg(argc, argv, i, consumed, "cart")) != nullptr) {
			if (!haveUnloadedAllImages) {
				g_sim.UnloadAll(ATUIGetBootUnloadStorageMask());
				haveUnloadedAllImages = true;
			}
			DoLoadDirect(val, bootImageWriteMode, imageCartMapper, kATImageType_Cartridge, -1);
			coldResetPending = true;
			hadBootImage = true;
			continue;
		}
		if ((val = ConsumeArg(argc, argv, i, consumed, "disk")) != nullptr) {
			if (diskIndex >= 15) {
				LOG_WARN("CmdLine", "--disk index %d exceeds maximum (15 drives), ignoring '%s'", diskIndex, val);
				continue;
			}
			if (!haveUnloadedAllImages) {
				g_sim.UnloadAll(ATUIGetBootUnloadStorageMask());
				haveUnloadedAllImages = true;
			}
			DoLoadDirect(val, bootImageWriteMode, imageCartMapper, kATImageType_Disk, diskIndex++);
			coldResetPending = true;
			hadBootImage = true;
			continue;
		}
		if ((val = ConsumeArg(argc, argv, i, consumed, "run")) != nullptr) {
			if (!haveUnloadedAllImages) {
				g_sim.UnloadAll(ATUIGetBootUnloadStorageMask());
				haveUnloadedAllImages = true;
			}
			DoLoadDirect(val, bootImageWriteMode, imageCartMapper, kATImageType_Program, -1);
			coldResetPending = true;
			hadBootImage = true;
			continue;
		}
		if ((val = ConsumeArg(argc, argv, i, consumed, "runbas")) != nullptr) {
			if (!haveUnloadedAllImages) {
				g_sim.UnloadAll(ATUIGetBootUnloadStorageMask());
				haveUnloadedAllImages = true;
			}
			DoLoadDirect(val, bootImageWriteMode, imageCartMapper, kATImageType_BasicProgram, -1);
			coldResetPending = true;
			hadBootImage = true;
			continue;
		}
		if ((val = ConsumeArg(argc, argv, i, consumed, "tape")) != nullptr) {
			if (!haveUnloadedAllImages) {
				g_sim.UnloadAll(ATUIGetBootUnloadStorageMask());
				haveUnloadedAllImages = true;
			}
			DoLoadDirect(val, bootImageWriteMode, imageCartMapper, kATImageType_Tape, -1);
			coldResetPending = true;
			hadBootImage = true;
			continue;
		}

		// ---- Tape position ----
		if ((val = ConsumeArg(argc, argv, i, consumed, "tapepos")) != nullptr) {
			initialTapePos = ParseTimeArgument(val);
			continue;
		}

		// ---- Device management (sequenced) ----
		if (MatchSwitch(sw, "cleardevices")) {
			consumed[i] = true;
			g_sim.GetDeviceManager()->RemoveAllDevices(false);
			continue;
		}
		if ((val = ConsumeArg(argc, argv, i, consumed, "adddevice")) != nullptr) {
			auto &dm = *g_sim.GetDeviceManager();
			VDStringW wval = VDTextU8ToW(VDStringSpanA(val));
			VDStringRefW params(wval);
			VDStringRefW tag;

			if (!params.split(L',', tag)) {
				tag = VDStringRefW(wval);
				params = VDStringRefW();
			}

			ATPropertySet pset;
			if (params.data())
				pset.ParseFromCommandLineString(params.data());

			const VDStringA tagA = VDTextWToA(tag);
			const ATDeviceDefinition *def = dm.GetDeviceDefinition(tagA.c_str());

			if (!def || (def->mFlags & kATDeviceDefFlag_Hidden)) {
				LOG_INFO("CmdLine", "Unknown device type: %s", val);
			} else if (def->mFlags & kATDeviceDefFlag_Internal) {
				IATDevice *dev = dm.GetDeviceByTag(tagA.c_str());
				if (dev) {
					try { dm.ReconfigureDevice(*dev, pset); }
					catch (const MyError& e) {
						LOG_ERROR("CmdLine", "Error configuring device '%s': %s", val, e.c_str());
					}
				} else
					LOG_INFO("CmdLine", "Missing internal device: %s", val);
			} else {
				try { dm.AddDevice(def, pset); }
				catch (const MyError& e) {
					LOG_ERROR("CmdLine", "Error adding device '%s': %s", val, e.c_str());
				}
			}
			continue;
		}
		if ((val = ConsumeArg(argc, argv, i, consumed, "setdevice")) != nullptr) {
			auto &dm = *g_sim.GetDeviceManager();
			VDStringW wval = VDTextU8ToW(VDStringSpanA(val));
			VDStringRefW params(wval);
			VDStringRefW tag;

			if (!params.split(L',', tag)) {
				tag = VDStringRefW(wval);
				params = VDStringRefW();
			}

			ATPropertySet pset;
			if (params.data())
				pset.ParseFromCommandLineString(params.data());

			const VDStringA tagA = VDTextWToA(tag);
			const ATDeviceDefinition *def = dm.GetDeviceDefinition(tagA.c_str());

			if (!def || (def->mFlags & kATDeviceDefFlag_Hidden)) {
				LOG_INFO("CmdLine", "Unknown device type: %s", val);
			} else {
				try {
					IATDevice *dev = dm.GetDeviceByTag(tagA.c_str());
					if (def->mFlags & kATDeviceDefFlag_Internal) {
						if (dev)
							dm.ReconfigureDevice(*dev, pset);
						else
							LOG_INFO("CmdLine", "Missing internal device: %s", val);
					} else {
						if (dev)
							dm.ReconfigureDevice(*dev, pset);
						else
							dm.AddDevice(def, pset);
					}
				} catch (const MyError& e) {
					LOG_ERROR("CmdLine", "Error with --setdevice '%s': %s", val, e.c_str());
				}
			}
			continue;
		}
		if ((val = ConsumeArg(argc, argv, i, consumed, "removedevice")) != nullptr) {
			auto &dm = *g_sim.GetDeviceManager();
			VDStringA tagA(val);
			IATDevice *dev = dm.GetDeviceByTag(tagA.c_str(), 0, true, true);
			if (dev) {
				ATDeviceInfo devInfo;
				dev->GetDeviceInfo(devInfo);
				if (!(devInfo.mpDef->mFlags & kATDeviceDefFlag_Internal))
					dm.RemoveDevice(dev);
			}
			continue;
		}

		// Unknown switch — warn but continue (don't abort like Windows)
		if (arg[0] == '-' && arg[1] == '-' && arg[2] != '\0') {
			LOG_INFO("CmdLine", "Unknown command-line switch: %s", arg);
			consumed[i] = true;
		}
	}

	// Pass 2: Process remaining unconsumed positional arguments as boot images.
	//
	// Some launchers (notably misbehaving .desktop / AppImage integrations)
	// word-split filenames before exec, so a single file path containing
	// spaces such as "Flob + V1.03.car" arrives as several argv entries.
	// To recover, when an unconsumed positional does not exist as a file on
	// disk, try progressively joining it with the following positional args
	// (separated by single spaces) until we find a path that does exist.
	for (int i = 1; i < argc; ++i) {
		if (consumed[i])
			continue;

		// Build the longest run of adjacent unconsumed positional args
		// starting at i that resolves to an existing file path.  Fall back
		// to argv[i] alone if nothing matches (preserves prior behaviour
		// for non-existent paths so the user still gets an error log).
		int joinEnd = i;  // inclusive index of last arg to join
		{
			VDStringA candidate(argv[i]);
			VDStringW wcandidate = VDTextU8ToW(VDStringSpanA(candidate));
			bool exists = VDDoesPathExist(wcandidate.c_str());
			if (!exists) {
				for (int j = i + 1; j < argc; ++j) {
					if (consumed[j])
						break;
					candidate += ' ';
					candidate += argv[j];
					wcandidate = VDTextU8ToW(VDStringSpanA(candidate));
					if (VDDoesPathExist(wcandidate.c_str())) {
						joinEnd = j;
						exists = true;
						break;
					}
				}
			}
		}

		// Compose final UTF-8 path (single arg, or joined run with spaces).
		VDStringA pathU8(argv[i]);
		for (int j = i + 1; j <= joinEnd; ++j) {
			pathU8 += ' ';
			pathU8 += argv[j];
		}

		// Treat as boot image
		if (!haveUnloadedAllImages) {
			g_sim.UnloadAll(ATUIGetBootUnloadStorageMask());
			haveUnloadedAllImages = true;
		}

		bool suppressColdReset = DoLoadDirect(pathU8.c_str(), bootImageWriteMode,
											  imageCartMapper, kATImageType_None,
											  ATImageLoadContext::kLoadIndexNextFree);

		if (!suppressColdReset)
			coldResetPending = true;

		hadBootImage = true;

		// Consume all joined args so they aren't reprocessed.
		for (int j = i; j <= joinEnd; ++j)
			consumed[j] = true;
		i = joinEnd;
	}

	// WebAssembly smoke mode: with no explicit startup media, auto-boot a
	// pre-bundled image from the virtual filesystem to validate startup.
#if defined(ALTIRRA_WASM_SMOKE_BOOT_PATH)
	if (!hadBootImage && argc <= 1) {
		const VDStringW smokePathW = VDTextU8ToW(VDStringSpanA(ALTIRRA_WASM_SMOKE_BOOT_PATH));
		if (!VDDoesPathExist(smokePathW.c_str())) {
			LOG_WARN("CmdLine", "WASM smoke media not found in VFS: %s", ALTIRRA_WASM_SMOKE_BOOT_PATH);
		} else {
			if (!haveUnloadedAllImages) {
				g_sim.UnloadAll(ATUIGetBootUnloadStorageMask());
				haveUnloadedAllImages = true;
			}

			// Smoke boot media may not include optional debugger sidecar files
			// (e.g. .lst/.lab/.atdbg). Temporarily disable auto-load to avoid
			// startup aborts when those files are absent in the WASM VFS.
			IATDebugger *dbg = ATGetDebugger();
			ATDebuggerSymbolLoadMode preStartSymMode = ATDebuggerSymbolLoadMode::Default;
			ATDebuggerSymbolLoadMode postStartSymMode = ATDebuggerSymbolLoadMode::Default;
			ATDebuggerScriptAutoLoadMode scriptMode = ATDebuggerScriptAutoLoadMode::Default;
			const bool restoreDebugModes = (dbg != nullptr);

			if (restoreDebugModes) {
				preStartSymMode = dbg->GetSymbolLoadMode(false);
				postStartSymMode = dbg->GetSymbolLoadMode(true);
				scriptMode = dbg->GetScriptAutoLoadMode();

				dbg->SetSymbolLoadMode(false, ATDebuggerSymbolLoadMode::Disabled);
				dbg->SetSymbolLoadMode(true, ATDebuggerSymbolLoadMode::Disabled);
				dbg->SetScriptAutoLoadMode(ATDebuggerScriptAutoLoadMode::Disabled);
			}

			const bool suppressColdReset = DoLoadDirect(
				ALTIRRA_WASM_SMOKE_BOOT_PATH,
				bootImageWriteMode,
				imageCartMapper,
				kATImageType_Program,
				-1);

			if (restoreDebugModes) {
				dbg->SetSymbolLoadMode(false, preStartSymMode);
				dbg->SetSymbolLoadMode(true, postStartSymMode);
				dbg->SetScriptAutoLoadMode(scriptMode);
			}

			if (!suppressColdReset)
				coldResetPending = true;

			hadBootImage = true;
			LOG_INFO("CmdLine", "WASM smoke auto-boot: %s", ALTIRRA_WASM_SMOKE_BOOT_PATH);
		}
	}
#endif

	// -----------------------------------------------------------------------
	// Post-processing (matches Windows PostSequenceCleanup at line 782-867)
	// -----------------------------------------------------------------------

	// Flush cold reset
	if (coldResetPending)
		g_sim.ColdReset();

	// Apply initial tape position
	if (initialTapePos > 0)
		g_sim.GetCassette().SeekToTime(initialTapePos);

	// Load startup.atdbg from program directory
	{
		const VDStringW dbgInitPath(VDMakePath(VDGetProgramPath().c_str(), L"startup.atdbg"));
		if (VDDoesPathExist(dbgInitPath.c_str())) {
			try {
				ATGetDebugger()->QueueBatchFile(dbgInitPath.c_str());
			} catch (const MyError&) {
				// ignore startup script errors
			}
			debugModeSuspend = true;
		}
	}

	// Debug suspend mode — suspend emulation and queue resume after commands.
	// The queued `g -n command will resume emulation once all debugger
	// commands have been processed.
	if (debugModeSuspend) {
		// If no boot image was loaded, we need to cold reset first so the
		// emulation is in a known state (matches Windows where the simulator
		// was already running before command-line processing).
		if (!hadBootImage && !coldResetPending)
			g_sim.ColdReset();

		g_sim.Suspend();
		ATGetDebugger()->QueueCommand("`g -n", false);
	}

	// Type text (--type switch)
	if (!keysToType.empty()) {
		// Apply tilde -> newline and backtick -> quote substitution
		// (matches Windows uicommandline.cpp:809-821)
		for (size_t j = 0; j < keysToType.size(); ++j) {
			if (keysToType[j] == '~')
				keysToType[j] = '\n';
			else if (keysToType[j] == '`')
				keysToType[j] = '"';
		}

		VDStringW wtext = VDTextU8ToW(VDStringSpanA(keysToType.c_str(), keysToType.c_str() + keysToType.size()));
		ATUIPasteTextDirect(wtext.data(), wtext.size());
	}

	// Resume emulation if we loaded anything (and debug suspend is not active —
	// in that case, the queued `g -n command handles resumption)
	if (hadBootImage && !debugModeSuspend)
		g_sim.Resume();

	// Return true if we handled startup (boot image loaded, or debug suspend
	// active, or any config switch was processed that requires us to suppress
	// the default ColdReset+Resume in the caller)
	return hadBootImage || debugModeSuspend;
}
