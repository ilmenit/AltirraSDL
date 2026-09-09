#ifndef f_AT_DEBUGDISPLAY_H
#define f_AT_DEBUGDISPLAY_H

#include <vd2/Kasumi/pixmap.h>
#include <vd2/Kasumi/pixmaputils.h>

class IVDVideoDisplay;
class ATAnticEmulator;
class ATGTIAEmulator;
class ATMemoryManager;

class ATDebugDisplay {
	ATDebugDisplay(const ATDebugDisplay&);
	ATDebugDisplay& operator=(const ATDebugDisplay&);
public:
	ATDebugDisplay();
	~ATDebugDisplay();

	void Init(ATMemoryManager *memory, ATAnticEmulator *antic, ATGTIAEmulator *gtia, IVDVideoDisplay *display);
	void Shutdown();

	enum Mode {
		kMode_AnticHistory,
		kMode_AnticHistoryStart,
		kModeCount
	};

	enum class PaletteMode : uint8{
		CurrentRegisters,
		Analysis,
		ColorTrace
	};

	Mode GetMode() const { return mMode; }
	void SetMode(Mode mode) { mMode = mode; }

	PaletteMode GetPaletteMode() const { return mPaletteMode; }
	void SetPaletteMode(PaletteMode mode) { mPaletteMode = mode; }

	void SetDLAddrOverride(sint32 addr) { mDLAddrOverride = addr; }
	void SetPFAddrOverride(sint32 addr) { mPFAddrOverride = addr; }

	// These accessors are used by the SDL3 presentation layer. Keep the
	// renderer state private so the platform frontends do not depend on its
	// implementation layout.
	const VDPixmapBuffer& GetDisplayBuffer() const { return mDisplayBuffer; }
	const uint32 *GetPalette() const { return mPalette; }
	sint32 GetDLAddrOverride() const { return mDLAddrOverride; }
	sint32 GetPFAddrOverride() const { return mPFAddrOverride; }

	bool GetPMGraphicsEnabled() const { return mbPMGraphicsEnabled; }
	void SetPMGraphicsEnabled(bool enable) { mbPMGraphicsEnabled = enable; }

	void Update();

private:
	static constexpr uint8 kCodeBAK = 0b000;
	static constexpr uint8 kCodePF0 = 0b100;
	static constexpr uint8 kCodePF1 = 0b101;
	static constexpr uint8 kCodePF2 = 0b110;
	static constexpr uint8 kCodePF3 = 0b111;

	struct DecodeState {
		uint8 mMode = 0;
		uint8 mDctr = 0;
		uint8 mRowBytes = 0;
		uint8 mCharRowInv = 0;
		uint8 mCharBlankMask = 0;
		uint8 mCharInvMask = 0;
	};

	void DecodeMode2 (const DecodeState& decodeState);
	void DecodeMode3 (const DecodeState& decodeState);
	void DecodeMode45(const DecodeState& decodeState);
	void DecodeMode67(const DecodeState& decodeState);
	void DecodeMode8 (const DecodeState& decodeState);
	void DecodeMode9 (const DecodeState& decodeState);
	void DecodeModeA (const DecodeState& decodeState);
	void DecodeModeBC(const DecodeState& decodeState);
	void DecodeModeDE(const DecodeState& decodeState);
	void DecodeModeF (const DecodeState& decodeState);

	void ConvertDecodeToPriorityLores();
	void ConvertDecodeToPriorityMode10();
	void ConvertDecodeToPriorityHires();

	struct RenderState {
		// ANTIC playfield decodes to $22-DD (188 color clocks). To make things
		// a little easier, we decode $20-DF (192 color clocks).
		static constexpr size_t kRenderColorClocks = 192;

		uint8 *mpDst = nullptr;
		const uint8 *mpPriTable = nullptr;

		uint8 mColorTable[24];
	};

	void RenderLores(const RenderState& renderState);
	void RenderHires(const RenderState& renderState);
	void RenderMode9(const RenderState& renderState);
	void RenderMode11(const RenderState& renderState);

	ATMemoryManager *mpMemory;
	ATAnticEmulator *mpAntic;
	ATGTIAEmulator *mpGTIA;
	IVDVideoDisplay *mpDisplay;

	Mode	mMode;
	PaletteMode	mPaletteMode;
	sint32	mDLAddrOverride;
	sint32	mPFAddrOverride;
	bool mbPMGraphicsEnabled = true;

	VDPixmapBuffer mDisplayBuffer;

	uint8	mFontHi[2048];
	uint8	mFontLo[1024];
	uint32	mPalette[256];

	uint8	mAnticLineBuffer[48] {};

	// ANTIC AN0-2 output, starting at pos $20
	uint8	mAnticDecodeBuffer[228 + 12] {};

	// GTIA internal priority decode, starting at pos 0. 34-221 are displayed.
	// 0-31 and 222-254 are needed to allow for P/M graphics rendering.
	uint8	mAnticPriorityBuffer[256] {};

	uint8	mPriTables[32][256];
};

#endif
