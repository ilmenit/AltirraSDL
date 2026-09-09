//	Altirra - Atari 800/800XL/5200 emulator
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
#include <regex>
#include <vd2/system/cmdline.h>
#include <vd2/system/filesys.h>
#include <vd2/system/strutil.h>
#include <vd2/Dita/services.h>
#include <at/atcore/deviceparent.h>
#include <at/atcore/media.h>
#include <at/atcore/propertyset.h>
#include <at/atio/cartridgetypes.h>
#include <at/atnativeui/genericdialog.h>
#include <at/atui/uicommandmanager.h>
#include "cassette.h"
#include "cheatengine.h"
#include "console.h"
#include "debugger.h"
#include "devicemanager.h"
#include "disk.h"
#include "firmwaremanager.h"
#include "hostdevice.h"
#include "options.h"
#include "oshelper.h"
#include "settings.h"
#include "simulator.h"
#include "uiaccessors.h"
#include "uicommondialogs.h"
#include "uikeyboard.h"
#include "versioninfo.h"

extern ATSimulator g_sim;
extern ATUIKeyboardOptions g_kbdOpts;
extern bool g_ATCmdLineHadAnything;
extern bool g_ATRegistryHadAnything;

void LoadBaselineSettings();
void ATDebuggerInitAutotestCommands();
void ATUIShowDialogSetupWizard(VDGUIHandle hParent);
void ATUIUpdateSpeedTiming();
void DoLoad(VDGUIHandle h, const wchar_t *path, const ATMediaWriteMode *writeMode, int cartmapper, ATImageType loadType = kATImageType_None, bool *suppressColdReset = NULL, int loadIndex = -1, bool autoProfile = false);
void Paste(const wchar_t *s, size_t len, bool useCooldown);

namespace {
	void SetKernelType(ATFirmwareType type) {
		uint64 id = g_sim.GetFirmwareManager()->GetFirmwareOfType(type, false);
		g_sim.SetKernel(id ? id : kATFirmwareId_NoKernel);
	}

	class ATTimeArgumentParseException final : public MyError {
	public:
		ATTimeArgumentParseException() : MyError("Invalid time argument format.") {}
	};

	float ATParseTimeArgument(const wchar_t *s) {
		// Formats we support:

		// [[hh:]mm:]ss.sss
		const std::wregex timeRegex1(LR"--((?:(?:([0-9]+)\:|)([0-9]+)\:|)([0-9]+(?:|\.[0-9]+)))--", std::regex_constants::icase);

		// [00h][00m][00.000s]
		const std::wregex timeRegex2(LR"--((?:([0-9]+)h|)(?:([0-9]+)m|)(?:([0-9]+(?:|\.[0-9]+))s|))--", std::regex_constants::icase);

		float t = 0;

		std::match_results<const wchar_t *> m;
		if (!std::regex_match(s, m, timeRegex1) && !std::regex_match(s, m, timeRegex2))
			throw ATTimeArgumentParseException();

		if (m[1].matched)
			t += (float)wcstol(m[1].str().c_str(), nullptr, 10) * 3600.0f;

		if (m[2].matched)
			t += (float)wcstol(m[2].str().c_str(), nullptr, 10) * 60.0f;

		if (m[3].matched)
			t += (float)wcstod(m[3].str().c_str(), nullptr);

		return t;
	}
}

class ATUICommandLineProcessor {
	using SelfType = ATUICommandLineProcessor;

public:
	ATUICommandLineProcessor(VDCommandLine&& cmdLine);

	void Run();

private:
	struct SwitchEntry;

	bool ProcessNextSequencedSwitch();
	void PostSequenceCleanup();

	void HandleClearDevices();
	void HandleAddDevice(const wchar_t *arg);
	void HandleSetDevice(const wchar_t *arg);

	static IATDeviceParent *ResolveDeviceParent(ATDeviceManager& dm, ATPropertySet& pset, VDStringW& parentTag);
	static void AddDevice(ATDeviceManager& dm, const ATDeviceDefinition& def, const ATPropertySet& pset, IATDeviceParent *parent, const VDStringW& parentTag);

	void HandleRemoveDevice(const wchar_t *arg);
	void HandleCmd(const wchar_t *arg);
	void HandleCmd1(const wchar_t *arg1, const wchar_t *arg2);

	void ExecCommand(const wchar_t *cmdName, std::initializer_list<const wchar_t *> args);
	void OnCommandCompleted(bool succeeded);

	void FlushColdReset();
	void UnloadAllIfFirstImage();

	bool mbHaveUnloadedAllImages = false;
	bool mbColdResetPending = false;
	bool mbAutoSwitchProfile = false;
	bool mbDebugModeSuspend = false;
	bool mbCommandPending = false;
	bool mbException = false;
	int mImageCartMapper = 0;
	float mInitialTapePos = 0;
	VDStringW mKeysToType;
	const ATMediaWriteMode *mpBootImageWriteMode = nullptr;
	VDCommandLineIterator mSequencedArgIterator;

	VDCommandLine mCmdLine;

	static const SwitchEntry kSequencedSwitches[];
};

ATUICommandLineProcessor *g_ATUICommandLineProcessor;

struct ATUICommandLineProcessor::SwitchEntry {
	const char *mpName = nullptr;
	uint32 mNameHash = 0;
	uint8 mArgCount = 0;
	union {
		void (SelfType::*mpArg0Handler)();
		void (SelfType::*mpArg1Handler)(const wchar_t *);
		void (SelfType::*mpArg2Handler)(const wchar_t *, const wchar_t *);
	};

	consteval SwitchEntry(const char *name, void (SelfType::*handler)())
		: mpName(name)
		, mNameHash(VDHashString32IC(name))
		, mArgCount(0)
		, mpArg0Handler(std::move(handler))
	{
	}

