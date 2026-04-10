//	AltirraSDL - Dear ImGui UI core
//	Init/shutdown, deferred action queue, status overlay, frame rendering,
//	compatibility warning, exit confirmation, and small dialogs.
//	Menu bar is in ui_menus.cpp, recording in ui_recording.cpp,
//	other dialogs in their own files (ui_system.cpp, ui_disk.cpp, etc.)

#include <stdafx.h>
#include <SDL3/SDL.h>
#include <imgui.h>
#include <imgui_impl_sdl3.h>
#include <imgui_impl_sdlrenderer3.h>
#include <imgui_impl_opengl3.h>
#include "display_backend.h"
#include "display_backend_gl33.h"

#include <vd2/system/vdtypes.h>
#include <vd2/system/VDString.h>
#include <vd2/system/text.h>
#include <vd2/system/registry.h>
#include <at/atcore/media.h>
#include <at/atcore/device.h>
#include <at/atio/image.h>
#include <at/atio/cartridgeimage.h>
#include <at/atio/cartridgetypes.h>

#include <vd2/system/error.h>
#include <vd2/system/file.h>
#include <vd2/system/filesys.h>
#include <at/atio/cassetteimage.h>

#include <at/atcore/serializable.h>

#include "ui_main.h"
#include "ui_main_internal.h"
#include "ui_mobile.h"
#include "ui_debugger.h"
#include "ui_testmode.h"
#include "ui_progress.h"
#include "ui_emuerror.h"
#include "ui_virtual_keyboard.h"
#include "display_sdl3_impl.h"
#include "simulator.h"
#include "mediamanager.h"
#include "gtia.h"
#include "cartridge.h"
#include "cassette.h"
#include "diskinterface.h"
#include "debugger.h"
#include <algorithm>
#include "videowriter.h"
#include <at/ataudio/pokey.h>
#include "uiaccessors.h"
#include "uiconfirm.h"
#include "uikeyboard.h"
#include "uitypes.h"
#include "options.h"
#include "sapconverter.h"
#include "firmwaremanager.h"
#include "oshelper.h"
#include "resource.h"
#include "compatengine.h"
#include "compatdb.h"
#include "uicompat.h"
#include "logging.h"
#include "ui_fonts.h"

extern ATSimulator g_sim;
extern ATUIKeyboardOptions g_kbdOpts;

// Display backend accessor — ui_rewind.cpp and ui_screenfx.cpp use this
// to create preview textures.  Returns nullptr until the backend is wired.
static IDisplayBackend *s_pDisplayBackend = nullptr;
IDisplayBackend *ATUIGetDisplayBackend() { return s_pDisplayBackend; }
void ATUISetDisplayBackend(IDisplayBackend *backend) { s_pDisplayBackend = backend; }

// Recording functions (defined in ui_recording.cpp)
void ATUIStartAudioRecording(const wchar_t *path, bool raw);
void ATUIStartSAPRecording(const wchar_t *path);
void ATUIStartVideoRecording(const wchar_t *path, ATVideoEncoding encoding);
void ATUIStartVGMRecording(const wchar_t *path);

// =========================================================================
// Deferred action queue — thread-safe handoff from file dialog callbacks
//
// SDL3 file dialog callbacks may run on a background thread (platform-
// dependent).  All simulator mutations must happen on the main thread.
// Callbacks push VDStringW paths here; the main loop drains the queue
// each frame via ATUIPollDeferredActions().
// =========================================================================

#include <mutex>
#include <vector>

// ATDeferredActionType enum is now in ui_main.h

struct ATDeferredAction {
	ATDeferredActionType type;
	VDStringW path;
	VDStringW path2;   // second path for two-file operations (SAP->EXE, tape analysis)
	int mInt = 0;
};

static std::mutex g_deferredMutex;
static std::vector<ATDeferredAction> g_deferredActions;

// Tools result popup state
static bool g_showToolsResult = false;
static VDStringA g_toolsResultMessage;

// Export ROM Set confirmation state
static bool g_showExportROMOverwrite = false;
static VDStringW g_exportROMPath;

// Forward declarations for tools
static void ATUIDoExportROMSet(const VDStringW &targetDir);

void ATUIPushDeferred(ATDeferredActionType type, const char *utf8path, int extra) {
	ATDeferredAction action;
	action.type = type;
	action.path = VDTextU8ToW(utf8path, -1);
	action.mInt = extra;

	std::lock_guard<std::mutex> lock(g_deferredMutex);
	g_deferredActions.push_back(std::move(action));
}

void ATUIPushDeferred2(ATDeferredActionType type, const char *utf8path1, const char *utf8path2) {
	ATDeferredAction action;
	action.type = type;
	action.path = VDTextU8ToW(utf8path1, -1);
	action.path2 = VDTextU8ToW(utf8path2, -1);

	std::lock_guard<std::mutex> lock(g_deferredMutex);
	g_deferredActions.push_back(std::move(action));
}

