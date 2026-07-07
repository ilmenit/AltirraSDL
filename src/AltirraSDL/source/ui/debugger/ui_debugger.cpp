//	AltirraSDL - Dear ImGui debugger pane manager implementation
//	Replaces Win32 ATUIPane / ATContainerWindow docking with ImGui docking.

#include <stdafx.h>
#include <cmath>
#include <condition_variable>
#include <mutex>
#include <vector>
#include <vd2/system/vdtypes.h>
#include <vd2/system/error.h>
#include <vd2/system/vdstl.h>
#include <vd2/system/text.h>
#include <vd2/Kasumi/pixmaputils.h>
#include <at/atcore/configvar.h>
#include <at/atnativeui/uiframe.h>
#include <at/atnativeui/genericdialog.h>
#include <SDL3/SDL.h>
#include <imgui.h>
#include <imgui_internal.h>
#include "ui_debugger.h"
#include "ui_main.h"
#include "ui_frame_capture.h"
#include "console.h"
#include "debugger.h"
#include "simulator.h"
#include "display_sdl3_impl.h"
#include "display_backend.h"
#include "ui_textselection.h"
#include "ui_autosuggest.h"
#include "logging.h"
#include "ui_fonts.h"
#include "uiaccessors.h"
#include "uitypes.h"
#include "gtia.h"
#include "uidisplay.h"
#include "videowriter.h"
#include "oshelper.h"

extern ATSimulator g_sim;
extern VDVideoDisplaySDL3 *g_pDisplay;
extern SDL_Window *g_pWindow;

ATConfigVarBool g_ATCVRecordingVideoShowMotionVectors(
	"recording.video.show_motion_vectors",
	false);

// =========================================================================
// ATImGuiDebuggerPane base class
// =========================================================================

ATImGuiDebuggerPane::ATImGuiDebuggerPane(uint32 paneId, const char *title)
	: mPaneId(paneId)
	, mTitle(title)
{
}

ATImGuiDebuggerPane::~ATImGuiDebuggerPane() {
}

void ATImGuiDebuggerPane::OnDebuggerSystemStateUpdate(const ATDebuggerSystemState& state) {
	mLastState = state;
	mbStateValid = true;
}

void ATImGuiDebuggerPane::OnDebuggerEvent(ATDebugEvent eventId) {
	// Subclasses override as needed
}

bool ATImGuiDebuggerPane::OnPaneCommand(ATUIPaneCommandId id) {
	return false;
}

void ATImGuiDebuggerPane::OnFrame() {
}

void *ATImGuiDebuggerPane::AsPaneInterface(uint32 iid) {
	if (iid == IATUIDebuggerPane::kTypeID)
		return static_cast<IATUIDebuggerPane *>(this);

	return nullptr;
}

// =========================================================================
// Pane manager globals
// =========================================================================

namespace {
	struct PaneEntry {
		uint32 id;
		vdrefptr<ATImGuiDebuggerPane> pane;
	};

	struct DisplayPaneRenderCallbackData {
		IDisplayBackend *backend = nullptr;
		float x = 0;
		float y = 0;
		float w = 0;
		float h = 0;
		float clipX = 0;
		float clipY = 0;
		float clipW = 0;
		float clipH = 0;
		int srcW = 0;
		int srcH = 0;
	};

	std::vector<PaneEntry> g_debugPanes;
	bool g_debuggerOpen = false;
	bool g_dockLayoutApplied = false;
	uint32 g_focusedPaneId = 0;
	uint32 g_activePaneId = 0;

	// Pane creator registry (populated by ATRegisterUIPaneType/Class)
	struct PaneCreatorEntry {
		uint32 id;
		ATPaneCreator creator;
		ATPaneClassCreator classCreator;
	};
	std::vector<PaneCreatorEntry> g_paneCreators;

	using ATImGuiPaneCreator = void (*)();

	struct ImGuiPaneCreatorEntry {
		uint32 id;
		ATImGuiPaneCreator creator;
	};
	std::vector<ImGuiPaneCreatorEntry> g_imguiPaneCreators;

	class ATSDLDisplayPaneInterface final : public IATDisplayPane {
	public:
		void ReleaseMouse() override {
			ATUIReleaseMouse();
		}

		void ToggleCaptureMouse() override {
			if (ATUIIsMouseCaptured())
				ATUIReleaseMouse();
			else
				ATUICaptureMouse();
		}

		void OnSize() override {
			UpdateFilterMode();
		}

		void ResetDisplay() override {
			if (g_pDisplay)
				g_pDisplay->Reset();
		}

		bool IsTextSelected() const override {
			return ATUIIsTextSelected();
		}

		void Deselect() override {
			ATUITextDeselect();
		}

		void SelectAll() override {
			ATUITextSelectAll();
		}

		void Copy(ATTextCopyMode copyMode) override {
			ATUITextCopy(copyMode);
		}

		void CopyFrame(bool trueAspect) override {
			ATUIRequestCopyFrame(trueAspect);
		}

		bool CopyFrameImage(bool trueAspect, VDPixmapBuffer& buf) override {
			return ATUICaptureEmulatorFrame(g_sim,
				trueAspect ? ATUIFrameCaptureMode::TrueAspect : ATUIFrameCaptureMode::Display,
				buf);
		}

		void SaveFrame(bool trueAspect, const wchar_t *path = nullptr) override {
			if (!path || !*path) {
				ATUIShowSaveFrameDialog(g_pWindow, trueAspect);
				return;
			}

			VDPixmapBuffer frame;
			if (!CopyFrameImage(trueAspect, frame)) {
				LOG_ERROR("UI", "Save frame: no emulator frame is available");
				return;
			}

			ATSaveFrame(frame, path);
		}

		void Paste(const wchar_t *s, size_t len) override {
			ATUIPasteTextDirect(s, len);
		}

		void UpdateTextDisplay(bool) override {
			// SDL text selection renders directly over the display and does not use
			// the native enhanced-text replacement pipeline.
		}

