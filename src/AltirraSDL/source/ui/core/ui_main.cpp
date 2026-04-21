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
#ifdef ALTIRRA_NETPLAY_ENABLED
#include "../netplay/ui_netplay.h"
#include "../netplay/ui_netplay_state.h"
#include "../netplay/ui_netplay_actions.h"
#include "netplay/netplay_input.h"
#include "netplay/netplay_simhash.h"
#include "netplay/netplay_glue.h"
#include "netplay/packets.h"
#endif
#include "ui_main_internal.h"
#include "ui_mobile.h"
#include "ui_mode.h"
#include "ui_debugger.h"
#include "ui_textselection.h"
#include "accel_sdl3.h"
#include "inputmanager.h"
#include "ui_testmode.h"
#include "ui_progress.h"
#include "ui_emuerror.h"
#include "ui_confirm_dialog.h"
#include "ui_virtual_keyboard.h"
#include "display_sdl3_impl.h"
#include "simulator.h"
#include "hleprogramloader.h"
#include "cpu.h"
#include "simeventmanager.h"
#include "mediamanager.h"
#include "gtia.h"
#include "cartridge.h"
#include "cassette.h"
#include "disk.h"
#include "diskinterface.h"
#include <at/atio/diskimage.h>
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
#include <at/atcore/logging.h>
#include "ui_fonts.h"

extern ATSimulator g_sim;
extern ATUIKeyboardOptions g_kbdOpts;