// Export ROM Set implementation — writes internal ROMs to a user-selected folder.
static void ATUIDoExportROMSet(const VDStringW &targetDir) {
	static const struct {
		ATFirmwareId mId;
		const wchar_t *mpFilename;
	} kOutputs[] = {
		{ kATFirmwareId_Invalid,       L"readme.html" },
		{ kATFirmwareId_Basic_ATBasic, L"atbasic.rom" },
		{ kATFirmwareId_Kernel_LLE,    L"altirraos-800.rom" },
		{ kATFirmwareId_Kernel_LLEXL,  L"altirraos-xl.rom" },
		{ kATFirmwareId_Kernel_816,    L"altirraos-816.rom" },
		{ kATFirmwareId_5200_LLE,      L"altirraos-5200.rom" },
	};

	vdfastvector<uint8> buf;

	try {
		for (auto &&out : kOutputs) {
			if (out.mId == kATFirmwareId_Invalid)
				ATLoadMiscResource(IDR_ROMSETREADME, buf);
			else
				ATLoadInternalFirmware(out.mId, nullptr, 0, 0, nullptr, nullptr, &buf);

			VDFile f(VDMakePath(targetDir, VDStringSpanW(out.mpFilename)).c_str(),
				nsVDFile::kWrite | nsVDFile::kCreateAlways | nsVDFile::kSequential);
			f.write(buf.data(), (long)buf.size());
		}

		g_toolsResultMessage = "ROM set successfully exported.";
		g_showToolsResult = true;
	} catch (const MyError &e) {
		g_toolsResultMessage = VDStringA("Export failed: ") + e.c_str();
		g_showToolsResult = true;
	}
}

// Called from main loop each frame — processes deferred file dialog results.
void ATUIPollDeferredActions();

// g_cartMapperPending and cartridge mapper dialog are in ui_cartmapper.cpp

// =========================================================================
// Save Frame state — file path set by dialog callback, consumed by render
// =========================================================================
std::mutex g_saveFrameMutex;
VDStringA g_saveFramePath;

bool g_copyFrameRequested = false;
// Deferred clipboard capture: set when g_copyFrameRequested is consumed,
// cleared and captured at the start of the NEXT frame before ImGui renders.
static bool g_copyFramePending = false;
ATUIDragDropState g_dragDropState;

void ATUISaveFrameCallback(void *, const char * const *filelist, int) {
	if (!filelist || !filelist[0]) return;
	std::lock_guard<std::mutex> lock(g_saveFrameMutex);
	g_saveFramePath = filelist[0];
}
// =========================================================================
// Deferred action execution (main thread only)
// =========================================================================