		void UpdateTextModeFont() override {
			// No native enhanced-text font pipeline in SDL.
		}

		void UpdateFilterMode() override {
			if (g_pDisplay)
				g_pDisplay->UpdateScaleMode();

			if (IDisplayBackend *backend = ATUIGetDisplayBackend())
				backend->SetFilterMode(ATUIGetDisplayFilterMode());
		}

		void UpdateCustomRefreshRate() override {
			// The SDL frame pacer owns refresh timing.
		}

		void RequestRenderedFrame(vdfunction<void(const VDPixmap *)> fn) override {
			VDPixmapBuffer frame;
			if (ATUICaptureEmulatorFrame(g_sim, ATUIFrameCaptureMode::Display, frame))
				fn(&frame);
			else
				fn(nullptr);
		}

		void SetVideoWriter(IATVideoWriter *writer) override {
			mpVideoWriter = writer;
		}

		IATVideoWriter *GetVideoWriter() const {
			return mpVideoWriter;
		}

		void SetAutoSuggestEnabled(bool enabled) override {
			ATUIAutoSuggest::SetAutoSuggestEnabled(enabled);
		}

		void ShowSuggestions() override {
			ATUIAutoSuggest::ShowSuggestionsOnce();
		}

	private:
		IATVideoWriter *mpVideoWriter = nullptr;
	};

	ATSDLDisplayPaneInterface g_displayPaneInterface;
}

static void RenderDisplayPaneFrameCallback(const ImDrawList *, const ImDrawCmd *cmd) {
	const auto *data = static_cast<const DisplayPaneRenderCallbackData *>(cmd->UserCallbackData);
	if (!data || !data->backend)
		return;

	data->backend->RenderFrameClipped(data->x, data->y, data->w, data->h,
		data->srcW, data->srcH, data->clipX, data->clipY, data->clipW, data->clipH);
}

static void DrawDisplayPaneVideoWriterOverlay(
	ImDrawList *drawList,
	const ImVec2& imagePos,
	const ImVec2& imageSize)
{
	IATVideoWriter *writer = g_displayPaneInterface.GetVideoWriter();
	if (!writer || !g_ATCVRecordingVideoShowMotionVectors)
		return;

	ATVideoRecordingDebugInfo debugInfo;
	if (!writer->GetDebugInfo(debugInfo)
		|| debugInfo.mImageWidth == 0
		|| debugInfo.mImageHeight == 0
		|| debugInfo.mVideoDestRect.empty()
		|| imageSize.x <= 0.0f
		|| imageSize.y <= 0.0f)
	{
		return;
	}

	const float mvScaleX = imageSize.x / debugInfo.mVideoDestRect.width();
	const float mvScaleY = imageSize.y / debugInfo.mVideoDestRect.height();
	const float mvBlockStepX = (float)debugInfo.mBlockWidth * mvScaleX;
	const float mvBlockStepY = (float)debugInfo.mBlockHeight * mvScaleY;

	if (mvBlockStepX <= 0.0f || mvBlockStepY <= 0.0f)
		return;

	if (debugInfo.mMotionVectors.size()
		< (size_t)debugInfo.mNumBlocksX * debugInfo.mNumBlocksY)
	{
		return;
	}

	const auto *mv = debugInfo.mMotionVectors.data();
	drawList->AddRectFilled(
		imagePos,
		ImVec2(imagePos.x + imageSize.x, imagePos.y + imageSize.y),
		IM_COL32(0, 0, 0, 192));

	float y = imagePos.y
		+ 0.5f * mvBlockStepY
		- debugInfo.mVideoDestRect.top * mvScaleY;

	for (uint32 by = 0; by < debugInfo.mNumBlocksY; ++by) {
		float x = imagePos.x
			+ 0.5f * mvBlockStepX
			- debugInfo.mVideoDestRect.left * mvScaleX;

		for (uint32 bx = 0; bx < debugInfo.mNumBlocksX; ++bx) {
			const ImVec2 p0(x, y);
			const ImVec2 p1(x + mv->mX * mvScaleX, y + mv->mY * mvScaleY);

			drawList->AddLine(p0, p1, IM_COL32(255, 255, 255, 255));
			drawList->AddRectFilled(
				ImVec2(x, y),
				ImVec2(x + 1.0f, y + 1.0f),
				IM_COL32(255, 255, 255, 255));

			++mv;
			x += mvBlockStepX;
		}

		y += mvBlockStepY;
	}
}

static void SaveImGuiLayoutNow() {
	ImGuiIO& io = ImGui::GetIO();
	if (io.IniFilename && *io.IniFilename)
		ImGui::SaveIniSettingsToDisk(io.IniFilename);
}

