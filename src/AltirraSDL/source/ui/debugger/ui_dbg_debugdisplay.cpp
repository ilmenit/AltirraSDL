//	AltirraSDL - Dear ImGui debugger debug display pane
//	Replaces Win32 ATDebugDisplayWindow (uidbgdebugdisplay.cpp).
//	Visualizes ANTIC display list rendering using the ATDebugDisplay engine,
//	with DL/PF address overrides and palette mode selection.

#include <stdafx.h>
#include <algorithm>
#include <cmath>
#include <imgui.h>
#include <SDL3/SDL.h>
#include <vd2/system/vdtypes.h>
#include <vd2/system/VDString.h>
#include <vd2/system/error.h>
#include <vd2/VDDisplay/display.h>
#include <vd2/Kasumi/pixmap.h>
#include <vd2/Kasumi/pixmaputils.h>
#include "ui_debugger.h"
#include "display_backend.h"
#include "gl_helpers.h"
#include "console.h"
#include "debugger.h"
#include "debugdisplay.h"
#include "simulator.h"
#include "antic.h"
#include "gtia.h"
#include "memorymanager.h"

extern ATSimulator g_sim;
extern SDL_Window *g_pWindow;

// Display dimensions (matching ATDebugDisplay).
static constexpr int kDebugDisplayW = 376;
static constexpr int kDebugDisplayH = 240;

// =========================================================================
// Null IVDVideoDisplay — minimal no-op implementation used by ATDebugDisplay
// to avoid needing a full display pipeline.  ATDebugDisplay::Update() calls
// SetSourcePersistent() at the end; we simply ignore it and read the
// protected mDisplayBuffer directly via a subclass.
// =========================================================================

class ATNullVideoDisplay final : public IVDVideoDisplay {
public:
	void Destroy() override {}
	void Reset() override {}
	void SetSourceMessage(const wchar_t *) override {}
	bool SetSource(bool, const VDPixmap&, bool) override { return true; }
	bool SetSourcePersistent(bool, const VDPixmap&, bool,
		const VDVideoDisplayScreenFXInfo *, IVDVideoDisplayScreenFXEngine *) override { return true; }
	void SetSourceSubrect(const vdrect32 *) override {}
	void SetSourceSolidColor(uint32) override {}
	void SetReturnFocus(bool) override {}
	void SetTouchEnabled(bool) override {}
	void SetUse16Bit(bool) override {}
	void SetHDREnabled(bool) override {}
	void SetFullScreen(bool, uint32, uint32, uint32) override {}
	void SetCustomDesiredRefreshRate(float, float, float) override {}
	void SetDestRect(const vdrect32 *, uint32) override {}
	void SetDestRectF(const vdrect32f *, uint32) override {}
	void SetPixelSharpness(float, float) override {}
	void SetCompositor(IVDDisplayCompositor *) override {}
	void SetSDRBrightness(float) override {}
	void PostBuffer(VDVideoDisplayFrame *) override {}
	bool RevokeBuffer(bool, VDVideoDisplayFrame **) override { return false; }
	void FlushBuffers() override {}
	void Invalidate() override {}
	void Update(int) override {}
	void Cache() override {}
	void SetCallback(IVDVideoDisplayCallback *) override {}
	void SetOnFrameStatusUpdated(vdfunction<void(int)>) override {}
	void SetAccelerationMode(AccelerationMode) override {}
	FilterMode GetFilterMode() override { return kFilterPoint; }
	void SetFilterMode(FilterMode) override {}
	float GetSyncDelta() const override { return 0; }
	int GetQueuedFrames() const override { return 0; }
	bool IsFramePending() const override { return false; }
	VDDVSyncStatus GetVSyncStatus() const override { return {}; }
	vdrect32 GetMonitorRect() override { return {}; }
	VDDMonitorInfo GetMonitorInformation() override { return {}; }
	bool IsScreenFXPreferred() const override { return false; }
	VDDHighColorAvailability GetHDRCapability() const override { return {}; }
	VDDHighColorAvailability GetWCGCapability() const override { return {}; }
	bool MapNormSourcePtToDest(vdfloat2&) const override { return true; }
	bool MapNormDestPtToSource(vdfloat2&) const override { return true; }
	void SetProfileHook(const vdfunction<void(ProfileEvent, uintptr)>&) override {}
	void RequestCapture(vdfunction<void(const VDPixmap *)>) override {}
};

