//	Altirra - Atari 800/800XL emulator
//	Copyright (C) 2009-2024 Avery Lee
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
#include <vd2/system/strutil.h>
#include <at/atcore/atascii.h>
#include <at/atcore/audiomixer.h>
#include <at/atcore/enumparseimpl.h>
#include <at/atcore/cio.h>
#include <at/atcore/propertyset.h>
#include "printer.h"
#include "kerneldb.h"
#include "printerfont.h"

////////////////////////////////////////////////////////////////////////////////

uint32 ATDevicePrinterBase::RenderedLine::GetNonZeroLength() const {
	auto it = mDotPatterns.begin();
	auto itEnd = mDotPatterns.end();

	while(it != itEnd && itEnd[-1] == 0)
		--itEnd;

	return (uint32)(itEnd - it);
}

uint32 ATDevicePrinterBase::RenderedLine::TrimZeroAtEnd() {
	uint32 n = GetNonZeroLength();

	mDotPatterns.resize(n);
	mPositions.resize(n);

	return n;
}

////////////////////////////////////////////////////////////////////////////////

ATDevicePrinterBase::ATDevicePrinterBase(bool lineBuffered, bool accurateTimingSupported, bool textSupported, bool graphicsSupported)
	: mbLineBuffered(lineBuffered)
	, mbAccurateTimingSupported(accurateTimingSupported)
	, mbGraphicsSupported(graphicsSupported)
	, mbTextSupported(textSupported)
{
	if (!mbTextSupported)
		mbGraphicsEnabled = true;

	mParallelBus.SetOnAttach(
		[this] {
			mpOutput = nullptr;
		}
	);
}

ATDevicePrinterBase::~ATDevicePrinterBase() {
}

void *ATDevicePrinterBase::AsInterface(uint32 id) {
	switch(id) {
		case IATDeviceSIO::kTypeID:
			return static_cast<IATDeviceSIO *>(this);
		case IATDeviceCIO::kTypeID:
			return static_cast<IATDeviceCIO *>(this);
		case IATDeviceParent::kTypeID:
			return static_cast<IATDeviceParent *>(this);
	}

	return ATDevice::AsInterface(id);
}

void ATDevicePrinterBase::Init() {
	mParallelBus.Init(this, 0, IATPrinterOutput::kTypeID, "parallel", L"Printer Output", "parport");
	mpScheduler = GetService<IATDeviceSchedulingService>()->GetMachineScheduler();

	mPrinterSoundSource.Init(*GetService<IATAudioMixer>(), *mpScheduler, "printer");

	if (mbGraphicsEnabled)
		BeginGraphics();

	ColdReset();
}

void ATDevicePrinterBase::Shutdown() {
	EndGraphics();

	if (mpCIOMgr) {
		mpCIOMgr->RemoveCIODevice(this);
		mpCIOMgr = nullptr;
	}

	mpSIOInterface = nullptr;
	mpSIOMgr = nullptr;

	if (mpScheduler) {
		mpScheduler->UnsetEvent(mpEventContinuePrinting);
		mpScheduler = nullptr;
	}

	mPrinterSoundSource.Shutdown();
	mParallelBus.Shutdown();

	mpOutput = nullptr;
}

void ATDevicePrinterBase::GetSettings(ATPropertySet& settings) {
	settings.Clear();

	if (mbGraphicsSupported && mbGraphicsEnabled)
		settings.SetBool("graphics", true);

	if (mbAccurateTimingEnabled)
		settings.SetBool("accurate_timing", true);

	if (mbSoundEnabled)
		settings.SetBool("sound", true);
}

bool ATDevicePrinterBase::SetSettings(const ATPropertySet& settings) {
	if (mbGraphicsSupported && mbTextSupported) {
		mbGraphicsEnabled = settings.GetBool("graphics");

		if (mpScheduler) {
			if (mbGraphicsEnabled)
				BeginGraphics();
			else
				EndGraphics();
		}
	}

	mbAccurateTimingEnabled = settings.GetBool("accurate_timing");
	mbSoundEnabled = settings.GetBool("sound");

	return true;
}

void ATDevicePrinterBase::WarmReset() {
	ColdReset();
}

void ATDevicePrinterBase::ColdReset() {
	mCIOBufferIndex = 0;
	mRenderedLinesQueued = 0;
	mRenderedLinesPrinted = 0;
	CancelPrinting();
}

void ATDevicePrinterBase::InitCIO(IATDeviceCIOManager *mgr) {
	mpCIOMgr = mgr;
	mpCIOMgr->AddCIODevice(this);
}

