#include <stdafx.h>
#include <algorithm>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>
#include "vbxe.h"
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
void ATIRQController::Assert(uint32, bool) { Check(false, "Unexpected IRQ assert"); }
void ATIRQController::Negate(uint32, bool) { Check(false, "Unexpected IRQ negate"); }
uint32 ATPaletteCorrector::CorrectSingleColor(uint32, bool, bool) const {
	Check(false, "Unexpected palette correction");
	return 0;
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

struct ATVBXERegressionTests {
	static void Run() {
		using namespace ATGTIA;
		ATVBXEEmulator v;
		uint8 tables[32][256];
		ATInitGTIAPriorityTables(tables);
		for (uint32 version : {124U, 126U}) {
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
		v.mbExtendedColor = true;
		std::fill(std::begin(v.mOvPriority), std::end(v.mOvPriority), 0xFF);
		for (int i = 0; i < 43; ++i)
			v.mAttrBuffer[i * 4] = (uint8)(i + 1);
		Check(v.RenderAttrPixels(96, 176) == 176, "Unexpected attribute span break");
		for (int x = 96; x < 176; ++x) {
			const int cell = (x - 96) & 63;
			Check(v.mAttrPixels[x].mColors[1] == (cell < 43 ? cell + 1 : 0), "Attribute cell wrap mismatch");
		}

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
		v.mColorTable[kColorP0] = 42;
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

		// Exercise the actual DMA fetch across the end of 512K RAM. Only
		// 43 cells are fetched; the remaining buffer cells must stay untouched.
		ATScheduler scheduler;
		v.mpScheduler = &scheduler;
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
		v.mpScheduler = nullptr;
	}
};

int main() {
	ATVBXERegressionTests::Run();
	puts("VBXE priority, attribute wrap, RES conversion and blit limits passed");
}
