//	Altirra - Atari 800/800XL emulator
//	Copyright (C) 2024 Avery Lee
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

#ifndef f_AT_PRINTER_H
#define f_AT_PRINTER_H

#include <at/atcore/audiomixer.h>
#include <at/atcore/deviceimpl.h>
#include <at/atcore/devicecio.h>
#include <at/atcore/deviceparent.h>
#include <at/atcore/deviceparentimpl.h>
#include <at/atcore/deviceprinter.h>
#include <at/atcore/devicesio.h>
#include <at/atcore/scheduler.h>
#include "printerbase.h"
#include "printertypes.h"

struct ATPrinterFontDesc;

class ATDevicePrinterBase
	: public ATDevice
	, public IATDeviceCIO
	, public IATDeviceSIO
	, public IATDeviceParent
	, public IATSchedulerCallback
{
public:
	ATDevicePrinterBase(bool lineBuffered, bool accurateTimingSupported, bool textSupported, bool graphicsSupported);
	virtual ~ATDevicePrinterBase();

	void *AsInterface(uint32 id) override;

	void Init() override;
	void Shutdown() override;
	void GetSettings(ATPropertySet& settings) override;
	bool SetSettings(const ATPropertySet& settings) override;
	void WarmReset() override;
	void ColdReset() override;

public:	// IATDeviceCIO
	void InitCIO(IATDeviceCIOManager *mgr) override;
	void GetCIODevices(IATDeviceCIODeviceList& deviceList) const override;
	sint32 OnCIOOpen(int channel, uint8 deviceNo, uint8 aux1, uint8 aux2, const uint8 *filename) override;
	sint32 OnCIOClose(int channel, uint8 deviceNo) override;
	sint32 OnCIOGetBytes(int channel, uint8 deviceNo, void *buf, uint32 len, uint32& actual) override;
	sint32 OnCIOPutBytes(int channel, uint8 deviceNo, const void *buf, uint32 len, uint32& actual) override;
	sint32 OnCIOGetStatus(int channel, uint8 deviceNo, uint8 statusbuf[4]) override;
	sint32 OnCIOSpecial(int channel, uint8 deviceNo, uint8 cmd, uint16 bufadr, uint16 buflen, uint8 aux[6]) override;
	void OnCIOAbortAsync() override;

public:	// IATDeviceSIO
	void InitSIO(IATDeviceSIOManager *mgr) override;
	CmdResponse OnSerialBeginCommand(const ATDeviceSIOCommand& cmd) override;
	void OnSerialAbortCommand() override;
	void OnSerialReceiveComplete(uint32 id, const void *data, uint32 len, bool checksumOK) override;
	void OnSerialFence(uint32 id) override;
	CmdResponse OnSerialAccelCommand(const ATDeviceSIORequest& request) override;

public:	// IATDeviceParent
	IATDeviceBus *GetDeviceBusById(sint32 id) override;
	sint32 GetDeviceBusIdByIndex(uint32 index) override;

public:	// IATSchedulerCallback
	void OnScheduledEvent(uint32 id) override;

protected:
	void BeginGraphics();
	void EndGraphics();

	virtual void InitSIOReceiveTimeout();
	virtual void OnCreatedGraphicalOutput();
	virtual ATPrinterGraphicsSpec GetGraphicsSpec() const;

	void GetStatusFrame(uint8 frame[4]);
	virtual void GetStatusFrameInternal(uint8 frame[4]) = 0;
	virtual bool IsSupportedDeviceId(uint8 id) const = 0;
	virtual bool IsSupportedOrientation(uint8 aux1) const = 0;
	virtual uint8 GetWidthForOrientation(uint8 aux1) const = 0;

	int FlushCIOBuffer(bool sideways);

	void HandleFrame(const uint8 *data, uint32 len, bool fromCIO);

	VDStringW GetPrinterOutputName();

	IATPrinterOutput *GetTextOutput();
	virtual void HandleFrameInternal(uint8 orientation, uint8 *buf, uint32 len, bool graphics) = 0;

	struct RenderLineParams {
		float mPreDelay = 0;
		float mPrintDelay = 0;
		float mPrintDelayXHome = 0;
		float mPrintDelayPerX = 0;
		float mPrintDelayPerY = 0;
		float mPostDelay = 0;
		float mLineAdvance = 0;

		ATAudioSampleId mHeadAdvanceSampleId {};
		float mHeadAdvanceSoundDelay = 0;
		float mHeadAdvanceSoundDuration = 0;

		ATAudioSampleId mHeadRetractSampleId {};
		float mHeadRetractSoundDelay = 0;

		ATAudioSampleId mHeadHomeSampleId {};
		float mHeadHomeSoundDelay = 0;
	};

	struct RenderedLine {
		vdfastvector<uint8> mDotPatterns;
		vdfastvector<float> mPositions;
		RenderLineParams mParams;

		uint32 GetNonZeroLength() const;
		uint32 TrimZeroAtEnd();
	};

	RenderedLine *BeginRenderLine(uint32 width);
	void EndRenderLine(const RenderLineParams& params);
	void RenderLineWithFont(const ATPrinterFontDesc& desc, const uint8 *fontData, uint8 *charData, uint32 n, float x, float xStep, float xSpacing, bool bold, const RenderLineParams& param);
	void FlushRenderedLines(bool fromCIO);
	void BeginAsyncPrinting();
	void EndAsyncPrinting();

	bool IsPrinting() const;
	void BeginPrinting(bool fromCIO);
	void ContinuePrinting();
	void CompletePrinting();
	void CancelPrinting();

	static constexpr uint32 kEventId_ContinuePrinting = 1;
	static constexpr uint32 kEventId_SubclassFirst = 16;

	IATDeviceCIOManager *mpCIOMgr = nullptr;
	IATDeviceSIOManager *mpSIOMgr = nullptr;
	vdrefptr<IATDeviceSIOInterface> mpSIOInterface;
	vdrefptr<IATPrinterOutput> mpOutput;
	vdrefptr<IATPrinterGraphicalOutput> mpPrinterGraphicalOutput;
	ATScheduler *mpScheduler = nullptr;
	ATEvent *mpEventContinuePrinting = nullptr;

	const bool mbLineBuffered;
	const bool mbAccurateTimingSupported;
	const bool mbGraphicsSupported;
	const bool mbTextSupported;

	bool mbGraphicsEnabled = false;
	bool mbAccurateTimingEnabled = false;
	bool mbSoundEnabled = false;
	bool mbPrintSIOCommandPending = false;

	uint8		mLastOrientationByte = 0xFF;

	uint8		mCIOBuffer[40] {};
	uint8		mCIOBufferIndex = 0;

	// We may need up to 4 lines -- two to handle line wrapping, and another
	// two for underlining.
	RenderedLine mRenderedLines[4];
	uint32 mRenderedLinesQueued = 0;
	
	uint32 mRenderedLinesPrinted = 0;

	enum class PrintState : uint8 {
		PreDelay,
		StartPrinting,
		PrintColumns,
		PostDelay,
		LineAdvance
	} mPrintState = PrintState::PreDelay;

	uint32 mPrintNextColumn = 0;
	uint32 mPrintLineStartTime = 0;
	bool mbPrintingFromCIO = false;
	bool mbAsyncPrinting = false;

	ATPrinterSoundSource mPrinterSoundSource;
	ATDeviceBusSingleChild mParallelBus;
};

