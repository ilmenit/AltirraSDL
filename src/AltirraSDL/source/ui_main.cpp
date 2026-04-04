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
#include "ui_debugger.h"
#include "ui_testmode.h"
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
#include "display_backend.h"

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
// Compatibility Warning — SDL3 replacement for Windows IDD_COMPATIBILITY
// =========================================================================

#include "compatengine.h"
#include "compatdb.h"
#include "uicompat.h"

// Pending compat check flag — set after boot, consumed by render loop
static bool g_compatCheckPending = false;

// Compat warning dialog state
static struct {
	const ATCompatDBTitle *pTitle = nullptr;
	vdfastvector<ATCompatKnownTag> tags;
	bool ignoreThistitle = false;
	bool ignoreAll = false;
} g_compatWarningState;

// SDL3 implementation of ATUICompatGetKnownTagDisplayName
// (Windows version is in uicompat.cpp which is excluded from SDL3 build)
const wchar_t *ATUICompatGetKnownTagDisplayName(ATCompatKnownTag knownTag) {
	static constexpr const wchar_t *kKnownTagNames[] = {
		L"Requires BASIC",
		L"Requires Atari BASIC revision A",
		L"Requires Atari BASIC revision B",
		L"Requires Atari BASIC revision C",
		L"Requires BASIC disabled",
		L"Requires OS-A",
		L"Requires OS-B",
		L"Requires XL/XE OS",
		L"Requires accurate disk timing",
		L"Requires no additional CIO devices",
		L"Requires no expanded memory",
		L"Requires CTIA",
		L"Incompatible with Ultimate1MB",
		L"Requires 6502 undocumented opcodes",
		L"Incompatible with 65C816 24-bit addressing",
		L"Requires writable disk",
		L"Incompatible with floating data bus",
		L"Cart: Use 5200 8K mapper",
		L"Cart: Use 5200 one-chip 16K mapper",
		L"Cart: Use 5200 two-chip 16K mapper",
		L"Cart: Use 5200 32K mapper",
		L"Requires 60Hz (NTSC ANTIC)",
		L"Requires 50Hz (PAL ANTIC)",
	};

	const size_t index = (size_t)knownTag - 1;
	if (index < sizeof(kKnownTagNames) / sizeof(kKnownTagNames[0]))
		return kKnownTagNames[index];

	return L"<Unknown tag>";
}

// SDL3 linker symbol for ATUIShowDialogCompatWarning.
// In the SDL3 build, compat checking is done directly in ATUIPollDeferredActions
// rather than through this function, but we provide it as a symbol since
// uicompat.h declares it and compatengine.cpp may reference it.
ATUICompatAction ATUIShowDialogCompatWarning(VDGUIHandle, const ATCompatDBTitle *title,
	const ATCompatKnownTag *tags, size_t numTags)
{
	g_compatWarningState.pTitle = title;
	g_compatWarningState.tags.assign(tags, tags + numTags);
	g_compatWarningState.ignoreThistitle = ATCompatIsTitleMuted(title);
	g_compatWarningState.ignoreAll = ATCompatIsAllMuted();
	g_compatCheckPending = true;
	return kATUICompatAction_Ignore;
}

void ATUICheckCompatibility(ATSimulator &, ATUIState &state) {
	// Compat check already happened in ATUIPollDeferredActions.
	// This just picks up the pending flag and shows the dialog.
	state.showCompatWarning = true;
}