// =========================================================================
// ATDebugDisplay subclass that exposes the protected buffer
// =========================================================================

class ATDebugDisplayImGui : public ATDebugDisplay {
public:
	const VDPixmapBuffer& GetBuffer() const { return mDisplayBuffer; }
	const uint32 *GetPalette() const { return mPalette; }
	sint32 GetDLAddrOverride() const { return mDLAddrOverride; }
	sint32 GetPFAddrOverride() const { return mPFAddrOverride; }
};

// =========================================================================
// Debug Display pane
// =========================================================================

class ATImGuiDebugDisplayPaneImpl final : public ATImGuiDebuggerPane {
public:
	ATImGuiDebugDisplayPaneImpl();
	~ATImGuiDebugDisplayPaneImpl() override;

	bool Render() override;
	void OnDebuggerSystemStateUpdate(const ATDebuggerSystemState& state) override;
	bool ApplyDLAddressForTest(const char *expr, VDStringA& outState);
	bool ApplyPFAddressForTest(const char *expr, VDStringA& outState);
	bool SetFilterModeForTest(int mode, VDStringA& outState);
	bool SetPaletteModeForTest(int mode, VDStringA& outState);
	void DescribeForTest(VDStringA& outState) const;

private:
	void RebuildDisplay();
	void UpdateTexture();
	bool UpdateBicubicTexture(int dstW, int dstH, bool useGL);
	void ApplyTextureFilter();
	bool ApplyDLAddress();
	bool ApplyPFAddress();
	void SetFilterMode(IVDVideoDisplay::FilterMode mode);

	ATNullVideoDisplay mNullDisplay;
	ATDebugDisplayImGui mDebugDisplay;

	SDL_Texture *mpTexture = nullptr;
	SDL_Texture *mpBicubicTexture = nullptr;
	uint32 mGLTexture = 0;	// GLuint for OpenGL backend
	uint32 mBicubicGLTexture = 0;
	bool mbNeedsRebuild = true;
	bool mbBicubicDirty = true;
	int mBicubicW = 0;
	int mBicubicH = 0;
	std::vector<uint32> mPixelBuffer;
	std::vector<uint32> mBicubicBuffer;

	// Address bar inputs
	char mDLAddrInput[16] = {};
	char mPFAddrInput[16] = {};
	IVDVideoDisplay::FilterMode mFilterMode = IVDVideoDisplay::kFilterBilinear;

	static constexpr int kDisplayW = kDebugDisplayW;
	static constexpr int kDisplayH = kDebugDisplayH;
};

ATImGuiDebugDisplayPaneImpl::ATImGuiDebugDisplayPaneImpl()
	: ATImGuiDebuggerPane(kATUIPaneId_DebugDisplay, "Debug Display")
{
	mDebugDisplay.Init(
		g_sim.GetMemoryManager(),
		&g_sim.GetAntic(),
		&g_sim.GetGTIA(),
		&mNullDisplay
	);
}

ATImGuiDebugDisplayPaneImpl::~ATImGuiDebugDisplayPaneImpl() {
	mDebugDisplay.Shutdown();

	if (mpTexture) {
		SDL_DestroyTexture(mpTexture);
		mpTexture = nullptr;
	}
	if (mpBicubicTexture) {
		SDL_DestroyTexture(mpBicubicTexture);
		mpBicubicTexture = nullptr;
	}
	if (mGLTexture) {
		glDeleteTextures(1, &mGLTexture);
		mGLTexture = 0;
	}
	if (mBicubicGLTexture) {
		glDeleteTextures(1, &mBicubicGLTexture);
		mBicubicGLTexture = 0;
	}
}

void ATImGuiDebugDisplayPaneImpl::OnDebuggerSystemStateUpdate(const ATDebuggerSystemState& state) {
	ATImGuiDebuggerPane::OnDebuggerSystemStateUpdate(state);
	mbNeedsRebuild = true;
}

void ATImGuiDebugDisplayPaneImpl::RebuildDisplay() {
	mbNeedsRebuild = false;

	// Update the debug display renderer
	mDebugDisplay.Update();

	// Update SDL texture from the indexed pixel buffer
	UpdateTexture();
}

