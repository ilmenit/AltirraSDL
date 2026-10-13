// Production CPU conformance runner; optional debugger services fail if used.
#include <stdafx.h>
#include <cstdio>
#include <cstdlib>
#include "cpu.h"
#include "cpuheatmap.h"
#include "verifier.h"
#include "bkptmanager.h"
#include "cpuhookmanager.h"
#include "disasm.h"
#include "cputest_runner.h"

static void Unexpected(const char *service) {
	fprintf(stderr, "Unexpected CPU test service: %s\n", service);
	std::exit(1);
}
void ATCPUHeatMap::ProcessInsn(const ATCPUEmulator&, uint8, uint16, uint16) { Unexpected("heat map"); }
void ATCPUVerifier::OnReset() { Unexpected("verifier reset"); }
void ATCPUVerifier::OnIRQEntry() { Unexpected("verifier IRQ"); }
void ATCPUVerifier::OnNMIEntry() { Unexpected("verifier NMI"); }
void ATCPUVerifier::VerifyInsn(const ATCPUEmulator&, uint8, uint16) { Unexpected("verifier instruction"); }
int ATBreakpointManager::CheckPCBreakpoints(uint32, uint32, const BreakpointIndices*) { Unexpected("breakpoint"); return 0; }
uint8 ATCPUHookManager::OnHookHit(uint16) const { Unexpected("CPU hook"); return 0; }
uint16 ATDisassembleInsn(uint16, uint8) { Unexpected("disassembler"); return 0; }
void ATConsoleWrite(const char *s) { fputs(s, stdout); }
void ATConsolePrintfImpl(const char *, ...) { Unexpected("console printf"); }
void ATConsoleTaggedPrintfImpl(const char *, ...) { Unexpected("tagged console printf"); }

int main(int argc, char **argv) {
	if (argc != 2) return 2;
	ATCPUTestOptions opts;
	opts.mPath = argv[1];
	opts.mCheckCycles = true;
	opts.mVerbose = true;
	return ATRunCPUTests(opts);
}
