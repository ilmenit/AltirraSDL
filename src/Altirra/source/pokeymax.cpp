//	Altirra - Atari 800/800XL/5200 emulator
//	Copyright (C) 2008-2012 Avery Lee
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
//	You should have received a copy of the GNU General Public License
//	along with this program; if not, write to the Free Software
//	Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.

//=========================================================================
// PokeyMax
//
// PokeyMax is an independent device that bundles several sound cores behind
// the POKEY address page. It contains its own Covox-compatible 8-bit DAC
// (ATCovoxEmulator), two YM2149 / AY-3-8910-compatible PSG chips
// (ATYMPSGEmulator) and exposes detection/configuration registers so that
// software can detect the board and query/select its features. It is a
// separate device from the standalone Covox device (covox.cpp), which is
// left untouched.
//
// $D2xx map (full / non-reduced PSG layout -- the cores do NOT overlap):
//
//   $D240-$D25F   SID 1 (MOS 6581/8580 registers $00-$18, direct-mapped;
//                 $19-$1F reserved)            internal mSID[0]
//   $D260-$D27F   SID 2                        internal mSID[1]
//   $D280-$D283   COVOX manual DAC registers (internal mCovox, 4ch LRRL)
//   $D284-$D293   SAMPLE player registers     internal mSample (Phase A:
//                 8-bit signed PCM block player, 4 channels, 42 KiB RAM)
//   $D294-$D29F   SAMPLE player (reserved)    (not yet emulated)
//   $D2A0-$D2AF   PSG 1 (registers R0-R15, direct-mapped)
//   $D2B0-$D2BF   PSG 2
//   $D2C0-$D2FF   reserved
//
// Detection / configuration registers (in the POKEY page):
//
//   $D20C / $D21C  CFG/ID: read returns $01 (PokeyMax present); writing
//                  $3F enables config mode, $00 disables it.
//   $D211          CAPABILITY (config mode only): feature bits and the
//                  POKEY channel count -- always reports the present/config
//                  flags, regardless of RESTRICT. See GetCapability().
//   $D214          VERSION (config mode only): write selects the index
//                  (0-7), read returns that character of the core string.
//   $D215          PSGMODE (config mode only): master clock / stereo /
//                  envelope step count / volume curve for both PSG chips.
//   $D216          SIDMODE (config mode only): per-SID chip model + digifix
//                  (default $11). bit0 SID1 type (0=8580,1=6581), bit1 SID1
//                  digifix, bit4 SID2 type, bit5 SID2 digifix.
//   $D217          RESTRICT (config mode only): a raw runtime register
//                  (default $1F) enabling/disabling cores at runtime.
//
// Two independent state axes:
//   * Present / capability flags (mbCovoxPresent / mbPSGPresent /
//     mbSIDPresent / mPokeyCapability), driven by the config dialog and
//     reported by $D211. These never change at runtime.
//   * Runtime RESTRICT (mRestrictReg, default $1F), driven by $D217. A core
//     is active for decode/audio iff present AND its restrict bit is set:
//       PSG   active = mbPSGPresent   && (mRestrictReg & 0x08)
//       SID   active = mbSIDPresent   && (mRestrictReg & 0x04)
//       COVOX active = mbCovoxPresent && (mRestrictReg & 0x10)
//
// The PokeyMax SID clock is derived from the live Atari PHI2 machine clock
// (NOT the fixed C64 ~0.985 MHz): PAL = PHI2 x 5/9, NTSC = PHI2 x 4/7. The
// TV standard is taken from the scheduler clock rate (the authoritative
// PHI2), so the SID pitch tracks the real machine on both standards.
//=========================================================================

#include <stdafx.h>
#include <vd2/system/binary.h>
#include <at/atcore/audiomixer.h>
#include <at/atcore/consoleoutput.h>
#include <at/atcore/deviceimpl.h>
#include <at/atcore/devicesnapshot.h>
#include <at/atcore/devicesystemcontrol.h>
#include <at/atcore/propertyset.h>
#include <at/atcore/savestate.h>
#include <at/atcore/scheduler.h>
#include <at/atcore/snapshotimpl.h>
#include "covox.h"
#include "ympsg.h"
#include "pokeymaxsid.h"
#include "pokeymaxsample.h"
#include "memorymanager.h"
#include "irqcontroller.h"

// 8-character PokeyMax "core" identification string reported via the $D214
// version register. Detection tools print this verbatim.
static const char g_ATPokeyMaxCoreName[8] = { 'A','l','t','i','r','r','a','1' };

// Composite PokeyMax snapshot. It nests the internal Covox DAC's own state
// object and serializes each PSG's raw generator blob plus the config/runtime
// registers, so a save-state round-trips the whole device.
class ATSaveStatePokeyMax final : public ATSnapExchangeObject<ATSaveStatePokeyMax, "ATSaveStatePokeyMax"> {
public:
	template<ATExchanger T>
	void Exchange(T& ex);

	vdrefptr<IATObjectState> mpCovoxState;
	vdfastvector<uint8> mPSG0;
	vdfastvector<uint8> mPSG1;
	vdfastvector<uint8> mSID0;
	vdfastvector<uint8> mSID1;
	vdfastvector<uint8> mSample;
	uint8 mRestrictReg = 0x1F;
	uint8 mPSGModeReg = 0;
	uint8 mSIDModeReg = 0x11;
	uint8 mModeReg = 0x31;
	uint8 mPokeyCapability = 0;
	uint8 mbConfigMode = 0;
	uint8 mVersionIndex = 0;
};