void ATImGuiDebugDisplayPaneImpl::UpdateTexture() {
	const VDPixmapBuffer& buf = mDebugDisplay.GetBuffer();
	const uint32 *palette = mDebugDisplay.GetPalette();

	if (buf.w <= 0 || buf.h <= 0)
		return;

	IDisplayBackend *backend = ATUIGetDisplayBackend();
	bool useGL = backend && backend->GetType() == DisplayBackendType::OpenGL;

	// Convert Pal8 → ARGB8888 into a persistent source buffer so the
	// bicubic path can resample the exact same frame.
	mPixelBuffer.assign(kDisplayW * kDisplayH, 0xFF000000u);

	const uint8 *src = (const uint8 *)buf.data;
	int srcPitch = buf.pitch;

	for (int y = 0; y < kDisplayH && y < buf.h; ++y) {
		const uint8 *srcRow = src + y * srcPitch;
		uint32 *dstRow = &mPixelBuffer[y * kDisplayW];
		int w = std::min((int)buf.w, kDisplayW);

		for (int x = 0; x < w; ++x) {
			uint8 idx = srcRow[x];
			dstRow[x] = palette[idx] | 0xFF000000u;
		}
	}

	if (useGL) {
		// Debugger display pane: convert palette → XRGB8888 in mPixelBuffer
		// then route through the per-profile XRGB8888 helper.
		if (!mGLTexture) {
			mGLTexture = GLCreateXRGB8888Texture(
				kDisplayW, kDisplayH,
				mFilterMode == IVDVideoDisplay::kFilterBilinear,
				mPixelBuffer.data());
		} else {
			glBindTexture(GL_TEXTURE_2D, mGLTexture);
			GLUploadXRGB8888(kDisplayW, kDisplayH, mPixelBuffer.data(), 0);
		}
	} else {
		if (!mpTexture) {
			SDL_Renderer *renderer = SDL_GetRenderer(g_pWindow);
			if (!renderer) return;
			mpTexture = SDL_CreateTexture(
				renderer,
				SDL_PIXELFORMAT_ARGB8888,
				SDL_TEXTUREACCESS_STREAMING,
				kDisplayW, kDisplayH
			);
			if (!mpTexture) return;
			ApplyTextureFilter();
		}

		void *pixels = nullptr;
		int pitch = 0;
		if (!SDL_LockTexture(mpTexture, nullptr, &pixels, &pitch))
			return;

		for (int y = 0; y < kDisplayH; ++y) {
			memcpy((uint8 *)pixels + y * pitch,
				   &mPixelBuffer[y * kDisplayW],
				   kDisplayW * 4);
		}

		SDL_UnlockTexture(mpTexture);
	}

	mbBicubicDirty = true;
}

static float ATUIDebugDisplayCubic(float p0, float p1, float p2, float p3, float t) {
	const float a0 = -0.5f * p0 + 1.5f * p1 - 1.5f * p2 + 0.5f * p3;
	const float a1 = p0 - 2.5f * p1 + 2.0f * p2 - 0.5f * p3;
	const float a2 = -0.5f * p0 + 0.5f * p2;

	return ((a0 * t + a1) * t + a2) * t + p1;
}

static uint8 ATUIDebugDisplayClampChannel(float v) {
	return (uint8)std::clamp((int)(v + 0.5f), 0, 255);
}

static uint8 ATUIDebugDisplaySampleChannel(
	const uint32 *src,
	int x,
	int y,
	int channelShift)
{
	x = std::clamp(x, 0, kDebugDisplayW - 1);
	y = std::clamp(y, 0, kDebugDisplayH - 1);
	return (uint8)((src[y * kDebugDisplayW + x] >> channelShift) & 0xff);
}