void ATUIRenderCompatWarning(ATSimulator &sim, ATUIState &state) {
	if (!state.showCompatWarning)
		return;

	// Helper to apply mute settings from checkboxes
	auto applyMuteSettings = [&]() {
		auto &s = g_compatWarningState;
		if (s.ignoreAll)
			ATCompatSetAllMuted(true);
		else if (s.ignoreThistitle)
			ATCompatSetTitleMuted(s.pTitle, true);
	};

	ImGui::SetNextWindowSize(ImVec2(480, 320), ImGuiCond_Appearing);
	ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));

	bool wasOpen = state.showCompatWarning;
	if (!ImGui::Begin("Compatibility Warning", &state.showCompatWarning,
			ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings)) {
		// Window collapsed — if user closed via X, resume emulation
		if (wasOpen && !state.showCompatWarning) {
			applyMuteSettings();
			sim.Resume();
		}
		ImGui::End();
		return;
	}

	// Detect X-button or ESC close (showCompatWarning toggled to false by ImGui)
	if (!state.showCompatWarning || ATUICheckEscClose()) {
		state.showCompatWarning = false;
		applyMuteSettings();
		sim.Resume();
		ImGui::End();
		return;
	}

	auto &s = g_compatWarningState;

	// Title text — mName is a UTF-8 byte string from the compat DB
	if (s.pTitle) {
		ImGui::TextWrapped("The title \"%s\" being booted has compatibility issues "
			"with current settings:", s.pTitle->mName.c_str());
	}

	ImGui::Spacing();

	// List of issues
	for (size_t i = 0; i < s.tags.size(); ++i) {
		VDStringA tagName = VDTextWToU8(VDStringW(ATUICompatGetKnownTagDisplayName(s.tags[i])));
		ImGui::BulletText("%s", tagName.c_str());
	}

	ImGui::Spacing();
	ImGui::TextWrapped("Do you want to automatically adjust emulation settings "
		"for better compatibility?");

	ImGui::Spacing();
	ImGui::Separator();
	ImGui::Spacing();

	// Action buttons — emulation is paused while this dialog is open
	if (ImGui::Button("Auto-adjust settings and reboot", ImVec2(-1, 0))) {
		applyMuteSettings();
		ATCompatAdjust(nullptr, s.tags.data(), s.tags.size());
		sim.ColdReset();
		sim.Resume();
		state.showCompatWarning = false;
	}

	if (ImGui::Button("Pause emulation to adjust manually", ImVec2(-1, 0))) {
		applyMuteSettings();
		// Leave paused — user wants to manually adjust settings
		state.showCompatWarning = false;
	}

	if (ImGui::Button("Boot anyway", ImVec2(-1, 0))) {
		applyMuteSettings();
		sim.Resume();
		state.showCompatWarning = false;
	}

	ImGui::Spacing();

	// Mute options
	ImGui::Checkbox("Turn off compatibility checks for this title", &s.ignoreThistitle);
	ImGui::Checkbox("Turn off all compatibility warnings", &s.ignoreAll);

	ImGui::End();
}

// =========================================================================
// Save Frame state — file path set by dialog callback, consumed by render
// =========================================================================
// =========================================================================
// Save Frame state — file path set by dialog callback, consumed by render
// =========================================================================
std::mutex g_saveFrameMutex;
VDStringA g_saveFramePath;

bool g_copyFrameRequested = false;
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
			fprintf(stderr, "[AltirraSDL] Deferred action %d failed for: %s\n", a.type, u8.c_str());
		}
	}
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
	io.IniFilename = "altirrasdl_imgui.ini";

	ImGui::StyleColorsDark();
	ImGuiStyle& style = ImGui::GetStyle();
	style.FrameRounding = 2.0f;
	style.WindowRounding = 4.0f;
	style.GrabRounding = 2.0f;

	s_pDisplayBackend = backend;

	if (backend->GetType() == DisplayBackendType::OpenGL33) {
		s_usingGLBackend = true;
		auto *glBackend = static_cast<DisplayBackendGL33 *>(backend);
		if (!ImGui_ImplSDL3_InitForOpenGL(window, glBackend->GetGLContext())) {
			fprintf(stderr, "[AltirraSDL] ImGui SDL3/OpenGL init failed\n");
			return false;
		}
		if (!ImGui_ImplOpenGL3_Init("#version 330 core")) {
			fprintf(stderr, "[AltirraSDL] ImGui OpenGL3 init failed\n");
			return false;
		}
		fprintf(stderr, "[AltirraSDL] ImGui initialized (OpenGL 3.3, docking enabled)\n");
	} else {
		s_usingGLBackend = false;
		SDL_Renderer *renderer = backend->GetSDLRenderer();
		if (!ImGui_ImplSDL3_InitForSDLRenderer(window, renderer)) {
			fprintf(stderr, "[AltirraSDL] ImGui SDL3 init failed\n");
			return false;
		}
		if (!ImGui_ImplSDLRenderer3_Init(renderer)) {
			fprintf(stderr, "[AltirraSDL] ImGui SDLRenderer3 init failed\n");
			return false;
		}
		fprintf(stderr, "[AltirraSDL] ImGui initialized (SDL_Renderer, docking enabled)\n");
	}

	return true;
}