template<ATExchanger T>
void ATSaveStatePokeyMax::Exchange(T& ex) {
	ex.Transfer("covox", &mpCovoxState);
	ex.Transfer("psg0", &mPSG0);
	ex.Transfer("psg1", &mPSG1);
	ex.Transfer("sid0", &mSID0);
	ex.Transfer("sid1", &mSID1);
	ex.Transfer("sample", &mSample);
	ex.Transfer("restrict", &mRestrictReg);
	ex.Transfer("psgmode", &mPSGModeReg);
	ex.Transfer("sidmode", &mSIDModeReg);
	ex.Transfer("mode", &mModeReg);
	ex.Transfer("pokey_capability", &mPokeyCapability);
	ex.Transfer("config_mode", &mbConfigMode);
	ex.Transfer("version_index", &mVersionIndex);
}

class ATDevicePokeyMax final : public VDAlignedObject<16>
					, public ATDevice
					, public IATDeviceMemMap
					, public IATDeviceScheduling
					, public IATDeviceAudioOutput
					, public IATDeviceDiagnostics
					, public IATDeviceCovoxControl
					, public IATDevicePokeyChannelControl
					, public IATDeviceSnapshot
{
public:
	virtual void *AsInterface(uint32 id) override;

	virtual void GetDeviceInfo(ATDeviceInfo& info) override;
	virtual void WarmReset() override;
	virtual void ColdReset() override;
	virtual void GetSettingsBlurb(VDStringW& buf) override;
	virtual void GetSettings(ATPropertySet& settings) override;
	virtual bool SetSettings(const ATPropertySet& settings) override;
	virtual void Init() override;
	virtual void Shutdown() override;

public: // IATDeviceMemMap
	virtual void InitMemMap(ATMemoryManager *memmap) override;
	virtual bool GetMappedRange(uint32 index, uint32& lo, uint32& hi) const override;

public:	// IATDeviceScheduling
	virtual void InitScheduling(ATScheduler *sch, ATScheduler *slowsch) override;

public:	// IATDeviceAudioOutput
	virtual void InitAudioOutput(IATAudioMixer *mixer) override;

public:	// IATDeviceDiagnostics
	virtual void DumpStatus(ATConsoleOutput& output) override;

public:	// IATDeviceCovoxControl
	virtual void InitCovoxControl(IATCovoxController& controller) override;

public:	// IATDevicePokeyChannelControl
	virtual void InitPokeyChannelControl(IATPokeyChannelController& controller) override;

public:	// IATDeviceSnapshot
	vdrefptr<IATObjectState> SaveState(ATSnapshotContext& ctx) const override;
	void LoadState(const IATObjectState *state, ATSnapshotContext& ctx) override;

private:
	void OnCovoxEnabled(bool enabled);

	uint8 GetCapability() const;

	static sint32 StaticConfigRead(void *thisptr, uint32 addr);
	static sint32 StaticConfigDebugRead(void *thisptr, uint32 addr);
	static bool StaticConfigWrite(void *thisptr, uint32 addr, uint8 value);

	ATMemoryManager *mpMemMan = nullptr;
	ATScheduler *mpScheduler = nullptr;
	IATAudioMixer *mpAudioMixer = nullptr;

	// Shared 6502 IRQ controller + the IRQ bit the SAMPLE engine asserts on
	// end-of-sample (SAMIRQACT & SAMIRQEN). Allocated in Init, freed in
	// Shutdown.
	ATIRQController *mpIrqController = nullptr;
	uint32 mSampleIrqBit = 0;

	// PokeyMax claims the whole Covox-compatible block at $D280-$D2FF for the
	// detection/config overlay; the manual COVOX DAC itself only decodes the
	// true four-register window $D280-$D283.
	static constexpr uint32 kAddrLo = 0xD280;
	static constexpr uint32 kAddrHi = 0xD2FF;
	static constexpr uint32 kCovoxLo = 0xD280;
	static constexpr uint32 kCovoxHi = 0xD283;

	IATCovoxController *mpCovoxController = nullptr;
	vdfunction<void(bool)> mCovoxCallback;
	bool mbCovoxCtrlEnabled = true;

	// Bridge to the core POKEY channel-count controller. When present, the
	// effective Mono/Stereo/Quad mode (capability gated by the $D217 RESTRICT
	// POKEY field) drives the core's real second/third/fourth POKEYs.
	IATPokeyChannelController *mpPokeyChanControl = nullptr;

	// Present / capability flags (driven by the device config dialog, reported
	// by $D211 CAPABILITY -- never change at runtime):
	//   mPokeyCapability  POKEY channel count: 0=Mono, 1=Stereo, 2=Quad
	//                     (internal value; $D211 maps quad 2 -> 3)
	//   mbCovoxPresent    Covox-compatible DAC present
	//   mbPSGPresent      PSG present
	//   mbSIDPresent      SID present
	uint32 mPokeyCapability = 0;
	bool mbCovoxPresent = true;
	bool mbPSGPresent = true;
	bool mbSIDPresent = true;
	bool mbSamplePresent = true;

	// Runtime RESTRICT register ($D217). Raw register, default $1F. NOT
	// derived from the present flags. Bits: 0-1 POKEY mode, bit2 SID, bit3
	// PSG, bit4 SAMPLE/COVOX. A core is active iff present AND restrict bit.
	uint8 mRestrictReg = 0x1F;

	// PSGMODE register ($D215): master clock / stereo / envelope / volume
	// curve. Applied to both PSG chips via SetMode().
	uint8 mPSGModeReg = 0;

	// SIDMODE register ($D216): per-SID chip model + digifix. Default $11 =
	// both SIDs 6581, digifix off. Applied to both SID chips via SetModel().
	//   bit0 SID1 type (0=8580, 1=6581)  bit1 SID1 digifix
	//   bit4 SID2 type (0=8580, 1=6581)  bit5 SID2 digifix
	uint8 mSIDModeReg = 0x11;

	// SID waveform/noise engine selector (Altirra extension; not real PokeyMax
	// hardware). true = "MAME" engine (measured combined-waveform ROM tables +
	// correct 24-bit noise LFSR), false = "SlightSID" engine (logical-AND
	// combined waveforms + approximate noise). Applies to both SID chips.
	bool mbSIDAccurateEngine = true;

	// MODE register ($D210, config bank). Hardware power-on default is $31
	// (49): bit0 Saturate=1 (POKEY saturation curve), bit4 MonoDet=1, bit5
	// PAL=1. bit0 drives the quad same-side mix curve via the channel-mode
	// bridge. The SID clock tracks the emulator's machine clock (authoritative
	// -- see RefreshSIDClocks), so this byte does NOT drive the clock; it keeps
	// the config-bank register coherent so it never falls through to the POKEY
	// mirror.
	uint8 mModeReg = 0x31;

	// Detection/config register state.
	ATMemoryLayer *mpMemLayerConfig = nullptr;
	bool mbConfigMode = false;
	uint8 mVersionIndex = 0;

	ATCovoxEmulator mCovox;
	ATYMPSGEmulator mPSG[2];
	ATPokeyMaxSIDEmulator mSID[2];
	ATPokeyMaxSampleEmulator mSample;

	bool IsPSGActive() const { return mbPSGPresent && (mRestrictReg & 0x08) != 0; }
	bool IsCovoxActive() const { return mbCovoxPresent && (mRestrictReg & 0x10) != 0; }
	bool IsSIDActive() const { return mbSIDPresent && (mRestrictReg & 0x04) != 0; }

	// The SAMPLE engine shares the COVOX RESTRICT bit ($D217 bit4): both live
	// in the same $D280 physical block.
	bool IsSampleActive() const { return mbSamplePresent && (mRestrictReg & 0x10) != 0; }
	void ApplyRuntimeEnables();

	// Push the effective POKEY channel mode to the core (PokeyMax quad). The
	// effective mode is mPokeyCapability gated by the $D217 RESTRICT POKEY
	// field (bits 1-0): the field is a MAX, and its reset/default value 3
	// (from $1F) means unrestricted. Calls SetExternalPokeyChannelMode when a
	// controller is installed, else does nothing.
	void ApplyPokeyChannelMode();

	// Apply mSIDModeReg to both SID chips (model + digifix selection).
	void ApplySIDModel();

	// Apply mbSIDAccurateEngine to both SID chips (MAME vs SlightSID engine).
	void ApplySIDEngine();

	// Re-derive the SID PHI2 step ratio (PAL 5/9, NTSC 4/7) from the live
	// scheduler clock rate and push it to both SID chips.
	void RefreshSIDClocks();
};