	consteval SwitchEntry(const char *name, void (SelfType::*handler)(const wchar_t *))
		: mpName(name)
		, mNameHash(VDHashString32IC(name))
		, mArgCount(1)
		, mpArg1Handler(std::move(handler))
	{
	}

	consteval SwitchEntry(const char *name, void (SelfType::*handler)(const wchar_t *, const wchar_t *))
		: mpName(name)
		, mNameHash(VDHashString32IC(name))
		, mArgCount(2)
		, mpArg2Handler(std::move(handler))
	{
	}

	bool Matches(uint32 hash, const wchar_t *arg) const {
		if (hash != mNameHash)
			return false;

		const wchar_t *s = arg;
		const char *t = mpName;

		for(;;) {
			wchar_t ch = *s++;

			if ((char)ch != ch)
				return false;

			if (*t++ != ch)
				return false;

			if (!ch)
				break;
		}

		return true;
	}
};

constexpr ATUICommandLineProcessor::SwitchEntry ATUICommandLineProcessor::kSequencedSwitches[] {
	{ "/cleardevices", &SelfType::HandleClearDevices },
	{ "/adddevice", &SelfType::HandleAddDevice },
	{ "/setdevice", &SelfType::HandleSetDevice },
	{ "/removedevice", &SelfType::HandleRemoveDevice },
	{ "/cmd", &SelfType::HandleCmd },
	{ "/cmd1", &SelfType::HandleCmd1 },
};

ATUICommandLineProcessor::ATUICommandLineProcessor(VDCommandLine&& cmdLine)
	: mCmdLine(std::move(cmdLine))
{
}

