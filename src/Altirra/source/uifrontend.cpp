//	Altirra - Atari 800/800XL/5200 emulator
//	Copyright (C) 2009-2023 Avery Lee
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
#include <vd2/system/error.h>
#include <at/atcore/devicesnapshot.h>
#include <at/atcore/snapshotimpl.h>
#include "audiowriter.h"
#include "sapwriter.h"
#include "simulator.h"
#include "uiaccessors.h"
#include "uicommondialogs.h"
#include "uidisplay.h"
#include "uifrontend.h"
#include "vgmwriter.h"
#include "videowriter.h"

extern ATSimulator g_sim;

class ATUIRecordingAlreadyActiveException final : public VDException {
public:
	ATUIRecordingAlreadyActiveException() : VDException(L"Cannot start recording because a recording session is already active.") {}
};

class ATUISaveStateFrontEnd final : public ATSnapExchangeObject<ATUISaveStateFrontEnd, "ATUISaveStateFrontEnd"> {
public:
	template<ATExchanger T>
	void Exchange(T& ex);

	VDStringA mOutputName;
};

template<ATExchanger T>
void ATUISaveStateFrontEnd::Exchange(T& ex) {
	ex.Transfer("output_name", &mOutputName);
}

///////////////////////////////////////////////////////////////////////////////

class ATUIFrontEnd final : public IATUIFrontEnd, public IATDeviceSnapshot {
	ATUIFrontEnd(const ATUIFrontEnd&) = delete;
	ATUIFrontEnd& operator=(const ATUIFrontEnd&) = delete;
public:
	ATUIFrontEnd() = default;

	void Shutdown() override;

	ATUIRecordingStatus GetRecordingStatus() const override;
	bool IsRecording() const override;

	void RecordRawAudio(const wchar_t *path) override;
	void RecordAudio(const wchar_t *path) override;
	void RecordVideo(const wchar_t *path, const ATVideoRecordingSettings& settings) override;
	void RecordSap(const wchar_t *path) override;
	void RecordVgm(const wchar_t *path) override;

	void StopRecording() override;

	void PauseRecording() override;
	void ResumeRecording() override;
	void TogglePauseResumeRecording() override;

	void CheckRecordingExceptions() override;

public:
	void StopAudioRecording();
	void StopVideoRecording();
	void StopSapRecording();
	void StopVgmRecording();

	void LoadState(const IATObjectState *state, ATSnapshotContext& ctx) override;
	vdrefptr<IATObjectState> SaveState(ATSnapshotContext& ctx) const override;

private:
	vdautoptr<ATAudioWriter> mpAudioWriter;
	vdautoptr<IATVideoWriter> mpVideoWriter;
	vdautoptr<IATSAPWriter> mpSapWriter;
	vdrefptr<IATVgmWriter> mpATVgmWriter;
};

void ATUIFrontEnd::Shutdown() {
	StopRecording();
}

ATUIRecordingStatus ATUIFrontEnd::GetRecordingStatus() const {
	if (mpVideoWriter)
		return kATUIRecordingStatus_Video;

	if (mpAudioWriter)
		return mpAudioWriter->IsRecordingRaw() ? kATUIRecordingStatus_RawAudio : kATUIRecordingStatus_Audio;

	if (mpSapWriter)
		return kATUIRecordingStatus_Sap;

	if (mpATVgmWriter)
		return kATUIRecordingStatus_Vgm;

	return kATUIRecordingStatus_None;
}

bool ATUIFrontEnd::IsRecording() const {
	return GetRecordingStatus() != kATUIRecordingStatus_None;
}

void ATUIFrontEnd::RecordRawAudio(const wchar_t *path) {
	if (IsRecording())
		throw ATUIRecordingAlreadyActiveException();

	const auto vs = g_sim.GetVideoStandard();
	mpAudioWriter = new ATAudioWriter(path, true, g_sim.IsDualPokeysEnabled(), vs != kATVideoStandard_NTSC && vs != kATVideoStandard_PAL60, g_sim.GetUIRenderer());
	g_sim.GetAudioOutput()->SetAudioTap(mpAudioWriter);
}