void ATCreateDevicePokeyMax(const ATPropertySet& pset, IATDevice **dev) {
	vdrefptr<ATDevicePokeyMax> p(new ATDevicePokeyMax);

	*dev = p.release();
}

extern const ATDeviceDefinition g_ATDeviceDefPokeyMax = { "pokeymax", "pokeymax", L"PokeyMax", ATCreateDevicePokeyMax };

void *ATDevicePokeyMax::AsInterface(uint32 id) {
	switch(id) {
		case IATDeviceMemMap::kTypeID:
			return static_cast<IATDeviceMemMap *>(this);

		case IATDeviceScheduling::kTypeID:
			return static_cast<IATDeviceScheduling *>(this);

		case IATDeviceAudioOutput::kTypeID:
			return static_cast<IATDeviceAudioOutput *>(this);

		case IATDeviceDiagnostics::kTypeID:
			return static_cast<IATDeviceDiagnostics *>(this);

		case IATDeviceCovoxControl::kTypeID:
			return static_cast<IATDeviceCovoxControl *>(this);

		case IATDevicePokeyChannelControl::kTypeID:
			return static_cast<IATDevicePokeyChannelControl *>(this);

		case IATDeviceSnapshot::kTypeID:
			return static_cast<IATDeviceSnapshot *>(this);

		default:
			return ATDevice::AsInterface(id);
	}
}

void ATDevicePokeyMax::GetDeviceInfo(ATDeviceInfo& info) {
	info.mpDef = &g_ATDeviceDefPokeyMax;
}

void ATDevicePokeyMax::WarmReset() {
	mCovox.WarmReset();
	mPSG[0].WarmReset();
	mPSG[1].WarmReset();
	mSID[0].WarmReset();
	mSID[1].WarmReset();
	mSample.WarmReset();

	// PokeyMax leaves config mode after a reset.
	mbConfigMode = false;
	mVersionIndex = 0;

	// Capability/RESTRICT are unchanged by a warm reset, but re-assert the
	// core channel mode for consistency (idempotent in the core).
	ApplyPokeyChannelMode();
}