void ATUICommandLineProcessor::Run() {
	if (mCmdLine.FindAndRemoveSwitch(L"crash")) {
		volatile char *volatile ptr = nullptr;
		*ptr = 0;
	}

	mbHaveUnloadedAllImages = mCmdLine.FindAndRemoveSwitch(L"autoprofile");

	if (mCmdLine.FindAndRemoveSwitch(L"launch")) {
		if (g_ATOptions.mbLaunchAutoProfile)
			mbHaveUnloadedAllImages = true;
	}

	if (mCmdLine.FindAndRemoveSwitch(L"noautoprofile"))
		mbHaveUnloadedAllImages = false;

	ATSettingsSetBootstrapProfileMode(mbHaveUnloadedAllImages);

	try {
		// This is normally intercepted early. If we got here, it was because of
		// another instance.
		if (mCmdLine.FindAndRemoveSwitch(L"baseline")) {
			LoadBaselineSettings();
			mbColdResetPending = true;
		}

		if (mCmdLine.FindAndRemoveSwitch(L"autotest")) {
			ATDebuggerInitAutotestCommands();
		}

		if (mCmdLine.FindAndRemoveSwitch(L"borderless")) {
			g_ATOptions.mbFullScreenBorderless = true;
			g_ATOptions.mbDirty = true;
		} else if (mCmdLine.FindAndRemoveSwitch(L"noborderless")) {
			g_ATOptions.mbFullScreenBorderless = false;
			g_ATOptions.mbDirty = true;
		}

		if (mCmdLine.FindAndRemoveSwitch(L"f"))
			ATSetFullscreen(true);

		if (mCmdLine.FindAndRemoveSwitch(L"ntsc"))
			g_sim.SetVideoStandard(kATVideoStandard_NTSC);
		else if (mCmdLine.FindAndRemoveSwitch(L"pal"))
			g_sim.SetVideoStandard(kATVideoStandard_PAL);
		else if (mCmdLine.FindAndRemoveSwitch(L"secam"))
			g_sim.SetVideoStandard(kATVideoStandard_SECAM);
		else if (mCmdLine.FindAndRemoveSwitch(L"ntsc50"))
			g_sim.SetVideoStandard(kATVideoStandard_NTSC50);
		else if (mCmdLine.FindAndRemoveSwitch(L"pal60"))
			g_sim.SetVideoStandard(kATVideoStandard_PAL60);

		ATUIUpdateSpeedTiming();

		if (mCmdLine.FindAndRemoveSwitch(L"burstio")) {
			g_sim.SetDiskBurstTransfersEnabled(true);
		} else if (mCmdLine.FindAndRemoveSwitch(L"burstiopolled")) {
			g_sim.SetDiskBurstTransfersEnabled(true);
		} else if (mCmdLine.FindAndRemoveSwitch(L"noburstio")) {
			g_sim.SetDiskBurstTransfersEnabled(false);
		}

		if (mCmdLine.FindAndRemoveSwitch(L"siopatch")) {
			g_sim.SetDiskSIOPatchEnabled(true);
			g_sim.SetDiskSIOOverrideDetectEnabled(false);
			g_sim.SetCassetteSIOPatchEnabled(true);
		} else if (mCmdLine.FindAndRemoveSwitch(L"siopatchsafe")) {
			g_sim.SetDiskSIOPatchEnabled(true);
			g_sim.SetDiskSIOOverrideDetectEnabled(true);
			g_sim.SetCassetteSIOPatchEnabled(true);
		} else if (mCmdLine.FindAndRemoveSwitch(L"nosiopatch")) {
			g_sim.SetDiskSIOPatchEnabled(false);
			g_sim.SetCassetteSIOPatchEnabled(false);
		}

		if (mCmdLine.FindAndRemoveSwitch(L"fastboot")) {
			g_sim.SetFastBootEnabled(true);
		} else if (mCmdLine.FindAndRemoveSwitch(L"nofastboot")) {
			g_sim.SetFastBootEnabled(false);
		}

		if (mCmdLine.FindAndRemoveSwitch(L"casautoboot")) {
			g_sim.SetCassetteAutoBootEnabled(true);
		} else if (mCmdLine.FindAndRemoveSwitch(L"nocasautoboot")) {
			g_sim.SetCassetteAutoBootEnabled(false);
		}

		if (mCmdLine.FindAndRemoveSwitch(L"casautobasicboot")) {
			g_sim.SetCassetteAutoBasicBootEnabled(true);
		} else if (mCmdLine.FindAndRemoveSwitch(L"nocasautobasicboot")) {
			g_sim.SetCassetteAutoBasicBootEnabled(false);
		}

		if (mCmdLine.FindAndRemoveSwitch(L"accuratedisk")) {
			g_sim.SetDiskAccurateTimingEnabled(true);
		} else if (mCmdLine.FindAndRemoveSwitch(L"noaccuratedisk")) {
			g_sim.SetDiskAccurateTimingEnabled(false);
		}

		if (mCmdLine.FindAndRemoveSwitch(L"stereo")) {
			g_sim.SetDualPokeysEnabled(true);
			mbColdResetPending = true;
		} else if (mCmdLine.FindAndRemoveSwitch(L"nostereo")) {
			g_sim.SetDualPokeysEnabled(false);
			mbColdResetPending = true;
		}

		if (mCmdLine.FindAndRemoveSwitch(L"basic")) {
			g_sim.SetBASICEnabled(true);
			mbColdResetPending = true;
		} else if (mCmdLine.FindAndRemoveSwitch(L"nobasic")) {
			g_sim.SetBASICEnabled(false);
			mbColdResetPending = true;
		}

		const wchar_t *arg;
		if (mCmdLine.FindAndRemoveSwitch(L"soundboard", arg)) {
			ATPropertySet pset;
			
			uint32 base = 0;

			if (!vdwcsicmp(arg, L"d2c0"))
				base = 0xD2C0;
			else if (!vdwcsicmp(arg, L"d500"))
				base = 0xD500;
			else if (!vdwcsicmp(arg, L"d600"))
				base = 0xD600;
			else
				throw VDException(L"Invalid SoundBoard memory base: '%ls'", arg);

			pset.SetUint32("base", base);

			auto *dm = g_sim.GetDeviceManager();
			auto *dev = dm->GetDeviceByTag("soundboard");

			if (dev)
				dev->SetSettings(pset);
			else
				dm->AddDevice("soundboard", pset);

			mbColdResetPending = true;
		} else if (mCmdLine.FindAndRemoveSwitch(L"nosoundboard")) {
			g_sim.GetDeviceManager()->RemoveDevice("soundboard");
			mbColdResetPending = true;
		}

		if (mCmdLine.FindAndRemoveSwitch(L"slightsid", arg)) {
			auto& dm = *g_sim.GetDeviceManager();
			if (!dm.GetDeviceByTag("slightsid"))
				dm.AddDevice("slightsid", ATPropertySet());
		} else if (mCmdLine.FindAndRemoveSwitch(L"noslightsid")) {
			g_sim.GetDeviceManager()->RemoveDevice("slightsid");
		}

		if (mCmdLine.FindAndRemoveSwitch(L"covox", arg)) {
			auto& dm = *g_sim.GetDeviceManager();
			if (!dm.GetDeviceByTag("covox"))
				dm.AddDevice("covox", ATPropertySet());
		} else if (mCmdLine.FindAndRemoveSwitch(L"nocovox")) {
			g_sim.GetDeviceManager()->RemoveDevice("covox");
		}

		if (mCmdLine.FindAndRemoveSwitch(L"hardware", arg)) {
			if (!vdwcsicmp(arg, L"800"))
				g_sim.SetHardwareMode(kATHardwareMode_800);
			else if (!vdwcsicmp(arg, L"800xl"))
				g_sim.SetHardwareMode(kATHardwareMode_800XL);
			else if (!vdwcsicmp(arg, L"1200xl"))
				g_sim.SetHardwareMode(kATHardwareMode_1200XL);
			else if (!vdwcsicmp(arg, L"130xe"))
				g_sim.SetHardwareMode(kATHardwareMode_130XE);
			else if (!vdwcsicmp(arg, L"xegs"))
				g_sim.SetHardwareMode(kATHardwareMode_XEGS);
			else if (!vdwcsicmp(arg, L"1400xl"))
				g_sim.SetHardwareMode(kATHardwareMode_1400XL);
			else if (!vdwcsicmp(arg, L"5200"))
				g_sim.SetHardwareMode(kATHardwareMode_5200);
			else
				throw VDException(L"Invalid hardware mode '%ls'", arg);
		}

		if (mCmdLine.FindAndRemoveSwitch(L"kernel", arg)) {
			if (!vdwcsicmp(arg, L"default"))
				g_sim.SetKernel(0);
			else if (!vdwcsicmp(arg, L"osa"))
				SetKernelType(kATFirmwareType_Kernel800_OSA);
			else if (!vdwcsicmp(arg, L"osb"))
				SetKernelType(kATFirmwareType_Kernel800_OSB);
			else if (!vdwcsicmp(arg, L"xl"))
				SetKernelType(kATFirmwareType_KernelXL);
			else if (!vdwcsicmp(arg, L"xegs"))
				SetKernelType(kATFirmwareType_KernelXEGS);
			else if (!vdwcsicmp(arg, L"1200xl"))
				SetKernelType(kATFirmwareType_Kernel1200XL);
			else if (!vdwcsicmp(arg, L"5200"))
				SetKernelType(kATFirmwareType_Kernel5200);
			else if (!vdwcsicmp(arg, L"lle"))
				g_sim.SetKernel(kATFirmwareId_Kernel_LLE);
			else if (!vdwcsicmp(arg, L"llexl"))
				g_sim.SetKernel(kATFirmwareId_Kernel_LLEXL);
			else if (!vdwcsicmp(arg, L"hle"))
				g_sim.SetKernel(kATFirmwareId_Kernel_LLE);
			else if (!vdwcsicmp(arg, L"5200lle"))
				g_sim.SetKernel(kATFirmwareId_5200_LLE);
			else
				throw VDException(L"Invalid kernel mode '%ls'", arg);
		}
		
		if (mCmdLine.FindAndRemoveSwitch(L"kernelref", arg)) {
			const auto id = g_sim.GetFirmwareManager()->GetFirmwareByRefString(arg, ATIsKernelFirmwareType);
			if (!id)
				throw VDException(L"Unable to find a match for kernel reference: %ls", arg);

			g_sim.SetKernel(id);
		}

		if (mCmdLine.FindAndRemoveSwitch(L"basicref", arg)) {
			const auto id = g_sim.GetFirmwareManager()->GetFirmwareByRefString(arg, [](ATFirmwareType type) { return type == kATFirmwareType_Basic; });
			if (!id)
				throw VDException(L"Unable to find a match for kernel reference: %ls", arg);

			g_sim.SetBasic(id);
		}

		if (mCmdLine.FindAndRemoveSwitch(L"memsize", arg)) {
			if (!vdwcsicmp(arg, L"8K"))
				g_sim.SetMemoryMode(kATMemoryMode_8K);
			else if (!vdwcsicmp(arg, L"16K"))
				g_sim.SetMemoryMode(kATMemoryMode_16K);
			else if (!vdwcsicmp(arg, L"24K"))
				g_sim.SetMemoryMode(kATMemoryMode_24K);
			else if (!vdwcsicmp(arg, L"32K"))
				g_sim.SetMemoryMode(kATMemoryMode_32K);
			else if (!vdwcsicmp(arg, L"40K"))
				g_sim.SetMemoryMode(kATMemoryMode_40K);
			else if (!vdwcsicmp(arg, L"48K"))
				g_sim.SetMemoryMode(kATMemoryMode_48K);
			else if (!vdwcsicmp(arg, L"52K"))
				g_sim.SetMemoryMode(kATMemoryMode_52K);
			else if (!vdwcsicmp(arg, L"64K"))
				g_sim.SetMemoryMode(kATMemoryMode_64K);
			else if (!vdwcsicmp(arg, L"128K"))
				g_sim.SetMemoryMode(kATMemoryMode_128K);
			else if (!vdwcsicmp(arg, L"256K"))
				g_sim.SetMemoryMode(kATMemoryMode_256K);
			else if (!vdwcsicmp(arg, L"320K"))
				g_sim.SetMemoryMode(kATMemoryMode_320K);
			else if (!vdwcsicmp(arg, L"320KCOMPY"))
				g_sim.SetMemoryMode(kATMemoryMode_320K_Compy);
			else if (!vdwcsicmp(arg, L"576K"))
				g_sim.SetMemoryMode(kATMemoryMode_576K);
			else if (!vdwcsicmp(arg, L"576KCOMPY"))
				g_sim.SetMemoryMode(kATMemoryMode_576K_Compy);
			else if (!vdwcsicmp(arg, L"1088K"))
				g_sim.SetMemoryMode(kATMemoryMode_1088K);
			else
				throw VDException(L"Invalid memory mode '%ls'", arg);
		}

		if (mCmdLine.FindAndRemoveSwitch(L"axlonmemsize", arg)) {
			if (!vdwcsicmp(arg, L"none"))
				g_sim.SetAxlonMemoryMode(0);
			if (!vdwcsicmp(arg, L"64K"))
				g_sim.SetAxlonMemoryMode(2);
			if (!vdwcsicmp(arg, L"128K"))
				g_sim.SetAxlonMemoryMode(3);
			if (!vdwcsicmp(arg, L"256K"))
				g_sim.SetAxlonMemoryMode(4);
			if (!vdwcsicmp(arg, L"512K"))
				g_sim.SetAxlonMemoryMode(5);
			if (!vdwcsicmp(arg, L"1024K"))
				g_sim.SetAxlonMemoryMode(6);
			if (!vdwcsicmp(arg, L"2048K"))
				g_sim.SetAxlonMemoryMode(7);
			if (!vdwcsicmp(arg, L"4096K"))
				g_sim.SetAxlonMemoryMode(7);
		}

		if (mCmdLine.FindAndRemoveSwitch(L"highbanks", arg)) {
			if (!vdwcsicmp(arg, L"na"))
				g_sim.SetHighMemoryBanks(-1);
			else if (!vdwcsicmp(arg, L"0"))
				g_sim.SetHighMemoryBanks(0);
			else if (!vdwcsicmp(arg, L"1"))
				g_sim.SetHighMemoryBanks(1);
			else if (!vdwcsicmp(arg, L"3"))
				g_sim.SetHighMemoryBanks(3);
			else if (!vdwcsicmp(arg, L"15"))
				g_sim.SetHighMemoryBanks(15);
			else if (!vdwcsicmp(arg, L"63"))
				g_sim.SetHighMemoryBanks(63);
		}

		if (mCmdLine.FindAndRemoveSwitch(L"artifact", arg)) {
			if (!vdwcsicmp(arg, L"none"))
				g_sim.GetGTIA().SetArtifactingMode(ATArtifactMode::None);
			else if (!vdwcsicmp(arg, L"ntsc"))
				g_sim.GetGTIA().SetArtifactingMode(ATArtifactMode::NTSC);
			else if (!vdwcsicmp(arg, L"ntschi"))
				g_sim.GetGTIA().SetArtifactingMode(ATArtifactMode::NTSCHi);
			else if (!vdwcsicmp(arg, L"pal"))
				g_sim.GetGTIA().SetArtifactingMode(ATArtifactMode::PAL);
			else if (!vdwcsicmp(arg, L"palhi"))
				g_sim.GetGTIA().SetArtifactingMode(ATArtifactMode::PALHi);
			else
				throw VDException(L"Invalid hardware mode '%ls'", arg);
		}

		if (mCmdLine.FindAndRemoveSwitch(L"vsync"))
			g_sim.GetGTIA().SetVsyncEnabled(true);
		else if (mCmdLine.FindAndRemoveSwitch(L"novsync"))
			g_sim.GetGTIA().SetVsyncEnabled(false);

		if (mCmdLine.FindAndRemoveSwitch(L"debug")) {
			// Open the console now so we see load messages.
			ATShowConsole();
		}

		IATDebugger *dbg = ATGetDebugger();
		if (mCmdLine.FindAndRemoveSwitch(L"debugbrkrun"))
			dbg->SetBreakOnEXERunAddrEnabled(true);
		else if (mCmdLine.FindAndRemoveSwitch(L"nodebugbrkrun"))
			dbg->SetBreakOnEXERunAddrEnabled(false);

		while(mCmdLine.FindAndRemoveSwitch(L"debugcmd", arg)) {
			mbDebugModeSuspend = true;

			dbg->QueueCommand(VDTextWToA(arg).c_str(), false);
		}

		if (mCmdLine.FindAndRemoveSwitch(L"bootro")) {
			static const auto modeRO = kATMediaWriteMode_RO;
			mpBootImageWriteMode = &modeRO;
		}

		if (mCmdLine.FindAndRemoveSwitch(L"bootrw")) {
			static const auto modeRW = kATMediaWriteMode_RW;
			mpBootImageWriteMode = &modeRW;
		}

		if (mCmdLine.FindAndRemoveSwitch(L"bootvrw")) {
			static const auto modeVRW = kATMediaWriteMode_VRW;
			mpBootImageWriteMode = &modeVRW;
		}

		if (mCmdLine.FindAndRemoveSwitch(L"bootvrwsafe")) {
			static const auto modeVRWSafe = kATMediaWriteMode_VRWSafe;
			mpBootImageWriteMode = &modeVRWSafe;
		}

		if (mCmdLine.FindAndRemoveSwitch(L"type", arg)) {
			mKeysToType += arg;
		}

		if (mCmdLine.FindAndRemoveSwitch(L"nopclink")) {
			g_sim.GetDeviceManager()->RemoveDevice("pclink");
		} else if (mCmdLine.FindAndRemoveSwitch(L"pclink", arg)) {
			VDStringRefW tokenizer(arg);
			VDStringRefW mode;

			if (!tokenizer.split(',', mode))
				throw VDException(L"Invalid PCLink mount string: %ls", arg);

			bool write = false;

			if (mode == L"rw")
				write = true;
			else if (mode != L"ro")
				throw MyError("Invalid PCLink mount mode: %.*ls", mode.size(), mode.data());

			ATPropertySet pset;
			pset.SetString("path", VDStringW(tokenizer).c_str());

			if (write)
				pset.SetBool("write", true);

			auto *dm = g_sim.GetDeviceManager();
			IATDevice *dev = dm->GetDeviceByTag("pclink");

			if (dev)
				dev->SetSettings(pset);
			else
				dm->AddDevice("pclink", pset);
		}

		if (mCmdLine.FindAndRemoveSwitch(L"nohdpath", arg)) {
			auto *dm = g_sim.GetDeviceManager();
			IATDevice *dev = dm->GetDeviceByTag("hostfs");

			if (dev) {
				dm->RemoveDevice(dev);
				mbColdResetPending = true;
			}
		}

		if (mCmdLine.FindAndRemoveSwitch(L"hdpath", arg)) {
			auto *dm = g_sim.GetDeviceManager();
			IATDevice *dev = dm->GetDeviceByTag("hostfs");

			if (!dev)
				dev = dm->AddDevice("hostfs", ATPropertySet());

			IATHostDeviceEmulator *hd = vdpoly_cast<IATHostDeviceEmulator *>(dev);
			if (hd) {
				hd->SetReadOnly(true);
				hd->SetBasePath(0, arg);

				mbColdResetPending = true;
			}
		}

		if (mCmdLine.FindAndRemoveSwitch(L"hdpathrw", arg)) {
			auto *dm = g_sim.GetDeviceManager();
			IATDevice *dev = dm->GetDeviceByTag("hostfs");

			if (!dev)
				dev = dm->AddDevice("hostfs", ATPropertySet());

			IATHostDeviceEmulator *hd = vdpoly_cast<IATHostDeviceEmulator *>(dev);
			if (hd) {
				hd->SetReadOnly(false);
				hd->SetBasePath(0, arg);

				mbColdResetPending = true;
			}
		}

		if (mCmdLine.FindAndRemoveSwitch(L"rawkeys"))
			g_kbdOpts.mbRawKeys = true;
		else if (mCmdLine.FindAndRemoveSwitch(L"norawkeys"))
			g_kbdOpts.mbRawKeys = false;

		if (mCmdLine.FindAndRemoveSwitch(L"nocartchecksum")) {
			mImageCartMapper = -1;
		} else if (mCmdLine.FindAndRemoveSwitch(L"cartmapper", arg)) {
			mImageCartMapper = ATGetCartridgeModeForMapper(wcstol(arg, NULL, 10));

			if (mImageCartMapper <= 0 || mImageCartMapper >= kATCartridgeModeCount)
				throw VDException(L"Unsupported or invalid cartridge mapper: %ls", arg);
		}

		if (mCmdLine.FindAndRemoveSwitch(L"nocheats")) {
			g_sim.SetCheatEngineEnabled(false);
		} else if (mCmdLine.FindAndRemoveSwitch(L"cheats", arg)) {
			g_sim.SetCheatEngineEnabled(true);
			g_sim.GetCheatEngine()->Load(arg);
		}

		if (mCmdLine.FindAndRemoveSwitch(L"diskemu", arg)) {
			auto result = ATParseEnum<ATDiskEmulationMode>(VDTextWToU8(VDStringSpanW(arg)));

			if (!result.mValid)
				throw VDException(L"Unsupported disk emulation mode: %ls", arg);

			for(int i=0; i<15; ++i)
				g_sim.GetDiskDrive(i).SetEmulationMode(result.mValue);
		}

		while(mCmdLine.FindAndRemoveSwitch(L"cart", arg)) {
			UnloadAllIfFirstImage();
			DoLoad(ATUIGetNewPopupOwner(), arg, mpBootImageWriteMode, mImageCartMapper, kATImageType_Cartridge, nullptr, -1, mbAutoSwitchProfile);
			mbColdResetPending = true;// required to set up cassette autoboot
		}

		int diskIndex = 0;
		while(mCmdLine.FindAndRemoveSwitch(L"disk", arg)) {
			UnloadAllIfFirstImage();
			DoLoad(ATUIGetNewPopupOwner(), arg, mpBootImageWriteMode, mImageCartMapper, kATImageType_Disk, nullptr, diskIndex++, mbAutoSwitchProfile);
			mbColdResetPending = true;// required to set up cassette autoboot
		}

		while(mCmdLine.FindAndRemoveSwitch(L"run", arg)) {
			UnloadAllIfFirstImage();
			DoLoad(ATUIGetNewPopupOwner(), arg, mpBootImageWriteMode, mImageCartMapper, kATImageType_Program, nullptr, -1, mbAutoSwitchProfile);
			mbColdResetPending = true;// required to set up cassette autoboot
		}

		while(mCmdLine.FindAndRemoveSwitch(L"runbas", arg)) {
			UnloadAllIfFirstImage();
			DoLoad(ATUIGetNewPopupOwner(), arg, mpBootImageWriteMode, mImageCartMapper, kATImageType_BasicProgram, nullptr, -1, mbAutoSwitchProfile);
			mbColdResetPending = true;// required to set up cassette autoboot
		}

		while(mCmdLine.FindAndRemoveSwitch(L"tape", arg)) {
			UnloadAllIfFirstImage();
			DoLoad(ATUIGetNewPopupOwner(), arg, mpBootImageWriteMode, mImageCartMapper, kATImageType_Tape, nullptr, -1, mbAutoSwitchProfile);
			mbColdResetPending = true;// required to set up cassette autoboot
		}

		if (mCmdLine.FindAndRemoveSwitch(L"tapepos", arg))
			mInitialTapePos = ATParseTimeArgument(arg);

		// We don't actually need to do anything with this switch. Its mere presence would have
		// suppressed the setup wizard as any other switch does.
		mCmdLine.FindAndRemoveSwitch(L"skipsetup");

		// prescan the remaining arguments and ensure that all switches are handled, before we do anything else
		for(VDCommandLineIterator it2; mCmdLine.GetNextSwitchArgument(it2, arg);) {
			const uint32 hash = VDHashString32I(arg);
			bool valid = false;

			for(const SwitchEntry& sh : kSequencedSwitches) {
				if (sh.Matches(hash, arg)) {
					valid = true;
					break;
				}
			}

			if (!valid)
				throw VDException(L"Unknown command-line switch: %ls. Use /? for help.", arg);
		}

		OnCommandCompleted(true);
	} catch(const MyError& e) {
		ATUIShowError2(ATUIGetNewPopupOwner(), e.wc_str(), L"Command-line error");
	}
}