void ATDevicePrinterBase::GetCIODevices(IATDeviceCIODeviceList& deviceList) const {
	uint32 unitMask = 0;

	for(int unit = 1; unit <= 9; ++unit) {
		if (IsSupportedDeviceId(0x40 + unit - 1))
			unitMask |= 1 << unit;
	}

	// if P1: through P9: are all supported, assume all printer units
	if (unitMask == 0b0'1111111110)
		unitMask = ~UINT32_C(0);

	deviceList.AddDevice('P', unitMask);
}

sint32 ATDevicePrinterBase::OnCIOOpen(int channel, uint8 deviceNo, uint8 aux1, uint8 aux2, const uint8 *filename) {
	return kATCIOStat_NotSupported;
}

sint32 ATDevicePrinterBase::OnCIOClose(int channel, uint8 deviceNo) {
	if (mCIOBufferIndex) {
		if (auto status = FlushCIOBuffer(mpCIOMgr->ReadByte(ATKernelSymbols::ICAX2Z) == 0x53))
			return status;
	}

	return kATCIOStat_Success;
}

sint32 ATDevicePrinterBase::OnCIOGetBytes(int channel, uint8 deviceNo, void *buf, uint32 len, uint32& actual) {
	return kATCIOStat_ReadOnly;
}

sint32 ATDevicePrinterBase::OnCIOPutBytes(int channel, uint8 deviceNo, const void *buf, uint32 len, uint32& actual) {
	actual = 0;

	if (IsPrinting())
		return kCIOStatus_PendingInterruptable;

	// read orientation byte and check if it is sideways; everything else treated as normal
	const uint8 cioOrientation = mpCIOMgr->ReadByte(ATKernelSymbols::ICAX2Z);
	const bool sideways = (cioOrientation == 0x53);
	const uint32 width = sideways ? 29 : 40;

	// buffer bytes and flush after EOL or buffer full
	const uint8 *src = (const uint8 *)buf;
	while(len) {
		if (mCIOBufferIndex >= width) {
			if (auto status = FlushCIOBuffer(sideways))
				return status;
		}

		const uint8 ch = *src++;
		mCIOBuffer[mCIOBufferIndex++] = ch;
		--len;
		++actual;

		if (ch == 0x9B) {
			if (auto status = FlushCIOBuffer(sideways))
				return status;
		}
	}

	if (!mbLineBuffered) {
		if (auto status = FlushCIOBuffer(sideways))
			return status;
	}

	return kATCIOStat_Success;
}

sint32 ATDevicePrinterBase::OnCIOGetStatus(int channel, uint8 deviceNo, uint8 statusbuf[4]) {
	GetStatusFrame(statusbuf);
	return kATCIOStat_Success;
}

sint32 ATDevicePrinterBase::OnCIOSpecial(int channel, uint8 deviceNo, uint8 cmd, uint16 bufadr, uint16 buflen, uint8 aux[6]) {
	return kATCIOStat_NotSupported;
}

void ATDevicePrinterBase::OnCIOAbortAsync() {
	CancelPrinting();
}

void ATDevicePrinterBase::InitSIO(IATDeviceSIOManager *mgr) {
	mpSIOMgr = mgr;
	mpSIOInterface = mpSIOMgr->AddDevice(this);

	InitSIOReceiveTimeout();
}

IATDeviceSIO::CmdResponse ATDevicePrinterBase::OnSerialBeginCommand(const ATDeviceSIOCommand& cmd) {
	if (!cmd.mbStandardRate)
		return kCmdResponse_NotHandled;

	if (!IsSupportedDeviceId(cmd.mDevice))
		return kCmdResponse_NotHandled;
	

	if (cmd.mCommand == 'W') {
		if (IsPrinting())
			return kCmdResponse_NotHandled;

		mLastOrientationByte = cmd.mAUX[0];

		if (!IsSupportedOrientation(cmd.mAUX[0]))
			return kCmdResponse_Fail_NAK;

		const uint8 len = GetWidthForOrientation(cmd.mAUX[0]);

		mpSIOInterface->BeginCommand();
		mpSIOInterface->SendACK();
		mpSIOInterface->ReceiveData(0, len, true);

		if (mbAccurateTimingEnabled) {
			mbPrintSIOCommandPending = true;
		} else {
			mpSIOInterface->SendComplete();
			mpSIOInterface->EndCommand();
		}

		return kCmdResponse_Start;
	} else if (cmd.mCommand == 'S') {
		uint8 statusData[4] {};

		GetStatusFrame(statusData);

		mpSIOInterface->HandleCommand(statusData, 4, true);
		return kCmdResponse_Start;
	}

	return kCmdResponse_NotHandled;
}

void ATDevicePrinterBase::OnSerialAbortCommand() {
	mbPrintSIOCommandPending = false;
}

void ATDevicePrinterBase::OnSerialReceiveComplete(uint32 id, const void *data, uint32 len, bool checksumOK) {
	HandleFrame((const uint8 *)data, len, false);

	if (!IsPrinting()) {
		mpSIOInterface->SendComplete();
		mpSIOInterface->EndCommand();
	}
}

void ATDevicePrinterBase::OnSerialFence(uint32 id) {
	BeginPrinting(false);
}

IATDeviceSIO::CmdResponse ATDevicePrinterBase::OnSerialAccelCommand(const ATDeviceSIORequest& request) {
	if (mbAccurateTimingEnabled && IsSupportedDeviceId(request.mDevice) && request.mCommand == 'W')
		return kCmdResponse_BypassAccel;

	return OnSerialBeginCommand(request);
}

IATDeviceBus *ATDevicePrinterBase::GetDeviceBusById(sint32 id) {
	return id || !mbTextSupported ? nullptr : &mParallelBus;
}

sint32 ATDevicePrinterBase::GetDeviceBusIdByIndex(uint32 index) {
	return index == 0 && mbTextSupported ? 0 : -1;
}

void ATDevicePrinterBase::OnScheduledEvent(uint32 id) {
	if (id == kEventId_ContinuePrinting) {
		mpEventContinuePrinting = nullptr;

		ContinuePrinting();
	}
}

void ATDevicePrinterBase::BeginGraphics() {
	mpOutput = nullptr;

	if (!mpPrinterGraphicalOutput) {
		ATDeviceInfo info;
		GetDeviceInfo(info);

		mpPrinterGraphicalOutput = GetService<IATPrinterOutputManager>()->CreatePrinterGraphicalOutput(GetPrinterOutputName().c_str(), GetGraphicsSpec());

		OnCreatedGraphicalOutput();
	}
}

void ATDevicePrinterBase::EndGraphics() {
	if (mpPrinterGraphicalOutput) {
		mpPrinterGraphicalOutput->SetOnClear(nullptr);
		mpPrinterGraphicalOutput = nullptr;
	}
}

void ATDevicePrinterBase::InitSIOReceiveTimeout() {
}

void ATDevicePrinterBase::OnCreatedGraphicalOutput() {
}

ATPrinterGraphicsSpec ATDevicePrinterBase::GetGraphicsSpec() const {
	return {};
}

void ATDevicePrinterBase::GetStatusFrame(uint8 frame[4]) {
	frame[0] = 0;
	frame[1] = 0;
	frame[2] = 16;
	frame[3] = 0;

	GetStatusFrameInternal(frame);
}

int ATDevicePrinterBase::FlushCIOBuffer(bool sideways) {
	if (!mCIOBufferIndex)
		return 0;

	if (IsPrinting())
		return kATCIOStat_Timeout;

	if (mpCIOMgr->IsBreakActive())
		return kATCIOStat_Break;

	uint32 width = mCIOBufferIndex;
	
	if (mbLineBuffered) {
		width = sideways ? 29 : 40;

		if (GetWidthForOrientation(sideways ? 0x53 : 0x4E) != width)
			return kATCIOStat_Timeout;

		// pad buffer with EOLs if we have a short line
		if (mCIOBufferIndex < width)
			memset(&mCIOBuffer[mCIOBufferIndex], 0x9B, width - mCIOBufferIndex);
	}

	// reset buffer index
	mCIOBufferIndex = 0;

	// set last orientation byte N/S, since we are short-circuiting an actual SIO frame; note
	// that this isn't just ICAX2Z as unrecognized values need to be converted to Normal
	mLastOrientationByte = sideways ? 0x53 : 0x4E;

	// send the (whole) buffer
	HandleFrame(mCIOBuffer, width, true);

	return IsPrinting() ? -1 : 0;
}

void ATDevicePrinterBase::HandleFrame(const uint8 *data, uint32 len, bool fromCIO) {
	const uint8 *src = (const uint8 *)data;

	if (len > 40)
		len = 40;

	// Ensure that the frame is EOL terminated. The additional byte is also guaranteed
	// to be available to HandleFrameInternal().
	uint8 buf[41];
	memcpy(buf, src, len);
	buf[len] = 0x9B;
	
	HandleFrameInternal(mLastOrientationByte, buf, len, mpPrinterGraphicalOutput && mbGraphicsEnabled);
	FlushRenderedLines(fromCIO);
}

VDStringW ATDevicePrinterBase::GetPrinterOutputName() {
	ATDeviceInfo info;
	GetDeviceInfo(info);

	VDStringW name(info.mpDef->mpName);

	uint8 deviceId = 0x48;		// P9:

	while(deviceId >= 0x40 && !IsSupportedDeviceId(deviceId))
		--deviceId;

	if (deviceId >= 0x40)
		name.append_sprintf(L" (P%c:)", L'1' + (deviceId - 0x40));

	return name;
}


IATPrinterOutput *ATDevicePrinterBase::GetTextOutput() {
	auto *printer = mParallelBus.GetChild<IATPrinterOutput>();
	if (!printer) {
		if (!mpOutput) {
			mpOutput = GetService<IATPrinterOutputManager>()->CreatePrinterOutput(GetPrinterOutputName().c_str());
		}

		printer = mpOutput;
	}

	return printer;
}

ATDevicePrinterBase::RenderedLine *ATDevicePrinterBase::BeginRenderLine(uint32 width) {
	if (mRenderedLinesQueued >= vdcountof(mRenderedLines))
		return nullptr;

	RenderedLine *rl = &mRenderedLines[mRenderedLinesQueued];

	rl->mDotPatterns.clear();
	rl->mDotPatterns.resize(width, 0);
	rl->mPositions.clear();
	rl->mPositions.resize(width, 0);

	return rl;
}

void ATDevicePrinterBase::EndRenderLine(const RenderLineParams& params) {
	if (mRenderedLinesQueued >= vdcountof(mRenderedLines)) {
		VDFAIL("Invalid EndRenderLine() call");
		return;
	}

	mRenderedLines[mRenderedLinesQueued].mParams = params;

	++mRenderedLinesQueued;
}

void ATDevicePrinterBase::RenderLineWithFont(const ATPrinterFontDesc& desc, const uint8 *fontData, uint8 *charData, uint32 n, float x, float xStep, float xSpacing, bool bold, const RenderLineParams& params) {
	RenderedLine *rl = BeginRenderLine((desc.mWidth + (bold ? 1 : 0)) * n);
	if (!rl)
		return;

	uint8 *dotDst = rl->mDotPatterns.data();
	float *posDst = rl->mPositions.data();

	for(uint32 i = 0; i < n; ++i) {
		uint8 ch = charData[i];

		if (ch < desc.mCharFirst || ch > desc.mCharLast)
			ch = desc.mCharFirst;

		const uint8 *src = fontData + (uint32)(ch - desc.mCharFirst) * desc.mWidth;

		if (bold) {
			uint8 last = 0;

			for(uint32 j = 0; j < desc.mWidth; ++j) {
				uint8 next = *src++;
				*dotDst++ = last | next;
				last = next;

				*posDst++ = x;
				x += xStep;
			}

			*dotDst++ = last;
			*posDst++ = x;

			x += xSpacing;
		} else {
			for(uint32 j = 0; j < desc.mWidth; ++j) {
				*dotDst++ = *src++;
				*posDst++ = x;
				x += xStep;
			}

			x += xSpacing;
		}
	}

	EndRenderLine(params);
}

void ATDevicePrinterBase::FlushRenderedLines(bool fromCIO) {
	if (mbAccurateTimingEnabled) {
		BeginPrinting(fromCIO);
		return;
	}

	if (mpPrinterGraphicalOutput) {
		for(uint32 i = mRenderedLinesPrinted; i < mRenderedLinesQueued; ++i) {
			const RenderedLine& rl = mRenderedLines[i];

			const float *pos = rl.mPositions.data();
			for(uint8 v : rl.mDotPatterns) {
				if (v)
					mpPrinterGraphicalOutput->Print(*pos, v);

				++pos;
			}

			if (rl.mParams.mLineAdvance != 0)
				mpPrinterGraphicalOutput->FeedPaper(rl.mParams.mLineAdvance);
		}
	}

	mRenderedLinesQueued = 0;
	mRenderedLinesPrinted = 0;
}

void ATDevicePrinterBase::BeginAsyncPrinting() {
	mbAsyncPrinting = true;
}

void ATDevicePrinterBase::EndAsyncPrinting() {
	mbAsyncPrinting = false;

	mpScheduler->SetEvent(1, this, kEventId_ContinuePrinting, mpEventContinuePrinting);
}

bool ATDevicePrinterBase::IsPrinting() const {
	return mpEventContinuePrinting != nullptr || mbAsyncPrinting;
}

void ATDevicePrinterBase::BeginPrinting(bool fromCIO) {
	mbPrintingFromCIO = fromCIO;

	ContinuePrinting();
}

void ATDevicePrinterBase::ContinuePrinting() {
	if (mbAsyncPrinting)
		return;

	for(;;) {
		if (mRenderedLinesPrinted >= mRenderedLinesQueued) {
			CompletePrinting();
			return;
		}
	
		const RenderedLine& rl = mRenderedLines[mRenderedLinesPrinted];

		if (mPrintState == PrintState::PreDelay) {
			mPrintNextColumn = 0;

			const uint32 preDelayCycles = VDRoundToInt32(rl.mParams.mPreDelay * mpScheduler->GetRate().asDouble());

			mPrintLineStartTime = mpScheduler->GetTick() + preDelayCycles;
			mPrintState = PrintState::StartPrinting;

			if (preDelayCycles) {
				mpScheduler->SetEvent(preDelayCycles, this, kEventId_ContinuePrinting, mpEventContinuePrinting);
				return;
			}
		} else if (mPrintState == PrintState::StartPrinting) {
			if (mbSoundEnabled) {
				if (rl.mParams.mHeadAdvanceSampleId != ATAudioSampleId{} && rl.mParams.mHeadAdvanceSoundDuration > 0) {
					mPrinterSoundSource.ScheduleSound(
						rl.mParams.mHeadAdvanceSampleId,
						true,
						rl.mParams.mHeadAdvanceSoundDelay,
						rl.mParams.mHeadAdvanceSoundDuration,
						1.0f
					);
				}

				if (rl.mParams.mHeadRetractSampleId != ATAudioSampleId{}) {
					mPrinterSoundSource.ScheduleSound(
						rl.mParams.mHeadRetractSampleId,
						false,
						rl.mParams.mHeadRetractSoundDelay,
						0,
						1.0f
					);
				}

				if (rl.mParams.mHeadHomeSampleId != ATAudioSampleId{}) {
					mPrinterSoundSource.ScheduleSound(
						rl.mParams.mHeadHomeSampleId,
						false,
						rl.mParams.mHeadHomeSoundDelay,
						0,
						1.0f
					);
				}
			}

			mPrintState = PrintState::PrintColumns;
		} else if (mPrintState == PrintState::PrintColumns) {
			while(mPrintNextColumn < rl.mPositions.size()) {
				float xpos = rl.mPositions[mPrintNextColumn];
				uint8 pattern = rl.mDotPatterns[mPrintNextColumn];
				++mPrintNextColumn;

				if (!pattern)
					continue;

				if (mpPrinterGraphicalOutput)
					mpPrinterGraphicalOutput->Print(xpos, pattern);

				const uint32 t = mpScheduler->GetTick();

				if (mbSoundEnabled) {
					if (rl.mParams.mPrintDelayPerY != 0) {
						// pins sequential
						const double schedulerRate = mpScheduler->GetRate().asDouble();

						for(int i=0; i<8; ++i) {
							if (pattern & (1 << i)) {
								mPrinterSoundSource.AddPinSound(t + VDRoundToInt32(schedulerRate * (float)i * rl.mParams.mPrintDelayPerY), 1);
							}
						}
					} else {
						// all pins at once
						uint8 pinCount = pattern;

						pinCount -= (pinCount >> 1) & 0x55;
						pinCount = ((pinCount >> 2) & 0x33) + (pinCount & 0x33);
						pinCount = (pinCount & 0x0F) + (pinCount >> 4);

						mPrinterSoundSource.AddPinSound(t, pinCount);
					}
				}

				float printTime = rl.mParams.mPrintDelayPerX * (xpos - rl.mParams.mPrintDelayXHome);

				if (printTime > 0) {
					uint32 printTimeNow = mpScheduler->GetTick() - mPrintLineStartTime;
					uint32 printTimeCycles = VDRoundToInt32(printTime * mpScheduler->GetRate().asDouble());

					if (printTimeCycles > printTimeNow) {
						mpScheduler->SetEvent(printTimeCycles - printTimeNow, this, kEventId_ContinuePrinting, mpEventContinuePrinting);
						return;
					}
				}
			}

			mPrintState = PrintState::PostDelay;

			const uint32 desiredDelay = VDRoundToInt32(rl.mParams.mPrintDelay * mpScheduler->GetRate().asDouble());
			const uint32 actualDelay = mpScheduler->GetTick() - mPrintLineStartTime;

			if (desiredDelay > actualDelay) {
				mpScheduler->SetEvent(desiredDelay - actualDelay, this, kEventId_ContinuePrinting, mpEventContinuePrinting);
				return;
			}
		} else if (mPrintState == PrintState::PostDelay) {
			const uint32 postDelayCycles = VDRoundToInt32(rl.mParams.mPostDelay * mpScheduler->GetRate().asDouble());

			mPrintState = PrintState::LineAdvance;

			if (postDelayCycles) {
				mpScheduler->SetEvent(postDelayCycles, this, kEventId_ContinuePrinting, mpEventContinuePrinting);
				return;
			}
		} else if (mPrintState == PrintState::LineAdvance) {
			if (mpPrinterGraphicalOutput)
				mpPrinterGraphicalOutput->FeedPaper(rl.mParams.mLineAdvance);

			++mRenderedLinesPrinted;
			mPrintState = PrintState::PreDelay;
		} else {
			VDFAIL("Undefined printing state");
			CompletePrinting();
		}
	}
}

void ATDevicePrinterBase::CompletePrinting() {
	VDASSERT(!mpEventContinuePrinting);

	if (mbPrintSIOCommandPending) {
		mbPrintSIOCommandPending = false;

		mpSIOInterface->SendComplete();
		mpSIOInterface->EndCommand();
	}

	mRenderedLinesQueued = 0;
	mRenderedLinesPrinted = 0;
}

void ATDevicePrinterBase::CancelPrinting() {
	mpScheduler->UnsetEvent(mpEventContinuePrinting);

	mbPrintSIOCommandPending = false;
}

///////////////////////////////////////////////////////////////////////////////

AT_DEFINE_ENUM_TABLE_BEGIN(ATPrinterPortTranslationMode)
	{ ATPrinterPortTranslationMode::Default, "default" },
	{ ATPrinterPortTranslationMode::Raw, "raw" },
	{ ATPrinterPortTranslationMode::AtasciiToUtf8, "atasciitoutf8" },
AT_DEFINE_ENUM_TABLE_END(ATPrinterPortTranslationMode, ATPrinterPortTranslationMode::Default)

void ATCreateDevicePrinter(const ATPropertySet& pset, IATDevice **dev) {
	vdrefptr<ATDevicePrinter> p(new ATDevicePrinter);

	*dev = p.release();
}

extern const ATDeviceDefinition g_ATDeviceDefPrinter = { "printer", "printer", L"Printer (P:)", ATCreateDevicePrinter };

ATDevicePrinter::ATDevicePrinter()
	: ATDevicePrinterBase(false, false, true, false)
{
	SetSaveStateAgnostic();
}

ATDevicePrinter::~ATDevicePrinter() {
}

void ATDevicePrinter::GetDeviceInfo(ATDeviceInfo& info) {
	info.mpDef = &g_ATDeviceDefPrinter;
}

void ATDevicePrinter::GetSettings(ATPropertySet& settings) {
	ATDevicePrinterBase::GetSettings(settings);

	settings.SetEnum("translation_mode", mTranslationMode);
}

bool ATDevicePrinter::SetSettings(const ATPropertySet& settings) {
	bool succeeded = ATDevicePrinterBase::SetSettings(settings);

	mTranslationMode = settings.GetEnum<ATPrinterPortTranslationMode>("translation_mode");

	return succeeded;
}

bool ATDevicePrinter::IsSupportedDeviceId(uint8 id) const {
	// The 820 only responds to P1:, and we follow that for the generic SIO printer.
	return id == 0x40;
}

bool ATDevicePrinter::IsSupportedOrientation(uint8 aux1) const {
	// support N/S like 820
	return aux1 == 0x4E || aux1 == 0x53;
}

uint8 ATDevicePrinter::GetWidthForOrientation(uint8 aux1) const {
	return aux1 == 0x53 ? 29 : 40;
}

void ATDevicePrinter::GetStatusFrameInternal(uint8 frame[4]) {
}

void ATDevicePrinter::HandleFrameInternal(uint8 orientation, uint8 *buf, uint32 len, bool graphics) {
	if (len > 40)
		VDRaiseInternalFailure();

	IATPrinterOutput *output = GetTextOutput();
	if (!output)
		return;

	// look for an EOL, and if there is one, truncate at that point and set eol flag
	bool eol = false;

	for(uint32 i=0; i<len; ++i) {
		if (buf[i] == 0x9B) {
			eol = true;
			len = i + 1;
			break;
		}
	}

	if (!output->WantUnicode()) {
		if (mTranslationMode == ATPrinterPortTranslationMode::AtasciiToUtf8) {
			uint8 dstbuf[160];
			uint8 *dst = dstbuf;

			for(uint32 i=0; i<len; ++i) {
				uint8 c = buf[i];

				if (c == 0x9B)
					*dst++ = 0x0A;
				else {
					wchar_t ch = kATATASCIITables.mATASCIIToUnicode[0][c & 0x7F];
					size_t srcUsed = 0;

					dst += VDCodePointToU8(dst, 4, &ch, 1, srcUsed);
				}
			}

			output->WriteRaw(dstbuf, (size_t)(dst - dstbuf));
		} else {
			if (mTranslationMode == ATPrinterPortTranslationMode::Default) {
				for(uint32 i = 0; i < len; ++i)
					buf[len] &= 0x7F;

				if (eol)
					buf[len - 1] = 0x0D;
			}

			output->WriteRaw(buf, len);
		}
		return;
	}

	wchar_t unibuf[40];
	size_t ulen = 0;

	for(uint32 i=0; i<len; ++i) {
		uint8 c = *buf++;

		if (c == 0x9B) {
			c = '\n';
		} else {
			c &= 0x7F;

			if (c < 0x20)
				continue;
		}

		unibuf[ulen++] = (wchar_t)c;
	}

	output->WriteUnicode(unibuf, ulen);
}

///////////////////////////////////////////////////////////////////////////////

void ATCreateDevicePrinter820(const ATPropertySet& pset, IATDevice **dev) {
	vdrefptr<ATDevicePrinter820> p(new ATDevicePrinter820);
	p->SetSettings(pset);

	*dev = p.release();
}

extern const ATDeviceDefinition g_ATDeviceDefPrinter820 = { "820", "820", L"820 Printer", ATCreateDevicePrinter820 };

ATDevicePrinter820::ATDevicePrinter820()
	: ATDevicePrinterBase(true, true, true, true)
{
	SetSaveStateAgnostic();
}

ATDevicePrinter820::~ATDevicePrinter820() {
}

void ATDevicePrinter820::GetDeviceInfo(ATDeviceInfo& info) {
	info.mpDef = &g_ATDeviceDefPrinter820;
}

ATPrinterGraphicsSpec ATDevicePrinter820::GetGraphicsSpec() const {
	ATPrinterGraphicsSpec spec {};
	spec.mPageWidthMM = 98.425f;			// 3+7/8" wide paper
	spec.mPageVBorderMM = 8.0f;				// vertical border
	spec.mDotRadiusMM = 0.22f;				// guess for dot radius
	spec.mVerticalDotPitchMM = 0.44704f;	// 0.0176" vertical pitch
	spec.mbBit0Top = true;
	spec.mNumPins = 7;
	return spec;
}

bool ATDevicePrinter820::IsSupportedDeviceId(uint8 id) const {
	// support P1: only
	return id == 0x40;
}

bool ATDevicePrinter820::IsSupportedOrientation(uint8 aux1) const {
	// support N/S
	return aux1 == 0x4E || aux1 == 0x53;
}

uint8 ATDevicePrinter820::GetWidthForOrientation(uint8 aux1) const {
	return aux1 == 0x53 ? 29 : 40;
}

void ATDevicePrinter820::GetStatusFrameInternal(uint8 frame[4]) {
	// The 820 sets the 'intelligent controller' bit 7 on device status.
	frame[0] |= 0x80;

	// The orientation byte from the last Write command (AUX1) is reported as second status byte
	frame[1] = mLastOrientationByte;

	// 820 timeout is 6*64 vblanks
	frame[2] = 6;

	// unused byte is $00
	frame[3] = 0;
}

void ATDevicePrinter820::HandleFrameInternal(uint8 orientation, uint8 *buf, uint32 len, bool graphics) {
	// check for sideways
	const bool sideways = (orientation == 0x53);
	if (sideways) {
		// sideways: mask off bit 7, convert $60-7F to 40-5F, then convert anything not in
		// $30-5F afterward to space, and write backwards
		for(uint32 i = 0; i < len; ++i) {
			buf[i] &= 0x7F;

			if (buf[i] < 0x20 || buf[i] > 0x7F)
				buf[i] = 0x20;
			else if (buf[i] >= 0x60)
				buf[i] -= 0x20;
		}

		std::reverse(buf, buf + len);
	} else {
		// normal: mask off bit 7, and convert anything not in $20-7E afterward to space
		for(uint32 i = 0; i < len; ++i) {
			buf[i] &= 0x7F;

			if (buf[i] < 0x20 || buf[i] > 0x7E)
				buf[i] = 0x20;
		}
	}

	// trim off any spaces at the end, just to make the printer output nicer
	while(len && buf[len - 1] == 0x20)
		--len;

	// put in a new EOL; buf[] is len+1 so this is allowed
	buf[len++] = 0x9B;

	// write out line
	if (graphics) {
		static constexpr float xStartMM = 8.19750786f;
		static constexpr float xStepMM = 0.3606987f;
		static constexpr float xSpacingMM = 0.275780499f;

		// 10.75 inches/sec per 820 service manual
		static constexpr float kSecsPerMM = 1.0f / (10.75f * 25.4f);

		static constexpr RenderLineParams params {
			.mPrintDelay = 0.800f,			// 800ms/line per service manual
			.mPrintDelayXHome = xStartMM,
			.mPrintDelayPerX = kSecsPerMM,
			.mLineAdvance = 4.23333f
		};

		if (sideways)
			RenderLineWithFont(g_ATPrinterFont820S.mDesc, g_ATPrinterFont820S.mColumns, buf, len - 1, xStartMM, xStepMM, xSpacingMM, false, params);
		else
			RenderLineWithFont(g_ATPrinterFont820.mDesc, g_ATPrinterFont820.mColumns, buf, len - 1, xStartMM, xStepMM, xSpacingMM, false, params);

		return;
	}

	IATPrinterOutput *output = GetTextOutput();
	if (!output)
		return;

	if (!output->WantUnicode()) {
		output->WriteRaw(buf, len);
		return;
	}

	// 820 character set is actually closer to ASCII than ATASCII. Normal mode is
	// entirely ASCII; sideways has up and left arrow for $5E/5F.
	wchar_t unibuf[41];

	if (sideways) {
		for(uint32 i=0; i<len; ++i) {
			wchar_t ch = buf[i];

			switch(ch) {
				case 0x5E:
					ch = L'\u2191';		// U+2191 Upwards arrow
					break;
				case 0x5F:
					ch = L'\u2190';		// U+2190 Leftwards arrow
					break;
				case 0x9B:
					ch = L'\n';
					break;
			}

			unibuf[i] = ch;
		}
	} else {
		for(uint32 i=0; i<len; ++i) {
			const uint8 c = buf[i];

			unibuf[i] = c == 0x9B ? L'\n' : c;
		}
	}

	output->WriteUnicode(unibuf, len);
}

///////////////////////////////////////////////////////////////////////////////

void ATCreateDevicePrinter1025(const ATPropertySet& pset, IATDevice **dev) {
	vdrefptr<ATDevicePrinter1025> p(new ATDevicePrinter1025);
	p->SetSettings(pset);

	*dev = p.release();
}

extern const ATDeviceDefinition g_ATDeviceDefPrinter1025 = { "1025", "1025", L"1025 Printer", ATCreateDevicePrinter1025 };

ATDevicePrinter1025::ATDevicePrinter1025()
	: ATDevicePrinterBase(false, true, true, true)
{
	SetSaveStateAgnostic();
}

ATDevicePrinter1025::~ATDevicePrinter1025() {
}

void ATDevicePrinter1025::GetDeviceInfo(ATDeviceInfo& info) {
	info.mpDef = &g_ATDeviceDefPrinter1025;
}

void ATDevicePrinter1025::ColdReset() {
	ATDevicePrinterBase::ColdReset();

	ResetState();
}

void ATDevicePrinter1025::InitSIOReceiveTimeout() {
	// 1025 per-byte timeout is approximately 845K cycles at 7.35MHz/12, or
	// ~1.37 seconds.
	mpSIOInterface->SetReceiveByteTimeout(2500000);
}

ATPrinterGraphicsSpec ATDevicePrinter1025::GetGraphicsSpec() const {
	ATPrinterGraphicsSpec spec {};
	spec.mPageWidthMM = 215.9f;				// 8.5" wide paper
	spec.mPageVBorderMM = 8.0f;				// vertical border
	spec.mDotRadiusMM = 0.28f;				// guess for dot radius
	spec.mVerticalDotPitchMM = 0.44704f;	// 0.0176" vertical pitch
	spec.mbBit0Top = true;
	spec.mNumPins = 7;
	return spec;
}

bool ATDevicePrinter1025::IsSupportedDeviceId(uint8 id) const {
	// support P1: or P3:
	return id == 0x40 || id == 0x42;
}

bool ATDevicePrinter1025::IsSupportedOrientation(uint8 aux1) const {
	// support N only
	return aux1 == 0x4E;
}

uint8 ATDevicePrinter1025::GetWidthForOrientation(uint8 aux1) const {
	return 40;
}

void ATDevicePrinter1025::GetStatusFrameInternal(uint8 frame[4]) {
	// The 1025 does not return last orientation byte.
	frame[1] = 0;

	// 1025 timeout is 15*64 vblanks
	frame[2] = 15;

	// unused byte is $30
	frame[3] = 0x30;

	// Rather unusually, the 1025 also resets all of its internal state when processing
	// a Status command. This makes it sensitive to whether LPRINT or PRINT is used,
	// since in the former case BASIC opens and closes P: for each line. This means that
	// settings from ESC codes, like density and European enables, get reset.

	ResetState();
}

void ATDevicePrinter1025::HandleFrameInternal(uint8 orientation, uint8 *buf, uint32 len, bool graphics) {
	// normal: mask off bit 7, and convert 00-1F/60/7B/7D-7F to space
	// european: just mask off bit 7
	//
	// The following escape codes are are only handled at start of line:
	//	ESC CTRL+T		Print 16.5 CPI
	//	ESC CTRL+O		Print 10 CPI
	//	ESC CTRL+N		Print 5 CPI
	//	ESC L			Set long line (80 chars)
	//	ESC S			Set short line (64 chars)
	//	ESC 6			Set to 6 LPI
	//	ESC 8			Set to 8 LPI
	//
	// The following escape codes are handled at any time:
	//	ESC CTRL+W		enable european characters
	//	ESC CTRL+X		ignore european characters
	//
	// Regardless of whether the escape is valid, the character is omitted
	// from the line buffer and escape modes are cleared after processing an
	// escape. EOL takes priority over ESC and escape is cleared after processing
	// an EOL.

	uint32 i = 0;
	for(; i < len; ++i) {
		uint8 c = buf[i];

		// check for EOL
		if (c == 0x9B) {
			// Flush line buffer, but only if we aren't in column 0 or didn't wrap from
			// the last frame -- we should not print a blank line if exactly 80
			// characters are printed, but we do want a blank line if only an EOL is
			// printed.
			if (mColumn != 0 || !mbLastLineContinued)
				FlushLine(graphics);

			mbLastLineContinued = false;
			// ignore remainder of data frame
			break;
		}

		// check for ESC
		if (c == 0x1B) {
			if (mColumn == 0)
				mbEscapeAtStart = true;
			else
				mbEscape = true;

			continue;
		} else if (mbEscapeAtStart || mbEscape) {
			if (mbEscapeAtStart) {
				// Bit 7 is ignored on the ESC code.
				switch(c & 0x7F) {
					case 0x14:	// Ctrl+T
						mCharDensity = CharDensity::Cpi16_5;
						break;

					case 0x0F:	// Ctrl+O
						mCharDensity = CharDensity::Cpi10;
						break;

					case 0x0E:	// Ctrl+N
						mCharDensity = CharDensity::Cpi5;
						break;

					case 0x4C:	// L
						mbShortLine = false;
						break;

					case 0x53:	// S
						mbShortLine = true;
						break;

					case 0x36:	// 6
						mbDenseLines = false;
						break;

					case 0x38:	// 8
						mbDenseLines = true;
						break;

					default:
						break;
				}
			}

			switch(c) {
				case 0x17:	// Ctrl+W
					mbEuropeanChars = true;
					break;

				case 0x18:	// Ctrl+X
					mbEuropeanChars = false;
					break;
			}

			mbEscape = false;
			mbEscapeAtStart = false;
			continue;
		}

		// Printable character -- put in line buffer.
		//
		// The 1025's character set is the international ATASCII set. In European
		// mode, the entire 7-bit character set is accessible except for ESC, which
		// cannot be printed (ESC ESC is a no-op). When European characters are
		// disabled, the characters that differ are suppressed -- 00-1F are ignored
		// and 60/7B/7D-7F are blanked.
		//
		// Bit 7 is ignored.

		c &= 0x7F;

		if (!mbEuropeanChars) {
			if (c < 0x20)
				continue;

			if (c == 0x60 || c == 0x7B || c >= 0x7D)
				c = 0x20;
		}

		if (!mColumn)
			UpdateLineLength();

		mRawLineBuffer[mColumn] = c;
		mUnicodeLineBuffer[mColumn] = kATATASCIITables.mATASCIIToUnicode[1][c];
		if (++mColumn >= mLineLength) {
			FlushLine(graphics);
			mbLastLineContinued = true;
		}
	}

}

void ATDevicePrinter1025::FlushLine(bool graphics) {
	if (graphics) {
		float xStartMM = 6.31359720f;
		float xStepMM = 0.21192742f;
		float xSpacingMM = 2.54f - xStepMM * 9;

		bool bold = false;

		if (mCharDensity == CharDensity::Cpi5) {
			xStepMM *= 2.0f;
			xSpacingMM *= 2.0f;

			bold = true;

			// short left margin at 5 CPI is 4 characters
			if (mbShortLine)
				xStartMM += 2.54f * 2.0f * 4;
		} else if (mCharDensity == CharDensity::Cpi16_5) {
			xStepMM *= 10.0f / 16.5f;
			xSpacingMM *= 10.0f / 16.5f;

			// short left margin at 16.5 CPI is 13 characters
			if (mbShortLine)
				xStartMM += 2.54f * 10.0f / 16.0f * 13;
		} else {
			// short left margin at 10 CPI is 8 characters
			if (mbShortLine)
				xStartMM += 2.54f * 8;
		}

		uint32 colsToPrint = mColumn;

		while(colsToPrint && mRawLineBuffer[colsToPrint - 1] == 0x20)
			--colsToPrint;

		// The 1025 prints at ~4060 cycles per phase in 5 or 10 CPI mode, with
		// each phase advancing by 1/40th of an inch. In 16.5 CPI mode, it slows
		// ~6740 cycles per phase. The machine clock is 7.37MHz / 12.
		static constexpr float kMachineClock = 7370000.0f / 12.0f;
		static constexpr float kMMPerPhase = 25.4f / 40.0f;
		static constexpr float kTimePerMMNormal = (4060.0f / kMachineClock) / kMMPerPhase;
		static constexpr float kTimePerMMCondensed = (6740.0f / kMachineClock) / kMMPerPhase;

		const float timePerMM = (mCharDensity == CharDensity::Cpi16_5)
			? kTimePerMMCondensed
			: kTimePerMMNormal;

		const RenderLineParams params {
			.mPrintDelay = timePerMM * (xStepMM * 6) * colsToPrint,
			.mPrintDelayXHome = xStartMM,
			.mPrintDelayPerX = timePerMM,
			.mLineAdvance = mbDenseLines ? 3.175f : 4.23333f
		};

		RenderLineWithFont(
			g_ATPrinterFont1025.mDesc,
			g_ATPrinterFont1025.mColumns,
			mRawLineBuffer,
			colsToPrint,
			xStartMM,
			xStepMM,
			xSpacingMM,
			bold,
			params
		);
	} else {
		// trim off any spaces at the end, just to make the printer output nicer
		while(mColumn && mRawLineBuffer[mColumn - 1] == 0x20)
			--mColumn;

		mRawLineBuffer[mColumn] = 0x9B;
		mUnicodeLineBuffer[mColumn] = L'\n';

		IATPrinterOutput *output = GetTextOutput();

		if (output) {
			if (output->WantUnicode())
				output->WriteUnicode(mUnicodeLineBuffer, mColumn + 1);
			else
				output->WriteRaw(mRawLineBuffer, mColumn + 1);
		}
	}

	mColumn = 0;
	mbEscape = false;
	mbEscapeAtStart = false;
}

void ATDevicePrinter1025::UpdateLineLength() {
	switch(mCharDensity) {
		case CharDensity::Cpi5:
			mLineLength = mbShortLine ? 32 : 40;
			break;

		case CharDensity::Cpi10:
		default:
			mLineLength = mbShortLine ? 64 : 80;
			break;

		case CharDensity::Cpi16_5:
			mLineLength = mbShortLine ? 105 : 132;
			break;
	}
}

void ATDevicePrinter1025::ResetState() {
	// According to the Atari 1025 Printer Field Service Manual, page 3-3, the default power-on
	// state is: 10 CPI, 6 LPI, 80 chars/line, European chars disabled.
	mbEscape = false;
	mbEscapeAtStart = false;
	mbEuropeanChars = false;
	mCharDensity = CharDensity::Cpi10;
	mbDenseLines = false;
	mbShortLine = false;
	mbLastLineContinued = false;

	// The line buffer is also flushed. This is important with OS-B, which pads any leftover
	// characters in the printer buffer with spaces instead of EOLs.
	mColumn = 0;
}

///////////////////////////////////////////////////////////////////////////////

void ATCreateDevicePrinter1029(const ATPropertySet& pset, IATDevice **dev) {
	vdrefptr<ATDevicePrinter1029> p(new ATDevicePrinter1029);
	p->SetSettings(pset);

	*dev = p.release();
}

extern const ATDeviceDefinition g_ATDeviceDefPrinter1029 = { "1029", "1029", L"1029 Printer", ATCreateDevicePrinter1029 };

ATDevicePrinter1029::ATDevicePrinter1029()
	: ATDevicePrinterBase(false, true, true, true)
{
	SetSaveStateAgnostic();
}

ATDevicePrinter1029::~ATDevicePrinter1029() {
}

void ATDevicePrinter1029::GetDeviceInfo(ATDeviceInfo& info) {
	info.mpDef = &g_ATDeviceDefPrinter1029;
}

void ATDevicePrinter1029::ColdReset() {
	ATDevicePrinterBase::ColdReset();

	ResetState();
}

ATPrinterGraphicsSpec ATDevicePrinter1029::GetGraphicsSpec() const {
	ATPrinterGraphicsSpec spec {};
	spec.mPageWidthMM = 215.9f;				// 8.5" wide paper
	spec.mPageVBorderMM = 8.0f;				// vertical border
	spec.mDotRadiusMM = 0.28f;				// guess for dot radius
	spec.mVerticalDotPitchMM = 0.403175f;	// 0.0159" vertical pitch
	spec.mbBit0Top = true;
	spec.mNumPins = 7;
	return spec;
}

bool ATDevicePrinter1029::IsSupportedDeviceId(uint8 id) const {
	// The 1029 responds to both P1: and P6:.
	return id == 0x40 || id == 0x45;
}

bool ATDevicePrinter1029::IsSupportedOrientation(uint8 aux1) const {
	// support all orientation values
	return aux1 == 0x4E || aux1 == 0x53;
}

uint8 ATDevicePrinter1029::GetWidthForOrientation(uint8 aux1) const {
	// treat all orientation values as normal
	return 40;
}

void ATDevicePrinter1029::GetStatusFrameInternal(uint8 frame[4]) {
	// The 1029 returns a completely fixed status frame.
	frame[0] = 0;
	frame[1] = 0;
	frame[2] = 20;		// 1029 timeout is 20*64 vblanks
	frame[3] = 0x29;

	// Rather unusually, the 1029 also resets all of its internal state when processing
	// a Status command. This makes it sensitive to whether LPRINT or PRINT is used,
	// since in the former case BASIC opens and closes P: for each line. This means that
	// settings from ESC codes, like density and European enables, get reset.

	ResetState();
}

void ATDevicePrinter1029::HandleFrameInternal(uint8 orientation, uint8 *buf, uint32 len, bool graphics) {
	// normal: mask off bit 7, and convert 00-1F/60/7B/7D-7F to space
	// european: just mask off bit 7
	//
	// Unlike the 1025, the 1029 processes all escape codes anywhere in a line:
	//	ESC CTRL+O		Print 10 CPI
	//	ESC CTRL+N		Print 5 CPI
	//	ESC 6			Set to 6 LPI
	//	ESC 9			Set to 9 LPI
	//	ESC CTRL+W		enable european characters
	//	ESC CTRL+X		ignore european characters
	//	ESC CTRL+Y		enable underlining
	//	ESC CTRL+Z		disable underlining
	//	ESC CTRL+A		begin bit image graphics
	//	ESC ESC			treated as ESC
	//	ESC EOL			treated as EOL
	//
	// Regardless of whether the escape is valid, the character is omitted
	// from the line buffer and escape modes are cleared after processing an
	// escape. EOL takes priority over ESC and escape is cleared after processing
	// an EOL. Escape sequences are not processed while bit image graphics are
	// active.

	for(uint32 i = 0; i < len; ++i) {
		uint8 c = buf[i];

		if (mBitImageLength) {
			--mBitImageLength;
			
			// Bit 7 is ignored; bit 6 is top and bit 0 is bottom. This means we
			// need to bit reverse the data.
			uint8 pat = c & 0x7F;

			pat = ((pat & 0x70) >> 4) + (pat & 0x08) + ((pat & 0x07) << 4);
			pat = ((pat & 0x44) >> 2) + (pat & 0x2A) + ((pat & 0x11) << 2);

			mDotBuffer[mDotColumn] = pat;

			if (++mDotColumn >= 480)
				FlushLine(graphics);
		} else {
			// check for EOL
			if (c == 0x9B) {
				// flush line buffer
				if (mDotColumn > 0 || !mbLastLineContinued)
					FlushLine(graphics);

				mbLastLineContinued = false;

				// clear escape mode in case we processed Esc+EOL
				mEscapeState = EscapeState::None;

				// underlining turns off after EOL is processed
				mbUnderline = false;

				// ignore remainder of data frame
				break;
			}

			// check for ESC -- note that ESC ESC is equivalent to ESC
			switch(mEscapeState) {
				case EscapeState::None:
					if (c == 0x1B) {
						mEscapeState = EscapeState::Esc;
						continue;
					}
					break;

				case EscapeState::Esc:
					// Bit 7 is ignored on the ESC code.
					mEscapeState = EscapeState::None;

					switch(c & 0x7F) {
						case 0x0F:	// Ctrl+O
							mbElongated = false;
							break;

						case 0x0E:	// Ctrl+N
							mbElongated = true;
							break;

						case 0x17:	// Ctrl+W
							mbEuropeanChars = true;
							break;

						case 0x18:	// Ctrl+X
							mbEuropeanChars = false;
							break;

						case 0x19:	// Ctrl+Y
							mbUnderline = true;
							break;

						case 0x1A:	// Ctrl+Z
							mbUnderline = false;
							break;

						case 0x1B:	// Esc Esc -> same as Esc
							mEscapeState = EscapeState::Esc;
							break;

						case 0x36:	// 6
							mbDenseLines = false;
							break;

						case 0x39:	// 9
							mbDenseLines = true;
							break;

						case 0x41:	// A -> begin bit image mode
							mEscapeState = EscapeState::EscA1;
							break;

						default:
							// Any other Esc sequence is ignored
							break;
					}

					continue;

				case EscapeState::EscA1:
					mBitImageLengthPending = c;
					mEscapeState = EscapeState::EscA2;
					continue;

				case EscapeState::EscA2:
					mBitImageLength = ((uint32)mBitImageLengthPending << 8) + c;
					mEscapeState = EscapeState::None;
					continue;
			}

			// Printable character -- put in line buffer.
			//
			// The 1029's character set is the international ATASCII set. In European
			// mode, the entire 7-bit character set is accessible except for ESC, which
			// cannot be printed (ESC ESC is a no-op). When European characters are
			// disabled, the characters that differ are suppressed -- 00-1F are ignored
			// and 60/7B/7D-7F are blanked.
			//
			// Bit 7 is ignored.

			c &= 0x7F;

			if (!mbEuropeanChars) {
				if (c < 0x20)
					continue;

				if (c == 0x60 || c == 0x7B || c >= 0x7D)
					c = 0x20;
			}

			// Check if we have room in the buffer. If there isn't enough room to add
			// the character itself (5 or 10 columns), flush the line before processing
			// the character. However, it's OK if the spacing doesn't fit.
			uint32 columnsNeeded = mbElongated ? 10 : 5;

			if (mDotColumn + columnsNeeded > 480)
				FlushLine(graphics);

			// We are now guaranteed to have room in the line buffer. Add the character
			// to the text buffer if we're doing text printing; we estimate the text
			// column from the dot column to handle bit image graphics and elongated
			// spacing.
			const uint32 textColumn = mDotColumn / 6;
			mRawLineBuffer[textColumn] = c;
			mUnicodeLineBuffer[textColumn] = kATATASCIITables.mATASCIIToUnicode[1][c];

			// If underlining is enabled, draw an underline under the character and
			// its spacing, and mark that we had an underline. Note that this can extend
			// up to 482 columns, not 480, since the spacing is allowed to extend past
			// 480, and then gets printed when underlining is active.
			if (mbUnderline) {
				mbAnyUnderlines = true;

				const uint32 underCols = mbElongated ? 12 : 6;
				uint32 col = mDotColumn;

				for(uint32 i = 0; i < underCols; ++i)
					mUnderlineBuffer[col++] = 0x02;
			}

			// Add the character pattern to the dot buffer. If in elongated mode, double
			// the width by repeating each column.
			const uint8 *chdat = &g_ATPrinterFont1029.mColumns[5 * c];

			for(int i=0; i<5; ++i) {
				const uint8 pat = *chdat++;

				mDotBuffer[mDotColumn++] = pat;

				if (mbElongated)
					mDotBuffer[mDotColumn++] = pat;
			}

			// Add the spacing (which may overrun), and then flush the line if it is
			// exactly or over-full.
			mDotColumn += mbElongated ? 2 : 1;

			if (mDotColumn >= 480) {
				FlushLine(graphics);
				mbLastLineContinued = true;
			}
		}
	}
}

void ATDevicePrinter1029::FlushLine(bool graphics) {
	if (graphics) {
		// The 1029 prints at 50 characters/second and 10 characters/inch, so the ratio is
		// 0.2 seconds/inch.
		static constexpr float kPrintDelayPerMM = 0.2f / 25.4f;

		// ~0.362ms/dot based on audio recordings
		static constexpr float kPrintDelayPerDot = 0.000362f;

		static constexpr float kLeftMarginMM = 0.25f * 25.4f;
		RenderedLine *rl = BeginRenderLine(480);

		static constexpr float xStep = 8.0f / 480.0f * 25.4f;
		if (rl) {
			float x = kLeftMarginMM;

			for(float& v : rl->mPositions) {
				v = x;
				x += xStep;
			}

			memcpy(rl->mDotPatterns.data(), mDotBuffer, 480);

			const uint32 len = rl->TrimZeroAtEnd();
			const float advanceDuration = kPrintDelayPerMM * xStep * (float)len;
			const float retractDuration = 0.5f * advanceDuration;

			const RenderLineParams params {
				// guessing that carriage returns in 60% of the time
				.mPrintDelay = advanceDuration + retractDuration,
				.mPrintDelayXHome = kLeftMarginMM,
				.mPrintDelayPerX = kPrintDelayPerMM,
				.mPrintDelayPerY = kPrintDelayPerDot,
				.mPostDelay = 0.3f,
				.mLineAdvance = mbDenseLines || mbAnyUnderlines ? 25.4f / 9.0f : 25.4f / 6.0f,
				.mHeadAdvanceSampleId = kATAudioSampleId_Printer1029Platen,
				.mHeadAdvanceSoundDelay = 0,
				.mHeadAdvanceSoundDuration = advanceDuration,
				.mHeadRetractSampleId = kATAudioSampleId_Printer1029Retract,
				.mHeadRetractSoundDelay = advanceDuration,
				.mHeadHomeSampleId = kATAudioSampleId_Printer1029Home,
				.mHeadHomeSoundDelay = advanceDuration + retractDuration
			};

			EndRenderLine(params);
		}

		if (mbAnyUnderlines) {
			// The underlining buffer has two extra columns due to the possibility
			// of an elongated character overhang.
			rl = BeginRenderLine(482);
			if (rl) {
				float x = kLeftMarginMM;

				for(float& v : rl->mPositions) {
					v = x;
					x += xStep;
				}

				memcpy(rl->mDotPatterns.data(), mUnderlineBuffer, 482);

				const uint32 len = rl->TrimZeroAtEnd();
				const float advanceDuration = kPrintDelayPerMM * (float)len;
				const float retractDuration = advanceDuration * 0.5f;

				const RenderLineParams underParams {
					.mPrintDelay = advanceDuration + retractDuration,
					.mPrintDelayXHome = kLeftMarginMM,
					.mPrintDelayPerX = kPrintDelayPerMM,
					.mPrintDelayPerY = kPrintDelayPerDot,
					.mPostDelay = 0.3f,
					.mLineAdvance = mbDenseLines ? 0 : 25.4f * (1.0f/6.0f - 1.0f/9.0f),
					.mHeadAdvanceSampleId = kATAudioSampleId_Printer1029Platen,
					.mHeadAdvanceSoundDelay = 0,
					.mHeadAdvanceSoundDuration = advanceDuration,
					.mHeadRetractSampleId = kATAudioSampleId_Printer1029Retract,
					.mHeadRetractSoundDelay = advanceDuration,
					.mHeadHomeSampleId = kATAudioSampleId_Printer1029Home,
					.mHeadHomeSoundDelay = advanceDuration + retractDuration
				};

				EndRenderLine(underParams);
			}
		}
	} else {
		// trim off any spaces at the end, just to make the printer output nicer
		uint32 textColumns = 80;

		while(textColumns && mRawLineBuffer[textColumns - 1] == 0x20)
			--textColumns;

		mRawLineBuffer[textColumns] = 0x9B;
		mUnicodeLineBuffer[textColumns] = L'\n';

		IATPrinterOutput *output = GetTextOutput();
		if (output) {
			if (output->WantUnicode())
				output->WriteUnicode(mUnicodeLineBuffer, textColumns + 1);
			else
				output->WriteRaw(mRawLineBuffer, textColumns + 1);
		}
	}

	mDotColumn = 0;

	// Printing out a line ends bit image graphics. It does NOT end underlining
	// unless an actual EOL is processed.
	mBitImageLength = 0;

	ClearLineBuffer();
}

void ATDevicePrinter1029::ResetState() {
	// According to the Atari 1029 Printer Field Service Manual, page 3-3, the default power-on
	// state is: 10 CPI, 6 LPI, 80 chars/line, European chars disabled.
	mEscapeState = EscapeState::None;
	mbEuropeanChars = false;
	mbElongated = false;
	mbUnderline = false;
	mbDenseLines = false;
	mbLastLineContinued = false;

	// The line buffer is also flushed. This is important with OS-B, which pads any leftover
	// characters in the printer buffer with spaces instead of EOLs.
	mDotColumn = 0;
	mbAnyUnderlines = false;

	ClearLineBuffer();
}

void ATDevicePrinter1029::ClearLineBuffer() {
	std::fill(std::begin(mDotBuffer), std::end(mDotBuffer), 0);
	std::fill(std::begin(mUnderlineBuffer), std::end(mUnderlineBuffer), 0);
	std::fill(std::begin(mRawLineBuffer), std::end(mRawLineBuffer), 0x20);
	std::fill(std::begin(mUnicodeLineBuffer), std::end(mUnicodeLineBuffer), 0x20);
}

////////////////////////////////////////////////////////////////////////////////

void ATCreateDevicePrinter825(const ATPropertySet& pset, IATDevice **dev) {
	vdrefptr<ATDevicePrinter825> p(new ATDevicePrinter825);

	*dev = p.release();
}

extern const ATDeviceDefinition g_ATDeviceDefPrinter825 = { "825", nullptr, L"825 80-Column Printer", ATCreateDevicePrinter825 };

ATDevicePrinter825::ATDevicePrinter825() {
	SetSaveStateAgnostic();
}

ATDevicePrinter825::~ATDevicePrinter825() {
}

void ATDevicePrinter825::GetDeviceInfo(ATDeviceInfo& info) {
	info.mpDef = &g_ATDeviceDefPrinter825;
}

void ATDevicePrinter825::Init() {
	ATPrinterGraphicsSpec spec {};
	spec.mPageWidthMM = 215.9f;				// 8.5" wide paper
	spec.mPageVBorderMM = 8.0f;				// vertical border
	spec.mDotRadiusMM = 0.22f;				// guess for dot radius
	spec.mVerticalDotPitchMM = 0.403175f;	// 0.0159" vertical pitch (guess)
	spec.mbBit0Top = true;
	spec.mNumPins = 9;
	mpGraphicsOutput = GetService<IATPrinterOutputManager>()->CreatePrinterGraphicalOutput(g_ATDeviceDefPrinter825.mpName, spec);
}

void ATDevicePrinter825::Shutdown() {
	mpGraphicsOutput = nullptr;
}

void ATDevicePrinter825::ColdReset() {
	mCurrentDensity = Density::Normal;
	mEscapeState = EscapeState::None;
	mDotColumn = 0;
	mbLastLineContinued = false;
	ApplyDensity();
}

bool ATDevicePrinter825::WantUnicode() const {
	return false;
}

void ATDevicePrinter825::WriteRaw(const uint8 *buf, size_t len) {
	while(len--) {
		uint8 ch = *buf++ & 0x7F;

		if (mEscapeState == EscapeState::Esc) {
			switch(ch) {
				case 0x01:	// 1 dot space
				case 0x02:	// 2 dot space
				case 0x03:	// 3 dot space
				case 0x04:	// 4 dot space
				case 0x05:	// 5 dot space
				case 0x06:	// 6 dot space
					InsertSpace(ch);
					break;

				case 0x0A:	// full reverse line feed
					if (mpGraphicsOutput)
						mpGraphicsOutput->FeedPaper(-25.4f / 6.0f);
					break;

				case 0x0E:	// start elongated printing
					mbElongated = true;
					break;
				case 0x0F:	// stop elongated printing
					mbElongated = false;
					break;

				case 0x11:	// select proportionally spaced character set
					SetDensity(Density::Proportional);
					break;

				case 0x13:	// select 10-cpi monospaced character set
					SetDensity(Density::Normal);
					break;

				case 0x14:	// select 16.7-cpi condensed character set
					SetDensity(Density::Condensed);
					break;

				case 0x1C:	// feed half-line forward
					if (mpGraphicsOutput)
						mpGraphicsOutput->FeedPaper(25.4f / 12.0f);
					break;

				case 0x1E:	// feed half-line reverse
					if (mpGraphicsOutput)
						mpGraphicsOutput->FeedPaper(-25.4f / 12.0f);
					break;
			}

			mEscapeState = EscapeState::None;
			continue;
		}
		
		if (mEscapeState == EscapeState::Bs) {
			if (mDotColumn <= ch)
				mDotColumn = 0;
			else
				mDotColumn -= ch;

			mEscapeState = EscapeState::None;
			continue;
		}

		if (ch < 0x20) {
			switch(ch) {
				case 0x08:	// BS: backspace
					mEscapeState = EscapeState::Bs;
					break;

				case 0x0A:	// LF: forward line feed
					if (mpGraphicsOutput)
						mpGraphicsOutput->FeedPaper(25.4f / 6.0f);
					break;

				case 0x0D:	// CR: carriage return
					if (mpGraphicsOutput && (!mbLastLineContinued || mDotColumn > 0))
						mpGraphicsOutput->FeedPaper(25.4f / 6.0f);

					mDotColumn = 0;
					mbElongated = false;
					break;

				case 0x0E:	// SO: stop underlining
					mbUnderline = false;
					break;

				case 0x0F:	// SI: start underlining
					mbUnderline = true;
					break;

				case 0x1B:	// ESC
					mEscapeState = EscapeState::Esc;
					break;
			}
		} else if (ch < 0x7F) {
			if (mLineDensity != Density::Proportional) {
				const uint8 *charDat = &g_ATPrinterFont825Mono.mColumns[(ch - 0x20) * g_ATPrinterFont825Mono.kWidth];

				if (mbElongated) {
					uint8 prev = 0;

					for(int i = 0; i < 7; ++i) {
						uint8 pat = charDat[i];

						PrintColumn(pat | prev);
						++mDotColumn;

						prev = pat;
					}

					PrintColumn(prev);

					// normal is 15+5=20 cols, condensed is 15+3=18 cols
					InsertSpace(mLineDensity == Density::Condensed ? 3 : 5);
				} else {
					for(int i = 0; i < 7; ++i)
						PrintColumn(charDat[i]);

					// normal is 7+3=10 cols, condensed is 7+2=9 cols
					InsertSpace(mLineDensity == Density::Condensed ? 2 : 3);
				}
			} else {
				const uint16 *charDat = &g_ATPrinterFont825Prop.mColumns[(ch - 0x20) * g_ATPrinterFont825Prop.kWidth];
				const int charWidth = g_ATPrinterFont825Prop.mAdvanceWidths[ch - 0x20];

				if (mbElongated) {
					uint16 prev = 0;

					if (ch == 0x67) {
						// Pretty much every elongated prop character can be generated by spacing out the
						// standard prop character and ORing one column over... except for the lowercase g.
						// For some reason, its descender is one column short of what the algorithm gives.
						// This has been double checked both in the Centronics 737 manual and actual output
						// from an 825 printer. The reason for this is unknown.
						//
						// ..........
						// ..........
						// ..########
						// .##.....##
						// .##.....##
						// .##.....##
						// ..########
						// ........##
						// .#######?.
						//

						static constexpr uint16 kLowerG[] {
							0b000000000,
							0b100111000,
							0b101111100,
							0b101000100,
							0b101000100,
							0b101000100,
							0b101000100,
							0b101000100,
							0b011111100,
							0b011111100,
						};

						for(uint16 pat : kLowerG) {
							PrintColumn(pat);
							++mDotColumn;
						}

						PrintColumn(0);
					} else {
						for(int i = 0; i < charWidth; ++i) {
							uint16 pat = charDat[i];

							PrintColumn(pat | prev);
							++mDotColumn;

							prev = pat;
						}

						PrintColumn(prev);
					}

					InsertSpace(3);
				} else {
					for(int i = 0; i < charWidth; ++i)
						PrintColumn(charDat[i]);

					InsertSpace(2);
				}				
			}
		}
	}
}

void ATDevicePrinter825::SetDensity(Density density) {
	mCurrentDensity = density;

	if (mDotColumn == 0 || (mCurrentDensity != Density::Normal && mLineDensity != Density::Normal))
		ApplyDensity();
}

void ATDevicePrinter825::InsertSpace(uint32 width) {
	while(width--)
		PrintColumn(0);

	if (mDotColumn > mMaxColumns)
		ForceEOL();
}

void ATDevicePrinter825::PrintColumn(uint32 pins) {
	if (mbUnderline) {
		pins &= 0xFF;

		if (!(mDotColumn & 1))
			pins |= 0x100;
	}

	if (mpGraphicsOutput && pins) {
		static constexpr float kLeftMarginMM = 8.0f;
		mpGraphicsOutput->Print(kLeftMarginMM + (float)mDotColumn * mAdvanceMMPerColumn, pins);
	}

	++mDotColumn;

}

void ATDevicePrinter825::ForceEOL() {
	if (mpGraphicsOutput)
		mpGraphicsOutput->FeedPaper(25.4f / 6.0f);

	mDotColumn = 0;
	mbElongated = false;
	mbLastLineContinued = true;
	ApplyDensity();
}

void ATDevicePrinter825::ApplyDensity() {
	mLineDensity = mCurrentDensity;

	// 1185 columns is documented in the Centronics 737 and Atari 825 manuals.
	// 790 columns for 10 cpi mode is a guess.
	if (mLineDensity == Density::Normal) {
		mMaxColumns = 790;

		// 100 dots/inch
		mAdvanceMMPerColumn = 25.4 / 100.0f;
	} else {
		mMaxColumns = 1185;

		// 150 dots/inch
		mAdvanceMMPerColumn = 25.4 / 150.0f;
	}
}