void ATDevicePokeyMax::ColdReset() {
	mCovox.ColdReset();
	mPSG[0].ColdReset();
	mPSG[1].ColdReset();
	mSID[0].ColdReset();
	mSID[1].ColdReset();
	mSample.ColdReset();

	// PokeyMax leaves config mode after a reset.
	mbConfigMode = false;
	mVersionIndex = 0;

	// RESTRICT resets to its hardware default ($1F = all cores enabled, POKEY
	// mode bits set). The active set is still gated by the present flags.
	mRestrictReg = 0x1F;
	mPSGModeReg = 0;

	// SIDMODE resets to $11 (both SIDs 6581, digifix off).
	mSIDModeReg = 0x11;

	// MODE config register resets to its hardware default $31 (Saturate on,
	// MonoDet, PAL).
	mModeReg = 0x31;

	mPSG[0].SetMode(mPSGModeReg);
	mPSG[1].SetMode(mPSGModeReg);

	// Re-derive the SID PHI2 clock ratio and re-apply the SID model selection
	// before gating the runtime enables.
	RefreshSIDClocks();
	ApplySIDModel();
	ApplySIDEngine();

	ApplyRuntimeEnables();
}

void ATDevicePokeyMax::ApplyRuntimeEnables() {
	const bool psgActive = IsPSGActive();

	mPSG[0].SetEnabled(psgActive);
	mPSG[1].SetEnabled(psgActive);

	const bool sidActive = IsSIDActive();

	mSID[0].SetEnabled(sidActive);
	mSID[1].SetEnabled(sidActive);

	// The internal Covox DAC is additionally gated by the global Covox audio
	// master switch (IATDeviceCovoxControl).
	mCovox.SetEnabled(mbCovoxCtrlEnabled && IsCovoxActive());

	// The SAMPLE engine shares the COVOX RESTRICT bit but has its own present
	// flag and is not gated by the Covox master switch.
	mSample.SetEnabled(IsSampleActive());

	// Keep the core POKEY channel count in sync with the capability + RESTRICT
	// state. This is gated on the bridge controller being present, so it is a
	// safe no-op until InitPokeyChannelControl has wired us up.
	ApplyPokeyChannelMode();
}

void ATDevicePokeyMax::ApplyPokeyChannelMode() {
	if (!mpPokeyChanControl)
		return;

	// RESTRICT POKEY field (bits 1-0) is a MAX; its reset/default value 3
	// (from $1F) means unrestricted, NOT mono. Only clamp when the field is
	// actually set below the capability.
	const uint32 restrictMode = mRestrictReg & 0x03;
	const uint32 effective = (restrictMode >= 3)
		? mPokeyCapability
		: std::min<uint32>(mPokeyCapability, restrictMode);

	mpPokeyChanControl->SetExternalPokeyChannelMode(this, effective);

	// $D210 MODE bit0 (Saturate) selects the same-side quad mix curve.
	mpPokeyChanControl->SetExternalPokeySaturation(this, (mModeReg & 0x01) != 0);
}

void ATDevicePokeyMax::ApplySIDModel() {
	// SIDMODE bit0/bit4: 0 = 8580, 1 = 6581. bit1/bit5: digifix.
	mSID[0].SetModel((mSIDModeReg & 0x01) != 0, (mSIDModeReg & 0x02) != 0);
	mSID[1].SetModel((mSIDModeReg & 0x10) != 0, (mSIDModeReg & 0x20) != 0);
}

void ATDevicePokeyMax::ApplySIDEngine() {
	mSID[0].SetEngineMode(mbSIDAccurateEngine);
	mSID[1].SetEngineMode(mbSIDAccurateEngine);
}

void ATDevicePokeyMax::RefreshSIDClocks() {
	// The PokeyMax SID clock tracks the live Atari PHI2 machine clock
	// (PAL = PHI2 x 5/9, NTSC = PHI2 x 4/7). The scheduler clock rate is the
	// authoritative PHI2, so the TV standard is derived from it: PAL PHI2 is
	// ~1.773 MHz and NTSC PHI2 is ~1.790 MHz.
	const double machineClockHz = mpScheduler ? mpScheduler->GetRate().asDouble() : 1789772.5;
	const bool palMode = machineClockHz < 1780000.0;

	mSID[0].SetClock(machineClockHz, palMode);
	mSID[1].SetClock(machineClockHz, palMode);
}

void ATDevicePokeyMax::GetSettingsBlurb(VDStringW& buf) {
	buf.sprintf(L"$%04X-%04X", kAddrLo, kAddrHi);
}

void ATDevicePokeyMax::GetSettings(ATPropertySet& settings) {
	settings.SetUint32("base", kAddrLo);
	settings.SetUint32("size", kAddrHi - kAddrLo + 1);
	settings.SetUint32("channels", 4);

	settings.SetUint32("pokeys", mPokeyCapability);
	settings.SetBool("covoxenabled", mbCovoxPresent);
	settings.SetBool("psgenabled", mbPSGPresent);
	settings.SetBool("sidenabled", mbSIDPresent);
	settings.SetBool("sampleenabled", mbSamplePresent);

	// SID engine (Altirra extension): true = MAME, false = SlightSID.
	settings.SetBool("sidaccurate", mbSIDAccurateEngine);
}