static ImVec4 ComputeDisplayPaneFrameRect(float viewportW, float viewportH) {
	float w = viewportW;
	float h = viewportH;

	if (w < 1.0f)
		w = 1.0f;
	if (h < 1.0f)
		h = 1.0f;

	const auto& gtia = g_sim.GetGTIA();
	const ATDisplayStretchMode stretchMode = ATUIGetDisplayStretchMode();

	if (stretchMode == kATDisplayStretchMode_PreserveAspectRatio
		|| stretchMode == kATDisplayStretchMode_IntegralPreserveAspectRatio)
	{
		int sw = 1;
		int sh = 1;
		bool rgb32 = false;
		gtia.GetRawFrameFormat(sw, sh, rgb32);

		const float fsw = (float)((double)sw * gtia.GetPixelAspectRatio());
		const float fsh = (float)sh;
		float zoom = std::min(w / fsw, h / fsh);

		if (stretchMode == kATDisplayStretchMode_IntegralPreserveAspectRatio && zoom > 1.0f)
			zoom = std::floor(zoom * 1.0001f);

		w = fsw * zoom;
		h = fsh * zoom;
	} else if (stretchMode == kATDisplayStretchMode_SquarePixels
		|| stretchMode == kATDisplayStretchMode_Integral)
	{
		int sw = 1;
		int sh = 1;
		gtia.GetFrameSize(sw, sh);

		const float fsw = (float)sw;
		const float fsh = (float)sh;

		const float continuousRatio = std::min(w / fsw, h / fsh);
		const float integerRatio = std::floor(continuousRatio);
		const bool wasteTooMuch =
			(stretchMode == kATDisplayStretchMode_Integral)
			&& (integerRatio >= 1.0f)
			&& (continuousRatio - integerRatio >= 0.25f * integerRatio);

		if (integerRatio < 1.0f
			|| stretchMode == kATDisplayStretchMode_SquarePixels
			|| wasteTooMuch)
		{
			if (w * fsh < h * fsw)
				h = (fsh * w) / fsw;
			else
				w = (fsw * h) / fsh;
		} else {
			w = fsw * integerRatio;
			h = fsh * integerRatio;
		}
	}

	const float displayZoom = ATUIGetDisplayZoom();
	w *= displayZoom;
	h *= displayZoom;

	const vdfloat2 pan = ATUIGetDisplayPanOffset();
	const vdfloat2 relOrigin = vdfloat2{0.5f, 0.5f} - pan;

	float left = w * (relOrigin.x - 1.0f) + viewportW * 0.5f;
	float top = h * (relOrigin.y - 1.0f) + viewportH * 0.5f;
	float right = w * relOrigin.x + viewportW * 0.5f;
	float bottom = h * relOrigin.y + viewportH * 0.5f;

	float errL = left - std::round(left);
	float errR = right - std::round(right);
	float errT = top - std::round(top);
	float errB = bottom - std::round(bottom);

	left -= 0.5f * (errL + errR);
	right -= 0.5f * (errL + errR);
	top -= 0.5f * (errT + errB);
	bottom -= 0.5f * (errT + errB);

	return ImVec4(left, top, right, bottom);
}

// =========================================================================
// Pane registration — called from debugger.cpp's InitDebugger
// =========================================================================

void ATRegisterUIPaneType(uint32 id, ATPaneCreator creator) {
	for (auto& e : g_paneCreators) {
		if (e.id == id) {
			e.creator = creator;
			e.classCreator = nullptr;
			return;
		}
	}
	g_paneCreators.push_back({id, creator, nullptr});
}

void ATRegisterUIPaneClass(uint32 id, ATPaneClassCreator creator) {
	for (auto& e : g_paneCreators) {
		if (e.id == id) {
			e.creator = nullptr;
			e.classCreator = creator;
			return;
		}
	}
	g_paneCreators.push_back({id, nullptr, creator});
}

static void ATRegisterImGuiPaneType(uint32 id, ATImGuiPaneCreator creator) {
	for (auto& e : g_imguiPaneCreators) {
		if (e.id == id) {
			e.creator = creator;
			return;
		}
	}

	g_imguiPaneCreators.push_back({id, creator});
}

// Forward declarations for pane creation functions
extern void ATUIDebuggerEnsureConsolePane();
extern void ATUIDebuggerEnsureRegistersPane();
extern void ATUIDebuggerEnsureDisassemblyPane();
extern void ATUIDebuggerEnsureHistoryPane();
extern void ATUIDebuggerEnsureMemoryPane(int index);
extern void ATUIDebuggerEnsureWatchPane(int index);
extern void ATUIDebuggerEnsureBreakpointsPane();
extern void ATUIDebuggerEnsureCallStackPane();
extern void ATUIDebuggerEnsureTargetsPane();
extern void ATUIDebuggerEnsureDebugDisplayPane();
extern void ATUIDebuggerEnsurePrinterOutputPane();
extern void ATUIDebuggerEnsureProfileViewPane();
extern void ATUIDebuggerEnsureTraceViewerPane();

static void EnsurePaneExists(uint32 id) {
	for (const auto& e : g_imguiPaneCreators) {
		if (e.id == id) {
			e.creator();
			return;
		}
	}

	// Memory/Watch are native class-style pane families. Keep the indexed
	// dispatch centralized here instead of duplicating four fixed IDs.
	if (id >= kATUIPaneId_MemoryN && id <= kATUIPaneId_MemoryN + 3) {
		ATUIDebuggerEnsureMemoryPane(id - kATUIPaneId_MemoryN);
		return;
	}

	if (id >= kATUIPaneId_WatchN && id <= kATUIPaneId_WatchN + 3) {
		ATUIDebuggerEnsureWatchPane(id - kATUIPaneId_WatchN);
		return;
	}

	for (const auto& e : g_paneCreators) {
		if (e.id == id || (e.classCreator && id >= e.id && id < e.id + 4)) {
			LOG_INFO("Debugger", "ATActivateUIPane(0x%x) — native pane is registered but has no SDL ImGui factory", id);
			return;
		}
	}

	LOG_INFO("Debugger", "ATActivateUIPane(0x%x) — no ImGui pane implemented yet", id);
}

void ATActivateUIPane(uint32 id, bool giveFocus, bool visible, uint32 relid, int reldock) {
	// Check if pane already exists
	for (auto& e : g_debugPanes) {
		if (e.id == id) {
			e.pane->SetVisible(visible);
			if (giveFocus) {
				e.pane->RequestFocus();
				g_activePaneId = id;
			}
			return;
		}
	}

	// Pane doesn't exist — create it on demand
	EnsurePaneExists(id);

	// Try again after creation
	for (auto& e : g_debugPanes) {
		if (e.id == id) {
			e.pane->SetVisible(visible);
			if (giveFocus) {
				e.pane->RequestFocus();
				g_activePaneId = id;
			}
			return;
		}
	}
}

// =========================================================================
// Pane manager API
// =========================================================================