static uint32 ATUIDebugDisplaySampleBicubic(
	const uint32 *src,
	float fx,
	float fy)
{
	const int ix = (int)std::floor(fx);
	const int iy = (int)std::floor(fy);
	const float tx = fx - (float)ix;
	const float ty = fy - (float)iy;

	float rowsR[4];
	float rowsG[4];
	float rowsB[4];

	for (int j = 0; j < 4; ++j) {
		float colsR[4];
		float colsG[4];
		float colsB[4];

		for (int i = 0; i < 4; ++i) {
			const int sx = ix + i - 1;
			const int sy = iy + j - 1;

			colsR[i] = (float)ATUIDebugDisplaySampleChannel(src, sx, sy, 16);
			colsG[i] = (float)ATUIDebugDisplaySampleChannel(src, sx, sy, 8);
			colsB[i] = (float)ATUIDebugDisplaySampleChannel(src, sx, sy, 0);
		}

		rowsR[j] = ATUIDebugDisplayCubic(colsR[0], colsR[1], colsR[2], colsR[3], tx);
		rowsG[j] = ATUIDebugDisplayCubic(colsG[0], colsG[1], colsG[2], colsG[3], tx);
		rowsB[j] = ATUIDebugDisplayCubic(colsB[0], colsB[1], colsB[2], colsB[3], tx);
	}

	const uint8 r = ATUIDebugDisplayClampChannel(
		ATUIDebugDisplayCubic(rowsR[0], rowsR[1], rowsR[2], rowsR[3], ty));
	const uint8 g = ATUIDebugDisplayClampChannel(
		ATUIDebugDisplayCubic(rowsG[0], rowsG[1], rowsG[2], rowsG[3], ty));
	const uint8 b = ATUIDebugDisplayClampChannel(
		ATUIDebugDisplayCubic(rowsB[0], rowsB[1], rowsB[2], rowsB[3], ty));

	return 0xff000000u | ((uint32)r << 16) | ((uint32)g << 8) | b;
}

bool ATImGuiDebugDisplayPaneImpl::UpdateBicubicTexture(int dstW, int dstH, bool useGL) {
	if (dstW <= 0 || dstH <= 0 || mPixelBuffer.empty())
		return false;

	const bool sizeChanged = dstW != mBicubicW || dstH != mBicubicH;

	if (!mbBicubicDirty && !sizeChanged) {
		if (useGL)
			return mBicubicGLTexture != 0;
		else
			return mpBicubicTexture != nullptr;
	}

	mBicubicW = dstW;
	mBicubicH = dstH;
	mBicubicBuffer.resize((size_t)dstW * dstH);

	const float scaleX = (float)kDisplayW / (float)dstW;
	const float scaleY = (float)kDisplayH / (float)dstH;

	for (int y = 0; y < dstH; ++y) {
		const float srcY = ((float)y + 0.5f) * scaleY - 0.5f;
		uint32 *dstRow = &mBicubicBuffer[(size_t)y * dstW];

		for (int x = 0; x < dstW; ++x) {
			const float srcX = ((float)x + 0.5f) * scaleX - 0.5f;
			dstRow[x] = ATUIDebugDisplaySampleBicubic(mPixelBuffer.data(), srcX, srcY);
		}
	}

	if (useGL) {
		if (!mBicubicGLTexture || sizeChanged) {
			if (mBicubicGLTexture)
				glDeleteTextures(1, &mBicubicGLTexture);

			mBicubicGLTexture = GLCreateXRGB8888Texture(
				dstW, dstH, false, mBicubicBuffer.data());
		} else {
			glBindTexture(GL_TEXTURE_2D, mBicubicGLTexture);
			GLUploadXRGB8888(dstW, dstH, mBicubicBuffer.data(), 0);
		}
	} else {
		if (!mpBicubicTexture || sizeChanged) {
			if (mpBicubicTexture)
				SDL_DestroyTexture(mpBicubicTexture);

			SDL_Renderer *renderer = SDL_GetRenderer(g_pWindow);
			if (!renderer)
				return false;

			mpBicubicTexture = SDL_CreateTexture(
				renderer,
				SDL_PIXELFORMAT_ARGB8888,
				SDL_TEXTUREACCESS_STREAMING,
				dstW,
				dstH);
			if (!mpBicubicTexture)
				return false;

			SDL_SetTextureScaleMode(mpBicubicTexture, SDL_SCALEMODE_NEAREST);
		}

		void *pixels = nullptr;
		int pitch = 0;
		if (!SDL_LockTexture(mpBicubicTexture, nullptr, &pixels, &pitch))
			return false;

		for (int y = 0; y < dstH; ++y) {
			memcpy(
				(uint8 *)pixels + y * pitch,
				&mBicubicBuffer[(size_t)y * dstW],
				(size_t)dstW * 4);
		}

		SDL_UnlockTexture(mpBicubicTexture);
	}

	mbBicubicDirty = false;
	return useGL ? mBicubicGLTexture != 0 : mpBicubicTexture != nullptr;
}

void ATImGuiDebugDisplayPaneImpl::ApplyTextureFilter() {
	const bool linear = mFilterMode == IVDVideoDisplay::kFilterBilinear;

	if (mGLTexture) {
		glBindTexture(GL_TEXTURE_2D, mGLTexture);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, linear ? GL_LINEAR : GL_NEAREST);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, linear ? GL_LINEAR : GL_NEAREST);
	}

	if (mpTexture)
		SDL_SetTextureScaleMode(mpTexture, linear ? SDL_SCALEMODE_LINEAR : SDL_SCALEMODE_NEAREST);
}