bool ATDevicePokeyMax::SetSettings(const ATPropertySet& settings) {
	// The config dialog only drives the present/capability flags. It does NOT
	// touch the runtime RESTRICT register (mRestrictReg), which is owned by
	// the $D217 register.
	uint32 pokeys = settings.GetUint32("pokeys", mPokeyCapability);
	mPokeyCapability = (pokeys < 3) ? pokeys : 0;

	mbCovoxPresent = settings.GetBool("covoxenabled", mbCovoxPresent);
	mbPSGPresent = settings.GetBool("psgenabled", mbPSGPresent);
	mbSIDPresent = settings.GetBool("sidenabled", mbSIDPresent);
	mbSamplePresent = settings.GetBool("sampleenabled", mbSamplePresent);
	mbSIDAccurateEngine = settings.GetBool("sidaccurate", mbSIDAccurateEngine);

	// The manual COVOX DAC decodes only its four-register window
	// ($D280-$D283); the wider config overlay handles detection and the PSG
	// windows.
	mCovox.SetAddressRange(kCovoxLo, kCovoxHi, false);
	mCovox.SetFourChannels(true);

	// A core that was just un-configured must go silent immediately, using the
	// current RESTRICT register.
	ApplyRuntimeEnables();
	ApplySIDEngine();

	return true;
}

void ATDevicePokeyMax::Init() {
	// The manual COVOX DAC decodes only its four-register window
	// ($D280-$D283). $D284-$D29F (SAMPLE) and the PSG windows are handled by
	// the config overlay below.
	mCovox.SetAddressRange(kCovoxLo, kCovoxHi, false);
	mCovox.SetFourChannels(true);
	mCovox.Init(mpMemMan, mpScheduler, mpAudioMixer);

	// Two YM2149 / AY-3-8910 PSG chips. Drive them with the live PHI2 machine
	// clock so pitch is correct on both NTSC and PAL.
	const double machineClockHz = mpScheduler ? mpScheduler->GetRate().asDouble() : 1789772.5;

	mPSG[0].Init(mpScheduler, mpAudioMixer, 0, machineClockHz);
	mPSG[1].Init(mpScheduler, mpAudioMixer, 1, machineClockHz);

	mPSG[0].SetMode(mPSGModeReg);
	mPSG[1].SetMode(mPSGModeReg);

	// Two MOS 6581/8580-compatible SID chips. SID1 -> left, SID2 -> right.
	// They do NOT install their own memory layer -- PokeyMax owns the $D2
	// overlay and forwards register accesses via WriteControl/ReadControl.
	mSID[0].Init(mpScheduler, mpAudioMixer, 0);
	mSID[1].Init(mpScheduler, mpAudioMixer, 1);

	// SAMPLE engine (4-channel block player: 8-bit / 4-bit signed / 4-bit
	// IMA-ADPCM). Like the SID cores it does NOT install its own memory
	// layer -- PokeyMax owns the $D2 overlay and forwards $D284-$D293 via
	// WriteControl/ReadControl. It registers itself as a stereo sync-audio
	// source and asserts a real 6502 IRQ on end-of-sample via a dedicated
	// IRQ bit from the shared controller.
	mpIrqController = GetService<ATIRQController>();
	if (mpIrqController)
		mSampleIrqBit = mpIrqController->AllocateIRQ();

	mSample.Init(mpScheduler, mpAudioMixer, mpIrqController, mSampleIrqBit);

	// Derive the SID PHI2 clock ratio and apply the configured SID model
	// before gating the runtime enables.
	RefreshSIDClocks();
	ApplySIDModel();
	ApplySIDEngine();

	ApplyRuntimeEnables();

	// Overlay the detection/config registers within the POKEY page
	// ($D200-$D2FF), above the POKEY hardware. Reads of the CFG/ID,
	// capability and version registers report the board; all other accesses
	// pass through to POKEY unchanged.
	if (mpMemMan && !mpMemLayerConfig) {
		ATMemoryHandlerTable handlers = {};
		handlers.mpThis = this;
		handlers.mbPassAnticReads = true;
		handlers.mbPassReads = true;
		handlers.mbPassWrites = true;
		handlers.mpDebugReadHandler = StaticConfigDebugRead;
		handlers.mpReadHandler = StaticConfigRead;
		handlers.mpWriteHandler = StaticConfigWrite;

		mpMemLayerConfig = mpMemMan->CreateLayer(kATMemoryPri_HardwareOverlay, handlers, 0xD2, 1);
		mpMemMan->SetLayerName(mpMemLayerConfig, "PokeyMax config");
		mpMemMan->EnableLayer(mpMemLayerConfig, true);
	}
}

void ATDevicePokeyMax::Shutdown() {
	if (mpMemLayerConfig) {
		mpMemMan->DeleteLayer(mpMemLayerConfig);
		mpMemLayerConfig = nullptr;
	}

	if (mpCovoxController) {
		mpCovoxController->GetCovoxEnableNotifyList().Remove(&mCovoxCallback);
		mpCovoxController = nullptr;
	}

	// Release the core POKEY channel override so detaching PokeyMax restores
	// the user's "Dual POKEYs" preference. Owner-token matched.
	if (mpPokeyChanControl) {
		mpPokeyChanControl->ClearExternalPokeyChannelMode(this);
		mpPokeyChanControl = nullptr;
	}

	// Shutdown drops the engine's hold on the IRQ line; now release the bit.
	mSample.Shutdown();

	if (mpIrqController) {
		mpIrqController->FreeIRQ(mSampleIrqBit);
		mpIrqController = nullptr;
		mSampleIrqBit = 0;
	}

	mSID[0].Shutdown();
	mSID[1].Shutdown();

	mPSG[0].Shutdown();
	mPSG[1].Shutdown();

	mCovox.Shutdown();

	mpAudioMixer = nullptr;
	mpScheduler = nullptr;
	mpMemMan = nullptr;
}