// Clipboard availability check — defined in win32_stubs.cpp (SDL3 bridge
// using SDL_HasClipboardText).  Used by the main-display right-click
// context menu to enable/disable the Paste item.
bool ATUIClipIsTextAvailable();

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
				if (idx >= 0 && idx < 15) {
					ATDiskInterface& diskIf = g_sim.GetDiskInterface(idx);
					ATDiskEmulator& disk = g_sim.GetDiskDrive(idx);
					ATMediaWriteMode wm = disk.IsEnabled() || diskIf.GetClientCount() > 1
						? diskIf.GetWriteMode() : g_ATOptions.mDefaultWriteMode;
					diskIf.LoadDisk(a.path.c_str());

					IATDiskImage *img = diskIf.GetDiskImage();
					if (img && !img->IsUpdatable())
						wm = (ATMediaWriteMode)(wm & ~kATMediaWriteMode_AutoFlush);
					diskIf.SetWriteMode(wm);

					if (diskIf.GetClientCount() < 2)
						disk.SetEnabled(true);

					ATAddMRU(a.path.c_str());
				}
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
#ifdef ALTIRRA_NETPLAY_ENABLED
			case kATDeferred_NetplayHostSnapshot: {
				// a.path carries the offer id.  Reads the game-file
				// bytes off disk and ships them via the existing
				// snapshot-chunk channel.  Name kept for history —
				// there is no savestate involved in v3.
				VDStringA u8OfferId = VDTextWToU8(a.path);
				ATNetplayUI::SubmitHostGameFileForGame(u8OfferId.c_str());
				break;
			}
			case kATDeferred_NetplayHostBoot: {
				// Netplay-only boot path: no compat-dialog gate (we
				// must Resume even if the title is flagged, otherwise
				// the snapshot captures a paused sim and both sides
				// end up with a frozen screen after Lockstepping).
				//
				// Unlike kATDeferred_BootImage, we carry two strings:
				//   a.path  = offer id (UTF-16 encoded UTF-8)
				//   a.path2 = game image path
				// so failures can be routed back to the specific offer
				// row via ATNetplayUI_HostBootFailed().
				extern ATOptions g_ATOptions;
				extern void ATNetplayUI_HostBootFailed(const char *,
					const char *);

				VDStringA gameIdU8 = VDTextWToU8(a.path);
				const VDStringW &imagePath = a.path2;

				if (imagePath.empty()) {
					ATNetplayUI_HostBootFailed(gameIdU8.c_str(),
						"internal: empty image path");
					break;
				}

				// Step 1: save the user's pre-session simulator state
				// into an in-memory snapshot so we can restore it
				// exactly when the session ends.  Altirra settings.ini
				// on disk is never touched — only the live sim mutates.
				if (!ATNetplayUI::SaveSessionRestorePoint()) {
					ATNetplayUI_HostBootFailed(gameIdU8.c_str(),
						"could not capture pre-session state; session refused");
					break;
				}

				// Step 2: apply the offer's machine config (hardware,
				// memory, video, CPU, firmware by CRC32).  Must
				// happen before UnloadAll+Load so the load uses the
				// correct hardware.
				{
					ATNetplayUI::HostedGame *hg =
						ATNetplayUI::FindHostedGame(std::string(gameIdU8.c_str()));
					ATNetplayUI::MachineConfig cfg =
						hg ? hg->config : ATNetplayUI::MachineConfig{};
					std::string err = ATNetplayUI::ApplyMachineConfig(cfg);
					if (!err.empty()) {
						ATNetplayUI::RestoreSessionRestorePoint();
						ATNetplayUI_HostBootFailed(gameIdU8.c_str(),
							err.c_str());
						break;
					}
				}

				// Lock the RNG seed to the same master seed the
				// joiner will use, so PIA floating inputs + HLE
				// program-launch delay are bit-identical.  Shared
				// constant with ui_netplay_actions.cpp.
				g_sim.SetLockedRandomSeed(0xA7C0BEEFu);

				g_sim.UnloadAll(ATUIGetBootUnloadStorageMask());

				ATCartLoadContext cartCtx {};
				cartCtx.mbReturnOnUnknownMapper = true;

				ATImageLoadContext imgCtx {};
				imgCtx.mpCartLoadContext = &cartCtx;

				ATMediaLoadContext mctx;
				mctx.mOriginalPath = imagePath;
				mctx.mImageName    = imagePath;
				mctx.mWriteMode    = g_ATOptions.mDefaultWriteMode;
				mctx.mbStopOnModeIncompatibility   = true;
				mctx.mbStopAfterImageLoaded        = true;
				mctx.mbStopOnMemoryConflictBasic   = true;
				mctx.mbStopOnIncompatibleDiskFormat = false;
				mctx.mpImageLoadContext            = &imgCtx;

				// Retry loop mirroring kATDeferred_BootImage
				// (ui_main.cpp:271-323).  We auto-resolve mode and
				// BASIC conflicts silently — for netplay there's no
				// user to answer a dialog and the peer is waiting on
				// the other end of the handshake.
				bool loadSuccess = false;
				VDStringA failReason;
				int safety = 10;
				for (;;) {
					try {
						if (g_sim.Load(mctx)) {
							loadSuccess = true;
							break;
						}
					} catch (const MyError &e) {
						failReason = e.c_str();
						break;
					}

					if (!--safety) { failReason = "retry budget exhausted"; break; }

					if (mctx.mbStopAfterImageLoaded)
						mctx.mbStopAfterImageLoaded = false;

					if (mctx.mbMode5200Required) {
						mctx.mbMode5200Required = false;
						if (g_sim.GetHardwareMode() != kATHardwareMode_5200) {
							if (!ATUISwitchHardwareMode(nullptr,
							        kATHardwareMode_5200, true)) {
								failReason = "could not switch to 5200 mode";
								break;
							}
						}
						continue;
					} else if (mctx.mbModeComputerRequired) {
						mctx.mbModeComputerRequired = false;
						if (g_sim.GetHardwareMode() == kATHardwareMode_5200) {
							if (!ATUISwitchHardwareMode(nullptr,
							        kATHardwareMode_800XL, true)) {
								failReason = "could not switch to computer mode";
								break;
							}
						}
						continue;
					} else if (mctx.mbMemoryConflictBasic) {
						// Silently disable BASIC — most common and
						// safe auto-resolution; netplay host cannot
						// show a dialog mid-handshake.
						mctx.mbStopOnMemoryConflictBasic = false;
						mctx.mbMemoryConflictBasic       = false;
						g_sim.SetBASICEnabled(false);
						continue;
					} else if (mctx.mbIncompatibleDiskFormat) {
						mctx.mbIncompatibleDiskFormat       = false;
						mctx.mbStopOnIncompatibleDiskFormat = false;
						continue;
					}

					// Unknown cart mapper, or some other load failure
					// we don't auto-resolve.  There's no user dialog
					// path for netplay.
					if (imgCtx.mLoadType == kATImageType_Cartridge)
						failReason = "cartridge mapper not recognised";
					else if (failReason.empty())
						failReason = "image could not be loaded";
					break;
				}

				if (!loadSuccess) {
					// Restore the user's pre-session state before
					// surfacing the error — otherwise they're left
					// with whatever partial state Load left behind.
					ATNetplayUI::RestoreSessionRestorePoint();
					ATNetplayUI_HostBootFailed(gameIdU8.c_str(),
						failReason.empty()
						    ? "image could not be loaded"
						    : failReason.c_str());
					break;
				}

				ATAddMRU(imagePath.c_str());
				g_sim.ColdReset();

				// v3 cold-boot: stay paused until lockstep engages.
				// The joiner does Load + ColdReset + Pause symmetrically,
				// so both peers are at "frame 0 post-ColdReset" when
				// lockstep entry Resumes them together in netplay_glue.
				// The HLE program loader's CPU trap at $01FE (if any)
				// fires inside lockstep on both peers at the same
				// emulated tick because mLockedRandomSeed seeds
				// mProgramLaunchDelay deterministically.
				{
					extern ATLogChannel g_ATLCNetplay;
					VDStringA imgU8 = VDTextWToU8(imagePath);
					g_ATLCNetplay("host boot: \"%s\" loaded "
						"(hw=%d mem=%d vid=%d), paused at cold-reset "
						"for lockstep entry",
						imgU8.c_str(),
						(int)g_sim.GetHardwareMode(),
						(int)g_sim.GetMemoryMode(),
						(int)g_sim.GetVideoStandard());
				}
				g_sim.Pause();
				break;
			}
			case kATDeferred_NetplayJoinerApply: {
				// a.path is a local cache file containing the host's
				// game bytes.  v3 cold-boot path: apply BootConfig,
				// set locked seed, UnloadAll, Load, ColdReset, Resume,
				// then ack the coordinator → Lockstepping.
				extern void ATNetplayUI_JoinerSnapshotApplied();
				extern void ATNetplayUI_JoinerSnapshotFailed(const char *);
				extern ATLogChannel g_ATLCNetplay;

				ATNetplayInput::AttachEventLogger();

				g_ATLCNetplay("joiner cold-boot: pre-state "
					"running=%d paused=%d hw=%d mem=%d vid=%d",
					g_sim.IsRunning() ? 1 : 0,
					g_sim.IsPaused()  ? 1 : 0,
					(int)g_sim.GetHardwareMode(),
					(int)g_sim.GetMemoryMode(),
					(int)g_sim.GetVideoStandard());

				if (!ATNetplayUI::SaveSessionRestorePoint()) {
					ATNetplayUI_JoinerSnapshotFailed(
						"could not capture pre-session state");
					break;
				}

				// Apply host's MachineConfig (firmware-by-CRC32).
				auto netBoot = ATNetplayGlue::JoinBootConfig();
				ATNetplayUI::MachineConfig cfg;
				cfg.hardwareMode    = (ATHardwareMode)netBoot.hardwareMode;
				cfg.memoryMode      = (ATMemoryMode)netBoot.memoryMode;
				cfg.videoStandard   = (ATVideoStandard)netBoot.videoStandard;
				cfg.cpuMode         = (ATCPUMode)netBoot.cpuMode;
				cfg.basicEnabled    = (netBoot.basicEnabled != 0);
				cfg.sioPatchEnabled = (netBoot.sioAcceleration != 0);
				cfg.kernelCRC32     = netBoot.kernelCRC32;
				cfg.basicCRC32      = netBoot.basicCRC32;
				std::string err = ATNetplayUI::ApplyMachineConfig(cfg);
				if (!err.empty()) {
					ATNetplayUI::RestoreSessionRestorePoint();
					ATNetplayUI_JoinerSnapshotFailed(err.c_str());
					break;
				}

				// Lock the RNG seed to the host's master seed so PIA
				// floating inputs + HLE program-launch delay are
				// deterministic across peers.
				g_sim.SetLockedRandomSeed(netBoot.masterSeed);

				// Unload the joiner's pre-session media before Load.
				// EndSession's RestoreSessionRestorePoint brings it
				// all back.
				g_sim.UnloadAll(ATUIGetBootUnloadStorageMask());

				VDStringA reason;
				bool ok = false;
				try {
					ATImageLoadContext ctx {};
					if (g_sim.Load(a.path.c_str(),
					        kATMediaWriteMode_RO, &ctx)) {
						g_sim.ColdReset();
						// Stay paused — lockstep entry in netplay_glue
						// Resumes both peers together, preventing any
						// free-run drift between ColdReset and the
						// first gated frame.
						g_sim.Pause();
						ATCPUEmulator &cpu = g_sim.GetCPU();
						g_ATLCNetplay("joiner cold-boot: OK "
							"hw=%d mem=%d vid=%d "
							"PC=%04X A=%02X X=%02X Y=%02X S=%02X P=%02X",
							(int)g_sim.GetHardwareMode(),
							(int)g_sim.GetMemoryMode(),
							(int)g_sim.GetVideoStandard(),
							(unsigned)cpu.GetInsnPC(),
							(unsigned)cpu.GetA(),
							(unsigned)cpu.GetX(),
							(unsigned)cpu.GetY(),
							(unsigned)cpu.GetS(),
							(unsigned)cpu.GetP());
						ok = true;
					} else {
						reason = "could not load the game file";
					}
				} catch (const MyError &e) {
					reason = e.c_str();
				} catch (...) {
					reason = "unknown error during cold-boot load";
				}

				if (ok) {
					ATNetplayUI_JoinerSnapshotApplied();
				} else {
					ATNetplayUI::RestoreSessionRestorePoint();
					ATNetplayUI_JoinerSnapshotFailed(
						reason.empty() ? "cold-boot failed"
						               : reason.c_str());
				}
				break;
			}
