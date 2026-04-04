//	AltirraSDL - Dear ImGui debugger debug display pane
//	Replaces Win32 ATDebugDisplayWindow (uidbgdebugdisplay.cpp).
//	Visualizes ANTIC display list rendering using the ATDebugDisplay engine,
//	with DL/PF address overrides and palette mode selection.

#include <stdafx.h>
#include <algorithm>
#include <imgui.h>
#include <SDL3/SDL.h>
#include <vd2/system/vdtypes.h>
#include <vd2/system/VDString.h>
#include <vd2/system/error.h>
#include <vd2/VDDisplay/display.h>
#include <vd2/Kasumi/pixmap.h>
#include <vd2/Kasumi/pixmaputils.h>
#include "ui_debugger.h"
#include "console.h"
#include "debugger.h"
#include "debugdisplay.h"
#include "simulator.h"
#include "antic.h"
#include "gtia.h"
#include "memorymanager.h"

extern ATSimulator g_sim;
extern SDL_Window *g_pWindow;

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

private:
	void RebuildDisplay();
	void UpdateTexture();

	ATNullVideoDisplay mNullDisplay;
	ATDebugDisplayImGui mDebugDisplay;

	SDL_Texture *mpTexture = nullptr;
	bool mbNeedsRebuild = true;

	// Address bar inputs
	char mDLAddrInput[16] = {};
	char mPFAddrInput[16] = {};

	// Display dimensions (matching ATDebugDisplay: 376x240)
	static constexpr int kDisplayW = 376;
	static constexpr int kDisplayH = 240;
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
}

void ATImGuiDebugDisplayPaneImpl::OnDebuggerSystemStateUpdate(const ATDebuggerSystemState& state) {
	ATImGuiDebuggerPane::OnDebuggerSystemStateUpdate(state);
	if (!state.mbRunning)
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

	// Create or recreate texture if needed
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
	}

	// Lock texture and convert Pal8 → ARGB8888
	void *pixels = nullptr;
	int pitch = 0;
	if (!SDL_LockTexture(mpTexture, nullptr, &pixels, &pitch))
		return;

	const uint8 *src = (const uint8 *)buf.data;
	int srcPitch = buf.pitch;

	for (int y = 0; y < kDisplayH && y < buf.h; ++y) {
		const uint8 *srcRow = src + y * srcPitch;
		uint32 *dstRow = (uint32 *)((uint8 *)pixels + y * pitch);
		int w = std::min((int)buf.w, kDisplayW);

		for (int x = 0; x < w; ++x) {
			uint8 idx = srcRow[x];
			dstRow[x] = palette[idx] | 0xFF000000u;  // ensure full alpha
		}
	}

	SDL_UnlockTexture(mpTexture);
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
		ImGui::TextUnformatted("DL:");
		ImGui::SameLine();
		ImGui::SetNextItemWidth(80);
		if (ImGui::InputText("##dl", mDLAddrInput, sizeof(mDLAddrInput),
				ImGuiInputTextFlags_EnterReturnsTrue)) {
			IATDebugger *dbg = ATGetDebugger();
			if (dbg && mDLAddrInput[0]) {
				try {
					sint32 addr = dbg->EvaluateThrow(mDLAddrInput);
					mDebugDisplay.SetDLAddrOverride(addr);
					mbNeedsRebuild = true;
				} catch (const MyError&) {}
			} else {
				mDebugDisplay.SetDLAddrOverride(-1);
				mbNeedsRebuild = true;
			}
		}

		ImGui::SameLine();
		ImGui::TextUnformatted("PF:");
		ImGui::SameLine();
		ImGui::SetNextItemWidth(80);
		if (ImGui::InputText("##pf", mPFAddrInput, sizeof(mPFAddrInput),
				ImGuiInputTextFlags_EnterReturnsTrue)) {
			IATDebugger *dbg = ATGetDebugger();
			if (dbg && mPFAddrInput[0]) {
				try {
					sint32 addr = dbg->EvaluateThrow(mPFAddrInput);
					mDebugDisplay.SetPFAddrOverride(addr);
					mbNeedsRebuild = true;
				} catch (const MyError&) {}
			} else {
				mDebugDisplay.SetPFAddrOverride(-1);
				mbNeedsRebuild = true;
			}
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
	if (mpTexture) {
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
			ImGui::Image((ImTextureID)(intptr_t)mpTexture, ImVec2(drawW, drawH));
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