void ATUIShutdown() {
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
	SDL_Surface *surface = static_cast<SDL_Surface *>(userdata);
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

	SDL_Surface *surface = SDL_CreateSurface(w, h, SDL_PIXELFORMAT_RGBA8888);
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
		fprintf(stderr, "[AltirraSDL] Copy frame: failed to read pixels: %s\n", SDL_GetError());
		return;
	}

	// Pass surface as userdata — SDL owns its lifetime via the cleanup callback.
	// When SDL_SetClipboardData is called again, SDL calls the OLD cleanup
	// with the OLD userdata (old surface), then installs the new callbacks.
	const char *mimeTypes[] = { "image/bmp" };
	if (SDL_SetClipboardData(ClipboardDataCallback, ClipboardCleanupCallback, surface, mimeTypes, 1))
		fprintf(stderr, "[AltirraSDL] Frame copied to clipboard\n");
	else
		fprintf(stderr, "[AltirraSDL] Failed to copy frame to clipboard: %s\n", SDL_GetError());
}

// =========================================================================
// Command-Line Help dialog
// =========================================================================

static void RenderCommandLineHelpDialog(ATUIState &state) {
	ImGui::SetNextWindowSize(ImVec2(520, 400), ImGuiCond_Appearing);
	ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
	if (!ImGui::Begin("Command-Line Help", &state.showCommandLineHelp, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings)) {
		ImGui::End();
		return;
	}

	if (ATUICheckEscClose()) {
		state.showCommandLineHelp = false;
		ImGui::End();
		return;
	}

	ImGui::TextWrapped("Usage: AltirraSDL [options] [image-file]");
	ImGui::Separator();

	ImGui::TextWrapped(
		"Positional arguments:\n"
		"  image-file            Load and boot the given disk/cartridge/tape image\n\n"
		"The emulator accepts ATR, XEX, BIN, ROM, CAR, CAS, WAV, and ATX files.\n"
		"Drag-and-drop onto the window also loads an image.\n\n"
		"Settings are stored in ~/.config/altirra/settings.ini");

	ImGui::Spacing();
	float buttonWidth = 80.0f;
	ImGui::SetCursorPosX(ImGui::GetWindowWidth() - buttonWidth - ImGui::GetStyle().WindowPadding.x);
	if (ImGui::Button("OK", ImVec2(buttonWidth, 0)))
		state.showCommandLineHelp = false;

	ImGui::End();
}

// =========================================================================
// Change Log dialog
// =========================================================================

static void RenderChangeLogDialog(ATUIState &state) {
	ImGui::SetNextWindowSize(ImVec2(520, 400), ImGuiCond_Appearing);
	ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
	if (!ImGui::Begin("Change Log", &state.showChangeLog, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings)) {
		ImGui::End();
		return;
	}

	if (ATUICheckEscClose()) {
		state.showChangeLog = false;
		ImGui::End();
		return;
	}

	ImGui::TextWrapped(
		"AltirraSDL - Cross-Platform Frontend\n\n"
		"This is the SDL3 + Dear ImGui cross-platform port of Altirra.\n"
		"It aims for full feature parity with the Windows version.\n\n"
		"Current status:\n"
		"  - Full emulation core (CPU, ANTIC, GTIA, POKEY, PIA)\n"
		"  - All hardware modes (800/800XL/1200XL/130XE/1400XL/XEGS/5200)\n"
		"  - Disk, cassette, and cartridge support\n"
		"  - Keyboard, gamepad, and mouse input\n"
		"  - Audio output via SDL3\n"
		"  - Settings persistence\n"
		"  - Profile management\n"
		"  - State save/load\n\n"
		"For the full Altirra change log, see the Windows version documentation.");

	ImGui::Spacing();
	float buttonWidth = 80.0f;
	ImGui::SetCursorPosX(ImGui::GetWindowWidth() - buttonWidth - ImGui::GetStyle().WindowPadding.x);
	if (ImGui::Button("OK", ImVec2(buttonWidth, 0)))
		state.showChangeLog = false;

	ImGui::End();
}