void ATUIDebuggerRegisterPane(ATImGuiDebuggerPane *pane) {
	if (!pane) return;

	// Remove existing pane with same ID
	for (auto it = g_debugPanes.begin(); it != g_debugPanes.end(); ++it) {
		if (it->id == pane->GetPaneId()) {
			IATDebugger *dbg = ATGetDebugger();
			if (dbg)
				dbg->RemoveClient(it->pane);
			g_debugPanes.erase(it);
			break;
		}
	}

	PaneEntry entry;
	entry.id = pane->GetPaneId();
	entry.pane = pane;
	g_debugPanes.push_back(entry);

	// Register as debugger client for state updates
	IATDebugger *dbg = ATGetDebugger();
	if (dbg && dbg->IsEnabled())
		dbg->AddClient(pane, true);
}

void ATUIDebuggerUnregisterPane(uint32 paneId) {
	for (auto it = g_debugPanes.begin(); it != g_debugPanes.end(); ++it) {
		if (it->id == paneId) {
			IATDebugger *dbg = ATGetDebugger();
			if (dbg)
				dbg->RemoveClient(it->pane);
			g_debugPanes.erase(it);
			return;
		}
	}
}

ATImGuiDebuggerPane *ATUIDebuggerGetPane(uint32 paneId) {
	for (auto& e : g_debugPanes) {
		if (e.id == paneId)
			return e.pane;
	}
	return nullptr;
}

void *ATUIDebuggerGetPaneAs(uint32 paneId, uint32 iid) {
	if (paneId == kATUIPaneId_Display && iid == IATDisplayPane::kTypeID)
		return &g_displayPaneInterface;

	ATImGuiDebuggerPane *pane = ATUIDebuggerGetPane(paneId);
	return pane ? pane->AsPaneInterface(iid) : nullptr;
}

IATDisplayPane *ATUIDebuggerGetDisplayPaneInterface() {
	return &g_displayPaneInterface;
}

// =========================================================================
// Init / Shutdown
// =========================================================================

static bool ATConsoleConfirmScriptLoadSDL() {
	ATUIGenericDialogOptions opts {};

	opts.mpCaption = L"Debugger script found";
	opts.mpTitle = L"Debugger script found";
	opts.mpMessage =
		L"Do you want to run the debugger script that is included with this image?\n"
		L"\n"
		L"Debugger scripts are powerful and should only be run for programs you are debugging and from sources that you trust. "
		L"Automatic debugger script loading and confirmation settings can be toggled in the Debugger section of Configure System.";
	opts.mpIgnoreTag = "RunDebuggerScript";
	opts.mIconType = kATUIGenericIconType_Warning;
	opts.mResultMask = kATUIGenericResultMask_OKCancel;
	opts.mValidIgnoreMask = kATUIGenericResultMask_OKCancel;
	opts.mAspectLimit = 4.0f;

	bool wasIgnored = false;
	opts.mpCustomIgnoreFlag = &wasIgnored;

	const bool result = (ATUIShowGenericDialogAutoCenter(opts) == kATUIGenericResult_OK);

	if (wasIgnored) {
		if (IATDebugger *dbg = ATGetDebugger()) {
			dbg->SetScriptAutoLoadMode(result
				? ATDebuggerScriptAutoLoadMode::Enabled
				: ATDebuggerScriptAutoLoadMode::Disabled);
		}
	}

	return result;
}

static bool g_debuggerFileRequestOverrideActive = false;
static VDStringW g_debuggerFileRequestOverridePath;

#if !defined(__EMSCRIPTEN__)
struct ATDebuggerFileDialogSyncState {
	std::mutex mMutex;
	std::condition_variable mCondition;
	VDStringW mPath;
	bool mbDone = false;
};

static void SDLCALL ATDebuggerFileDialogCallback(void *userdata,
	const char * const *filelist, int)
{
	auto *state = static_cast<ATDebuggerFileDialogSyncState *>(userdata);
	if (!state)
		return;

	VDStringW path;
	if (filelist && filelist[0])
		path = VDTextU8ToW(filelist[0], -1);
	else if (!filelist)
		LOG_ERROR("Debugger", "Debugger file dialog failed: %s", SDL_GetError());

	{
		std::lock_guard<std::mutex> lock(state->mMutex);
		state->mPath = path;
		state->mbDone = true;
	}
	state->mCondition.notify_one();
}
#endif

static void ATConsoleRequestFileSDL(ATDebuggerRequestFileEvent& event) {
	if (g_debuggerFileRequestOverrideActive) {
		event.mPath = g_debuggerFileRequestOverridePath;
		return;
	}

#if defined(__EMSCRIPTEN__)
	LOG_ERROR("Debugger", "Debugger file request ignored: synchronous file requests are unsupported on WebAssembly");
	event.mPath.clear();
	return;
#else
	if (!SDL_IsMainThread()) {
		LOG_ERROR("Debugger", "Debugger file request ignored off the SDL main thread");
		return;
	}

	static const SDL_DialogFileFilter kAllFilesFilter[] = {
		{ "All files", "*" },
	};

	ATDebuggerFileDialogSyncState state;

	if (event.mbSave) {
		SDL_ShowSaveFileDialog(ATDebuggerFileDialogCallback, &state, g_pWindow,
			kAllFilesFilter, 1, nullptr);
	} else {
		SDL_ShowOpenFileDialog(ATDebuggerFileDialogCallback, &state, g_pWindow,
			kAllFilesFilter, 1, nullptr, false);
	}

	std::unique_lock<std::mutex> lock(state.mMutex);
	while (!state.mbDone) {
		lock.unlock();
		SDL_PumpEvents();
		lock.lock();
		state.mCondition.wait_for(lock, std::chrono::milliseconds(50));
	}

	event.mPath = state.mPath;
#endif
}

bool ATUIDebuggerRequestFileForTest(bool save, const char *utf8Path, VDStringW& outPath) {
	ATDebuggerRequestFileEvent event {};
	event.mbSave = save;

	g_debuggerFileRequestOverridePath = utf8Path ? VDTextU8ToW(utf8Path, -1) : VDStringW();
	g_debuggerFileRequestOverrideActive = true;
	ATConsoleRequestFileSDL(event);
	g_debuggerFileRequestOverrideActive = false;
	g_debuggerFileRequestOverridePath.clear();

	outPath = event.mPath;
	return true;
}