void ATImGuiDebugDisplayPaneImpl::SetFilterMode(IVDVideoDisplay::FilterMode mode) {
	if (mFilterMode == mode)
		return;

	mFilterMode = mode;
	ApplyTextureFilter();
	mbNeedsRebuild = true;
}

bool ATImGuiDebugDisplayPaneImpl::ApplyDLAddress() {
	IATDebugger *dbg = ATGetDebugger();
	if (!mDLAddrInput[0]) {
		mDebugDisplay.SetDLAddrOverride(-1);
		mbNeedsRebuild = true;
		return true;
	}

	const sint32 addr = dbg ? dbg->ResolveSymbol(mDLAddrInput) : -1;
	if (addr < 0 || addr > 0xffff) {
		ATConsolePrintf("Invalid debug display DL address: %s\n", mDLAddrInput);
		return false;
	}

	mDebugDisplay.SetMode(ATDebugDisplay::kMode_AnticHistoryStart);
	mDebugDisplay.SetDLAddrOverride(addr);
	mbNeedsRebuild = true;
	return true;
}

bool ATImGuiDebugDisplayPaneImpl::ApplyPFAddress() {
	IATDebugger *dbg = ATGetDebugger();
	if (!mPFAddrInput[0]) {
		mDebugDisplay.SetPFAddrOverride(-1);
		mbNeedsRebuild = true;
		return true;
	}

	const sint32 addr = dbg ? dbg->ResolveSymbol(mPFAddrInput) : -1;
	if (addr < 0 || addr > 0xffff) {
		ATConsolePrintf("Invalid debug display PF address: %s\n", mPFAddrInput);
		return false;
	}

	mDebugDisplay.SetPFAddrOverride(addr);
	mbNeedsRebuild = true;
	return true;
}

void ATImGuiDebugDisplayPaneImpl::DescribeForTest(VDStringA& outState) const {
	outState.sprintf("mode=%d,palette=%d,filter=%d,dl=%d,pf=%d",
		(int)mDebugDisplay.GetMode(),
		(int)mDebugDisplay.GetPaletteMode(),
		(int)mFilterMode,
		(int)mDebugDisplay.GetDLAddrOverride(),
		(int)mDebugDisplay.GetPFAddrOverride());
}

bool ATImGuiDebugDisplayPaneImpl::ApplyDLAddressForTest(const char *expr,
	VDStringA& outState)
{
	snprintf(mDLAddrInput, sizeof(mDLAddrInput), "%s", expr ? expr : "");
	const bool applied = ApplyDLAddress();
	DescribeForTest(outState);
	return applied;
}

bool ATImGuiDebugDisplayPaneImpl::ApplyPFAddressForTest(const char *expr,
	VDStringA& outState)
{
	snprintf(mPFAddrInput, sizeof(mPFAddrInput), "%s", expr ? expr : "");
	const bool applied = ApplyPFAddress();
	DescribeForTest(outState);
	return applied;
}

bool ATImGuiDebugDisplayPaneImpl::SetFilterModeForTest(int mode,
	VDStringA& outState)
{
	if (mode < (int)IVDVideoDisplay::kFilterPoint
		|| mode > (int)IVDVideoDisplay::kFilterBicubic) {
		DescribeForTest(outState);
		return false;
	}

	SetFilterMode((IVDVideoDisplay::FilterMode)mode);
	DescribeForTest(outState);
	return true;
}

bool ATImGuiDebugDisplayPaneImpl::SetPaletteModeForTest(int mode,
	VDStringA& outState)
{
	if (mode < 0 || mode >= ATDebugDisplay::kPaletteModeCount) {
		DescribeForTest(outState);
		return false;
	}

	mDebugDisplay.SetPaletteMode((ATDebugDisplay::PaletteMode)mode);
	mbNeedsRebuild = true;
	DescribeForTest(outState);
	return true;
}