void ATUIFrontEnd::RecordAudio(const wchar_t *path) {
	if (IsRecording())
		throw ATUIRecordingAlreadyActiveException();

	const auto vs = g_sim.GetVideoStandard();
	mpAudioWriter = new ATAudioWriter(path, false, g_sim.IsDualPokeysEnabled(), vs != kATVideoStandard_NTSC && vs != kATVideoStandard_PAL60, g_sim.GetUIRenderer());
	g_sim.GetAudioOutput()->SetAudioTap(mpAudioWriter);
}

void ATUIFrontEnd::RecordVideo(const wchar_t *path, const ATVideoRecordingSettings& settings) {
	if (IsRecording())
		throw ATUIRecordingAlreadyActiveException();

	try {
		ATGTIAEmulator& gtia = g_sim.GetGTIA();

		ATCreateVideoWriter(~mpVideoWriter);

		int w;
		int h;
		bool rgb32;
		gtia.GetRawFrameFormat(w, h, rgb32);

		uint32 palette[256];
		if (!rgb32)
			gtia.GetPalette(palette);

		const bool hz50 = g_sim.GetVideoStandard() != kATVideoStandard_NTSC && g_sim.GetVideoStandard() != kATVideoStandard_PAL60;
		VDFraction frameRate = hz50 ? VDFraction(1773447, 114*312) : VDFraction(3579545, 2*114*262);
		double samplingRate = hz50 ? 1773447.0 / 28.0 : 3579545.0 / 56.0;

		switch(settings.mFrameRate) {
			case kATVideoRecordingFrameRate_NTSCRatio:
				if (hz50) {
					samplingRate = samplingRate * (50000.0 / 1001.0) / frameRate.asDouble();
					frameRate = VDFraction(50000, 1001);
				} else {
					samplingRate = samplingRate * (60000.0 / 1001.0) / frameRate.asDouble();
					frameRate = VDFraction(60000, 1001);
				}
				break;

			case kATVideoRecordingFrameRate_Integral:
				if (hz50) {
					samplingRate = samplingRate * 50.0 / frameRate.asDouble();
					frameRate = VDFraction(50, 1);
				} else {
					samplingRate = samplingRate * 60.0 / frameRate.asDouble();
					frameRate = VDFraction(60, 1);
				}
				break;
		}

		double par = 1.0f;
		
		if (settings.mAspectRatioMode != ATVideoRecordingAspectRatioMode::None) {
			if (settings.mAspectRatioMode == ATVideoRecordingAspectRatioMode::FullCorrection)
				par = gtia.GetPixelAspectRatio();
			else {
				int px = 2;
				int py = 2;
				gtia.GetPixelAspectMultiple(px, py);
				par = (double)py / (double)px;
			}
		}

		mpVideoWriter->Init(path,
			settings.mEncoding,
			settings.mVideoBitRate,
			settings.mAudioBitRate,
			w,
			h,
			frameRate,
			par,
			settings.mResamplingMode,
			settings.mScalingMode,
			rgb32 ? NULL : palette,
			samplingRate,
			g_sim.IsDualPokeysEnabled(),
			hz50 ? 1773447.0f : 1789772.5f,
			settings.mbHalfRate,
			settings.mbEncodeAll,
			g_sim.GetUIRenderer()
		);

		g_sim.GetAudioOutput()->SetAudioTap(mpVideoWriter->AsAudioTap());
		gtia.AddVideoTap(mpVideoWriter->AsVideoTap());

		if (IATDisplayPane *dp = ATUIGetDisplayPane())
			dp->SetVideoWriter(mpVideoWriter);
	} catch(...) {
		StopVideoRecording();
		throw;
	}
}

void ATUIFrontEnd::RecordSap(const wchar_t *path) {
	const auto vs = g_sim.GetVideoStandard();
	vdautoptr<IATSAPWriter> sw(ATCreateSAPWriter());
	sw->Init(g_sim.GetEventManager(), &g_sim.GetPokey(), g_sim.GetUIRenderer(), path, vs != kATVideoStandard_NTSC && vs != kATVideoStandard_PAL60);

	mpSapWriter = std::move(sw);
}

void ATUIFrontEnd::RecordVgm(const wchar_t *path) {
	if (IsRecording())
		throw ATUIRecordingAlreadyActiveException();

	auto p = ATCreateVgmWriter();
	p->Init(path, g_sim);

	mpATVgmWriter = std::move(p);
}