class ATDevicePrinter final : public ATDevicePrinterBase {
public:
	ATDevicePrinter();
	~ATDevicePrinter();

	void GetDeviceInfo(ATDeviceInfo& info) override;
	void GetSettings(ATPropertySet& settings) override;
	bool SetSettings(const ATPropertySet& settings) override;

	bool IsSupportedDeviceId(uint8 id) const override;
	bool IsSupportedOrientation(uint8 aux1) const override;
	uint8 GetWidthForOrientation(uint8 aux1) const override;
	void GetStatusFrameInternal(uint8 frame[4]) override;
	void HandleFrameInternal(uint8 orientation, uint8 *buf, uint32 len, bool graphics) override;

private:
	ATPrinterPortTranslationMode mTranslationMode = ATPrinterPortTranslationMode::Default;
};

class ATDevicePrinter820 final : public ATDevicePrinterBase {
public:
	ATDevicePrinter820();
	~ATDevicePrinter820();

	void GetDeviceInfo(ATDeviceInfo& info) override;

	ATPrinterGraphicsSpec GetGraphicsSpec() const override;
	bool IsSupportedDeviceId(uint8 id) const override;
	bool IsSupportedOrientation(uint8 aux1) const override;
	uint8 GetWidthForOrientation(uint8 aux1) const override;
	void GetStatusFrameInternal(uint8 frame[4]) override;
	void HandleFrameInternal(uint8 orientation, uint8 *buf, uint32 len, bool graphics) override;
};

class ATDevicePrinter1025 final : public ATDevicePrinterBase {
public:
	ATDevicePrinter1025();
	~ATDevicePrinter1025();