bool ATImGuiDebugDisplayPaneImpl::Render() {
	bool open = true;

	if (mbFocusRequested) {
		ImGui::SetNextWindowFocus();
		mbFocusRequested = false;
	}

	ImGui::SetNextWindowSize(ImVec2(420, 350), ImGuiCond_FirstUseEver);
	if (!ImGui::Begin(mTitle.c_str(), &open)) {
		mbHasFocus = false;
		ImGui::End();
		return open;
	}
	mbHasFocus = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);

	// Toolbar: DL address, PF address, palette mode
	{
		const char *dlModes[] = { "Auto DL (History)", "Auto DL (History start)" };
		int dlMode = (int)mDebugDisplay.GetMode();
		ImGui::SetNextItemWidth(155);
		if (ImGui::Combo("##dlmode", &dlMode, dlModes, vdcountof(dlModes))) {
			mDebugDisplay.SetMode((ATDebugDisplay::Mode)dlMode);
			mbNeedsRebuild = true;
		}

		ImGui::SameLine();
		ImGui::SetNextItemWidth(70);
		if (ImGui::InputText("##dl", mDLAddrInput, sizeof(mDLAddrInput),
				ImGuiInputTextFlags_EnterReturnsTrue)) {
			ApplyDLAddress();
		}

		ImGui::SameLine();
		if (ImGui::Button("Auto PF Address")) {
			mDebugDisplay.SetPFAddrOverride(-1);
			mPFAddrInput[0] = 0;
			mbNeedsRebuild = true;
		}

		ImGui::SameLine();
		ImGui::SetNextItemWidth(70);
		if (ImGui::InputText("##pf", mPFAddrInput, sizeof(mPFAddrInput),
				ImGuiInputTextFlags_EnterReturnsTrue)) {
			ApplyPFAddress();
		}

		ImGui::SameLine();
		ImGui::Spacing();
		ImGui::SameLine();

		// Palette mode — query authoritative state from mDebugDisplay
		const char *palModes[] = { "Registers", "Analysis" };
		int palCurrent = (int)mDebugDisplay.GetPaletteMode();
		ImGui::SetNextItemWidth(100);
		if (ImGui::Combo("##pal", &palCurrent, palModes, 2)) {
			mDebugDisplay.SetPaletteMode((ATDebugDisplay::PaletteMode)palCurrent);
			mbNeedsRebuild = true;
		}
	}

	ImGui::Separator();

	if (mbNeedsRebuild)
		RebuildDisplay();

	// Render the display texture
	const bool hasBaseTexture = mpTexture || mGLTexture;
	if (hasBaseTexture) {
		ImVec2 avail = ImGui::GetContentRegionAvail();
		if (avail.x > 0 && avail.y > 0) {
			// Maintain aspect ratio
			float aspectRatio = (float)kDisplayW / (float)kDisplayH;
			float drawW = avail.x;
			float drawH = drawW / aspectRatio;
			if (drawH > avail.y) {
				drawH = avail.y;
				drawW = drawH * aspectRatio;
			}
			// Center in available space
			float offsetX = (avail.x - drawW) * 0.5f;
			float offsetY = (avail.y - drawH) * 0.5f;
			if (offsetX > 0) ImGui::SetCursorPosX(ImGui::GetCursorPosX() + offsetX);
			if (offsetY > 0) ImGui::SetCursorPosY(ImGui::GetCursorPosY() + offsetY);
			IDisplayBackend *backend = ATUIGetDisplayBackend();
			const bool useGL = backend && backend->GetType() == DisplayBackendType::OpenGL;
			void *texID = mpTexture ? (void *)(intptr_t)mpTexture
					   : mGLTexture ? (void *)(intptr_t)mGLTexture
					   : nullptr;

			if (mFilterMode == IVDVideoDisplay::kFilterBicubic) {
				const int bicubicW = std::max(1, (int)std::round(drawW));
				const int bicubicH = std::max(1, (int)std::round(drawH));

				if (UpdateBicubicTexture(bicubicW, bicubicH, useGL)) {
					drawW = (float)bicubicW;
					drawH = (float)bicubicH;
					texID = useGL
						? (void *)(intptr_t)mBicubicGLTexture
						: (void *)(intptr_t)mpBicubicTexture;
				}
			}

			ImVec2 uv0(0.0f, 0.0f);
			ImVec2 uv1(1.0f, 1.0f);
			if (useGL) {
				uv0.y = 1.0f;
				uv1.y = 0.0f;
			}

			if (texID)
				ImGui::Image((ImTextureID)texID, ImVec2(drawW, drawH), uv0, uv1);
		}
	} else {
		ImGui::TextDisabled("(no debug display available)");
	}

	// Context menu (right-click) — matches IDR_DEBUGDISPLAY_CONTEXT_MENU
	if (ImGui::BeginPopupContextWindow("##DebugDisplayCtx")) {
		if (ImGui::MenuItem("Force Update")) {
			mDebugDisplay.Update();
			UpdateTexture();
		}

		ImGui::Separator();

		if (ImGui::MenuItem("Point", nullptr,
				mFilterMode == IVDVideoDisplay::kFilterPoint))
			SetFilterMode(IVDVideoDisplay::kFilterPoint);
		if (ImGui::MenuItem("Bilinear", nullptr,
				mFilterMode == IVDVideoDisplay::kFilterBilinear))
			SetFilterMode(IVDVideoDisplay::kFilterBilinear);
		if (ImGui::MenuItem("Bicubic", nullptr,
				mFilterMode == IVDVideoDisplay::kFilterBicubic))
			SetFilterMode(IVDVideoDisplay::kFilterBicubic);

		ImGui::Separator();

		const auto curPal = mDebugDisplay.GetPaletteMode();
		if (ImGui::MenuItem("Current Register Values", nullptr,
				curPal == ATDebugDisplay::kPaletteMode_Registers)) {
			mDebugDisplay.SetPaletteMode(ATDebugDisplay::kPaletteMode_Registers);
			mbNeedsRebuild = true;
		}
		if (ImGui::MenuItem("Analysis Palette", nullptr,
				curPal == ATDebugDisplay::kPaletteMode_Analysis)) {
			mDebugDisplay.SetPaletteMode(ATDebugDisplay::kPaletteMode_Analysis);
			mbNeedsRebuild = true;
		}

		ImGui::EndPopup();
	}

	// Escape → focus Console (matches Windows pattern)
	if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows)
			&& !ImGui::GetIO().WantTextInput
			&& ImGui::IsKeyPressed(ImGuiKey_Escape))
		ATUIDebuggerFocusConsole();

	ImGui::End();
	return open;
}