bool ATUICommandLineProcessor::ProcessNextSequencedSwitch() {
	bool isSwitch = false;
	const wchar_t *arg = nullptr;

	if (!mCmdLine.GetNextArgument(mSequencedArgIterator, arg, isSwitch))
		return false;

	if (isSwitch) {
		const uint32 hash = VDHashString32I(arg);

		for(const SwitchEntry& sh : kSequencedSwitches) {
			if (sh.Matches(hash, arg)) {
				const wchar_t *args[2] {};

				for(int i=0; i<sh.mArgCount && i<2; ++i) {
					if (!mCmdLine.GetNextArgument(mSequencedArgIterator, args[i], isSwitch) || isSwitch)
						throw VDException(L"Command-line error: not enough arguments for switch %ls.", sh.mpName);
				}

				switch(sh.mArgCount) {
					case 0:
						(this->*sh.mpArg0Handler)();
						break;

					case 1:
						(this->*sh.mpArg1Handler)(args[0]);
						break;

					case 2:
						(this->*sh.mpArg2Handler)(args[0], args[1]);
						break;
				}
				break;
			}
		}
	} else {
		UnloadAllIfFirstImage();

		bool suppressColdReset = false;
		DoLoad(ATUIGetNewPopupOwner(), arg, mpBootImageWriteMode, mImageCartMapper, kATImageType_None, &suppressColdReset, ATImageLoadContext::kLoadIndexNextFree, mbAutoSwitchProfile);

		VDSetLastLoadSavePath("load"_vdfcctypeid, VDGetFullPath(arg).c_str());

		if (!suppressColdReset)
			mbColdResetPending = true; // required to set up cassette autoboot
	}

	return true;
}