void ATUIPollDeferredActions() {
	std::vector<ATDeferredAction> actions;
	{
		std::lock_guard<std::mutex> lock(g_deferredMutex);
		actions.swap(g_deferredActions);
	}

	for (auto& a : actions) {
		try {
			switch (a.type) {
			case kATDeferred_BootImage:
			case kATDeferred_OpenImage: {
				// Matches Windows DoLoadStream retry loop (main.cpp:1186-1369):
				// 1. Unload storage before boot (per user-configured mask)
				// 2. Load with retry loop for hardware mode switching, BASIC conflicts
				// 3. Cold reset (boot only)
				// 4. Check compatibility
				// 5. Resume

				extern ATOptions g_ATOptions;
				if (a.type == kATDeferred_BootImage)
					g_sim.UnloadAll(ATUIGetBootUnloadStorageMask());

				vdfastvector<uint8> captureBuffer;
				ATCartLoadContext cartCtx {};
				cartCtx.mbReturnOnUnknownMapper = true;
				cartCtx.mpCaptureBuffer = &captureBuffer;

				if (a.mInt > 0) {
					// Re-load with user-selected mapper (from mapper dialog)
					cartCtx.mbReturnOnUnknownMapper = false;
					cartCtx.mCartMapper = a.mInt;
					cartCtx.mpCaptureBuffer = nullptr;
				}

				ATStateLoadContext stateCtx {};
				ATImageLoadContext ctx {};
				ctx.mpCartLoadContext = &cartCtx;
				ctx.mpStateLoadContext = &stateCtx;

				// Build full ATMediaLoadContext with stop flags (matches Windows)
				ATMediaLoadContext mctx;
				mctx.mOriginalPath = a.path;
				mctx.mImageName = a.path;
				mctx.mpStream = nullptr;
				mctx.mWriteMode = g_ATOptions.mDefaultWriteMode;
				mctx.mbStopOnModeIncompatibility = true;
				mctx.mbStopAfterImageLoaded = true;
				mctx.mbStopOnMemoryConflictBasic = true;
				mctx.mbStopOnIncompatibleDiskFormat = true;
				mctx.mpImageLoadContext = &ctx;

				bool loadSuccess = false;
				bool suppressColdReset = false;

				// Retry loop matching Windows DoLoadStream (up to 10 retries)
				int safetyCounter = 10;
				for (;;) {
					if (g_sim.Load(mctx)) {
						loadSuccess = true;
						break;
					}

					if (!--safetyCounter)
						break;

					if (mctx.mbStopAfterImageLoaded)
						mctx.mbStopAfterImageLoaded = false;

					if (mctx.mbMode5200Required) {
						// Auto-switch to 5200 mode
						mctx.mbMode5200Required = false;
						if (g_sim.GetHardwareMode() != kATHardwareMode_5200) {
							if (!ATUISwitchHardwareMode(nullptr, kATHardwareMode_5200, true))
								break;
						}
						continue;
					} else if (mctx.mbModeComputerRequired) {
						// Auto-switch to computer mode (or just clear the flag and retry
						// if already in computer mode — Load() sets this flag whenever
						// mbStopAfterImageLoaded is true for non-cart images)
						mctx.mbModeComputerRequired = false;
						if (g_sim.GetHardwareMode() == kATHardwareMode_5200) {
							if (!ATUISwitchHardwareMode(nullptr, kATHardwareMode_800XL, true))
								break;
						}
						continue;
					} else if (mctx.mbMemoryConflictBasic) {
						// Auto-disable BASIC on memory conflict (SDL3 can't show
						// modal dialog, so auto-disable matching common user choice)
						mctx.mbStopOnMemoryConflictBasic = false;
						g_sim.SetBASICEnabled(false);
						continue;
					} else if (mctx.mbIncompatibleDiskFormat) {
						// Allow incompatible disk format (SDL3 auto-accepts)
						mctx.mbIncompatibleDiskFormat = false;
						mctx.mbStopOnIncompatibleDiskFormat = false;
						continue;
					}

					// Unknown cart mapper — show selection dialog
					if (ctx.mLoadType == kATImageType_Cartridge) {
						ATUIOpenCartridgeMapperDialog(a.type, a.path, 0,
							(a.type == kATDeferred_BootImage),
							captureBuffer, cartCtx.mCartSize);
						break;  // mapper dialog will re-push deferred action
					}

					break;  // unknown failure
				}

				if (loadSuccess) {
					ATAddMRU(a.path.c_str());

					// Save state loads suppress cold reset (matches Windows)
					if (ctx.mLoadType == kATImageType_SaveState || ctx.mLoadType == kATImageType_SaveState2)
						suppressColdReset = true;

					if (a.type == kATDeferred_BootImage && !suppressColdReset)
						g_sim.ColdReset();

					// Check compatibility before resuming (matches Windows
					// modal dialog behavior — emulation stays paused while
					// the user decides what to do).
					bool compatIssue = false;
					try {
						vdfastvector<ATCompatKnownTag> compatTags;
						auto *compatTitle = ATCompatCheck(compatTags);
						if (compatTitle) {
							g_compatWarningState.pTitle = compatTitle;
							g_compatWarningState.tags = std::move(compatTags);
							g_compatWarningState.ignoreThistitle = ATCompatIsTitleMuted(compatTitle);
							g_compatWarningState.ignoreAll = ATCompatIsAllMuted();
							g_compatCheckPending = true;
							compatIssue = true;
						}
					} catch (...) {
						// Compat check failure should not block boot
					}
					// Only resume for Boot Image (matches Windows — Open Image
					// does not resume; user may be paused to inspect media)
					if (!compatIssue && a.type == kATDeferred_BootImage)
						g_sim.Resume();
				}
				break;
			}
			case kATDeferred_AttachCartridge:
			case kATDeferred_AttachSecondaryCartridge: {
				int slot = (a.type == kATDeferred_AttachSecondaryCartridge) ? 1 : 0;
				vdfastvector<uint8> captureBuffer;
				ATCartLoadContext ctx {};
				ctx.mbReturnOnUnknownMapper = true;
				ctx.mpCaptureBuffer = &captureBuffer;

				if (a.mInt > 0) {
					// Re-load with user-selected mapper (from mapper dialog)
					ctx.mbReturnOnUnknownMapper = false;
					ctx.mCartMapper = a.mInt;
					ctx.mpCaptureBuffer = nullptr;
				}

				if (g_sim.LoadCartridge(slot, a.path.c_str(), &ctx)) {
					if (ATUIIsResetNeeded(kATUIResetFlag_CartridgeChange))
						g_sim.ColdReset();

					// Check compatibility before resuming
					bool compatIssue = false;
					try {
						vdfastvector<ATCompatKnownTag> compatTags;
						auto *compatTitle = ATCompatCheck(compatTags);
						if (compatTitle) {
							g_compatWarningState.pTitle = compatTitle;
							g_compatWarningState.tags = std::move(compatTags);
							g_compatWarningState.ignoreThistitle = ATCompatIsTitleMuted(compatTitle);
							g_compatWarningState.ignoreAll = ATCompatIsAllMuted();
							g_compatCheckPending = true;
							compatIssue = true;
						}
					} catch (...) {
					}
					if (!compatIssue)
						g_sim.Resume();
				} else if (ctx.mLoadStatus == kATCartLoadStatus_UnknownMapper) {
					// Unknown mapper — show selection dialog
					ATUIOpenCartridgeMapperDialog(a.type, a.path, slot, true,
						captureBuffer, ctx.mCartSize);
				}
				break;
			}
			case kATDeferred_AttachDisk: {
				int idx = a.mInt;
				if (idx >= 0 && idx < 15)
					g_sim.GetDiskInterface(idx).LoadDisk(a.path.c_str());
				break;
			}
			case kATDeferred_LoadState: {
				ATImageLoadContext ctx {};
				if (g_sim.Load(a.path.c_str(), kATMediaWriteMode_RO, &ctx))
					g_sim.Resume();
				break;
			}
			case kATDeferred_SaveState:
				g_sim.SaveState(a.path.c_str());
				break;
			case kATDeferred_SaveCassette: {
				IATCassetteImage *image = g_sim.GetCassette().GetImage();
				if (image) {
					VDFileStream fs(a.path.c_str(), nsVDFile::kWrite | nsVDFile::kDenyAll | nsVDFile::kCreateAlways);
					ATSaveCassetteImageCAS(fs, image);
					g_sim.GetCassette().SetImagePersistent(a.path.c_str());
					g_sim.GetCassette().SetImageClean();
				}
				break;
			}
			case kATDeferred_ExportCassetteAudio: {
				IATCassetteImage *image = g_sim.GetCassette().GetImage();
				if (image) {
					VDFileStream f(a.path.c_str(), nsVDFile::kWrite | nsVDFile::kDenyAll | nsVDFile::kCreateAlways);
					ATSaveCassetteImageWAV(f, image);
					g_sim.GetCassette().SetImageClean();
				}
				break;
			}
			case kATDeferred_SaveCartridge: {
				ATCartridgeEmulator *cart = g_sim.GetCartridge(0);
				if (cart && cart->GetMode()) {
					VDStringA u8 = VDTextWToU8(a.path);
					const char *ext = strrchr(u8.c_str(), '.');
					bool includeHeader = true;
					if (ext && (strcasecmp(ext, ".bin") == 0 || strcasecmp(ext, ".rom") == 0))
						includeHeader = false;
					cart->Save(a.path.c_str(), includeHeader);
				}
				break;
			}
			case kATDeferred_SaveFirmware:
				g_sim.SaveStorage((ATStorageId)(kATStorageId_Firmware + a.mInt), a.path.c_str());
				break;
			case kATDeferred_LoadCassette:
				g_sim.GetCassette().Load(a.path.c_str());
				g_sim.GetCassette().Play();
				break;
			case kATDeferred_StartRecordRaw:
				ATUIStartAudioRecording(a.path.c_str(), true);
				break;
			case kATDeferred_StartRecordWAV:
				ATUIStartAudioRecording(a.path.c_str(), false);
				break;
			case kATDeferred_StartRecordSAP:
				ATUIStartSAPRecording(a.path.c_str());
				break;
			case kATDeferred_StartRecordVideo:
				ATUIStartVideoRecording(a.path.c_str(), (ATVideoEncoding)a.mInt);
				break;
			case kATDeferred_StartRecordVGM:
				ATUIStartVGMRecording(a.path.c_str());
				break;
			case kATDeferred_SetCompatDBPath: {
				extern ATOptions g_ATOptions;
				ATOptions prev(g_ATOptions);
				g_ATOptions.mCompatExternalDBPath = a.path;
				if (g_ATOptions != prev) {
					g_ATOptions.mbDirty = true;
					ATOptionsRunUpdateCallbacks(&prev);
					ATOptionsSave();
				}
				break;
			}
			case kATDeferred_ConvertSAPToEXE:
				ATConvertSAPToPlayer(a.path2.c_str(), a.path.c_str());
				g_toolsResultMessage = "SAP file successfully converted to executable.";
				g_showToolsResult = true;
				break;
			case kATDeferred_ExportROMSet: {
				// Check if any target files exist — show overwrite confirm if so
				static const wchar_t *kROMNames[] = {
					L"readme.html", L"atbasic.rom", L"altirraos-800.rom",
					L"altirraos-xl.rom", L"altirraos-816.rom", L"altirraos-5200.rom",
				};
				bool needConfirm = false;
				for (auto *name : kROMNames) {
					if (VDDoesPathExist(VDMakePath(a.path, VDStringSpanW(name)).c_str())) {
						needConfirm = true;
						break;
					}
				}
				if (needConfirm) {
					g_exportROMPath = a.path;
					g_showExportROMOverwrite = true;
				} else {
					ATUIDoExportROMSet(a.path);
				}
				break;
			}
			case kATDeferred_AnalyzeTapeDecode: {
				if (VDFileIsPathEqual(a.path.c_str(), a.path2.c_str()))
					throw MyError("The analysis file needs to be different from the source tape file.");

				VDFileStream f2(a.path2.c_str(), nsVDFile::kWrite | nsVDFile::kDenyAll | nsVDFile::kSequential | nsVDFile::kCreateAlways);
				ATCassetteLoadContext ctx;
				g_sim.GetCassette().GetLoadOptions(ctx);
				(void)ATLoadCassetteImage(a.path.c_str(), &f2, ctx);
				g_toolsResultMessage = "Tape analysis complete.";
				g_showToolsResult = true;
				break;
			}
			}
		} catch (const MyError& e) {
			g_toolsResultMessage = VDStringA("Error: ") + e.c_str();
			g_showToolsResult = true;
		} catch (...) {
			VDStringA u8 = VDTextWToU8(a.path);
			LOG_ERROR("UI", "Deferred action %d failed for: %s", a.type, u8.c_str());
		}
	}
}