void ATDevicePokeyMax::InitMemMap(ATMemoryManager *memmap) {
	mpMemMan = memmap;
}

bool ATDevicePokeyMax::GetMappedRange(uint32 index, uint32& lo, uint32& hi) const {
	if (index == 0) {
		// Manual COVOX DAC window.
		lo = kCovoxLo;
		hi = kCovoxHi + 1;
		return true;
	}

	if (index == 1) {
		// PSG 1 + PSG 2 register windows ($D2A0-$D2BF).
		lo = 0xD2A0;
		hi = 0xD2BF + 1;
		return true;
	}

	if (index == 2) {
		// SID 1 + SID 2 register windows ($D240-$D27F).
		lo = 0xD240;
		hi = 0xD27F + 1;
		return true;
	}

	return false;
}

void ATDevicePokeyMax::InitScheduling(ATScheduler *sch, ATScheduler *slowsch) {
	mpScheduler = sch;
}

void ATDevicePokeyMax::InitAudioOutput(IATAudioMixer *mixer) {
	mpAudioMixer = mixer;
}

void ATDevicePokeyMax::DumpStatus(ATConsoleOutput& output) {
	mCovox.DumpStatus(output);

	output("");
	output("RESTRICT ($D217) = $%02X   PSGMODE ($D215) = $%02X   SIDMODE ($D216) = $%02X   MODE ($D210) = $%02X", mRestrictReg, mPSGModeReg, mSIDModeReg, mModeReg);
	output("SID active = %s   (present=%s, RESTRICT bit2=%s)", IsSIDActive() ? "yes" : "no", mbSIDPresent ? "yes" : "no", (mRestrictReg & 0x04) ? "set" : "clear");

	for(int i = 0; i < 2; ++i) {
		output("");
		output("PSG %d (%s):", i + 1, IsPSGActive() ? "active" : "inactive");
		mPSG[i].DumpStatus(output);
	}

	for(int i = 0; i < 2; ++i) {
		output("");
		output("SID %d (%s):", i + 1, IsSIDActive() ? "active" : "inactive");
		mSID[i].DumpStatus(output);
	}

	output("");
	output("SAMPLE (%s, present=%s, RESTRICT bit4=%s):",
		IsSampleActive() ? "active" : "inactive",
		mbSamplePresent ? "yes" : "no",
		(mRestrictReg & 0x10) ? "set" : "clear");
	mSample.DumpStatus(output);
}

void ATDevicePokeyMax::InitCovoxControl(IATCovoxController& controller) {
	mpCovoxController = &controller;
	mpCovoxController->GetCovoxEnableNotifyList().Add(&mCovoxCallback);

	OnCovoxEnabled(mpCovoxController->IsCovoxEnabled());
	mCovoxCallback = [this](bool enable) { OnCovoxEnabled(enable); };
}

void ATDevicePokeyMax::InitPokeyChannelControl(IATPokeyChannelController& controller) {
	mpPokeyChanControl = &controller;

	// Push the current effective mode now that the bridge is wired.
	ApplyPokeyChannelMode();
}

vdrefptr<IATObjectState> ATDevicePokeyMax::SaveState(ATSnapshotContext& ctx) const {
	vdrefptr state { new ATSaveStatePokeyMax };

	state->mpCovoxState = mCovox.SaveState();
	mPSG[0].SaveState(state->mPSG0);
	mPSG[1].SaveState(state->mPSG1);
	mSID[0].SaveState(state->mSID0);
	mSID[1].SaveState(state->mSID1);
	mSample.SaveState(state->mSample);
	state->mRestrictReg = mRestrictReg;
	state->mPSGModeReg = mPSGModeReg;
	state->mSIDModeReg = mSIDModeReg;
	state->mModeReg = mModeReg;
	state->mPokeyCapability = (uint8)mPokeyCapability;
	state->mbConfigMode = mbConfigMode ? 1 : 0;
	state->mVersionIndex = mVersionIndex;

	return state;
}

void ATDevicePokeyMax::LoadState(const IATObjectState *state, ATSnapshotContext& ctx) {
	if (!state) {
		// No saved state: reset to defaults.
		mCovox.LoadState(nullptr);
		mPSG[0].ColdReset();
		mPSG[1].ColdReset();
		mSID[0].ColdReset();
		mSID[1].ColdReset();
		mSample.ColdReset();
		mSIDModeReg = 0x11;
		mModeReg = 0x31;
		RefreshSIDClocks();
		ApplySIDModel();
		ApplySIDEngine();
		return;
	}

	const ATSaveStatePokeyMax& pmState = atser_cast<const ATSaveStatePokeyMax&>(*state);

	mCovox.LoadState(pmState.mpCovoxState);

	mRestrictReg = pmState.mRestrictReg;
	mPSGModeReg = pmState.mPSGModeReg;
	mSIDModeReg = pmState.mSIDModeReg;
	mModeReg = pmState.mModeReg;
	mbConfigMode = pmState.mbConfigMode != 0;
	mVersionIndex = pmState.mVersionIndex & 7;

	// Re-apply PSGMODE before loading the generator blobs so the envelope
	// step mask / clock are correct.
	mPSG[0].SetMode(mPSGModeReg);
	mPSG[1].SetMode(mPSGModeReg);

	mPSG[0].LoadState(pmState.mPSG0.data(), pmState.mPSG0.size());
	mPSG[1].LoadState(pmState.mPSG1.data(), pmState.mPSG1.size());

	// Re-apply the SID clock + model before loading the generator blobs (the
	// blob also carries its own model/clock and will overwrite them, but this
	// keeps the table pointers valid if the blob is short).
	RefreshSIDClocks();
	ApplySIDModel();

	mSID[0].LoadState(pmState.mSID0.data(), pmState.mSID0.size());
	mSID[1].LoadState(pmState.mSID1.data(), pmState.mSID1.size());

	mSample.LoadState(pmState.mSample.data(), pmState.mSample.size());

	// The SID engine is a configuration preference (not a hardware register),
	// so the current config stays authoritative across a snapshot load.
	ApplySIDEngine();

	ApplyRuntimeEnables();
}