// =========================================================================
// About dialog
// =========================================================================

static void RenderAboutDialog(ATUIState &state) {
	ImGui::SetNextWindowSize(ImVec2(420, 220), ImGuiCond_Appearing);
	ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
	if (!ImGui::Begin("About AltirraSDL", &state.showAboutDialog, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings)) {
		ImGui::End();
		return;
	}

	if (ATUICheckEscClose()) {
		state.showAboutDialog = false;
		ImGui::End();
		return;
	}

	ImGui::Text("AltirraSDL");
	ImGui::Separator();
	ImGui::TextWrapped(
		"Atari 800/800XL/5200 emulator\n"
		"Based on Altirra by Avery Lee\n"
		"SDL3 + Dear ImGui cross-platform frontend\n\n"
		"Licensed under GNU GPL v2+");

	ImGui::Spacing();
	float buttonWidth = 80.0f;
	ImGui::SetCursorPosX(ImGui::GetWindowWidth() - buttonWidth - ImGui::GetStyle().WindowPadding.x);
	if (ImGui::Button("OK", ImVec2(buttonWidth, 0)))
		state.showAboutDialog = false;

	ImGui::End();
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
// Exit confirmation dialog
// =========================================================================

// Persistent message string — built when the dialog opens, displayed until closed.
static VDStringA g_exitConfirmMsgUtf8;

// Build the dirty-storage message, matching Windows ATUIConfirmDiscardAllStorageGetMessage().
// Returns UTF-8 string ready for ImGui, using "  " indent instead of "\t" (tabs
// don't render as indentation in ImGui).
static VDStringA BuildDirtyStorageMessage(ATSimulator &sim) {
	vdfastvector<ATStorageId> dirtyIds;
	sim.GetDirtyStorage(dirtyIds, ~(uint32)0);

	vdfastvector<ATDebuggerStorageId> dbgDirtyIds;
	IATDebugger *dbg = ATGetDebugger();
	if (dbg)
		dbg->GetDirtyStorage(dbgDirtyIds);

	if (dirtyIds.empty() && dbgDirtyIds.empty())
		return VDStringA();

	std::sort(dirtyIds.begin(), dirtyIds.end());
	std::sort(dbgDirtyIds.begin(), dbgDirtyIds.end());

	VDStringA msg;
	msg = "The following modified items have not been saved:\n\n";
	msg += "  Contents of emulation memory\n";

	for (const ATStorageId id : dirtyIds) {
		const uint32 type = id & kATStorageId_TypeMask;
		const uint32 unit = id & kATStorageId_UnitMask;

		switch (type) {
			case kATStorageId_Cartridge:
				msg += "  Cartridge";
				if (unit)
					msg.append_sprintf(" %u", unit + 1);
				break;

			case kATStorageId_Disk:
				msg.append_sprintf("  Disk (D%u:)", unit + 1);
				break;

			case kATStorageId_Tape:
				msg += "  Tape";
				break;

			case kATStorageId_Firmware:
				switch (unit) {
					case 0: msg += "  IDE main firmware"; break;
					case 1: msg += "  IDE SDX firmware"; break;
					case 2: msg += "  Ultimate1MB firmware"; break;
					case 3: msg += "  Rapidus flash firmware"; break;
					case 4: msg += "  Rapidus PBI firmware"; break;
				}
				break;
		}
		msg += '\n';
	}

	for (const ATDebuggerStorageId id : dbgDirtyIds) {
		switch (id) {
			case kATDebuggerStorageId_CustomSymbols:
				msg += "  Debugger: Custom Symbols\n";
				break;
			default:
				break;
		}
	}

	msg += "\nAre you sure you want to exit?";
	return msg;
}

void ATUIRenderExitConfirm(ATSimulator &sim, ATUIState &state) {
	// First frame: build the message.
	if (g_exitConfirmMsgUtf8.empty()) {
		g_exitConfirmMsgUtf8 = BuildDirtyStorageMessage(sim);

		// Windows Altirra always confirms on exit:
		// - If dirty storage: lists dirty items + memory warning
		// - If nothing dirty: still warns about emulation memory loss
		if (g_exitConfirmMsgUtf8.empty())
			g_exitConfirmMsgUtf8 = "Any unsaved work in emulation memory will be lost.\n\nAre you sure you want to exit?";
	}

	ImGui::SetNextWindowSize(ImVec2(440, 0), ImGuiCond_Appearing);
	ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(),
		ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));

	bool open = state.showExitConfirm;
	if (!ImGui::Begin("Confirm Exit", &open,
			ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize
			| ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings)) {
		if (!open) {
			state.showExitConfirm = false;
			g_exitConfirmMsgUtf8.clear();
		}
		ImGui::End();
		return;
	}

	// User closed via title bar X button or ESC
	if (!open || ATUICheckEscClose()) {
		state.showExitConfirm = false;
		g_exitConfirmMsgUtf8.clear();
		ImGui::End();
		return;
	}

	ImGui::TextWrapped("%s", g_exitConfirmMsgUtf8.c_str());
	ImGui::Separator();

	float buttonWidth = 120.0f;
	float spacing = ImGui::GetStyle().ItemSpacing.x;
	float totalWidth = buttonWidth * 2 + spacing;
	ImGui::SetCursorPosX((ImGui::GetWindowWidth() - totalWidth) * 0.5f);

	if (ImGui::Button("OK", ImVec2(buttonWidth, 0))) {
		state.showExitConfirm = false;
		state.exitConfirmed = true;
		g_exitConfirmMsgUtf8.clear();
		ImGui::End();

		// Push quit event so the main loop exits.
		SDL_Event quit{};
		quit.type = SDL_EVENT_QUIT;
		SDL_PushEvent(&quit);
		return;
	}
	ImGui::SameLine();
	if (ImGui::Button("Cancel", ImVec2(buttonWidth, 0))) {
		state.showExitConfirm = false;
		g_exitConfirmMsgUtf8.clear();
		ImGui::End();
		return;
	}

	ImGui::End();
}