// =========================================================================
// Theme / Style
// =========================================================================

static bool s_systemThemeIsDark = false;

static bool ResolveIsDark(ATUIThemeMode mode) {
	switch (mode) {
	case ATUIThemeMode::Light:  return false;
	case ATUIThemeMode::Dark:   return true;
	case ATUIThemeMode::System:
	default:                    return s_systemThemeIsDark;
	}
}

void ATUIApplyTheme() {
	bool dark = ResolveIsDark(g_ATOptions.mThemeMode);
	if (dark)
		ImGui::StyleColorsDark();
	else
		ImGui::StyleColorsLight();

	ImGuiStyle& style = ImGui::GetStyle();
	style.FrameRounding = 2.0f;
	style.WindowRounding = 4.0f;
	style.GrabRounding = 2.0f;

	float alpha = std::clamp(g_ATOptions.mUIAlpha, 0.2f, 1.0f);

	// ImGui's dark/light themes set WindowBg and PopupBg to alpha 0.94
	// so the desktop or content behind can bleed through slightly.  Most
	// other semi-transparent colors (FrameBg 0.54, TableRowBgAlt 0.06,
	// Button 0.40, Header 0.31, etc.) are intentionally designed as
	// overlay tints that blend with the surface beneath them inside the
	// window — forcing those to 1.0 breaks the look (e.g. alternating
	// table rows become solid white).
	//
	// At 100% opacity: override only the surface/container backgrounds
	// to alpha 1.0 so windows are fully opaque, but leave overlay tints
	// untouched.  Below 100%: use style.Alpha as a global multiplier.
	if (alpha >= 1.0f) {
		style.Alpha = 1.0f;
		style.Colors[ImGuiCol_WindowBg].w = 1.0f;
		style.Colors[ImGuiCol_PopupBg].w = 1.0f;
	} else {
		style.Alpha = alpha;
	}

	// Light theme tweaks: give a subtle grey tint to the window
	// background so it doesn't look flat-white and sterile.
	if (!dark) {
		style.Colors[ImGuiCol_WindowBg] = ImVec4(0.94f, 0.94f, 0.94f, alpha >= 1.0f ? 1.0f : 0.94f);
		style.Colors[ImGuiCol_MenuBarBg] = ImVec4(0.86f, 0.86f, 0.86f, 1.0f);
	}
}

