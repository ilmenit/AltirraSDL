#include <stdafx.h>
#include <algorithm>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>
#include "vbxe.h"
#include "vbxestate.h"
#include "savestateio.h"
#include <vd2/system/file.h>
#include <vd2/system/zip.h>
#include "gtiatables.h"
#include "gtiarenderer.h"
#include "irqcontroller.h"
#include "artifacting.h"

static void Check(bool ok, const char *message) {
	if (!ok) {
		fprintf(stderr, "%s\n", message);
		std::exit(1);
	}
}

// Test doubles for services outside this renderer/dump test. Unexpected
// interaction is fatal, never silently accepted as emulation success.
static bool allowStateServices = false;
ATIRQController::ATIRQController() {}
ATIRQController::~ATIRQController() {}
void ATIRQController::Assert(uint32, bool) { Check(false, "Unexpected IRQ assert"); }
void ATIRQController::Negate(uint32, bool) { Check(allowStateServices, "Unexpected IRQ negate"); }
uint32 ATPaletteCorrector::CorrectSingleColor(uint32 color, bool, bool) const {
	Check(allowStateServices, "Unexpected palette correction");
	return color;
}
static std::string console;
void ATConsoleWrite(const char *s) { console += s; }
void ATConsolePrintfImpl(const char *format, ...) {
	char buffer[4096];
	va_list args;
	va_start(args, format);
	const int n = vsnprintf(buffer, sizeof buffer, format, args);
	va_end(args);
	Check(n >= 0 && n < (int)sizeof buffer, "Console capture truncated");
	console += buffer;
}
void ATConsoleTaggedPrintfImpl(const char *, ...) { Check(false, "Unexpected blitter execution"); }

static vdrefptr<ATSaveStateVbxe> RoundTripState(IATObjectState *state) {
	VDMemoryBufferStream data;
	{
		vdautoptr<IVDZipArchiveWriter> zip(VDCreateZipArchiveWriter(data));
		vdautoptr<IATSaveStateSerializer> writer(ATCreateSaveStateSerializer(L"vbxe.json"));
		writer->Serialize(*zip, *state);
		zip->Finalize();
	}
	data.Seek(0);
	VDZipArchive zip;
	zip.Init(&data);
	vdautoptr<IATSaveStateDeserializer> reader(ATCreateSaveStateDeserializer(L"vbxe.json"));
	vdrefptr<IATSerializable> restored;
	reader->Deserialize(zip, ~restored);
	auto *typed = atser_cast<ATSaveStateVbxe *>(restored);
	Check(typed != nullptr, "VBXE serialized snapshot has wrong type");
	return vdrefptr<ATSaveStateVbxe>(typed);
}