void ATUIDebuggerInit() {
	ATRegisterImGuiPaneType(kATUIPaneId_Console, ATUIDebuggerEnsureConsolePane);
	ATRegisterImGuiPaneType(kATUIPaneId_Registers, ATUIDebuggerEnsureRegistersPane);
	ATRegisterImGuiPaneType(kATUIPaneId_Disassembly, ATUIDebuggerEnsureDisassemblyPane);
	ATRegisterImGuiPaneType(kATUIPaneId_History, ATUIDebuggerEnsureHistoryPane);
	ATRegisterImGuiPaneType(kATUIPaneId_Breakpoints, ATUIDebuggerEnsureBreakpointsPane);
	ATRegisterImGuiPaneType(kATUIPaneId_CallStack, ATUIDebuggerEnsureCallStackPane);
	ATRegisterImGuiPaneType(kATUIPaneId_Targets, ATUIDebuggerEnsureTargetsPane);
	ATRegisterImGuiPaneType(kATUIPaneId_DebugDisplay, ATUIDebuggerEnsureDebugDisplayPane);
	ATRegisterImGuiPaneType(kATUIPaneId_PrinterOutput, ATUIDebuggerEnsurePrinterOutputPane);
	ATRegisterImGuiPaneType(kATUIPaneId_Profiler, ATUIDebuggerEnsureProfileViewPane);
	ATRegisterImGuiPaneType(kATUIPaneId_PerformanceAnalyzerSDL, ATUIDebuggerEnsureTraceViewerPane);

	if (IATDebugger *dbg = ATGetDebugger()) {
		dbg->SetScriptAutoLoadConfirmFn(ATConsoleConfirmScriptLoadSDL);
		dbg->SetOnRequestFile(ATConsoleRequestFileSDL);
	}
}

extern void ATUIDebuggerClearSourceWindows();

void ATUIDebuggerShutdown() {
	IATDebugger *dbg = ATGetDebugger();
	for (auto& e : g_debugPanes) {
		if (dbg)
			dbg->RemoveClient(e.pane);
	}
	g_debugPanes.clear();
	g_paneCreators.clear();
	g_imguiPaneCreators.clear();
	g_debuggerOpen = false;
	ATUIDebuggerClearSourceWindows();
}

// =========================================================================
// Display pane — renders emulation texture inside an ImGui dockable window
// =========================================================================

static void RenderDisplayPane() {
	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
	bool open = true;
	if (ImGui::Begin("Display", &open)) {
		IDisplayBackend *backend = ATUIGetDisplayBackend();
		if (backend && backend->HasTexture()) {
			ImVec2 avail = ImGui::GetContentRegionAvail();
			if (avail.x > 0 && avail.y > 0) {
				float texW = (float)backend->GetTextureWidth();
				float texH = (float)backend->GetTextureHeight();
				if (texW > 0 && texH > 0) {
					const ImVec2 viewportPos = ImGui::GetCursorScreenPos();
					const ImVec4 frameRect = ComputeDisplayPaneFrameRect(avail.x, avail.y);
					const ImVec2 imagePos(
						viewportPos.x + frameRect.x,
						viewportPos.y + frameRect.y);
					const ImVec2 imageSize(
						frameRect.z - frameRect.x,
						frameRect.w - frameRect.y);

					ImGui::InvisibleButton("##DisplayFrameViewport", avail);

					DisplayPaneRenderCallbackData cbData;
					cbData.backend = backend;
					cbData.x = imagePos.x;
					cbData.y = imagePos.y;
					cbData.w = imageSize.x;
					cbData.h = imageSize.y;
					cbData.clipX = viewportPos.x;
					cbData.clipY = viewportPos.y;
					cbData.clipW = avail.x;
					cbData.clipH = avail.y;
					cbData.srcW = backend->GetTextureWidth();
					cbData.srcH = backend->GetTextureHeight();

					ImDrawList *drawList = ImGui::GetWindowDrawList();
					drawList->AddCallback(RenderDisplayPaneFrameCallback, &cbData, sizeof cbData);
					drawList->AddCallback(ImDrawCallback_ResetRenderState, nullptr);
					DrawDisplayPaneVideoWriterOverlay(drawList, imagePos, imageSize);

					// Text selection: handle mouse drag and draw highlight overlay.
					// Always call when a drag is active (mouse may leave window);
					// otherwise only when the Display window is hovered.
					ATTextSelectionState& sel = ATUIGetTextSelection();
					if (sel.mbDragActive || ImGui::IsItemHovered())
						ATUITextSelectionHandleMouse(imagePos, imageSize);
					ATUITextSelectionDrawOverlay(imagePos, imageSize);
				}
			}
		} else {
			ImGui::TextDisabled("(no display)");
		}
	}
	ImGui::End();
	ImGui::PopStyleVar();
}

// =========================================================================
// Render all panes
// =========================================================================