void ATUIUpdateSystemTheme() {
	SDL_SystemTheme sysTheme = SDL_GetSystemTheme();
	s_systemThemeIsDark = (sysTheme == SDL_SYSTEM_THEME_DARK);
}

// =========================================================================
// Init / Shutdown
// =========================================================================

static bool s_usingGLBackend = false;

bool ATUIInit(SDL_Window *window, IDisplayBackend *backend) {
	IMGUI_CHECKVERSION();
	ImGui::CreateContext();

	ImGuiIO& io = ImGui::GetIO();
	io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;

	// Store imgui.ini in user config directory alongside settings.ini
	{
		extern VDStringA ATGetConfigDir();
		static VDStringA s_imguiIniPath;
		s_imguiIniPath = ATGetConfigDir();
		s_imguiIniPath += "/altirrasdl_imgui.ini";
		io.IniFilename = s_imguiIniPath.c_str();
	}

	ATUIUpdateSystemTheme();
	ATUIApplyTheme();

	// Per-display content scale.  On mobile this is normally 2.0-3.0x for
	// finger-friendly text; on desktop it is 1.0 unless the user runs at a
	// scaled HiDPI resolution.  Used both for font pixel sizing and (on
	// mobile) for widget padding via ScaleAllSizes().
	float contentScale = 1.0f;
	{
		SDL_DisplayID displayID = SDL_GetDisplayForWindow(window);
		float cs = SDL_GetDisplayContentScale(displayID);
		if (cs >= 1.0f && cs <= 4.0f)
			contentScale = cs;
	}

#ifdef ALTIRRA_MOBILE
	// Scale spacing/padding/rounding to match the larger touch-target font.
	{
		ImGuiStyle &style = ImGui::GetStyle();
		style.ScaleAllSizes(contentScale);
		LOG_INFO("UI", "Mobile DPI scale: %.2f", contentScale);
	}
#endif

	// Discover bundled TTFs, load persisted choices, and build the initial
	// font atlas.  Must run BEFORE the renderer backend is initialised so
	// the backend picks up our atlas on its first NewFrame instead of
	// shipping ProggyClean.
	io.FontGlobalScale = 1.0f;
	const bool usingGL = (backend->GetType() == DisplayBackendType::OpenGL33);
	ATUIFontsInit(contentScale, usingGL);

	s_pDisplayBackend = backend;

	if (backend->GetType() == DisplayBackendType::OpenGL33) {
		s_usingGLBackend = true;
		auto *glBackend = static_cast<DisplayBackendGL33 *>(backend);
		if (!ImGui_ImplSDL3_InitForOpenGL(window, glBackend->GetGLContext())) {
			LOG_ERROR("UI", "ImGui SDL3/OpenGL init failed");
			return false;
		}
		if (!ImGui_ImplOpenGL3_Init("#version 330 core")) {
			LOG_ERROR("UI", "ImGui OpenGL3 init failed");
			return false;
		}
		LOG_INFO("UI", "ImGui initialized (OpenGL 3.3, docking enabled)");
	} else {
		s_usingGLBackend = false;
		SDL_Renderer *renderer = backend->GetSDLRenderer();
		if (!ImGui_ImplSDL3_InitForSDLRenderer(window, renderer)) {
			LOG_ERROR("UI", "ImGui SDL3 init failed");
			return false;
		}
		if (!ImGui_ImplSDLRenderer3_Init(renderer)) {
			LOG_ERROR("UI", "ImGui SDLRenderer3 init failed");
			return false;
		}
		LOG_INFO("UI", "ImGui initialized (SDL_Renderer, docking enabled)");
	}

	return true;
}

void ATUIShutdown() {
	ATUIVirtualKeyboard_Shutdown();
	ATUIShutdownPaletteSolver();
	ATUIStopRecording();
	if (s_usingGLBackend) {
		ImGui_ImplOpenGL3_Shutdown();
	} else {
		ImGui_ImplSDLRenderer3_Shutdown();
	}
	ImGui_ImplSDL3_Shutdown();
	ImGui::DestroyContext();
}

bool ATUIProcessEvent(const SDL_Event *event) {
	// React to OS dark/light theme changes in real time
	if (event->type == SDL_EVENT_SYSTEM_THEME_CHANGED) {
		ATUIUpdateSystemTheme();
		if (g_ATOptions.mThemeMode == ATUIThemeMode::System)
			ATUIApplyTheme();
	}
	return ImGui_ImplSDL3_ProcessEvent(event);
}

bool ATUIWantCaptureKeyboard() { return ImGui::GetIO().WantCaptureKeyboard; }
bool ATUIWantCaptureMouse() { return ImGui::GetIO().WantCaptureMouse; }
// =========================================================================
// Copy Frame to Clipboard (SDL3)
// =========================================================================

// Clipboard callbacks receive the SDL_Surface* via userdata, so each
// clipboard operation owns its own surface independently.  When SDL
// replaces the clipboard (e.g. a second Copy Frame), it calls the OLD
// cleanup callback with the OLD userdata, correctly freeing only the
// old surface.