void ATDevicePokeyMax::OnCovoxEnabled(bool enabled) {
	mbCovoxCtrlEnabled = enabled;

	mCovox.SetEnabled(mbCovoxCtrlEnabled && IsCovoxActive());
}

uint8 ATDevicePokeyMax::GetCapability() const {
	// Capability/feature byte returned via $D211 in config mode:
	//   bit 6 ($40)  Flash support      (not emulated)
	//   bit 5 ($20)  Sample support     (internal mSample, Phase A 8-bit PCM)
	//   bit 4 ($10)  Covox support
	//   bit 3 ($08)  PSG support
	//   bit 2 ($04)  SID support
	//   bits 1-0     POKEY channels: 0=Mono, 1=Stereo, 3=Quad
	uint8 cap = 0;

	if (mbSamplePresent)
		cap |= 0x20;
	if (mbCovoxPresent)
		cap |= 0x10;
	if (mbPSGPresent)
		cap |= 0x08;
	if (mbSIDPresent)
		cap |= 0x04;

	// Internal mPokeyCapability is {0=Mono,1=Stereo,2=Quad} (drives the core
	// ATPokeyChannelMode enum). The hardware $D211 field encodes quad as 3,
	// so map 2 -> 3 here while leaving the internal value untouched.
	cap |= (mPokeyCapability == 2) ? 0x03 : (uint8)(mPokeyCapability & 3);

	return cap;
}

sint32 ATDevicePokeyMax::StaticConfigRead(void *thisptr0, uint32 addr) {
	auto *self = (ATDevicePokeyMax *)thisptr0;

	// SID register windows (direct-mapped, accessed in normal mode). When SID
	// is active the FULL 32-byte windows ($D240-$D25F and $D260-$D27F) are
	// consumed -- $19-$1F must NOT fall through to the POKEY mirror -- with
	// $1B/$1C returning OSC3/ENV3 and the rest SID-style defaults. When SID is
	// not active the whole window falls through to the POKEY-1 mirror.
	if (addr >= 0xD240 && addr <= 0xD27F) {
		if (self->IsSIDActive()) {
			const int chip = (addr >= 0xD260) ? 1 : 0;

			return self->mSID[chip].ReadControl((uint8)(addr & 0x1F));
		}

		return -1;
	}

	// PSG register windows (direct-mapped, accessed in normal mode). When a
	// PSG is active these reads are consumed (AY register readback); otherwise
	// they fall through to the POKEY-1 mirror.
	if (addr >= 0xD2A0 && addr <= 0xD2BF) {
		if (self->IsPSGActive()) {
			const int chip = (addr >= 0xD2B0) ? 1 : 0;

			return self->mPSG[chip].ReadReg((uint8)(addr & 0x0F));
		}

		return -1;
	}

	// SAMPLE engine register window ($D284-$D293, accessed in normal mode).
	// When SAMPLE is active these reads are consumed (RAM/register readback,
	// RAMDATAINC auto-increments); otherwise they fall through to the POKEY-1
	// mirror underneath. $D280-$D283 stay with the manual COVOX DAC.
	if (addr >= 0xD284 && addr <= 0xD293) {
		if (self->IsSampleActive())
			return self->mSample.ReadControl((uint8)(addr - 0xD280));

		return -1;
	}

	switch(addr) {
		case 0xD20C:	// CFG/ID
		case 0xD21C:	// CFG/ID (alias)
			return 0x01;

		case 0xD211:	// capability
			if (self->mbConfigMode)
				return self->GetCapability();
			break;

		case 0xD214:	// version string
			if (self->mbConfigMode)
				return (uint8)g_ATPokeyMaxCoreName[self->mVersionIndex & 7];
			break;

		case 0xD215:	// PSGMODE
			if (self->mbConfigMode)
				return self->mPSGModeReg;
			break;

		case 0xD216:	// SIDMODE
			if (self->mbConfigMode)
				return self->mSIDModeReg;
			break;

		case 0xD217:	// RESTRICT (raw runtime register)
			if (self->mbConfigMode)
				return self->mRestrictReg;
			break;
	}

	// Config bank: while CFGEN is latched the ENTIRE $D210-$D21F range is the
	// PokeyMax config bank and must be fully consumed -- a config-bank read
	// must NEVER fall through to the POKEY register mirror underneath. The
	// switch above already returned for the implemented config registers; any
	// other address in the bank (e.g. $D210 MODE, $D212/$D213, $D218-$D21B,
	// $D21D/$D21E/$D21F) returns its stored value or a 0 default. When CFGEN
	// is NOT latched, $D210-$D21F is the POKEY mirror and we fall through.
	if (self->mbConfigMode && addr >= 0xD210 && addr <= 0xD21F)
		return (addr == 0xD210) ? self->mModeReg : 0x00;

	return -1;
}