// =========================================================================
// Registration
// =========================================================================

void ATUIDebuggerEnsureDebugDisplayPane() {
	if (!ATUIDebuggerGetPane(kATUIPaneId_DebugDisplay)) {
		auto *pane = new ATImGuiDebugDisplayPaneImpl();
		ATUIDebuggerRegisterPane(pane);
	}
}

static ATImGuiDebugDisplayPaneImpl *ATUIDebuggerGetDebugDisplayPaneForTest() {
	ATUIDebuggerOpen();
	ATActivateUIPane(kATUIPaneId_DebugDisplay, true, true);

	return static_cast<ATImGuiDebugDisplayPaneImpl *>(
		ATUIDebuggerGetPane(kATUIPaneId_DebugDisplay));
}

bool ATUIDebuggerDescribeDebugDisplayForTest(VDStringA& outState) {
	ATImGuiDebugDisplayPaneImpl *pane = ATUIDebuggerGetDebugDisplayPaneForTest();
	if (!pane)
		return false;

	pane->DescribeForTest(outState);
	return true;
}

bool ATUIDebuggerApplyDebugDisplayDLForTest(const char *expr,
	VDStringA& outState)
{
	ATImGuiDebugDisplayPaneImpl *pane = ATUIDebuggerGetDebugDisplayPaneForTest();
	return pane ? pane->ApplyDLAddressForTest(expr, outState) : false;
}

bool ATUIDebuggerApplyDebugDisplayPFForTest(const char *expr,
	VDStringA& outState)
{
	ATImGuiDebugDisplayPaneImpl *pane = ATUIDebuggerGetDebugDisplayPaneForTest();
	return pane ? pane->ApplyPFAddressForTest(expr, outState) : false;
}

bool ATUIDebuggerSetDebugDisplayFilterForTest(int mode,
	VDStringA& outState)
{
	ATImGuiDebugDisplayPaneImpl *pane = ATUIDebuggerGetDebugDisplayPaneForTest();
	return pane ? pane->SetFilterModeForTest(mode, outState) : false;
}

bool ATUIDebuggerSetDebugDisplayPaletteForTest(int mode,
	VDStringA& outState)
{
	ATImGuiDebugDisplayPaneImpl *pane = ATUIDebuggerGetDebugDisplayPaneForTest();
	return pane ? pane->SetPaletteModeForTest(mode, outState) : false;
}