static const void *ClipboardDataCallback(void *userdata, const char *mime_type, size_t *size) {
	// SDL3 does not free the returned data, so we cache it and free the
	// previous allocation on each call to avoid leaking.
	static void *s_cachedBmp = nullptr;

	SDL_Surface *surface = static_cast<SDL_Surface *>(userdata);

	// Callback with NULL mime_type means clipboard cleared — free cache.
	if (!mime_type) {
		SDL_free(s_cachedBmp);
		s_cachedBmp = nullptr;
		*size = 0;
		return nullptr;
	}

	if (!surface || strcmp(mime_type, "image/bmp") != 0) {
		*size = 0;
		return nullptr;
	}

	// Encode the surface as BMP into a dynamic memory stream
	SDL_IOStream *mem = SDL_IOFromDynamicMem();
	if (!mem) {
		*size = 0;
		return nullptr;
	}

	if (!SDL_SaveBMP_IO(surface, mem, false)) {
		SDL_CloseIO(mem);
		*size = 0;
		return nullptr;
	}

	Sint64 bmpSize = SDL_TellIO(mem);
	if (bmpSize <= 0) {
		SDL_CloseIO(mem);
		*size = 0;
		return nullptr;
	}

	SDL_SeekIO(mem, 0, SDL_IO_SEEK_SET);
	void *bmpData = SDL_malloc((size_t)bmpSize);
	if (!bmpData) {
		SDL_CloseIO(mem);
		*size = 0;
		return nullptr;
	}

	SDL_ReadIO(mem, bmpData, (size_t)bmpSize);
	SDL_CloseIO(mem);

	SDL_free(s_cachedBmp);
	s_cachedBmp = bmpData;

	*size = (size_t)bmpSize;
	return bmpData;
}

static void ClipboardCleanupCallback(void *userdata) {
	SDL_Surface *surface = static_cast<SDL_Surface *>(userdata);
	if (surface)
		SDL_DestroySurface(surface);
}

// Read the current framebuffer into an SDL_Surface (works for both backends).
static SDL_Surface *ReadFramebufferToSurface(IDisplayBackend *backend) {
	if (!backend)
		return nullptr;

	if (backend->GetType() == DisplayBackendType::SDLRenderer) {
		return SDL_RenderReadPixels(backend->GetSDLRenderer(), nullptr);
	}

	// GL path: read via IDisplayBackend::ReadPixels into an RGBA surface
	int w, h;
	SDL_GetWindowSizeInPixels(backend->GetWindow(), &w, &h);
	if (w <= 0 || h <= 0)
		return nullptr;

	// glReadPixels(GL_RGBA, GL_UNSIGNED_BYTE) writes bytes R,G,B,A in memory.
	// SDL_PIXELFORMAT_RGBA8888 on little-endian stores them A,B,G,R — wrong.
	// SDL_PIXELFORMAT_ABGR8888 stores them R,G,B,A — matches glReadPixels.
	SDL_Surface *surface = SDL_CreateSurface(w, h, SDL_PIXELFORMAT_ABGR8888);
	if (!surface)
		return nullptr;

	if (!backend->ReadPixels(surface->pixels, surface->pitch, 0, 0, w, h)) {
		SDL_DestroySurface(surface);
		return nullptr;
	}

	return surface;
}

static void CopyFrameToClipboard(IDisplayBackend *backend) {
	SDL_Surface *surface = ReadFramebufferToSurface(backend);
	if (!surface) {
		LOG_ERROR("UI", "Copy frame: failed to read pixels: %s", SDL_GetError());
		return;
	}

	// Pass surface as userdata — SDL owns its lifetime via the cleanup callback.
	// When SDL_SetClipboardData is called again, SDL calls the OLD cleanup
	// with the OLD userdata (old surface), then installs the new callbacks.
	const char *mimeTypes[] = { "image/bmp" };
	if (SDL_SetClipboardData(ClipboardDataCallback, ClipboardCleanupCallback, surface, mimeTypes, 1))
		LOG_INFO("UI", "Frame copied to clipboard");
	else
		LOG_ERROR("UI", "Failed to copy frame to clipboard: %s", SDL_GetError());
}

// =========================================================================
// Status overlay
// =========================================================================