void ATUICommandLineProcessor::PostSequenceCleanup() {
	ATSettingsSetBootstrapProfileMode(false);

	FlushColdReset();

	if (mInitialTapePos > 0) {
		g_sim.GetCassette().SeekToTime(mInitialTapePos);
	}

	const VDStringW dbgInitPath(VDMakePath(VDGetProgramPath().c_str(), L"startup.atdbg"));

	if (VDDoesPathExist(dbgInitPath.c_str())) {
		try {
			ATGetDebugger()->QueueBatchFile(dbgInitPath.c_str());
		} catch(const MyError&) {
			// ignore startup error
		}

		mbDebugModeSuspend = true;
	}

	if (mbDebugModeSuspend) {
		g_sim.Suspend();
		ATGetDebugger()->QueueCommand("`g -n", false);
	}

	if (!mKeysToType.empty()) {
		VDStringW::size_type i = 0;
		while((i = mKeysToType.find('~', i)) != VDStringW::npos) {
			mKeysToType[i] = '\n';
			++i;
		}

		i = 0;
		while((i = mKeysToType.find('`', i)) != VDStringW::npos) {
			mKeysToType[i] = '"';
			++i;
		}

		Paste(mKeysToType.data(), mKeysToType.size(), false);
	}

	// Check if we should display the setup wizard at this time. We skip it if:
	//
	// - We've already tried to show it
	// - There was something on the command line other than /portable or /portablealt
	// - We think the program's been run before
	//
	{
		VDRegistryAppKey key;

		if (!key.getBool("ShownSetupWizard")) {
			key.setBool("ShownSetupWizard", true);

			if (!g_ATCmdLineHadAnything && !g_ATRegistryHadAnything) {
				ATUIShowDialogSetupWizard(ATUIGetNewPopupOwner());
			}
		}
	}

#ifndef VD_CPU_ARM64
	if (ATIsProcessRunningUnderARMEmulation()) {
		ATUIGenericDialogOptions opts;
		opts.mhParent = ATUIGetNewPopupOwner();
		opts.mpTitle = L"Native ARM64 version available";
		opts.mpMessage = L"This version of the program is running in emulation on this computer. A native ARM64 version is available that would run faster.\n\n"
			L"Would you like to visit the download page?"
			;
		opts.mpIgnoreTag = "ARM64Emu";
		opts.mIconType = kATUIGenericIconType_Info;
		opts.mAspectLimit = 4.0f;
		opts.mResultMask = kATUIGenericResultMask_OKCancel;
		opts.mValidIgnoreMask = kATUIGenericResultMask_Cancel;
		if (ATUIShowGenericDialogAutoCenter(opts) == kATUIGenericResult_OK) {
			ATLaunchURL(AT_DOWNLOAD_URL);
		}
	}
#endif

	if (g_ATUICommandLineProcessor == this)
		g_ATUICommandLineProcessor = nullptr;

	ATUIPushStep(
		[self = std::shared_ptr<ATUICommandLineProcessor>(this)] {}
	);
}