// Apply default docking layout using ImGui DockBuilder.
// Extends Windows ATLoadDefaultPaneLayout (console.cpp:921) with additional
// panes (Memory, Watch, Call Stack) in a resolution-independent layout:
//   ┌───────────┬──────────┬────────┬──────────────┐
//   │           │          │ Regs   │              │
//   │  Display  │ Memory 1 ├────────┤ Disassembly  │
//   │           │          │ CStk   │              │
//   │           ├──────────┤────────┤              │
//   │           │ Watch 1  │ Hist   │              │
//   ├───────────┴──────────┴────────┴──────────────┤
//   │                    Console                    │
//   └──────────────────────────────────────────────┘
static void ApplyDefaultDockLayout() {
	if (g_dockLayoutApplied)
		return;
	g_dockLayoutApplied = true;

	ImGuiID dockspace_id = ImGui::GetID("DebuggerDockSpace");

	// Only apply if the dockspace has no saved layout yet.
	// DockSpace() auto-creates an empty node, so check for children
	// to distinguish "has real layout" from "just auto-created".
	ImGuiDockNode *node = ImGui::DockBuilderGetNode(dockspace_id);
	LOG_INFO("DockLayout", "dockspace_id=0x%08X node=%p children=%p",
		dockspace_id, (void*)node,
		node ? (void*)node->ChildNodes[0] : nullptr);
	if (node != nullptr && node->ChildNodes[0] != nullptr) {
		LOG_INFO("DockLayout", "Skipping — saved layout exists");
		return;
	}

	LOG_INFO("DockLayout", "Applying default layout");
	ImGui::DockBuilderRemoveNode(dockspace_id);
	ImGui::DockBuilderAddNode(dockspace_id, ImGuiDockNodeFlags_DockSpace);

	// Use the host window's actual size (viewport minus menu bar)
	ImVec2 hostSize = ImGui::GetWindowSize();
	ImGui::DockBuilderSetNodeSize(dockspace_id, hostSize);

	// Split: bottom for Console (~19% height), top for the rest
	ImGuiID dock_top, dock_bottom;
	ImGui::DockBuilderSplitNode(dockspace_id, ImGuiDir_Down, 0.19f, &dock_bottom, &dock_top);

	// Split top: Disassembly on the far right (~24% width)
	ImGuiID dock_top_left, dock_disasm;
	ImGui::DockBuilderSplitNode(dock_top, ImGuiDir_Right, 0.24f, &dock_disasm, &dock_top_left);

	// Split remaining: Display on the left (~39% of total width)
	// dock_top_left is ~76% of total; Display should be ~39/76 ≈ 51%
	ImGuiID dock_display, dock_middle;
	ImGui::DockBuilderSplitNode(dock_top_left, ImGuiDir_Left, 0.52f, &dock_display, &dock_middle);

	// Split middle into: left column (Memory/Watch) and right column (Regs/CallStack/History)
	// Memory+Watch ~26% of total, Regs column ~10.4% of total
	// In this remaining space: Regs = 10.4/(26+10.4) ≈ 29%
	ImGuiID dock_mem_watch, dock_regs_col;
	ImGui::DockBuilderSplitNode(dock_middle, ImGuiDir_Right, 0.29f, &dock_regs_col, &dock_mem_watch);

	// Split Memory/Watch column vertically: Memory ~71%, Watch ~29%
	ImGuiID dock_memory, dock_watch;
	ImGui::DockBuilderSplitNode(dock_mem_watch, ImGuiDir_Down, 0.29f, &dock_watch, &dock_memory);

	// Split Registers column vertically into 3: Registers ~27%, Call Stack ~23%, History ~50%
	ImGuiID dock_regs_top, dock_history;
	ImGui::DockBuilderSplitNode(dock_regs_col, ImGuiDir_Down, 0.50f, &dock_history, &dock_regs_top);
	ImGuiID dock_registers, dock_callstack;
	ImGui::DockBuilderSplitNode(dock_regs_top, ImGuiDir_Down, 0.46f, &dock_callstack, &dock_registers);

	LOG_INFO("DockLayout", "hostSize=%.0fx%.0f", hostSize.x, hostSize.y);
	LOG_INFO("DockLayout", "dock IDs: display=0x%08X memory=0x%08X watch=0x%08X regs=0x%08X callstack=0x%08X history=0x%08X disasm=0x%08X bottom=0x%08X",
		dock_display, dock_memory, dock_watch, dock_registers, dock_callstack, dock_history, dock_disasm, dock_bottom);

	// Dock panes into layout
	ImGui::DockBuilderDockWindow("Display", dock_display);
	ImGui::DockBuilderDockWindow("Console", dock_bottom);
	ImGui::DockBuilderDockWindow("Memory 1", dock_memory);
	ImGui::DockBuilderDockWindow("Watch 1", dock_watch);
	ImGui::DockBuilderDockWindow("Registers", dock_registers);
	ImGui::DockBuilderDockWindow("Call Stack", dock_callstack);
	ImGui::DockBuilderDockWindow("History", dock_history);
	ImGui::DockBuilderDockWindow("Disassembly", dock_disasm);

	ImGui::DockBuilderFinish(dockspace_id);
	LOG_INFO("DockLayout", "Finished applying default layout");
}