void ATUIFrontEnd::StopRecording() {
	StopAudioRecording();
	StopVideoRecording();
	StopSapRecording();
	StopVgmRecording();
}

void ATUIFrontEnd::PauseRecording() {
	if (mpVideoWriter)
		mpVideoWriter->Pause();
}

void ATUIFrontEnd::ResumeRecording() {
	if (mpVideoWriter)
		mpVideoWriter->Resume();
}

void ATUIFrontEnd::TogglePauseResumeRecording() {
	if (mpVideoWriter) {
		if (mpVideoWriter->IsPaused())
			mpVideoWriter->Resume();
		else
			mpVideoWriter->Pause();
	}
}

void ATUIFrontEnd::CheckRecordingExceptions() {
	try {
		if (mpVideoWriter)
			mpVideoWriter->CheckExceptions();
	} catch(const VDException& e) {
		if (e.visible())
			ATUIShowError2(ATUIGetNewPopupOwner(), e.wc_str(), L"Video recording stopped with an error");

		StopVideoRecording();
	}

	try {
		if (mpAudioWriter)
			mpAudioWriter->CheckExceptions();
	} catch(const VDException& e) {
		if (e.visible())
			ATUIShowError2(ATUIGetNewPopupOwner(), e.wc_str(), L"Audio recording stopped with an error");

		StopAudioRecording();
	}

	try {
		if (mpSapWriter)
			mpSapWriter->CheckExceptions();
	} catch(const VDException& e) {
		if (e.visible())
			ATUIShowError2(ATUIGetNewPopupOwner(), e.wc_str(), L"SAP recording stopped with an error");

		StopSapRecording();
	}

	try {
		if (mpATVgmWriter)
			mpATVgmWriter->CheckExceptions();
	} catch(const VDException& e) {
		if (e.visible())
			ATUIShowError2(ATUIGetNewPopupOwner(), e.wc_str(), L"SAP recording stopped with an error");

		StopVgmRecording();
	}
}

void ATUIFrontEnd::LoadState(const IATObjectState *state, ATSnapshotContext& ctx) {
	if (!state) {
		const ATUISaveStateFrontEnd kDefaultState {};

		return LoadState(&kDefaultState, ctx);
	}

	const ATUISaveStateFrontEnd& festate = atser_cast<const ATUISaveStateFrontEnd&>(*state);

	ATUISetCurrentAltOutputName(festate.mOutputName.c_str());
}

vdrefptr<IATObjectState> ATUIFrontEnd::SaveState(ATSnapshotContext& ctx) const {
	vdrefptr state { new ATUISaveStateFrontEnd };

	state->mOutputName = ATUIGetCurrentAltOutputName();

	return state;
}

void ATUIFrontEnd::StopAudioRecording() {
	if (mpAudioWriter) {
		g_sim.GetAudioOutput()->SetAudioTap(nullptr);
		mpAudioWriter->Finalize();
		mpAudioWriter = NULL;
	}
}

void ATUIFrontEnd::StopVideoRecording() {
	if (mpVideoWriter) {
		if (IATDisplayPane *dp = ATUIGetDisplayPane())
			dp->SetVideoWriter(nullptr);

		g_sim.GetGTIA().RemoveVideoTap(mpVideoWriter->AsVideoTap());
		mpVideoWriter->Shutdown();
		g_sim.GetAudioOutput()->SetAudioTap(nullptr);
		mpVideoWriter = nullptr;
	}
}

void ATUIFrontEnd::StopSapRecording() {
	if (mpSapWriter) {
		mpSapWriter->Shutdown();
		mpSapWriter = nullptr;
	}
}

void ATUIFrontEnd::StopVgmRecording() {
	if (mpATVgmWriter) {
		mpATVgmWriter->Shutdown();
		mpATVgmWriter = nullptr;
	}
}

////////////////////////////////////////////////////////////////////////////////

ATUIFrontEnd g_ATUIFrontEnd;

IATDeviceSnapshot *ATUIGetFrontEndSnapshot() {
	return &g_ATUIFrontEnd;
}

IATUIFrontEnd& ATUIGetFrontEnd() {
	return g_ATUIFrontEnd;
}