void ATUICommandLineProcessor::HandleClearDevices() {
	g_sim.GetDeviceManager()->RemoveAllDevices(false);
}

void ATUICommandLineProcessor::HandleAddDevice(const wchar_t *arg) {
	auto& dm = *g_sim.GetDeviceManager();
	VDStringRefW params(arg);
	VDStringRefW tag;

	if (!params.split(L',', tag)) {
		tag = params;
		params.clear();
	}

	// parse parameters, if any
	ATPropertySet pset;

	if (!params.empty())
		pset.ParseFromCommandLineString(params.data());

	// check for a parent parameter
	VDStringW parentTag;
	IATDeviceParent *parent = ResolveDeviceParent(dm, pset, parentTag);

	const VDStringA tagA = VDTextWToA(tag);
	const ATDeviceDefinition *def = dm.GetDeviceDefinition(tagA.c_str());

	// don't allow adding hidden devices
	if (!def || (def->mFlags & kATDeviceDefFlag_Hidden))
		throw VDException(L"Unknown device type: %.*ls", (int)tag.size(), tag.data());

	// check if the device is internal
	if (def->mFlags & kATDeviceDefFlag_Internal) {
		// internal device -- reconfigure it
		IATDevice *dev = dm.GetDeviceByTag(tagA.c_str());

		if (!dev)
			throw VDException(L"Missing internal device: %.*ls", (int)tag.size(), tag.data());

		dm.ReconfigureDevice(*dev, pset);
	} else {
		// external device -- add it
		AddDevice(dm, *def, pset, parent, parentTag);
	}
}