static void RenderStatusOverlay(ATSimulator &sim) {
	bool showFPS = ATUIGetShowFPS();
	bool showIndicators = ATUIGetDisplayIndicators();
	bool paused = sim.IsPaused();
	bool recording = ATUIIsRecording();

	// Check for any active drive or cassette activity
	bool hasDriveActivity = false;
	bool hasCassetteActivity = false;
	if (showIndicators) {
		for (int i = 0; i < 15; ++i) {
			auto& di = sim.GetDiskInterface(i);
			if (di.GetClientCount() > 0 && di.IsDiskLoaded()) {
				hasDriveActivity = true;
				break;
			}
		}
		hasCassetteActivity = (sim.GetCassette().GetImage() != nullptr);
	}

	if (!showFPS && !paused && !recording && !hasDriveActivity && !hasCassetteActivity)
		return;

	const ImGuiIO& io = ImGui::GetIO();
	ImGui::SetNextWindowPos(
		ImVec2(io.DisplaySize.x - 10.0f, io.DisplaySize.y - 10.0f),
		ImGuiCond_Always, ImVec2(1.0f, 1.0f));
	ImGui::SetNextWindowBgAlpha(0.5f);

	ImGuiWindowFlags flags =
		ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
		ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoMove |
		ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing |
		ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoInputs;

	if (ImGui::Begin("##StatusOverlay", nullptr, flags)) {
		bool needSep = false;

		if (showFPS) {
			float fps = ATUIGetMeasuredFPS();
			if (fps > 0.0f)
				ImGui::Text("%.0f FPS", fps);
			else
				ImGui::Text("%.0f FPS", io.Framerate);
			needSep = true;
		}
		if (paused) {
			if (needSep) ImGui::SameLine();
			ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.0f, 1.0f), "PAUSED");
			needSep = true;
		}
		if (recording) {
			if (needSep) ImGui::SameLine();
			bool recPaused = ATUIIsRecordingPaused();
			if (recPaused)
				ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.3f, 1.0f), "REC PAUSED");
			else
				ImGui::TextColored(ImVec4(1.0f, 0.3f, 0.3f, 1.0f), "REC");
			needSep = true;
		}

		// Drive and cassette indicators
		if (showIndicators) {
			for (int i = 0; i < 15; ++i) {
				auto& di = sim.GetDiskInterface(i);
				if (di.GetClientCount() == 0 || !di.IsDiskLoaded())
					continue;

				if (needSep) ImGui::SameLine();
				ImGui::TextColored(ImVec4(0.5f, 0.8f, 0.5f, 1.0f), "D%d", i + 1);
				needSep = true;
			}

			// Cassette position indicator
			ATCassetteEmulator& cas = sim.GetCassette();
			if (cas.GetImage()) {
				if (needSep) ImGui::SameLine();

				float posSec = cas.GetPosition();
				int posMin = (int)(posSec / 60.0f);
				int posFrac = (int)fmodf(posSec, 60.0f);

				bool casActive = cas.IsPlayEnabled() || cas.IsRecordEnabled();
				ImVec4 casColor = casActive
					? ImVec4(0.2f, 1.0f, 0.2f, 1.0f)
					: ImVec4(0.5f, 0.5f, 0.5f, 1.0f);

				ImGui::TextColored(casColor, "C: %d:%02d", posMin, posFrac);
				needSep = true;
			}
		}
	}
	ImGui::End();
}

// =========================================================================
// Top-level frame render
// =========================================================================