struct ATVBXERegressionTests {
	static void Run() {
		using namespace ATGTIA;
		ATVBXEEmulator v;
		for (uint32 version : {120U, 124U, 126U, 127U}) {
			v.SetVersion(version);
			Check(v.HasNTSCVerticalOffset() == (version < 127),
				"FX1.27 NTSC vertical offset incorrectly enabled");
		}
		uint8 tables[32][256];
		ATInitGTIAPriorityTables(tables);
		for (uint32 version : {124U, 126U, 127U}) {
			v.SetVersion(version);
			for (int t = 0; t < 32; ++t) {
				for (int i = 0; i < 256; ++i) {
					int expected = -1;
					switch (tables[t][i]) {
						case kColorPF2P2: expected = PF2 | P2; break;
						case kColorPF2P3: expected = PF2 | P3; break;
						case kColorPF2P2P3: expected = PF2 | P2 | P3; break;
						case kColorPF3: expected = version >= 126 ? PF2 : PF3; break;
						case kColorBAK: expected = version >= 126 ? PF3 : 0; break;
					}
					if (expected >= 0) {
						Check(v.mPriorityTables[t][i].mOvPri == expected, "VBXE overlay priority mismatch");
						Check(v.mPriorityTablesHi[t][i].mOvPri == expected, "VBXE hires priority mismatch");
					}
				}
			}
		}

		// 43 fetched cells followed by zero cells; hardware index wraps at 64.
		v.mOvWidth = ATVBXEEmulator::kOvWidth_Normal;
		v.mAttrWidth = 1;
		v.mAttrHscroll = 0;
		v.mbExtendedColor1 = true;
		std::fill(std::begin(v.mOvPriority), std::end(v.mOvPriority), 0xFF);
		for (int i = 0; i < 43; ++i)
			v.mAttrBuffer[i * 4] = (uint8)(i + 1);
		for (uint32 version : {124U, 126U, 127U}) {
			v.SetVersion(version);
			Check(v.GetVersion() == version, "VBXE core version mismatch");
			Check(v.RenderAttrPixels(96, 176) == 176, "Unexpected attribute span break");
			for (int x = 96; x < 176; ++x) {
				const int cell = (x - 96) % (version >= 127 ? 43 : 64);
				Check(v.mAttrPixels[x].mColors[1] == (cell < 43 ? cell + 1 : 0),
					"Attribute cell wrap mismatch");
			}
		}
		v.SetVersion(126);


		// Exercise actual RES conversion and renderer with a poisoned scratch
		// buffer: non-PF2 pixels must preserve the current player bits.
		uint32 pixels[912] {};
		uint8 merge[228], antic[228] {};
		std::fill(std::begin(merge), std::end(merge), P0);
		std::fill(std::begin(v.mTempMergeBuffer), std::end(v.mTempMergeBuffer), PF3);
		std::fill(std::begin(v.mAttrBuffer), std::end(v.mAttrBuffer), 0);
		for (int i = 0; i < 43; ++i) v.mAttrBuffer[i * 4 + 3] = 4;
		for (auto& palette : v.mPalette)
			for (int i = 0; i < 256; ++i) palette[i] = (uint32)i;
		v.mColorTable[kColorP0] = v.mColorTableExt[kColorP0] = 42;
		v.mAttrWidth = 8;
		v.mbAttrMapEnabled = true;
		v.mbHiresMode = true;
		v.mOvCollMask = 0;
		v.mX = 48;
		v.mpDst = pixels;
		v.mpMergeBuffer = v.mpMergeBuffer0 = merge;
		v.mpAnticBuffer = v.mpAnticBuffer0 = antic;
		v.RenderScanline(56, true);
		for (int x = 48; x < 56; ++x) {
			Check(v.mTempMergeBuffer[x] == P0, "RES conversion reused stale pixel");
			Check(pixels[x * 4] == 42, "RES conversion changed player color");
			Check(v.mOvPriDecode[x * 4] == P0, "Renderer lost player overlay priority");
		}

		// PF0 in a hires playfield uses its global color; set
		// pixels substitute PF1 luma without changing that chroma.
		std::fill(std::begin(merge), std::end(merge), PF0);
		std::fill(std::begin(antic), std::end(antic), 2);
		v.mpMergeBuffer = merge;
		v.mpAnticBuffer = antic;
		v.mbAttrMapEnabled = false;
		v.mbExtendedColor1 = false;
		v.UpdateColorTable();
		v.mColorTable[kColorPF0] = 0xA6;
		v.mColorTable[kColorPF1] = 0x0B;
		v.mX = 48;
		v.RenderScanline(50, true);
		Check(pixels[192] == 0xAB && pixels[194] == 0xA6,
			"Hires PF0 lost attribute chroma or PF1 luma");

		// Blitter completion with IRQ disabled must leave both the
		// request bit and the external IRQ line inactive.
		v.mbIRQEnabled = false;
		v.mbIRQRequest = false;
		v.mBlitState = ATVBXEEmulator::BlitState::Stopping;
		v.OnScheduledEvent(1);
		Check(!v.mbIRQRequest, "Disabled blitter IRQ latched a request");
		Check(v.mBlitState == ATVBXEEmulator::BlitState::Stopped,
			"Blitter completion did not stop");

		// The command parser supplies maxCount; test the production dump loop
		// below/at/above the historical 256-entry cap, including the upper limit.
		std::vector<uint8> memory(0x80000, 8); // next-entry flag in every control byte
		v.SetMemory(memory.data());
		for (uint32 limit : {1U, 256U, 300U, 65536U}) {
			console.clear();
			v.DumpBlitList(0, true, limit);
			Check(std::count(console.begin(), console.end(), '\n') == limit + 1,
				"Blit-list limit not honored");
			Check(console.find("exceeds max entries") != std::string::npos, "Missing limit diagnostic");
		}
		memory[20] = 0;
		console.clear();
		v.DumpBlitList(0, true, 300);
		Check(std::count(console.begin(), console.end(), '\n') == 1, "Blit-list terminator ignored");

		// Decode a real BCB for each operation. The offsets and control
		// encodings follow LoadBCB/SaveBCB in the Windows implementation.
		uint8 bcb[21] {};
		bcb[0] = 0x00; bcb[1] = 0x10; // source $1000
		bcb[5] = 1;
		bcb[6] = 0x00; bcb[7] = 0x20; // destination $2000
		bcb[11] = 1;
		bcb[15] = 0xFF;
		bcb[17] = 0xFF;
		for (uint32 version : {124U, 126U, 127U}) {
			v.SetVersion(version);
			memory[0x1000] = 0x12;
			memory[0x2000] = 0xAB;
			bcb[20] = 6; // HR overlay
			v.LoadBCB(bcb);
			v.StartBlit();
			v.RunBlitterRow();
			Check(memory[0x2000] == 0x12, "HR overlay result mismatch");
			Check(v.mBlitCollisionCode == 0xAB,
				"HR overlay failed to report both colliding nibbles");
		}

		v.SetVersion(127);
		for (uint8 mode : {uint8(2), uint8(0x12), uint8(0x22)}) {
			memory[0x1000] = 0x30;
			memory[0x2000] = 0xE0;
			bcb[20] = mode;
			v.LoadBCB(bcb);
			v.RunBlitterRow();
			Check(memory[0x2000] == (mode == 2 ? 0x10 : 0xFF),
				"FX1.27 add/carry/saturating add mismatch");
		}
		// The table mode uses BCB submode/collision mask as a table
		// address and maps even a zero source byte through that table.
		bcb[20] = 7;
		bcb[17] = 0x30;
		memory[0x1000] = 0;
		memory[0x3000] = 0x6D;
		v.LoadBCB(bcb);
		v.RunBlitterRow();
		Check(memory[0x2000] == 0x6D, "FX1.27 table blit skipped zero source");

		// Raycasting uses fixed-point texture coordinates and leaves the
		// base source address unchanged as coordinates advance.
		bcb[20] = 0;
		bcb[12] = 1; // two pixels
		bcb[17] = 0;
		v.LoadBCB(bcb);
		v.mBlitListFetchAddr = 0x4000;
		memory[0x4000] = 0x20; // final raycasting container
		memory[0x4001] = 0; memory[0x4002] = 1; // X = 1.0
		memory[0x4003] = 0; memory[0x4004] = 0; // Y = 0.0
		memory[0x4005] = 1; // X step = 1.0 + BCB low byte
		Check(v.LoadNextBCBContainer(), "FX1.27 raycasting container rejected");
		memory[0x1001] = 0x52;
		memory[0x1002] = 0x73;
		v.RunBlitterRow();
		Check(memory[0x2000] == 0x52 && memory[0x2001] == 0x73,
			"FX1.27 raycasting texture samples mismatch");
		Check(v.mBlitSrcAddr == 0x1000 && v.mBlitRCSrcX == 0x302,
			"FX1.27 raycasting coordinate advance mismatch");

		// Invalid containers stop immediately and never request an IRQ.
		v.mBlitListFetchAddr = 0x4000;
		memory[0x4000] = 0x40;
		Check(!v.LoadNextBCBContainer(), "Invalid BCB container accepted");
		Check(v.mBlitState == ATVBXEEmulator::BlitState::Stopped && !v.mbIRQRequest,
			"Invalid BCB container failed to stop without IRQ");
		v.SetVersion(126);

		// Exercise the actual DMA fetch across the end of 512K RAM. Only
		// 43 cells are fetched; the remaining buffer cells must stay untouched.
		ATScheduler scheduler;
		v.mpScheduler = &scheduler;
		v.mbAttrMapEnabled = true;
		v.mAttrAddr = 0x7FFF0;
		v.mAttrRow = 0;
		v.mXdlRepeatCounter = 2;
		for (size_t i = 0; i < memory.size(); ++i) memory[i] = (uint8)i;
		std::fill(std::begin(v.mAttrBuffer), std::end(v.mAttrBuffer), 0xA5);
		v.BeginScanline(1, nullptr, merge, antic, false);
		Check(v.mDMACyclesAttrMap == 43 * 4, "Attribute DMA byte count mismatch");
		for (int i = 0; i < 256; ++i)
			Check(v.mAttrBuffer[i] == (i < 172 ? (uint8)(0xF0 + i) : 0xA5),
				"Attribute DMA did not wrap RAM or exceeded 43 cells");
		// A snapshot taken while processing an FX1.27 texture must restore
		// the consumed container, extended control bits and BCB submode.
		ATIRQController irq;
		ATPaletteCorrector corrector;
		v.mpIRQController = &irq;
		v.mpPaletteCorrector = &corrector;
		for (auto& palette : v.mArchPalette)
			std::fill(std::begin(palette), std::end(palette), 0);
		allowStateServices = true;
		v.SetVersion(127);
		v.WriteControl(0x4B, 0x99);
		Check(v.mpColorTable == v.mColorTableExt,
			"FEATURE_CONTROL did not activate extended GTIA colors");
		bcb[20] = 0x22;
		v.LoadBCB(bcb);
		v.mBlitState = ATVBXEEmulator::BlitState::ProcessBlit;
		v.mBlitHeightLeft = 1;
		v.mbBlitRCMode = true;
		v.mBlitRCSrcX = 0x103;
		v.mBlitRCSrcY = 0x209;
		v.mBlitRCSrcInitX = 0x100;
		v.mBlitRCSrcInitY = 0x200;
		v.mBlitRCSrcStepX = 0x102;
		v.mBlitRCSrcStepY = 0x207;
		v.mBlitRCTexSizeBitsX = 5;
		v.mBlitRCTexSizeBitsY = 6;
		v.mBlitCollisionMask = 0xFF;
		v.mBlitActiveCollisionMask = 0;
		auto textureState = RoundTripState(v.SaveState());
		v.WriteControl(0x4B, 0);
		v.mbBlitRCMode = false;
		v.mBlitRCSrcX = v.mBlitRCSrcY = 0;
		v.LoadState(textureState);
		Check(v.mFeatureControl == 0x99 && v.mPfPaletteIndexInit == 1
			&& v.mbExtendedColor1 && v.mbExtendedColor0
			&& v.mpColorTable == v.mColorTableExt,
			"FX1.27 snapshot lost feature control");
		Check(v.mBlitActiveCollisionMask == 0 && v.mBlitCollisionMask == 0xFF,
			"Serialized snapshot lost collision latch");
		Check(v.mBlitterMode == 2 && v.mBlitterSubMode == 0x20,
			"FX1.27 snapshot lost BCB submode");
		Check(v.mbBlitRCMode && v.mBlitRCSrcX == 0x103
			&& v.mBlitRCSrcY == 0x209 && v.mBlitRCSrcInitX == 0x100
			&& v.mBlitRCSrcInitY == 0x200 && v.mBlitRCSrcStepX == 0x102
			&& v.mBlitRCSrcStepY == 0x207 && v.mBlitRCTexSizeBitsX == 5
			&& v.mBlitRCTexSizeBitsY == 6,
			"FX1.27 snapshot lost raycasting state");

		// Continue the restored texture operation and compare a known sample.
		memory[0x1000 | 1 | (2 << 5)] = 0xF0;
		memory[0x2000] = 0x20;
		v.RunBlitterRow();
		Check(memory[0x2000] == 0xFF && v.mBlitRCSrcX == 0x307
			&& v.mBlitRCSrcY == 0x617,
			"Serialized raycasting blit did not continue correctly");

		v.mBlitState = ATVBXEEmulator::BlitState::ProcessContainers;
		v.mBlitListFetchAddr = 0x4000;
		auto containerState = RoundTripState(v.SaveState());
		v.mBlitState = ATVBXEEmulator::BlitState::Stopped;
		v.LoadState(containerState);
		Check(v.mBlitState == ATVBXEEmulator::BlitState::ProcessContainers
			&& v.mBlitHeightLeft == 1 && v.mBlitListFetchAddr == 0x4000,
			"FX1.27 snapshot did not resume BCB containers");

		// Complete a restored pending absolute-jump container and verify
		// the final container transitions back to the decoded blit.
		memory[0x4000] = 0;
		memory[0x4001] = 0x34;
		memory[0x4002] = 0x12;
		memory[0x4003] = 0;
		Check(v.LoadNextBCBContainer()
			&& v.mBlitState == ATVBXEEmulator::BlitState::ProcessBlit
			&& v.mBlitListFetchAddr == 0x1234,
			"Serialized BCB container did not continue correctly");

		// Old snapshots omit the FX1.27 presence flag. Their VIDEO_CONTROL
		// must still restore XCOLOR[1] and clear a previous FX1.27 config.
		const char legacyJSON[] =
			"{\"objects\":[{\"_type\":\"ATSaveStateVbxe\",\"arch_video_control\":2}]}";
		VDMemoryStream legacyData(legacyJSON, sizeof(legacyJSON) - 1);
		vdautoptr<IATSaveStateDeserializer> legacyReader(ATCreateSaveStateDeserializer());
		vdrefptr<IATSerializable> legacyObject;
		legacyReader->Deserialize(legacyData, ~legacyObject);
		auto *legacyState = atser_cast<ATSaveStateVbxe *>(legacyObject);
		Check(legacyState && !legacyState->mbFeatureControlPresent
			&& !legacyState->mbIntBlitRaycast && !legacyState->mbIntBlitContainersPending,
			"Legacy serialized snapshot did not default absent FX1.27 fields");
		v.LoadState(legacyState);
		Check(v.mbExtendedColor1 && !v.mbExtendedColor0
			&& v.mFeatureControl == 0x10 && v.mPfPaletteIndexInit == 0,
			"Legacy snapshot inherited FX1.27 feature bits");
		v.WriteControl(0x4B, 0x08); // XCOLOR[0] only
		Check(v.mpColorTable == v.mColorTableExt, "XCOLOR[0] did not select extended table");
		v.SetVersion(126);
		Check(!v.mbExtendedColor0 && v.mpColorTable == v.mColorTable,
			"Core downgrade left FX1.27 extended color selection active");

		allowStateServices = false;
		v.mpIRQController = nullptr;
		v.mpPaletteCorrector = nullptr;
		v.mpScheduler = nullptr;
	}
};

int main() {
	ATVBXERegressionTests::Run();
	puts("VBXE priority, hires, IRQ, FX1.27 blitter and attribute regressions passed");
}