void ATUICommandLineProcessor::HandleSetDevice(const wchar_t *arg) {
	auto& dm = *g_sim.GetDeviceManager();
	VDStringRefW params(arg);
	VDStringRefW tag;

	if (!params.split(L',', tag)) {
		tag = params;
		params.clear();
	}

	// parse parameters, if any
	ATPropertySet pset;

	if (!params.empty())
		pset.ParseFromCommandLineString(params.data());

	// check for a parent parameter
	VDStringW parentTag;
	IATDeviceParent *parent = ResolveDeviceParent(dm, pset, parentTag);

	const VDStringA tagA = VDTextWToA(tag);
	const ATDeviceDefinition *def = dm.GetDeviceDefinition(tagA.c_str());

	// don't allow adding hidden devices
	if (!def || (def->mFlags & kATDeviceDefFlag_Hidden))
		throw VDException(L"Unknown device type: %.*ls", (int)tag.size(), tag.data());

	// look for the device
	IATDevice *dev = nullptr;
	uint32 index = 0;

	for(;;) {
		dev = dm.GetDeviceByTag(tagA.c_str(), index, true);

		if (!dev)
			break;

		if (!parent)
			break;

		// we have a candidate device and a desired parent, check for a match
		if (dev->GetParent() == parent)
			break;

		// mismatched parent, next
		++index;
	}

	// check if the device is internal
	if (def->mFlags & kATDeviceDefFlag_Internal) {
		// internal device -- reconfigure it

		if (!dev)
			throw VDException(L"Missing internal device: %.*ls", (int)tag.size(), tag.data());

		dm.ReconfigureDevice(*dev, pset);
	} else {
		// external device -- add it if it doesn't already exist
		if (dev)
			dm.ReconfigureDevice(*dev, pset);
		else
			AddDevice(dm, *def, pset, parent, parentTag);

	}
}