void ATUIRenderFrame(ATSimulator &sim, VDVideoDisplaySDL3 &display,
	IDisplayBackend *backend, ATUIState &state)
{
	// Clipboard capture — must happen BEFORE ImGui::NewFrame() so the
	// framebuffer contains only the Atari frame, not the ImGui overlay.
	// g_copyFramePending was set at the end of the previous frame when
	// the menu item was clicked (ImGui had already rendered that frame).
	if (g_copyFramePending) {
		g_copyFramePending = false;
		CopyFrameToClipboard(backend);
	}

	// If the user changed any font setting last frame, rebuild the atlas
	// (and tear down the backend texture so the next NewFrame re-uploads).
	ATUIFontsRebuildIfDirty();

	if (s_usingGLBackend) {
		ImGui_ImplOpenGL3_NewFrame();
	} else {
		ImGui_ImplSDLRenderer3_NewFrame();
	}
	ImGui_ImplSDL3_NewFrame();
	ImGui::NewFrame();

	// Crash report viewer — no-op if no report is pending.  Rendered
	// early so it is on top and usable even if something later in the
	// frame disables parts of the UI.
	{
		extern void ATCrashReportRender();
		ATCrashReportRender();
	}

	SDL_Window *window = backend->GetWindow();

#ifdef ALTIRRA_MOBILE
	// Mobile UI: render touch controls, hamburger menu, file browser, settings
	// instead of the desktop menu bar
	{
		extern ATMobileUIState g_mobileState;
		ATMobileUI_Render(sim, state, g_mobileState, window);
	}
#else
	ATUIRenderMainMenu(sim, window, backend, state);
#endif
	RenderStatusOverlay(sim);

	// Pick up pending cartridge mapper dialog from deferred actions
	if (g_cartMapperPending) {
		state.showCartridgeMapper = true;
		g_cartMapperPending = false;
	}

	if (g_tapeEditorRequest.pending) {
		state.showTapeEditor = true;
		// Location is consumed by the tape editor on next render
		g_tapeEditorRequest.pending = false;
	}

	// Pick up pending compatibility check from boot
	if (g_compatCheckPending) {
		g_compatCheckPending = false;
		ATUICheckCompatibility(sim, state);
	}

	if (state.showSystemConfig)      ATUIRenderSystemConfig(sim, state);
	if (state.showDiskManager)       ATUIRenderDiskManager(sim, state, window);
	if (state.showCassetteControl)   ATUIRenderCassetteControl(sim, state, window);
	if (state.showAdjustColors)      ATUIRenderAdjustColors(sim, state);
	if (state.showDisplaySettings)   ATUIRenderDisplaySettings(sim, state);
	if (state.showCartridgeMapper)   ATUIRenderCartridgeMapper(state);
	if (state.showAudioOptions)      ATUIRenderAudioOptionsDialog(state);
	if (state.showInputMappings)     ATUIRenderInputMappings(sim, state);
	if (state.showInputSetup)        ATUIRenderInputSetup(sim, state);
	if (state.showAboutDialog)       ATUIRenderAboutDialog(state);
	if (state.showProfiles)          ATUIRenderProfiles(sim, state);
	if (state.showCommandLineHelp)   ATUIRenderCommandLineHelpDialog(state);
	if (state.showChangeLog)         ATUIRenderChangeLogDialog(state);
	if (state.showCompatWarning)     ATUIRenderCompatWarning(sim, state);
	if (state.showExitConfirm)       ATUIRenderExitConfirm(sim, state);
	if (state.showDiskExplorer)      ATUIRenderDiskExplorer(sim, state, window);
	if (state.showSetupWizard)       ATUIRenderSetupWizard(sim, state, window);
	if (state.showKeyboardShortcuts) ATUIRenderKeyboardShortcuts(state);
	if (state.showKeyboardCustomize) ATUIRenderKeyboardCustomize(state);
	if (state.showCompatDB)          ATUIRenderCompatDB(sim, state);
	if (state.showAdvancedConfig)    ATUIRenderAdvancedConfig(state);
	if (state.showCheater)          ATUIRenderCheater(sim, state);
	if (state.showRewind)           ATUIRenderRewindDialog(sim, state);
	if (state.showLightPen)         ATUIRenderLightPenDialog(sim, state);
	if (state.showTapeEditor)       ATUIRenderTapeEditor(sim, state, window);
	if (state.showScreenEffects)    ATUIRenderScreenEffects(sim, state);
	if (state.showShaderParams)     ATUIRenderShaderParameters(state);
	if (state.showShaderSetup)      ATUIRenderShaderSetupHelp(state);
	if (state.showCalibrate)        ATUIRenderCalibrationDialog(state);
	if (state.showCustomizeHud)     ATUIRenderCustomizeHudDialog(state);
	ATUIShaderPresetsPoll(backend);
	ATUIRenderVideoRecordingDialog(window);

	// Debugger dialogs (self-managed visibility)
	ATUIRenderVerifierDialog();
	ATUIDebuggerRenderSourceListDialog();

	// Virtual on-screen keyboard
	if (ATUIRenderVirtualKeyboard(sim, state.showVirtualKeyboard, state.oskPlacement)) {
		ATUIVirtualKeyboard_ReleaseAll(sim);
		state.showVirtualKeyboard = false;
	}

	// HUD overlay (drive LEDs, status, FPS, pause, errors)
	ATUIRenderHUDOverlay();

	// Fullscreen entry notification (fading hint)
	ATUIRenderFullscreenNotification();

	// Debugger panes (dockable windows)
	ATUIDebuggerRenderPanes(sim, state);

	// Tools result popup (success/error messages from deferred tool actions)
#ifdef ALTIRRA_MOBILE
	if (g_showToolsResult) {
		ATMobileUI_ShowInfoModal("Tool Result", g_toolsResultMessage.c_str());
		g_showToolsResult = false;
	}
#else
	if (g_showToolsResult) {
		ImGui::OpenPopup("Tool Result");
		g_showToolsResult = false;
	}
	if (ImGui::BeginPopupModal("Tool Result", nullptr, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings)) {
		ImGui::TextUnformatted(g_toolsResultMessage.c_str());
		ImGui::Spacing();
		if (ImGui::Button("OK", ImVec2(120, 0)))
			ImGui::CloseCurrentPopup();
		ImGui::EndPopup();
	}
#endif

	// Export ROM overwrite confirmation popup
#ifdef ALTIRRA_MOBILE
	if (g_showExportROMOverwrite) {
		VDStringW path = g_exportROMPath;
		ATMobileUI_ShowConfirmDialog("Overwrite Existing Files?",
			"There are existing files with the same names that will be overwritten.\nAre you sure?",
			[path]() { ATUIDoExportROMSet(path); });
		g_showExportROMOverwrite = false;
	}
#else
	if (g_showExportROMOverwrite) {
		ImGui::OpenPopup("Overwrite Existing Files?");
		g_showExportROMOverwrite = false;
	}
	if (ImGui::BeginPopupModal("Overwrite Existing Files?", nullptr, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings)) {
		ImGui::TextUnformatted("There are existing files with the same names that will be overwritten.\nAre you sure?");
		ImGui::Spacing();
		if (ImGui::Button("Yes", ImVec2(120, 0))) {
			ImGui::CloseCurrentPopup();
			ATUIDoExportROMSet(g_exportROMPath);
		}
		ImGui::SameLine();
		if (ImGui::Button("No", ImVec2(120, 0)))
			ImGui::CloseCurrentPopup();
		ImGui::EndPopup();
	}
#endif

	// Progress dialog popup (firmware scan, background tasks)
	ATUIRenderProgress();

	// Emulation error dialog popup (program crash recovery)
	ATUIRenderEmuErrorDialog(sim);

	// Drag-and-drop visual feedback overlay
	ATUIRenderDragDropOverlay();

	ImGui::Render();
	if (s_usingGLBackend) {
		ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
	} else {
		ImGui_ImplSDLRenderer3_RenderDrawData(ImGui::GetDrawData(), backend->GetSDLRenderer());
	}

	// Process test mode pending actions (click state machine, deferred responses)
	ATTestModePostRender(sim, state);

	// Save Frame / Copy Frame — readback after rendering, before present
	{
		VDStringA savePath;
		{
			std::lock_guard<std::mutex> lock(g_saveFrameMutex);
			savePath.swap(g_saveFramePath);
		}
		if (!savePath.empty()) {
			SDL_Surface *surface = ReadFramebufferToSurface(backend);
			if (surface) {
				if (SDL_SaveBMP(surface, savePath.c_str()))
					LOG_INFO("UI", "Frame saved to %s", savePath.c_str());
				else
					LOG_ERROR("UI", "Failed to save frame: %s", SDL_GetError());
				SDL_DestroySurface(surface);
			} else {
				LOG_ERROR("UI", "Failed to read pixels: %s", SDL_GetError());
			}
		}

		if (g_copyFrameRequested) {
			g_copyFrameRequested = false;
			// Don't capture here — ImGui has already rendered onto the
			// framebuffer this frame (menu is visible). Defer to the
			// start of the next frame, before ImGui::NewFrame().
			g_copyFramePending = true;
		}
	}
}