void ATUIDebuggerRenderPanes(ATSimulator &sim, ATUIState &state) {
	if (!g_debuggerOpen) {
		g_focusedPaneId = 0;

		for (auto& e : g_debugPanes) {
			if (e.id != kATUIPaneId_PerformanceAnalyzerSDL)
				continue;

			e.pane->OnFrame();

			if (!e.pane->IsVisible())
				continue;

			if (!e.pane->Render())
				e.pane->SetVisible(false);
		}

		return;
	}

	// Create a full-window dockspace below the menu bar.
	// All debugger panes (including Display) dock into this space.
	ImGuiViewport *viewport = ImGui::GetMainViewport();

	float menuBarHeight = ImGui::GetFrameHeight();
	ImVec2 dockPos(viewport->WorkPos.x, viewport->WorkPos.y + menuBarHeight);
	ImVec2 dockSize(viewport->WorkSize.x, viewport->WorkSize.y - menuBarHeight);

	ImGui::SetNextWindowPos(dockPos);
	ImGui::SetNextWindowSize(dockSize);
	ImGui::SetNextWindowViewport(viewport->ID);

	ImGuiWindowFlags hostFlags =
		ImGuiWindowFlags_NoTitleBar |
		ImGuiWindowFlags_NoCollapse |
		ImGuiWindowFlags_NoResize |
		ImGuiWindowFlags_NoMove |
		ImGuiWindowFlags_NoBringToFrontOnFocus |
		ImGuiWindowFlags_NoNavFocus |
		ImGuiWindowFlags_NoBackground;

	ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
	ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
	ImGui::Begin("DebuggerDockHost", nullptr, hostFlags);
	ImGui::PopStyleVar(3);

	// GetID must be called inside DebuggerDockHost's Begin/End so the
	// ID stack is consistent between ApplyDefaultDockLayout and DockSpace.
	ImGuiID dockspace_id = ImGui::GetID("DebuggerDockSpace");

	// Apply default layout BEFORE DockSpace() — DockBuilder must configure
	// the node tree before the dockspace is submitted for this frame.
	ApplyDefaultDockLayout();

	ImGui::DockSpace(dockspace_id, ImVec2(0, 0), ImGuiDockNodeFlags_None);

	ImGui::End();

	// Render the Display pane (emulation texture inside a dockable window)
	RenderDisplayPane();

	// Render all debugger panes and track which one has focus.
	// Push the monospace font so all debugger pane content uses it — this
	// matches Windows Altirra where debugger panes use a fixed-width font.
	ImFont *monoFont = ATUIGetFontMono();
	if (monoFont)
		ImGui::PushFont(monoFont);

	g_focusedPaneId = 0;
	for (auto& e : g_debugPanes) {
		e.pane->OnFrame();

		if (!e.pane->IsVisible())
			continue;

		if (!e.pane->Render()) {
			// Pane closed via X button
			e.pane->SetVisible(false);
		}

		if (e.pane->HasFocus()) {
			g_focusedPaneId = e.id;
			g_activePaneId = e.id;
		}
	}

	if (monoFont)
		ImGui::PopFont();
}

// =========================================================================
// Tick
// =========================================================================

void ATUIDebuggerTick() {
	IATDebugger *dbg = ATGetDebugger();
	if (!dbg)
		return;

	// Process all pending commands, not just one.  Windows does this via
	// the idle loop (Tick returns true → loop again immediately).  We
	// replicate by looping here, with a safety limit to avoid blocking
	// the UI if a command generates infinite sub-commands.
	//
	// No IsEnabled() guard — Windows calls Tick() unconditionally, and
	// --debugcmd queues commands before the debugger UI is opened.
	// Tick() is a no-op when no commands are queued.
	for (int safety = 0; safety < 1000; ++safety) {
		if (!dbg->Tick())
			break;
	}
}

// =========================================================================
// Open / Close
// =========================================================================

void ATUIDebuggerOpen() {
	if (g_debuggerOpen)
		return;

	IATDebugger *dbg = ATGetDebugger();
	if (!dbg)
		return;

	SaveImGuiLayoutNow();

	g_debuggerOpen = true;
	g_dockLayoutApplied = false;	// reset so layout is applied on next render
	dbg->SetEnabled(true);

	// Match Windows: do NOT break on open.  The console input is greyed
	// out while the simulator is running.  The user can break manually
	// with F5 (Run/Break) or the menu.

	// Register all existing panes as debugger clients before showing
	// the banner (which generates console output).
	for (auto& e : g_debugPanes) {
		dbg->AddClient(e.pane, true);
	}

	// Create default panes — extends Windows ATLoadDefaultPaneLayout
	// with Memory 1, Watch 1, and Call Stack for a richer default.
	EnsurePaneExists(kATUIPaneId_Console);
	EnsurePaneExists(kATUIPaneId_Registers);
	EnsurePaneExists(kATUIPaneId_Disassembly);
	EnsurePaneExists(kATUIPaneId_History);
	EnsurePaneExists(kATUIPaneId_MemoryN);
	EnsurePaneExists(kATUIPaneId_WatchN);
	EnsurePaneExists(kATUIPaneId_CallStack);

	dbg->ShowBannerOnce();
}

void ATUIDebuggerClose() {
	if (!g_debuggerOpen)
		return;

	SaveImGuiLayoutNow();

	IATDebugger *dbg = ATGetDebugger();
	if (dbg) {
		for (auto& e : g_debugPanes) {
			dbg->RemoveClient(e.pane);
		}
		dbg->SetEnabled(false);
	}

	g_debuggerOpen = false;
	g_focusedPaneId = 0;
	g_activePaneId = 0;
}

bool ATUIDebuggerIsOpen() {
	return g_debuggerOpen;
}

// =========================================================================
// Focus management
// =========================================================================

uint32 ATUIDebuggerGetFocusedPaneId() {
	return g_focusedPaneId;
}

void ATUIDebuggerFocusConsole() {
	ATActivateUIPane(kATUIPaneId_Console, true, true);
}

bool ATUIDebuggerHandleTextInput(const char *text) {
	if (!g_debuggerOpen || !text || !*text)
		return false;

	if (ImGui::GetIO().WantTextInput)
		return false;

	if (g_focusedPaneId != kATUIPaneId_Disassembly)
		return false;

	ATUIDebuggerFocusConsoleWithText(text);
	return true;
}

// =========================================================================
// Source mode detection — matches Windows ATUIGetDebugSrcMode()
// =========================================================================

static ATDebugSrcMode GetDebugSrcMode() {
	uint32 focusId = g_focusedPaneId;

	if (focusId == kATUIPaneId_Disassembly)
		return kATDebugSrcMode_Disasm;

	if (focusId >= kATUIPaneId_Source)
		return kATDebugSrcMode_Source;

	return kATDebugSrcMode_Same;
}

// =========================================================================
// Debug commands (for menu/shortcut wiring)
// =========================================================================

void ATUIDebuggerRun() {
	IATDebugger *dbg = ATGetDebugger();
	if (!dbg)
		return;

	if (!dbg->IsEnabled()) {
		ATUIDebuggerOpen();
		return;
	}

	if (dbg->IsRunning() || dbg->AreCommandsQueued())
		return;

	if (ATImGuiDebuggerPane *pane = ATUIDebuggerGetPane(g_focusedPaneId)) {
		if (pane->OnPaneCommand(kATUIPaneCommandId_DebugRun))
			return;
	}

	dbg->Run(GetDebugSrcMode());
}