	void GetDeviceInfo(ATDeviceInfo& info) override;
	void ColdReset() override;

	void InitSIOReceiveTimeout() override;
	ATPrinterGraphicsSpec GetGraphicsSpec() const override;
	bool IsSupportedDeviceId(uint8 id) const override;
	bool IsSupportedOrientation(uint8 aux1) const override;
	uint8 GetWidthForOrientation(uint8 aux1) const override;
	void GetStatusFrameInternal(uint8 frame[4]) override;
	void HandleFrameInternal(uint8 orientation, uint8 *buf, uint32 len, bool graphics) override;

private:
	void FlushLine(bool graphics);
	void UpdateLineLength();
	void ResetState();

	uint8 mColumn = 0;
	uint8 mLineLength = 0;

	enum class CharDensity : uint8 {
		Cpi5,
		Cpi10,
		Cpi16_5,
	} mCharDensity = CharDensity::Cpi10;

	bool mbEscapeAtStart = false;
	bool mbEscape = false;
	bool mbEuropeanChars = false;
	bool mbDenseLines = false;
	bool mbShortLine = false;
	bool mbLastLineContinued = false;

	uint8 mRawLineBuffer[133] {};
	wchar_t mUnicodeLineBuffer[133] {};
};

class ATDevicePrinter1029 final : public ATDevicePrinterBase {
public:
	ATDevicePrinter1029();
	~ATDevicePrinter1029();

	void GetDeviceInfo(ATDeviceInfo& info) override;
	void ColdReset() override;

	ATPrinterGraphicsSpec GetGraphicsSpec() const override;
	bool IsSupportedDeviceId(uint8 id) const override;
	bool IsSupportedOrientation(uint8 aux1) const override;
	uint8 GetWidthForOrientation(uint8 aux1) const override;
	void GetStatusFrameInternal(uint8 frame[4]) override;
	void HandleFrameInternal(uint8 orientation, uint8 *buf, uint32 len, bool graphics) override;

private:
	void FlushLine(bool graphics);
	void ResetState();
	void ClearLineBuffer();

	uint32 mDotColumn = 0;

	enum class EscapeState : uint8 {
		None,
		Esc,
		EscA1,
		EscA2
	};

	EscapeState mEscapeState = EscapeState::None;

	bool mbElongated = false;
	bool mbEuropeanChars = false;
	bool mbDenseLines = false;
	bool mbUnderline = false;
	bool mbLastLineContinued = false;
	uint32 mBitImageLength = 0;
	uint8 mBitImageLengthPending = 0;

	bool mbAnyUnderlines = false;

	uint8 mRawLineBuffer[81] {};
	wchar_t mUnicodeLineBuffer[81] {};

	// Unlike the other printers, the 1029 pre-rasterizes everything into a
	// 482x7 image buffer, and so it is able to mix character densities and
	// graphics in ways that the 1025 can't.
	//
	// Note that the underlining buffer needs two extra columns. This is
	// because the 1029 allows the post-character spacing to run over the
	// right margin, but those columns can then get printed when underlining
	// is enabled. With an elongated character this results in two extra
	// columns being printed.
	uint8 mDotBuffer[480] {};
	uint8 mUnderlineBuffer[482] {};
};

class ATDevicePrinter825 final : public ATDeviceT<IATPrinterOutput> {
public:
	ATDevicePrinter825();
	~ATDevicePrinter825();

	void GetDeviceInfo(ATDeviceInfo& info) override;
	void Init() override;
	void Shutdown() override;
	void ColdReset() override;

public:	// IATPrinterOutput
	bool WantUnicode() const override;
	void WriteRaw(const uint8 *buf, size_t len) override;

private:
	enum class Density : uint8 {
		Normal,
		Condensed,
		Proportional
	};

	enum class EscapeState : uint8 {
		None,
		Esc,
		Bs,
	};

	void SetDensity(Density density);
	void InsertSpace(uint32 width);
	void PrintColumn(uint32 pins);
	void ForceEOL();
	void ApplyDensity();

	uint32 mDotColumn = 0;
	uint32 mMaxColumns = 0;
	float mAdvanceMMPerColumn = 0;
	Density mLineDensity = Density::Normal;
	Density mCurrentDensity = Density::Normal;

	EscapeState mEscapeState = EscapeState::None;

	bool mbElongated = false;
	bool mbUnderline = false;
	bool mbLastLineContinued = false;

	vdrefptr<IATPrinterGraphicalOutput> mpGraphicsOutput;
};

#endif