#endif
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

	// Gamepad / keyboard focus ring.  ImGui's default NavHighlight is
	// barely visible against the Gaming-Mode palette on either theme, so
	// paint it with the palette's accent — this keeps the ring readable
	// and re-tunes automatically whenever the theme changes.  The ring
	// only appears after the first gamepad/keyboard nav event, so touch
	// users never see it.
	{
		uint32 nav = dark
			? IM_COL32(102, 180, 255, 255)   // cyan-blue on dark
			: IM_COL32( 28,  98, 196, 255);  // deep blue on light
		style.Colors[ImGuiCol_NavHighlight] = ImVec4(
			((nav >> IM_COL32_R_SHIFT) & 0xFF) / 255.0f,
			((nav >> IM_COL32_G_SHIFT) & 0xFF) / 255.0f,
			((nav >> IM_COL32_B_SHIFT) & 0xFF) / 255.0f,
			1.0f);
	}
}

void ATUIUpdateSystemTheme() {
	SDL_SystemTheme sysTheme = SDL_GetSystemTheme();
	s_systemThemeIsDark = (sysTheme == SDL_SYSTEM_THEME_DARK);
}

bool ATUIIsDarkTheme() {
	return ResolveIsDark(g_ATOptions.mThemeMode);
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

	ATUIApplyModeStyle(contentScale);

	// Discover bundled TTFs, load persisted choices, and build the initial
	// font atlas.  Must run BEFORE the renderer backend is initialised so
	// the backend picks up our atlas on its first NewFrame instead of
	// shipping ProggyClean.
	// Fonts are rasterised at contentScale × logical-pt-size for crisp
	// glyphs on HiDPI, then scaled back to logical coordinates so they
	// appear at the intended point size.
	io.FontGlobalScale = 1.0f / contentScale;
	const bool usingGL = (backend->GetType() == DisplayBackendType::OpenGL);
	ATUIFontsInit(contentScale, usingGL);

	s_pDisplayBackend = backend;

	if (backend->GetType() == DisplayBackendType::OpenGL) {
		s_usingGLBackend = true;
		auto *glBackend = static_cast<DisplayBackendGL *>(backend);
		if (!ImGui_ImplSDL3_InitForOpenGL(window, glBackend->GetGLContext())) {
			LOG_ERROR("UI", "ImGui SDL3/OpenGL init failed");
			return false;
		}
		// Pass the GLSL version that matches the active GL profile.
		// ImGui uses this string verbatim as the shader header, so it
		// must agree with the context — otherwise the ImGui draw shaders
		// either fail to compile or fall back to the wrong feature set.
		const char *imguiGlslVersion = (GLGetActiveProfile() == GLProfile::ES30)
			? "#version 300 es" : "#version 330 core";
		if (!ImGui_ImplOpenGL3_Init(imguiGlslVersion)) {
			LOG_ERROR("UI", "ImGui OpenGL3 init failed");
			return false;
		}
		LOG_INFO("UI", "ImGui initialized (%s, docking enabled)",
			GLGetActiveProfile() == GLProfile::ES30
				? "OpenGL ES 3.0" : "OpenGL 3.3 Core");
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

#ifdef ALTIRRA_NETPLAY_ENABLED
	ATNetplayUI_Initialize(window);
#endif

	return true;
}

void ATUIShutdown() {
#ifdef ALTIRRA_NETPLAY_ENABLED
	ATNetplayUI_Shutdown();
#endif
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

SDL_Surface *ATUIReadFramebuffer() {
	return ReadFramebufferToSurface(ATUIGetDisplayBackend());
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
// Main display text selection — Windows parity for mouse-drag selection and
// right-click context menu on the Atari frame.
// =========================================================================
//
// Unlike the debugger Display pane (which hosts the Atari texture inside an
// ImGui window and handles selection there), the main display is drawn
// directly to the SDL framebuffer by the backend.  Mouse events still flow
// through ImGui (SDL events are forwarded to ImGui by ATUIProcessEvent()),
// so we can piggy-back on ImGui::IsMouseClicked()/IsMouseDown() for the
// drag and on OpenPopup()/BeginPopup() for the context menu.  The highlight
// overlay is drawn into the viewport background draw list so it appears
// above the Atari frame but below any ImGui window that opens on top.
//
// Mirrors Windows ATDisplayPane::OnDisplayContextMenu() (uidisplay.cpp:1949)
// and IDR_DISPLAY_CONTEXT_MENU (Altirra.rc:239).

void ATUIRenderMainDisplayTextSelection() {
	// When the debugger is open the main display is not drawn; the debugger
	// Display pane renders its own Atari texture and runs its own selection
	// handler inside that window.  Skip here to avoid double-handling.
	if (ATUIDebuggerIsOpen())
		return;

	float rx, ry, rw, rh;
	if (!ATGetMainDisplayRect(rx, ry, rw, rh))
		return;

	const ImVec2 imagePos(rx, ry);
	const ImVec2 imageSize(rw, rh);

	ATInputManager *im = g_sim.GetInputManager();
	const bool mouseRoutedToInput = im && im->IsMouseMapped()
		&& (ATUIIsMouseCaptured() || im->IsMouseAbsoluteMode());

	const bool uiConsumesMouse = ImGui::GetIO().WantCaptureMouse;

	// Pan/Zoom tool owns LMB drag and Ctrl+LMB (zoom).  SDL events are
	// forwarded to ImGui unconditionally, so IsMouseClicked(LMB) still
	// fires when pan/zoom is consuming the click — without this guard
	// the tool and text selection would race on the same drag.
	const bool panZoomActive = ATUIIsPanZoomToolActive();

	// Drag handling — only when the click wouldn't otherwise be consumed by
	// the input manager, pan/zoom, or an ImGui window.  The handler itself
	// gates on the mouse being inside the display rect, so we don't
	// duplicate that.  Always call through when a drag is already in
	// progress so that a mouse-up (even on an ImGui window that popped
	// open mid-drag) still finalizes the selection and clears mbDragActive.
	ATTextSelectionState& sel = ATUIGetTextSelection();
	if (sel.mbDragActive || (!uiConsumesMouse && !mouseRoutedToInput && !panZoomActive))
		ATUITextSelectionHandleMouse(imagePos, imageSize);

	// Right-click context menu — opens at the cursor when the click is
	// inside the display rect and not consumed elsewhere.  Matches Windows
	// ATDisplayPane which shows the menu on WM_CONTEXTMENU over the display.
	const ImVec2 mouse = ImGui::GetMousePos();
	const bool inDisplay = mouse.x >= imagePos.x && mouse.x < imagePos.x + imageSize.x
	                    && mouse.y >= imagePos.y && mouse.y < imagePos.y + imageSize.y;

	if (!uiConsumesMouse && !mouseRoutedToInput && !panZoomActive && inDisplay
		&& ImGui::IsMouseClicked(ImGuiMouseButton_Right))
	{
		ImGui::OpenPopup("##DisplayContextMenu");
	}

	if (ImGui::BeginPopup("##DisplayContextMenu")) {
		const bool hasSelection = ATUIIsTextSelected();
		const bool hasClipText  = ATUIClipIsTextAvailable();

		// Items match IDR_DISPLAY_CONTEXT_MENU in Altirra.rc:239-247.
		if (ImGui::MenuItem("Copy Text",
				ATUIGetShortcutStringForCommand("Edit.CopyText"), false, hasSelection))
			ATUITextCopy(ATTextCopyMode::ASCII);
		if (ImGui::MenuItem("Copy Escaped Text", nullptr, false, hasSelection))
			ATUITextCopy(ATTextCopyMode::Escaped);
		if (ImGui::MenuItem("Copy Hex", nullptr, false, hasSelection))
			ATUITextCopy(ATTextCopyMode::Hex);
		if (ImGui::MenuItem("Copy Unicode", nullptr, false, hasSelection))
			ATUITextCopy(ATTextCopyMode::Unicode);
		ImGui::Separator();
		if (ImGui::MenuItem("Paste",
				ATUIGetShortcutStringForCommand("Edit.PasteText"), false, hasClipText))
			ATUIPasteText();
		ImGui::EndPopup();
	}

	// Highlight overlay — drawn into the viewport background so it appears
	// above the Atari frame but below any open ImGui window/dialog.
	ATUITextSelectionDrawOverlay(imagePos, imageSize, ImGui::GetBackgroundDrawList());
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

	if (ATUIIsGamingMode()) {
		extern ATMobileUIState g_mobileState;
		ATMobileUI_Render(sim, state, g_mobileState, window);
	} else {
		ATUIRenderMainMenu(sim, window, backend, state);
	}
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
#ifdef ALTIRRA_NETPLAY_ENABLED
	ATNetplayUI_RenderDesktop(sim, state, window);
#endif
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
	if (ATUIIsGamingMode()) {
		if (g_showToolsResult) {
			ATMobileUI_ShowInfoModal("Tool Result", g_toolsResultMessage.c_str());
			g_showToolsResult = false;
		}
	} else {
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
	}

	// Export ROM overwrite confirmation popup
	if (ATUIIsGamingMode()) {
		if (g_showExportROMOverwrite) {
			VDStringW path = g_exportROMPath;
			ATMobileUI_ShowConfirmDialog("Overwrite Existing Files?",
				"There are existing files with the same names that will be overwritten.\nAre you sure?",
				[path]() { ATUIDoExportROMSet(path); });
			g_showExportROMOverwrite = false;
		}
	} else {
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
	}

	// Progress dialog popup (firmware scan, background tasks)
	ATUIRenderProgress();

	// Emulation error dialog popup (program crash recovery)
	ATUIRenderEmuErrorDialog(sim);

	// Drag-and-drop visual feedback overlay
	ATUIRenderDragDropOverlay();

	// Reusable confirmation dialogs — drawn last so they sit above
	// every other window.  Also re-centers and captures keyboard focus.
	ATUIRenderConfirmDialogs();

	// Main display text-mode selection: mouse drag, highlight overlay, and
	// right-click context menu.  Runs only when the debugger is closed;
	// the debugger Display pane already wires selection in ui_debugger.cpp.
	// Disabled in Gaming Mode where touch events drive on-screen controls.
	if (!ATUIIsGamingMode())
		ATUIRenderMainDisplayTextSelection();

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