void ATUIDebuggerRunStop() {
	IATDebugger *dbg = ATGetDebugger();
	if (!dbg) return;

	if (!dbg->IsEnabled()) {
		ATUIDebuggerOpen();
		return;
	}

	// Match Windows cmddebug.cpp OnCommandDebugRunStop:
	// break if running OR if commands are still queued
	if (dbg->IsRunning() || dbg->AreCommandsQueued()) {
		ATOpenConsole();  // no-op if already open, but ensures console exists
		dbg->Break();
	} else {
		dbg->Run(GetDebugSrcMode());
	}
}

void ATUIDebuggerBreak() {
	IATDebugger *dbg = ATGetDebugger();
	if (!dbg)
		return;

	if (!dbg->IsEnabled()) {
		ATUIDebuggerOpen();
		return;
	}

	dbg->Break();
}

void ATUIDebuggerToggleBreakpoint() {
	IATDebugger *dbg = ATGetDebugger();
	if (!dbg || !dbg->IsEnabled())
		return;

	uint32 paneId = g_focusedPaneId ? g_focusedPaneId : g_activePaneId;
	if (ATImGuiDebuggerPane *pane = ATUIDebuggerGetPane(paneId))
		pane->OnPaneCommand(kATUIPaneCommandId_DebugToggleBreakpoint);
}

void ATUIDebuggerStepInto() {
	IATDebugger *dbg = ATGetDebugger();
	if (!dbg)
		return;

	if (!dbg->IsEnabled()) {
		ATUIDebuggerOpen();
		return;
	}

	if (ATImGuiDebuggerPane *pane = ATUIDebuggerGetPane(g_focusedPaneId)) {
		if (pane->OnPaneCommand(kATUIPaneCommandId_DebugStepInto))
			return;
	}

	try {
		dbg->StepInto(GetDebugSrcMode());
	} catch(const MyError& e) {
		ATConsolePrintf("%s\n", e.c_str());
	}
}

void ATUIDebuggerStepOver() {
	IATDebugger *dbg = ATGetDebugger();
	if (!dbg)
		return;

	if (!dbg->IsEnabled()) {
		ATUIDebuggerOpen();
		return;
	}

	if (ATImGuiDebuggerPane *pane = ATUIDebuggerGetPane(g_focusedPaneId)) {
		if (pane->OnPaneCommand(kATUIPaneCommandId_DebugStepOver))
			return;
	}

	try {
		dbg->StepOver(GetDebugSrcMode());
	} catch(const MyError& e) {
		ATConsolePrintf("%s\n", e.c_str());
	}
}

void ATUIDebuggerStepOut() {
	IATDebugger *dbg = ATGetDebugger();
	if (!dbg)
		return;

	if (!dbg->IsEnabled()) {
		ATUIDebuggerOpen();
		return;
	}

	if (ATImGuiDebuggerPane *pane = ATUIDebuggerGetPane(g_focusedPaneId)) {
		if (pane->OnPaneCommand(kATUIPaneCommandId_DebugStepOut))
			return;
	}

	try {
		dbg->StepOut(GetDebugSrcMode());
	} catch(const MyError& e) {
		ATConsolePrintf("%s\n", e.c_str());
	}
}

void ATUIDebuggerFocusDisplay() {
	// Focus the Display pane in the debugger dockspace
	ImGui::SetWindowFocus("Display");
}

// =========================================================================
// Window management — Close / Undock / Cycle panes
// =========================================================================

void ATUIDebuggerClosePaneById(uint32 paneId) {
	for (auto& e : g_debugPanes) {
		if (e.id == paneId) {
			e.pane->SetVisible(false);
			return;
		}
	}
}

bool ATUIDebuggerHasVisiblePanes() {
	for (auto& e : g_debugPanes) {
		if (e.pane->IsVisible())
			return true;
	}
	return false;
}

void ATUIDebuggerCloseActivePane() {
	if (!g_debuggerOpen || g_focusedPaneId == 0)
		return;

	// Find and close the focused pane
	for (auto& e : g_debugPanes) {
		if (e.id == g_focusedPaneId) {
			e.pane->SetVisible(false);
			g_focusedPaneId = 0;
			if (g_activePaneId == e.id)
				g_activePaneId = 0;
			return;
		}
	}
}

void ATUIDebuggerUndockActivePane() {
	if (!g_debuggerOpen || g_focusedPaneId == 0)
		return;

	// Find the focused pane and undock it from the docking layout
	for (auto& e : g_debugPanes) {
		if (e.id == g_focusedPaneId) {
			ImGuiWindow *window = ImGui::FindWindowByName(e.pane->GetTitle());
			if (window && window->DockId != 0) {
				// Setting DockId to 0 undocks the window on the next frame
				ImGui::SetWindowDock(window, 0, ImGuiCond_Always);
			}
			return;
		}
	}
}

void ATUIDebuggerCyclePane(int direction) {
	if (!g_debuggerOpen)
		return;

	// Build ordered list of visible pane indices
	std::vector<size_t> visibleIndices;
	size_t currentIdx = SIZE_MAX;

	for (size_t i = 0; i < g_debugPanes.size(); ++i) {
		if (g_debugPanes[i].pane->IsVisible()) {
			if (g_debugPanes[i].id == g_focusedPaneId)
				currentIdx = visibleIndices.size();
			visibleIndices.push_back(i);
		}
	}

	if (visibleIndices.empty())
		return;

	// If no pane is focused, start from the first one
	if (currentIdx == SIZE_MAX)
		currentIdx = 0;

	// Advance with wraparound
	int n = (int)visibleIndices.size();
	int next = ((int)currentIdx + direction + n) % n;

	auto& target = g_debugPanes[visibleIndices[next]];
	ImGui::SetWindowFocus(target.pane->GetTitle());
	g_focusedPaneId = target.id;
	g_activePaneId = target.id;
}