// =========================================================================
// Drag-and-drop overlay — highlight drop target during file drag
// =========================================================================

void ATUIRenderDragDropOverlay() {
	if (!g_dragDropState.active)
		return;

	ImDrawList *fg = ImGui::GetForegroundDrawList();
	float cx = g_dragDropState.x;
	float cy = g_dragDropState.y;

	// Check which drop target the cursor is over
	struct DropTarget {
		ImVec2 pos, size;
		const char *label;
	};
	DropTarget target = {};

	ImVec2 p, s;
	if (ATUIDiskExplorerGetDropRect(p, s)
		&& cx >= p.x && cy >= p.y && cx <= p.x + s.x && cy <= p.y + s.y) {
		target = {p, s, "Import to disk image"};
	} else if (ATUIFirmwareManagerGetDropRect(p, s)
		&& cx >= p.x && cy >= p.y && cx <= p.x + s.x && cy <= p.y + s.y) {
		target = {p, s, "Add firmware"};
	} else {
		// Default: entire viewport = boot image
		ImVec2 vp = ImGui::GetMainViewport()->Pos;
		ImVec2 vs = ImGui::GetMainViewport()->Size;
		target = {vp, vs, "Boot image"};
	}

	// Draw highlight border around target window
	ImU32 borderColor = IM_COL32(80, 160, 255, 200);
	ImU32 fillColor = IM_COL32(80, 160, 255, 30);
	float thickness = 3.0f;
	ImVec2 tl = target.pos;
	ImVec2 br = ImVec2(tl.x + target.size.x, tl.y + target.size.y);
	fg->AddRectFilled(tl, br, fillColor);
	fg->AddRect(tl, br, borderColor, 0.0f, 0, thickness);

	// Draw label tooltip near cursor
	ImVec2 textSize = ImGui::CalcTextSize(target.label);
	ImVec2 padding = ImVec2(8, 4);
	ImVec2 labelPos = ImVec2(cx + 16, cy + 16);

	// Keep label within viewport
	ImVec2 vpMax = ImVec2(ImGui::GetMainViewport()->Pos.x + ImGui::GetMainViewport()->Size.x,
		ImGui::GetMainViewport()->Pos.y + ImGui::GetMainViewport()->Size.y);
	if (labelPos.x + textSize.x + padding.x * 2 > vpMax.x)
		labelPos.x = cx - textSize.x - padding.x * 2 - 4;
	if (labelPos.y + textSize.y + padding.y * 2 > vpMax.y)
		labelPos.y = cy - textSize.y - padding.y * 2 - 4;

	ImVec2 bgMin = labelPos;
	ImVec2 bgMax = ImVec2(labelPos.x + textSize.x + padding.x * 2,
		labelPos.y + textSize.y + padding.y * 2);
	fg->AddRectFilled(bgMin, bgMax, IM_COL32(30, 30, 30, 220), 4.0f);
	fg->AddRect(bgMin, bgMax, borderColor, 4.0f);
	fg->AddText(ImVec2(labelPos.x + padding.x, labelPos.y + padding.y),
		IM_COL32(255, 255, 255, 255), target.label);
}