sint32 ATDevicePokeyMax::StaticConfigDebugRead(void *thisptr0, uint32 addr) {
	auto *self = (ATDevicePokeyMax *)thisptr0;

	// The SAMPLE engine's normal read auto-increments the RAM pointer for
	// RAMDATAINC ($D287); a debug read must be side-effect-free, so route it
	// through DebugReadControl. All other registers have no read side effects.
	if (addr >= 0xD284 && addr <= 0xD293) {
		if (self->IsSampleActive())
			return self->mSample.DebugReadControl((uint8)(addr - 0xD280));

		return -1;
	}

	return StaticConfigRead(thisptr0, addr);
}

bool ATDevicePokeyMax::StaticConfigWrite(void *thisptr0, uint32 addr, uint8 value) {
	auto *self = (ATDevicePokeyMax *)thisptr0;

	// SID register windows (direct-mapped, written in normal mode). When SID
	// is active the FULL 32-byte windows ($D240-$D25F and $D260-$D27F) are
	// consumed -- $19-$1F writes are swallowed by WriteControl, NOT passed to
	// the POKEY mirror. When SID is not active the whole window falls through
	// to the POKEY-1 mirror.
	if (addr >= 0xD240 && addr <= 0xD27F) {
		if (self->IsSIDActive()) {
			const int chip = (addr >= 0xD260) ? 1 : 0;

			self->mSID[chip].WriteControl((uint8)(addr & 0x1F), value);
			return true;
		}

		return false;
	}

	// PSG register windows (direct-mapped, written in normal mode). When a PSG
	// is active the write is consumed; otherwise it falls through to the
	// POKEY-1 mirror.
	if (addr >= 0xD2A0 && addr <= 0xD2BF) {
		if (self->IsPSGActive()) {
			const int chip = (addr >= 0xD2B0) ? 1 : 0;

			self->mPSG[chip].WriteReg((uint8)(addr & 0x0F), value);
			return true;
		}

		return false;
	}

	// SAMPLE engine register window ($D284-$D293, written in normal mode).
	// When SAMPLE is active the write is consumed (RAM loader, channel params,
	// SAMDMA/IRQ); otherwise it falls through to the POKEY-1 mirror.
	// $D280-$D283 stay with the manual COVOX DAC.
	if (addr >= 0xD284 && addr <= 0xD293) {
		if (self->IsSampleActive()) {
			self->mSample.WriteControl((uint8)(addr - 0xD280), value);
			return true;
		}

		return false;
	}

	switch(addr) {
		case 0xD20C:	// CFG/ID -> config-mode enable
		case 0xD21C:	// CFG/ID (alias) -> config-mode enable
			self->mbConfigMode = (value == 0x3F);
			return true;

		case 0xD214:	// version index select (config mode only)
			if (self->mbConfigMode) {
				self->mVersionIndex = value & 7;
				return true;
			}
			break;

		case 0xD215:	// PSGMODE (config mode only)
			if (self->mbConfigMode) {
				self->mPSGModeReg = value;
				self->mPSG[0].SetMode(value);
				self->mPSG[1].SetMode(value);
				return true;
			}
			break;

		case 0xD216:	// SIDMODE (config mode only)
			if (self->mbConfigMode) {
				// Per-SID model + digifix. SetModel() leads with a flush so
				// the change takes effect exactly at "now".
				self->mSIDModeReg = value;
				self->ApplySIDModel();
				return true;
			}
			break;

		case 0xD217:	// RESTRICT (config mode only)
			if (self->mbConfigMode) {
				// Raw runtime register: update it, then re-apply the active
				// audio enables. Does NOT touch the present/capability flags.
				self->mRestrictReg = value;
				self->ApplyRuntimeEnables();
				return true;
			}
			break;
	}

	// Config bank: while CFGEN is latched the ENTIRE $D210-$D21F range is the
	// PokeyMax config bank and must be fully consumed -- a config-bank write
	// must NEVER fall through to the POKEY register mirror underneath (which
	// would corrupt POKEY AUDF/AUDC/AUDCTL/IRQEN/SKCTL). The switch above
	// already returned for the implemented config registers; any other address
	// in the bank swallows the write ($D210 MODE stores the byte). Players
	// that intend to reach POKEY use the dual-write pattern (write both the
	// $D21x config alias and the canonical $D20x POKEY register), so consuming
	// the config alias here is safe. When CFGEN is NOT latched, $D210-$D21F is
	// the POKEY mirror and we fall through.
	if (self->mbConfigMode && addr >= 0xD210 && addr <= 0xD21F) {
		if (addr == 0xD210) {
			// MODE: bit5 0=PAL / 1=NTSC (read-back only; the SID clock is
			// derived from the emulator's machine clock, not this bit). bit0
			// Saturate selects the quad same-side mix curve, so push the new
			// MODE state to the core channel-mode bridge.
			self->mModeReg = value;
			self->ApplyPokeyChannelMode();
		}

		return true;
	}

	// $D210-$D21F written while config is NOT latched -> POKEY mirror
	// ($D200-$D20F): fall through.
	return false;
}