void ATUICommandLineProcessor::AddDevice(ATDeviceManager& dm, const ATDeviceDefinition& def, const ATPropertySet& pset, IATDeviceParent *parent, const VDStringW& parentTag) {
	IATDevice *dev = dm.AddDevice(&def, pset, parent != nullptr);

	// try to attach to parent device
	if (parent) {
		try {
			uint32 busIdx = 0;

			for(;;) {
				auto busId = parent->GetDeviceBusIdByIndex(busIdx);
				if (busId < 0) {
					throw VDException(L"Device \"%hs\" cannot attach to any bus on parent device \"%ls\"."
						, def.mpTag
						, parentTag.c_str()
					);
				}

				IATDeviceBus *bus = parent->GetDeviceBusById(busId);
				if (bus) {
					bus->AddChildDevice(dev);

					if (dev->GetParent())
						break;
				}

				++busIdx;
			}
		} catch(...) {
			dm.RemoveDevice(dev);
			throw;
		}
	}
}

IATDeviceParent *ATUICommandLineProcessor::ResolveDeviceParent(ATDeviceManager& dm, ATPropertySet& pset, VDStringW& parentTag) {
	IATDeviceParent *parent = nullptr;

	parentTag = pset.GetString("parent", L"");

	pset.Unset("parent");

	if (!parentTag.empty()) {
		// try to find parent device
		IATDevice *parentDevice = dm.GetDeviceByTag(VDTextWToA(parentTag).c_str());

		if (!parentDevice)
			throw VDException(L"Can't find parent device: %ls", parentTag.c_str());

		parent = vdpoly_cast<IATDeviceParent *>(parentDevice);
		if (!parent || parent->GetDeviceBusIdByIndex(0) < 0)
			throw VDException(L"Device '%ls' does not have a bus for child devices.", parentTag.c_str());
	}

	return parent;
}

void ATUICommandLineProcessor::HandleRemoveDevice(const wchar_t *arg) {
	auto& dm = *g_sim.GetDeviceManager();

	IATDevice *dev = dm.GetDeviceByTag(VDTextWToA(arg).c_str(), 0, true, true);
	if (dev) {
		ATDeviceInfo devInfo;
		dev->GetDeviceInfo(devInfo);

		if (!(devInfo.mpDef->mFlags & kATDeviceDefFlag_Internal))
			dm.RemoveDevice(dev);
	}
}

void ATUICommandLineProcessor::HandleCmd(const wchar_t *arg) {
	ExecCommand(arg, {});
}

void ATUICommandLineProcessor::HandleCmd1(const wchar_t *arg1, const wchar_t *arg2) {
	ExecCommand(arg1, {arg2});
}

void ATUICommandLineProcessor::ExecCommand(const wchar_t *cmdName, std::initializer_list<const wchar_t *> args) {
	FlushColdReset();

	ATUICommandManager& cmdMgr = ATUIGetCommandManager();

	VDStringA s = VDTextWToU8(VDStringSpanW(cmdName));
	ATUICommandOptions opts;
	bool noErrors = false;

	while(!s.empty()) {
		const auto ch = s.back();
		if (ch == '!') {
			s.pop_back();

			opts.mbQuiet = true;
		} else if (ch == '?') {
			s.pop_back();

			noErrors = true;
		} else
			break;
	}

	size_t n = args.size();
	opts.mInArgs.resize(n);
	for(size_t i = 0; i < n; ++i)
		opts.mInArgs[i] = VDStringSpanW(args.begin()[i]);

	const ATUICommand *cmd = cmdMgr.GetCommand(s.c_str());
	if (!cmd)
		throw VDException(L"Unknown command: %ls", cmdName);

	try {
		mbCommandPending = true;

		opts.mpOnCommandExecuted = [](const ATUICommand *cmd, const auto&...) {
			if (g_ATUICommandLineProcessor)
				g_ATUICommandLineProcessor->OnCommandCompleted(cmd != nullptr);
		};

		if (!cmdMgr.ExecuteCommand(*cmd, opts)) {
			if (!opts.mbQuiet && !noErrors)
				throw VDException(L"The command %hs is not currently available.", s.c_str());
		}
	} catch(const VDException&) {
		mbCommandPending = false;

		ATUIPushStep(
			[] {
				if (g_ATUICommandLineProcessor)
					g_ATUICommandLineProcessor->OnCommandCompleted(false);
			}
		);

		if (!noErrors)
			throw;
	}
}

void ATUICommandLineProcessor::OnCommandCompleted(bool succeeded) {
	if (succeeded) {
		for(;;) {
			if (mbException || !ProcessNextSequencedSwitch())
				break;

			if (mbCommandPending)
				return;
		}
	}

	PostSequenceCleanup();
}

void ATUICommandLineProcessor::FlushColdReset() {
	if (mbColdResetPending)
		g_sim.ColdReset();
}

void ATUICommandLineProcessor::UnloadAllIfFirstImage() {
	if (!mbHaveUnloadedAllImages) {
		mbHaveUnloadedAllImages = true;
		g_sim.UnloadAll();
	}
}

////////////////////////////////////////////////////////////////////////////////

void ATUICancelProcessCommandLine() {
	if (g_ATUICommandLineProcessor) {
		delete g_ATUICommandLineProcessor;
		g_ATUICommandLineProcessor = nullptr;
	}
}

void ATUIProcessCommandLine(VDCommandLine&& cmdLine) {
	ATUICancelProcessCommandLine();

	g_ATUICommandLineProcessor = new ATUICommandLineProcessor(std::move(cmdLine));
	g_ATUICommandLineProcessor->Run();
}