// =========================================================================
// Top-level frame render
// =========================================================================

void ATUIRenderFrame(ATSimulator &sim, VDVideoDisplaySDL3 &display,
	IDisplayBackend *backend, ATUIState &state)
{
	if (s_usingGLBackend) {
		ImGui_ImplOpenGL3_NewFrame();
	} else {
		ImGui_ImplSDLRenderer3_NewFrame();
	}
	ImGui_ImplSDL3_NewFrame();
	ImGui::NewFrame();

	SDL_Window *window = backend->GetWindow();

	ATUIRenderMainMenu(sim, window, backend, state);
	RenderStatusOverlay(sim);

	// Pick up pending cartridge mapper dialog from deferred actions
	if (g_cartMapperPending) {
		state.showCartridgeMapper = true;
		g_cartMapperPending = false;
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
	if (state.showAboutDialog)       RenderAboutDialog(state);
	if (state.showProfiles)          ATUIRenderProfiles(sim, state);
	if (state.showCommandLineHelp)   RenderCommandLineHelpDialog(state);
	if (state.showChangeLog)         RenderChangeLogDialog(state);
	if (state.showCompatWarning)     ATUIRenderCompatWarning(sim, state);
	if (state.showExitConfirm)       ATUIRenderExitConfirm(sim, state);
	if (state.showDiskExplorer)      ATUIRenderDiskExplorer(sim, state, window);
	if (state.showSetupWizard)       ATUIRenderSetupWizard(sim, state, window);
	if (state.showKeyboardShortcuts) ATUIRenderKeyboardShortcuts(state);
	if (state.showCompatDB)          ATUIRenderCompatDB(sim, state);
	if (state.showAdvancedConfig)    ATUIRenderAdvancedConfig(state);
	if (state.showCheater)          ATUIRenderCheater(sim, state);
	if (state.showRewind)           ATUIRenderRewindDialog(sim, state);
	if (state.showLightPen)         ATUIRenderLightPenDialog(sim, state);
	if (state.showScreenEffects)    ATUIRenderScreenEffects(sim, state);
	ATUIRenderVideoRecordingDialog(window);

	// HUD overlay (drive LEDs, status, FPS, pause, errors)
	ATUIRenderHUDOverlay();

	// Debugger panes (dockable windows)
	ATUIDebuggerRenderPanes(sim, state);

	// Tools result popup (success/error messages from deferred tool actions)
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

	// Export ROM overwrite confirmation popup
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
					fprintf(stderr, "[AltirraSDL] Frame saved to %s\n", savePath.c_str());
				else
					fprintf(stderr, "[AltirraSDL] Failed to save frame: %s\n", SDL_GetError());
				SDL_DestroySurface(surface);
			} else {
				fprintf(stderr, "[AltirraSDL] Failed to read pixels: %s\n", SDL_GetError());
			}
		}

		if (g_copyFrameRequested) {
			g_copyFrameRequested = false;
			CopyFrameToClipboard(backend);
		}
	}
}
