//	Altirra - Atari 800/800XL/5200 emulator
//	SDL3 frontend - JSON processor-test (Tom Harte SingleStepTests) CLI runner
//	Copyright (C) 2009-2026 Avery Lee
//
//	This program is free software; you can redistribute it and/or modify
//	it under the terms of the GNU General Public License as published by
//	the Free Software Foundation; either version 2 of the License, or
//	(at your option) any later version.
//
//	This program is distributed in the hope that it will be useful,
//	but WITHOUT ANY WARRANTY; without even the implied warranty of
//	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//	GNU General Public License for more details.
//
//	You should have received a copy of the GNU General Public License
//	along with this program; if not, write to the Free Software
//	Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.

#ifndef f_AT_CPUTEST_RUNNER_H
#define f_AT_CPUTEST_RUNNER_H

#include <string>

// Mode filter selecting which per-opcode test files to run.
//   e    -> only emulation-mode files (hh.e.json)
//   n    -> only native-mode files    (hh.n.json)
//   both -> both
enum class ATCPUTestMode {
	Emulation,
	Native,
	Both
};

struct ATCPUTestOptions {
	// Path to either a single .json test file or a directory containing
	// per-opcode files named "hh.e.json" / "hh.n.json".
	std::string mPath;

	// When >= 0, only the file(s) for this opcode (0x00-0xFF) are run.
	int mOpcodeFilter = -1;

	// Which of the e/n variants to run when mPath is a directory.
	ATCPUTestMode mMode = ATCPUTestMode::Both;

	// Maximum number of tests to run per file (0 = all).
	int mLimit = 0;

	// Stop the whole run on the first failing test.
	bool mStopOnFail = false;

	// Print per-register / per-RAM diffs for each failing test.
	bool mVerbose = false;
};

// Runs the 65C816 conformance harness described by opts and returns a
// process exit code: 0 = all passed, 1 = one or more failures, 2 = fatal
// I/O or JSON parse error.
int ATRunCPUTests(const ATCPUTestOptions& opts);

#endif	// f_AT_CPUTEST_RUNNER_H
