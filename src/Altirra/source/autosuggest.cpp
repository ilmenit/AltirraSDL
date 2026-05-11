//	Altirra - Atari 800/800XL/5200 emulator
//	Copyright (C) 2026 Avery Lee
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
//	You should have received a copy of the GNU General Public License along
//	with this program. If not, see <http://www.gnu.org/licenses/>.

#include <stdafx.h>
#include <charconv>
#include <regex>
#include <vd2/system/text.h>
#include <at/atcore/deviceautosuggest.h>
#include <at/atdebugger/defsymbols.h>
#include "autosuggest.h"
#include "devicemanager.h"
#include "simulator.h"

extern ATSimulator g_sim;

// ---------------------------------------------------------------------------
// Atari + Altirra BASIC keyword catalogue
// ---------------------------------------------------------------------------
//
// Source of truth: src/ATBasic/source/tokens.inc + Altirra BASIC Reference
// Manual (2018-08-12 edition).  This catalogue is consumed by
// HandleStatementKeyword / HandleFunctionKeyword below and is intentionally
// shaped as data so additional keywords (e.g. future Altirra BASIC
// extensions) can be added without touching engine logic.
//
// Each entry carries:
//   mName          — canonical uppercase identifier (no abbreviation).
//   mDescription   — short hint shown to the right of the popup row.
//   mbHasArguments — whether the canonical insertion appends a trailing
//                    space.  Statements/functions that take arguments
//                    get a space so the user can keep typing; bare
//                    keywords (END, NEW, …) do not.
//
// The popup orders results in the order they are emitted, which matches
// the order in these tables.  Most-used statements are grouped near the
// top of each category for fast scanning.

namespace {
	struct AtariBasicKeyword {
		const char     *mName;
		const wchar_t  *mDescription;
		bool            mbHasArguments;
		// Popularity weight, 0-100.  Used to sort suggestions so the
		// statements/functions a typical user reaches for first show
		// up at the top of the popup.  Ordering ties are broken by
		// table position (so groups stay together visually).  Values
		// are calibrated from empirical sampling of Atari BASIC type-
		// in listings + the Altirra BASIC reference manual examples;
		// they err toward "loop / control flow / I/O common, file-
		// management / housekeeping rare".
		uint8           mPopularity;
		// Canonical Atari BASIC abbreviation (e.g. "PR." for PRINT).
		// Shown in the popup as a hint.  Null = no canonical abbrev
		// (some keywords are short enough not to need one, others
		// have no standard abbreviation in Atari BASIC).
		const char     *mAbbrev;
	};

	constexpr AtariBasicKeyword kAtariBasicStatements[] = {
		// I/O & control flow — most-used statements in any session.
		{ "PRINT",      L"Print to screen / IOCB",                    true,  98, "PR."  },
		{ "INPUT",      L"Read from keyboard / IOCB",                 true,  90, "I."   },
		{ "IF",         L"Conditional branch",                        true,  95, nullptr },
		{ "FOR",        L"Begin counted loop",                        true,  94, "F."   },
		{ "NEXT",       L"Iterate counted loop",                      true,  92, "N."   },
		{ "GOTO",       L"Unconditional jump",                        true,  91, "G."   },
		{ "GOSUB",      L"Call subroutine",                           true,  85, "GOS." },
		{ "RETURN",     L"Return from GOSUB",                         false, 82, "RET." },
		{ "LET",        L"Variable assignment",                       true,  80, "LE."  },
		{ "REM",        L"Remark / comment",                          true,  78, "."    },
		{ "?",          L"PRINT shorthand",                           true,  75, nullptr },
		{ "END",        L"End program",                               false, 74, nullptr },
		{ "DIM",        L"Allocate array",                            true,  73, "DI."  },
		{ "DATA",       L"DATA values for READ",                      true,  70, "D."   },
		{ "READ",       L"Read from DATA",                            true,  68, "REA." },
		{ "POKE",       L"Store byte to address",                     true,  68, nullptr },
		{ "GET",        L"Read one byte from IOCB",                   true,  60, "GE."  },
		{ "PUT",        L"Write one byte to IOCB",                    true,  58, "PU."  },
		{ "OPEN",       L"Open IOCB channel",                         true,  60, "O."   },
		{ "CLOSE",      L"Close IOCB channel",                        true,  58, "CL."  },
		// Block IF (Altirra extension).
		{ "THEN",       L"IF body",                                   true,  90, nullptr },
		{ "ELSE",       L"IF block else branch (Altirra)",            false, 60, nullptr },
		{ "ENDIF",      L"End IF block (Altirra)",                    false, 55, nullptr },
		// Graphics & sound — common in games / demos.
		{ "GRAPHICS",   L"Set graphics mode",                         true,  72, "GR."  },
		{ "COLOR",      L"Plot colour for next PLOT/DRAWTO",          true,  68, "C."   },
		{ "PLOT",       L"Plot point",                                true,  65, "PL."  },
		{ "DRAWTO",     L"Draw line to point",                        true,  64, "DR."  },
		{ "POSITION",   L"Move text / plot cursor",                   true,  60, "POS." },
		{ "LOCATE",     L"Read pixel at position",                    true,  50, "LOC." },
		{ "SETCOLOR",   L"Set hardware palette register",             true,  68, "SE."  },
		{ "SOUND",      L"Set tone on POKEY voice",                   true,  68, "SO."  },
		// Program control — RUN/LIST/NEW are common but tend to be
		// typed at session start, not deep into a coding flow.
		{ "RUN",        L"Start program execution",                   false, 64, "RU."  },
		{ "LIST",       L"List program lines",                        true,  62, "L."   },
		{ "CONT",       L"Continue after STOP/break",                 false, 40, "CON." },
		{ "STOP",       L"Stop program (for debugging)",              false, 35, "STO." },
		{ "TRAP",       L"Set error-trap line",                       true,  45, "TR."  },
		{ "ON",         L"Computed GOTO / GOSUB",                     true,  55, nullptr },
		// File management — rare in immediate use.
		{ "LOAD",       L"Load tokenised program",                    true,  45, "LO."  },
		{ "SAVE",       L"Save tokenised program",                    true,  45, "S."   },
		{ "CLOAD",      L"Load program from cassette",                false, 30, "CLOA." },
		{ "CSAVE",      L"Save program to cassette",                  false, 30, "CS."  },
		{ "ENTER",      L"Read source text as program",               true,  40, "E."   },
		{ "DIR",        L"Directory listing",                         true,  40, nullptr },
		{ "ERASE",      L"Delete file",                               true,  30, nullptr },
		{ "PROTECT",    L"Set file read-only",                        true,  20, nullptr },
		{ "RENAME",     L"Rename file",                               true,  25, nullptr },
		{ "UNPROTECT",  L"Clear file read-only",                      true,  20, nullptr },
		// Memory / data — moderate use.
		{ "RESTORE",    L"Reset DATA pointer",                        true,  50, "RES." },
		{ "CLR",        L"Clear variables",                           false, 35, nullptr },
		{ "DPOKE",      L"Store 16-bit word",                         true,  40, nullptr },
		{ "MOVE",       L"Block memory copy",                         true,  35, nullptr },
		{ "LOMEM",      L"Adjust low memory pointer",                 true,  20, nullptr },
		// Trig modes — rare in immediate use.
		{ "DEG",        L"Trig in degrees",                           false, 25, nullptr },
		{ "RAD",        L"Trig in radians",                           false, 25, nullptr },
		// I/O extensions — uncommon.
		{ "LPRINT",     L"Print to printer (P:)",                     true,  35, "LP."  },
		{ "STATUS",     L"IOCB status query",                         true,  40, "ST."  },
		{ "XIO",        L"Generic IOCB command",                      true,  40, "X."   },
		{ "NOTE",       L"Record file position",                      true,  35, "NO."  },
		{ "POINT",      L"Seek file position",                        true,  35, "PO."  },
		{ "BGET",       L"Block read from IOCB",                      true,  30, nullptr },
		{ "BPUT",       L"Block write to IOCB",                       true,  30, nullptr },
		// Stack ops — niche.
		{ "POP",        L"Drop runtime-stack entry",                  false, 25, nullptr },
		// Synonyms / less common spellings.
		{ "COM",        L"DIM alias",                                 true,  20, nullptr },
		// Player / missile — niche but legitimate.
		{ "PMGRAPHICS", L"Enable P/M graphics",                       true,  30, nullptr },
		{ "PMCOLOR",    L"Set P/M colour",                            true,  28, nullptr },
		{ "PMMOVE",     L"Move player / missile",                     true,  28, nullptr },
		{ "PMCLR",      L"Clear player / missile area",               true,  25, nullptr },
		{ "MISSILE",    L"Set missile pixels",                        true,  25, nullptr },
		// Misc — exit BASIC.
		{ "BYE",        L"Exit BASIC to memo pad / self-test",        false, 25, "B."   },
		{ "DOS",        L"Exit BASIC to DOS",                         false, 35, "DO."  },
		{ "NEW",        L"Erase program",                             false, 30, nullptr },
	};

	// Expression-level "clause" keywords that introduce sub-clauses inside
	// statements.  They appear in the same regex contexts as functions so
	// we emit them alongside HandleFunctionKeyword's output.
	constexpr AtariBasicKeyword kAtariBasicClauses[] = {
		{ "THEN",       L"IF body",                                   true,  92, nullptr },
		{ "TO",         L"FOR upper bound",                           true,  90, nullptr },
		{ "STEP",       L"FOR step size",                             true,  70, nullptr },
		{ "AND",        L"Logical AND",                               true,  60, nullptr },
		{ "OR",         L"Logical OR",                                true,  55, nullptr },
		{ "NOT",        L"Logical NOT",                               true,  45, nullptr },
		{ "XOR",        L"Bitwise XOR (Altirra)",                     true,  20, nullptr },
	};

	constexpr AtariBasicKeyword kAtariBasicFunctions[] = {
		// Common math / random.
		{ "INT",        L"Integer floor",                             false, 85, nullptr },
		{ "ABS",        L"Absolute value",                            false, 75, nullptr },
		{ "RND",        L"Pseudo-random 0..1",                        false, 85, nullptr },
		{ "SGN",        L"Sign (-1, 0, +1)",                          false, 50, nullptr },
		{ "SQR",        L"Square root",                               false, 60, nullptr },
		{ "EXP",        L"Exponential e^x",                           false, 30, nullptr },
		{ "LOG",        L"Natural log",                               false, 30, nullptr },
		{ "CLOG",       L"Common (base-10) log",                      false, 25, nullptr },
		// Trig.
		{ "SIN",        L"Sine (DEG/RAD)",                            false, 55, nullptr },
		{ "COS",        L"Cosine (DEG/RAD)",                          false, 55, nullptr },
		{ "ATN",        L"Arctangent",                                false, 35, nullptr },
		// Strings — heavily used.
		{ "CHR$",       L"Character from ATASCII code",               false, 88, nullptr },
		{ "ASC",        L"ATASCII code of first char",                false, 78, nullptr },
		{ "STR$",       L"Number to string",                          false, 70, nullptr },
		{ "VAL",        L"String to number",                          false, 65, nullptr },
		{ "LEN",        L"String length",                             false, 75, nullptr },
		{ "HEX$",       L"Number to hex string (Altirra)",            false, 25, nullptr },
		// Memory — very common in poke-heavy programs.
		{ "PEEK",       L"Read byte from address",                    false, 85, nullptr },
		{ "DPEEK",      L"Read 16-bit word",                          false, 45, nullptr },
		{ "ADR",        L"Address of string array",                   false, 65, nullptr },
		{ "FRE",        L"Free memory bytes",                         false, 55, nullptr },
		{ "USR",        L"Call machine code",                         false, 65, nullptr },
		// Controllers — common in games.
		{ "STICK",      L"Joystick direction 0-15",                   false, 70, nullptr },
		{ "STRIG",      L"Trigger 0/1",                               false, 65, nullptr },
		{ "PADDLE",     L"Paddle 0-228",                              false, 50, nullptr },
		{ "PTRIG",      L"Paddle trigger 0/1",                        false, 40, nullptr },
		{ "HSTICK",     L"Horizontal axis -1/0/+1 (Altirra)",         false, 30, nullptr },
		{ "VSTICK",     L"Vertical axis -1/0/+1 (Altirra)",           false, 30, nullptr },
		// P/M graphics — niche.
		{ "PMADR",      L"P/M graphics base address",                 false, 25, nullptr },
		{ "BUMP",       L"P/M collision detect",                      false, 20, nullptr },
		// Error handling.
		{ "ERR",        L"Last TRAP error code",                      false, 35, nullptr },
	};

	// Statements whose argument lists are themselves expressions.  These
	// names enable the function/variable-completion regex to fire after
	// "PRINT |", "POKE |", "LET A=" ... — every BASIC token that is
	// immediately followed by an expression.  Kept compact (alternation
	// inside one regex) for engine startup speed.
	constexpr const char *kExpressionContextKeywords =
		"PRINT|INPUT|LET|POKE|DPOKE|IF|FOR|ON|TRAP|SOUND|PLOT|DRAWTO|"
		"POSITION|GRAPHICS|SETCOLOR|COLOR|DIM|LOCATE|GOTO|GOSUB|STEP|"
		"TO|XIO|READ|MOVE|RESTORE|THEN|ELSE|NOT|AND|OR|XOR|RETURN|"
		"LPRINT|GET|PUT|STATUS|NOTE|POINT|BGET|BPUT|LIST|LOAD|SAVE|"
		"ENTER|OPEN|CLOSE|RENAME|ERASE|PROTECT|UNPROTECT|DIR|COM|"
		"NEXT|PMGRAPHICS|PMCOLOR|PMMOVE|PMCLR|MISSILE|LOMEM";

	// Canonical syntax line for each BASIC keyword.  Sourced from the
	// Altirra BASIC Reference Manual (2018-08-12 edition) `Syntax:`
	// lines, normalised:
	//   * markdown italic markers `*x*` stripped → `x`,
	//   * escaped brackets `\[ \] \= \# \, \.` → plain characters,
	//   * whitespace collapsed,
	//   * standalone clauses (THEN, TO, STEP, AND, OR, NOT, XOR) point
	//     to their containing statement's syntax so the user gets
	//     useful context (e.g. THEN → "IF aexp THEN ...").
	//
	// `aexp` = numeric expression, `sexp` = string expression,
	// `avar` = numeric variable, `mvar` = matrix/array variable,
	// `iocb` = I/O control block number (0-7), `lineno` = line number.
	// These are the Atari BASIC manual's conventional placeholders;
	// keeping them verbatim makes the popup hint directly cross-
	// referenceable with the printed reference manual.
	struct AtariBasicSyntax {
		const char    *mName;
		const wchar_t *mSyntax;
	};

	constexpr AtariBasicSyntax kAtariBasicSyntax[] = {
		// --- statements ---------------------------------------------------
		{ "PRINT",      L"PRINT [#iocb,] expr [;, expr...]" },
		{ "?",          L"? [#iocb,] expr [;, expr...]" },
		{ "INPUT",      L"INPUT [#iocb,] avar [, avar...]" },
		{ "IF",         L"IF aexp THEN {lineno | statements}" },
		{ "THEN",       L"IF aexp THEN {lineno | statements}" },
		{ "ELSE",       L"ELSE (block IF, Altirra extension)" },
		{ "ENDIF",      L"ENDIF (closes block IF, Altirra extension)" },
		{ "FOR",        L"FOR avar = aexp TO aexp [STEP aexp]" },
		{ "NEXT",       L"NEXT avar" },
		{ "TO",         L"FOR avar = aexp TO aexp [STEP aexp]" },
		{ "STEP",       L"FOR avar = aexp TO aexp [STEP aexp]" },
		{ "GOTO",       L"GOTO aexp" },
		{ "GOSUB",      L"GOSUB aexp" },
		{ "RETURN",     L"RETURN" },
		{ "LET",        L"[LET] avar = aexp" },
		{ "REM",        L"REM ..." },
		{ "END",        L"END" },
		{ "DIM",        L"DIM mvar(N) [, mvar(N)...]" },
		{ "COM",        L"COM mvar(N) [, mvar(N)...]   (alias of DIM)" },
		{ "DATA",       L"DATA value [, value...]" },
		{ "READ",       L"READ avar [, avar...]" },
		{ "RESTORE",    L"RESTORE [aexp]" },
		{ "POKE",       L"POKE aexp, aexp" },
		{ "DPOKE",      L"DPOKE aexp, aexp" },
		{ "GET",        L"GET #iocb, avar" },
		{ "PUT",        L"PUT #iocb, aexp" },
		{ "OPEN",       L"OPEN #iocb, aexp, aexp, sexp" },
		{ "CLOSE",      L"CLOSE #iocb" },
		{ "GRAPHICS",   L"GRAPHICS aexp" },
		{ "COLOR",      L"COLOR aexp" },
		{ "PLOT",       L"PLOT aexp, aexp" },
		{ "DRAWTO",     L"DRAWTO aexp, aexp" },
		{ "POSITION",   L"POSITION aexp, aexp" },
		{ "LOCATE",     L"LOCATE aexp, aexp, avar" },
		{ "SETCOLOR",   L"SETCOLOR aexp, aexp, aexp" },
		{ "SOUND",      L"SOUND aexp, aexp, aexp, aexp" },
		{ "RUN",        L"RUN [sexp]" },
		{ "LIST",       L"LIST [sexp]" },
		{ "CONT",       L"CONT" },
		{ "STOP",       L"STOP" },
		{ "TRAP",       L"TRAP aexp" },
		{ "ON",         L"ON aexp {GOTO|GOSUB} lineno [, lineno...]" },
		{ "LOAD",       L"LOAD sexp" },
		{ "SAVE",       L"SAVE sexp" },
		{ "CLOAD",      L"CLOAD" },
		{ "CSAVE",      L"CSAVE" },
		{ "ENTER",      L"ENTER sexp" },
		{ "DIR",        L"DIR [sexp]" },
		{ "ERASE",      L"ERASE sexp" },
		{ "PROTECT",    L"PROTECT sexp" },
		{ "RENAME",     L"RENAME sexp" },
		{ "UNPROTECT",  L"UNPROTECT sexp" },
		{ "LPRINT",     L"LPRINT [;, ] expr [;, expr...]" },
		{ "STATUS",     L"STATUS #iocb, avar" },
		{ "XIO",        L"XIO aexp, #iocb, aexp, aexp, sexp" },
		{ "NOTE",       L"NOTE #iocb, avar, avar" },
		{ "POINT",      L"POINT #iocb, avar, avar" },
		{ "BGET",       L"BGET #iocb, aexp, aexp" },
		{ "BPUT",       L"BPUT #iocb, aexp, aexp" },
		{ "MOVE",       L"MOVE aexp, aexp, aexp" },
		{ "LOMEM",      L"LOMEM aexp" },
		{ "DEG",        L"DEG" },
		{ "RAD",        L"RAD" },
		{ "POP",        L"POP" },
		{ "CLR",        L"CLR" },
		{ "PMGRAPHICS", L"PMGRAPHICS aexp" },
		{ "PMCOLOR",    L"PMCOLOR aexp, aexp, aexp" },
		{ "PMMOVE",     L"PMMOVE aexp [, aexp] [;aexp]" },
		{ "PMCLR",      L"PMCLR aexp" },
		{ "MISSILE",    L"MISSILE aexp, aexp, aexp" },
		{ "BYE",        L"BYE" },
		{ "DOS",        L"DOS" },
		{ "NEW",        L"NEW" },
		// --- functions ----------------------------------------------------
		{ "INT",        L"aexp = INT(aexp)" },
		{ "ABS",        L"aexp = ABS(aexp)" },
		{ "RND",        L"aexp = RND(aexp)" },
		{ "SGN",        L"aexp = SGN(aexp)" },
		{ "SQR",        L"aexp = SQR(aexp)" },
		{ "EXP",        L"aexp = EXP(aexp)" },
		{ "LOG",        L"aexp = LOG(aexp)" },
		{ "CLOG",       L"aexp = CLOG(aexp)" },
		{ "SIN",        L"aexp = SIN(aexp)" },
		{ "COS",        L"aexp = COS(aexp)" },
		{ "ATN",        L"aexp = ATN(aexp)" },
		{ "CHR$",       L"sexp = CHR$(aexp)" },
		{ "ASC",        L"aexp = ASC(sexp)" },
		{ "STR$",       L"sexp = STR$(aexp)" },
		{ "VAL",        L"aexp = VAL(sexp)" },
		{ "LEN",        L"aexp = LEN(sexp)" },
		{ "HEX$",       L"sexp = HEX$(aexp)   (Altirra extension)" },
		{ "PEEK",       L"aexp = PEEK(aexp)" },
		{ "DPEEK",      L"aexp = DPEEK(aexp)" },
		{ "ADR",        L"aexp = ADR(sexp)" },
		{ "FRE",        L"aexp = FRE(aexp)" },
		{ "USR",        L"aexp = USR(aexp [, aexp]...)" },
		{ "STICK",      L"aexp = STICK(aexp)" },
		{ "STRIG",      L"aexp = STRIG(aexp)" },
		{ "PADDLE",     L"aexp = PADDLE(aexp)" },
		{ "PTRIG",      L"aexp = PTRIG(aexp)" },
		{ "HSTICK",     L"aexp = HSTICK(aexp)   (Altirra extension)" },
		{ "VSTICK",     L"aexp = VSTICK(aexp)   (Altirra extension)" },
		{ "PMADR",      L"aexp = PMADR(aexp)" },
		{ "BUMP",       L"aexp = BUMP(aexp, aexp)" },
		{ "ERR",        L"aexp = ERR(0)" },
	};

	const wchar_t *LookupBasicSyntax(const char *name) {
		// Linear scan — table is ~100 entries; the popup only displays
		// the syntax for the highlighted/top result, so this fires at
		// most once per Render call.  Keeping the table flat (no
		// pointer-into-keyword-struct) lets us add manual-only clauses
		// like THEN/TO/STEP without touching the keyword tables.
		for (const AtariBasicSyntax& e : kAtariBasicSyntax) {
			if (strcmp(e.mName, name) == 0)
				return e.mSyntax;
		}
		return nullptr;
	}

	// Helper: case-insensitive prefix match.  BASIC editor traditionally
	// uses uppercase; the regex below already restricts the match to
	// uppercase letters, so this is just a paranoia check.  Callers
	// guarantee prefix.size() <= strlen(name) before calling.
	bool PrefixMatchesAscii(const char *name, const VDStringSpanA& prefix) {
		const size_t pn = prefix.size();
		for (size_t i = 0; i < pn; ++i) {
			char c = prefix[i];
			if (c >= 'a' && c <= 'z') c = (char)(c - 32);
			if (c != name[i])
				return false;
		}
		return true;
	}

	void EmitBasicCandidates(
		const AtariBasicKeyword *table, size_t count,
		const VDStringSpanA& prefix,
		const IATAutoSuggestEngine::ResultSink& sink)
	{
		const size_t prefLen = prefix.size();

		// Two-pass: collect matches with their popularity weight, sort
		// descending so the most-used keyword for the typed prefix lands
		// at the top of the popup.  Ties stay in declaration order
		// (stable sort) which keeps the rough category grouping
		// (statements together, functions together) visually intact
		// among equally-weighted entries.
		struct Match {
			const AtariBasicKeyword *mEntry;
			uint16                   mOriginalIndex;
		};
		vdvector<Match> matches;
		matches.reserve(count);

		for (size_t i = 0; i < count; ++i) {
			const AtariBasicKeyword& kw = table[i];
			const size_t nameLen = strlen(kw.mName);
			if (prefLen > nameLen)
				continue;
			if (!PrefixMatchesAscii(kw.mName, prefix))
				continue;
			matches.push_back(Match { &kw, (uint16)i });
		}

		std::stable_sort(matches.begin(), matches.end(),
			[](const Match& a, const Match& b) {
				return a.mEntry->mPopularity > b.mEntry->mPopularity;
			});

		for (const Match& m : matches) {
			const AtariBasicKeyword& kw = *m.mEntry;
			const size_t nameLen = strlen(kw.mName);

			IATAutoSuggestEngine::ResultInfo ri;
			// Insertion text completes the prefix; trailing space goes on
			// argument-bearing keywords so the user can keep typing.
			ri.mInsertionText.assign(kw.mName + prefLen, nameLen - prefLen);
			if (kw.mbHasArguments)
				ri.mInsertionText.push_back(' ');

			// Item shows the full keyword.  Abbreviation, if any,
			// gets prepended to the description so the user learns
			// the canonical Atari BASIC shorthand alongside.
			VDStringSpanA nameSpan(kw.mName, kw.mName + nameLen);
			ri.mItemText = VDTextU8ToW(nameSpan);

			VDStringW desc;
			if (kw.mAbbrev && *kw.mAbbrev) {
				desc.append_sprintf(L"[%hs] ", kw.mAbbrev);
			}
			if (kw.mDescription)
				desc += kw.mDescription;
			ri.mDescriptionText = std::move(desc);

			// Optional canonical-syntax hint from the reference manual,
			// surfaced under the highlighted popup entry.  Lookup is by
			// keyword name so the syntax table can also carry entries
			// for sub-clauses (THEN/TO/STEP/...) that aren't first-class
			// keywords in the popup but appear via the clauses table.
			if (const wchar_t *syn = LookupBasicSyntax(kw.mName))
				ri.mSyntaxText = syn;

			sink(ri);
		}
	}
}

// ---------------------------------------------------------------------------
// BASIC line state — a small forward-scan analyser that walks the editor
// buffer once and reports the "where am I in the BASIC line" answer at
// the cursor.  Used by the keyword/function/variable handlers to:
//
//   * suppress suggestions inside string literals or REM comments,
//   * route variable suggestions to the front (and append `=`) when the
//     cursor is in a FOR or LET variable slot,
//   * later: drive whatever else needs "what statement is in progress".
//
// The scanner is intentionally *not* a full BASIC parser — it makes
// best-effort guesses on a single line of editor text.  Wrong guesses
// produce slightly off ordering, never invalid suggestions, because the
// regex pre-filter still owns the trigger decision.
//
// Pure data; no allocations beyond what the caller provides.
namespace {
	enum class BasicSlot : uint8 {
		StatementStart,    // line start, after ':', after THEN/ELSE
		InsideStatement,   // generic — after a keyword, no special context
		AfterForVar,       // FOR <here> — user is typing the loop variable
		AfterLetVar,       // LET <here> — user is typing the lvalue
		AfterAssign,       // = <here> — RHS expression context
		AfterPrint,        // PRINT <here>, PRINT A,<here>, PRINT A;<here>
		AfterIf,           // IF <here> — boolean expression
		AfterOnComputed,   // ON <here> — selector expression
		AfterGotoTarget,   // GOTO <here>, GOSUB <here>, RESTORE <here>, TRAP <here>, THEN <here>
		AfterLineRef,      // GOTO N THEN <line> — bare line number expected
		AfterFunctionArg,  // fn(<here> or fn(...,<here>)
		AfterIoHash,       // # <here> — IOCB number
		InString,          // inside "..."
		InComment,         // inside REM
	};

	struct BasicLineState {
		BasicSlot         mSlot         = BasicSlot::StatementStart;
		const char       *mLastKeyword  = nullptr;  // pointer into kAtariBasicStatements[].mName (no copy)
		int               mParenDepth   = 0;
		bool              mInIfCondition = false;   // between IF and THEN/colon
	};

	bool MatchesKeyword(const char *kwName, const char *buf, size_t bufLen) {
		const size_t kwLen = strlen(kwName);
		if (bufLen != kwLen) return false;
		for (size_t i = 0; i < kwLen; ++i) {
			char a = buf[i];
			if (a >= 'a' && a <= 'z') a = (char)(a - 32);
			if (a != kwName[i]) return false;
		}
		return true;
	}

	// Walk `buf` left-to-right tracking parse state.  Returns the state
	// AT THE END (cursor position) — the partial identifier the user is
	// currently typing has NOT been consumed (HandleXxxKeyword owns that
	// via the regex group).
	//
	// Complexity is O(n) in buffer length with no allocations; the
	// "is this word a known statement?" inner loop touches the static
	// keyword table once per word, which is fine for the ~80-char
	// buffers the BASIC editor produces.
	BasicLineState ScanBasicLineState(const VDStringSpanA& buf) {
		BasicLineState st;
		bool atStmtStart = true;
		size_t i = 0;
		const size_t n = buf.size();

		auto enterStatementStart = [&](BasicLineState& s) {
			atStmtStart = true;
			s.mLastKeyword = nullptr;
			s.mSlot = BasicSlot::StatementStart;
			s.mInIfCondition = false;
		};

		while (i < n) {
			const char c = buf[i];

			// String literal: everything up to matching `"` or EOL is
			// inert as far as keyword scanning goes.  We DON'T mark
			// mSlot=InString unless the buffer ends inside the string,
			// which is checked after the loop.
			if (c == '"') {
				++i;
				while (i < n && buf[i] != '"')
					++i;
				if (i < n) ++i;  // consume closing quote
				atStmtStart = false;
				continue;
			}

			// REM comment: rest of statement (until ':' or end) is inert.
			// REM was detected as a keyword below; here we just consume.

			if (c == ':') {
				enterStatementStart(st);
				++i;
				continue;
			}

			if (c == ' ' || c == '\t') { ++i; continue; }

			if (c == '(') { ++st.mParenDepth; atStmtStart = false; ++i; continue; }
			if (c == ')') { if (st.mParenDepth > 0) --st.mParenDepth; atStmtStart = false; ++i; continue; }

			if (c == '=') {
				st.mSlot = BasicSlot::AfterAssign;
				atStmtStart = false;
				++i;
				continue;
			}

			if (c == ',' || c == ';') {
				// In PRINT context, comma/semicolon separate items;
				// in DIM/POKE/etc. they separate args.  Either way
				// the slot becomes "generic expression" until we
				// see something more specific.
				if (st.mLastKeyword && MatchesKeyword("PRINT", st.mLastKeyword, strlen(st.mLastKeyword)))
					st.mSlot = BasicSlot::AfterPrint;
				else
					st.mSlot = BasicSlot::AfterAssign;
				atStmtStart = false;
				++i;
				continue;
			}

			if (c == '#') {
				st.mSlot = BasicSlot::AfterIoHash;
				atStmtStart = false;
				++i;
				continue;
			}

			// Number — skip.
			if (c >= '0' && c <= '9') {
				while (i < n && ((buf[i] >= '0' && buf[i] <= '9') || buf[i] == '.'))
					++i;
				// At statement start, a leading number is the BASIC line
				// number, so DON'T flip out of statement-start mode.
				if (!atStmtStart)
					st.mSlot = BasicSlot::InsideStatement;
				continue;
			}

			// Identifier (potential keyword).
			if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z')) {
				const size_t start = i;
				while (i < n) {
					const char x = buf[i];
					if ((x >= 'A' && x <= 'Z') || (x >= 'a' && x <= 'z') || (x >= '0' && x <= '9'))
						++i;
					else
						break;
				}
				const size_t wordLen = i - start;
				const char *wp = buf.data() + start;

				// `$` and `(` suffixes belong to the identifier visually
				// but don't affect keyword classification for the
				// statement keyword (functions handle their own suffix).
				const bool stringSuffix = (i < n && buf[i] == '$');
				if (stringSuffix) ++i;

				// REM at statement start → rest of statement is comment.
				if (atStmtStart && MatchesKeyword("REM", wp, wordLen)) {
					st.mSlot = BasicSlot::InComment;
					// Consume to next `:` or end.
					while (i < n && buf[i] != ':')
						++i;
					continue;
				}

				// Check statement table for a known keyword.
				const AtariBasicKeyword *stmt = nullptr;
				for (const auto& kw : kAtariBasicStatements) {
					if (MatchesKeyword(kw.mName, wp, wordLen)) {
						stmt = &kw;
						break;
					}
				}

				if (stmt) {
					st.mLastKeyword = stmt->mName;

					if (MatchesKeyword("THEN", stmt->mName, strlen(stmt->mName)) ||
					    MatchesKeyword("ELSE", stmt->mName, strlen(stmt->mName)))
					{
						// THEN/ELSE: either followed by a bare line
						// number (implicit GOTO) or a new statement.
						// We default to StatementStart so the caller
						// gets both kinds of suggestions.
						enterStatementStart(st);
						st.mInIfCondition = false;
					} else if (MatchesKeyword("FOR", stmt->mName, 3)) {
						st.mSlot = BasicSlot::AfterForVar;
						atStmtStart = false;
					} else if (MatchesKeyword("LET", stmt->mName, 3)) {
						st.mSlot = BasicSlot::AfterLetVar;
						atStmtStart = false;
					} else if (MatchesKeyword("IF", stmt->mName, 2)) {
						st.mSlot = BasicSlot::AfterIf;
						st.mInIfCondition = true;
						atStmtStart = false;
					} else if (MatchesKeyword("PRINT", stmt->mName, 5) ||
					           MatchesKeyword("?", stmt->mName, 1) ||
					           MatchesKeyword("LPRINT", stmt->mName, 6)) {
						st.mSlot = BasicSlot::AfterPrint;
						atStmtStart = false;
					} else if (MatchesKeyword("GOTO", stmt->mName, 4) ||
					           MatchesKeyword("GOSUB", stmt->mName, 5) ||
					           MatchesKeyword("RESTORE", stmt->mName, 7) ||
					           MatchesKeyword("TRAP", stmt->mName, 4)) {
						st.mSlot = BasicSlot::AfterGotoTarget;
						atStmtStart = false;
					} else if (MatchesKeyword("ON", stmt->mName, 2)) {
						st.mSlot = BasicSlot::AfterOnComputed;
						atStmtStart = false;
					} else {
						st.mSlot = BasicSlot::InsideStatement;
						atStmtStart = false;
					}
					continue;
				}

				// Non-keyword identifier (variable name).
				atStmtStart = false;
				continue;
			}

			// Anything else (operator-ish) — generic expression context.
			st.mSlot = BasicSlot::AfterAssign;
			atStmtStart = false;
			++i;
		}

		// Post-scan: if the buffer ended INSIDE a string literal (odd
		// number of `"` seen), the cursor is in-string.
		bool insideString = false;
		for (size_t k = 0; k < n; ++k) {
			if (buf[k] == '"') insideString = !insideString;
		}
		if (insideString)
			st.mSlot = BasicSlot::InString;

		return st;
	}
}

class ATAutoSuggestEngine final : public vdrefcounted<IATAutoSuggestEngine> {
public:
	ATAutoSuggestEngine();

	void AddStaticPattern(
		VDStringSpanA regex,
		VDStringSpanA insertionText,
		VDStringSpanW displayText
	) override;

	void AddDynamicPattern(VDStringSpanA regex, vdfunction<void(const ResultSink& resultSink, const MatchInfo&)> handler) override;

	void SetOptions(const ATAutoSuggestOptions& options) override;

	void Update(VDStringSpanA text) override;

	size_t GetNumResults() const override;
	const ResultInfo& GetResult(size_t i) const override;

private:
	void HandlePeekPoke(bool poke, const ResultSink& resultSink, const MatchInfo& mi);
	void HandleCIOPath(const ResultSink& resultSink, const MatchInfo& mi);
	void HandleStatementKeyword(const ResultSink& resultSink, const MatchInfo& mi);
	void HandleExpressionKeyword(const ResultSink& resultSink, const MatchInfo& mi);
	void EmitVariableMatches(const VDStringSpanA& prefix, const ResultSink& resultSink);

	// Same shape as AddDynamicPattern but constructs the std::regex
	// with the ECMAScript `icase` flag so the BASIC patterns match
	// both uppercase and lowercase user input.
	void AddIcaseDynamicPattern(const char *regex, vdfunction<void(const ResultSink&, const MatchInfo&)> handler) {
		mRules.emplace_back(
			Rule {
				std::regex(regex, std::regex_constants::ECMAScript | std::regex_constants::icase),
				std::move(handler)
			}
		);
	}

	struct Rule {
		std::regex mRegex;
		vdfunction<void(const ResultSink& resultSink, const MatchInfo&)> mHandler;
	};

	ATAutoSuggestOptions mOptions;
	vdvector<Rule> mRules;
	vdvector<ResultInfo> mResults;
	BasicLineState mLineState;
};

ATAutoSuggestEngine::ATAutoSuggestEngine() {
	AddDynamicPattern(VDStringSpanA("POKE ([1-9][0-9]*)$"), std::bind_front(&ATAutoSuggestEngine::HandlePeekPoke, this, true));
	AddDynamicPattern(VDStringSpanA("PEEK\\(([1-9][0-9]*)$"), std::bind_front(&ATAutoSuggestEngine::HandlePeekPoke, this, false));
	AddDynamicPattern(VDStringSpanA("[^A-Z][A-Z](?:|[1-9]):([A-Z]*)$"), std::bind_front(&ATAutoSuggestEngine::HandleCIOPath, this));

	// --- BASIC keyword / variable completion ------------------------------
	//
	// Two regex contexts cover the practical cases:
	//   (a) "Statement start" — line start, after ':' (statement separator),
	//       after THEN or ELSE.  Only statements are offered.  The leading
	//       optional digit run absorbs the BASIC line number (e.g. "10 PR").
	//   (b) "Expression context" — after an operator (= ( , + - * / < > ^),
	//       or immediately after a known statement / clause keyword that
	//       takes an expression argument (PRINT |, POKE |, IF |, …).
	//       Functions, clause keywords, and BASIC variables are offered.
	//
	// The handlers themselves consult mOptions and short-circuit if the
	// corresponding category is disabled by the user.

	// Build case-insensitive regexes for the BASIC patterns so users
	// that type lowercase in the BASIC editor (default state when CAPS
	// LOCK is off) get the same triggering as uppercase typists.
	//
	// AddDynamicPattern's overload uses the default ECMAScript flavor
	// without flags; AddIcaseDynamicPattern (below) is the
	// case-insensitive sibling.  The captured group is still passed
	// verbatim to the handler, which uppercases it before matching
	// against the canonical keyword tables.
	AddIcaseDynamicPattern(
		"(?:^|:|\\bTHEN |\\bELSE )(?: *[0-9]+)? *([A-Za-z]+)$",
		std::bind_front(&ATAutoSuggestEngine::HandleStatementKeyword, this)
	);

	// Operator/punctuation set that introduces an expression in Atari
	// BASIC: assignment, comma, opening paren, comparison, arithmetic,
	// power, the `?` shorthand for PRINT, the `;` PRINT separator, and
	// `#` for IOCB channels (e.g. PRINT #1,EXPR).
	const VDStringA exprPattern(
		VDStringA("(?:[=,(<>+\\-*/^?;#] *|\\b(?:") + kExpressionContextKeywords + ") +)([A-Za-z]+\\$?)$"
	);
	AddIcaseDynamicPattern(
		exprPattern.c_str(),
		std::bind_front(&ATAutoSuggestEngine::HandleExpressionKeyword, this)
	);
}

void ATAutoSuggestEngine::SetOptions(const ATAutoSuggestOptions& options) {
	mOptions = options;
}

void ATAutoSuggestEngine::AddStaticPattern(
	VDStringSpanA regex,
	VDStringSpanA insertionText,
	VDStringSpanW displayText
) {
	AddDynamicPattern(
		regex,
		[ri = ResultInfo { insertionText, displayText }](const ResultSink& resultSink, const MatchInfo& mi) {
			resultSink(ri);
		}
	);
}

void ATAutoSuggestEngine::AddDynamicPattern(VDStringSpanA regex, vdfunction<void(const ResultSink& resultSink, const MatchInfo&)> handler) {
	mRules.emplace_back(
		Rule {
			std::regex(regex.data(), regex.size()),
			std::move(handler)
		}
	);
}

void ATAutoSuggestEngine::Update(VDStringSpanA text) {
	mResults.clear();

	// Analyse the BASIC line state once per Update so all handlers see
	// a consistent view.  Cheap (linear in buffer length) and lets us
	// suppress suggestions inside strings/REM and refine ordering when
	// the cursor sits in a FOR/LET variable slot.
	mLineState = ScanBasicLineState(text);

	// Hard suppress in string/comment context.  The regex pre-filter
	// would also miss most of these but we keep the popup clean
	// regardless of which trigger matches.
	if (mLineState.mSlot == BasicSlot::InString ||
	    mLineState.mSlot == BasicSlot::InComment)
	{
		return;
	}

	const vdfunction<void(const ResultInfo&)> resultSink = [this](const ResultInfo& ri) {
		mResults.push_back(ri);
	};

	for(const Rule& rule : mRules) {
		std::cmatch m;

		if (std::regex_search(text.begin(), text.end(), m, rule.mRegex)) {
			MatchInfo mi;
			mi.mMatchedText = VDStringSpanA(m[0].first, m[0].second);

			if (m.size() > 1)
				mi.mMatchedGroup = VDStringSpanA(m[1].first, m[1].second);

			rule.mHandler(resultSink, mi);
		}
	}
}

size_t ATAutoSuggestEngine::GetNumResults() const {
	return mResults.size();
}

const ATAutoSuggestEngine::ResultInfo& ATAutoSuggestEngine::GetResult(size_t i) const {
	return mResults[i];
}

void ATAutoSuggestEngine::HandlePeekPoke(bool poke, const ResultSink& resultSink, const MatchInfo& mi) {
	unsigned v = 0;
	(void)std::from_chars(mi.mMatchedGroup.begin(), mi.mMatchedGroup.end(), v, 10);

	// OS variables are between 512 ($0200) and 1024 ($0400), so we do autosuggest for
	// 51-102 with an extra digit.

	const std::pair<vdspan<const ATDefaultSymbolInfo>, uint32> symSources[] {
		{ ATGetSortedSymbolsForOSVariables(), 0 },
		{ ATGetSortedSymbolsForGTIAVariables(), 0xD000 },
		{ ATGetSortedSymbolsForPOKEYVariables(), 0xD200 },
		{ ATGetSortedSymbolsForPIAVariables(), 0xD300 },
		{ ATGetSortedSymbolsForANTICVariables(), 0 },
	};

	for(uint32 factor : { 10, 100 }) {
		const uint32 vbegin = v * factor;
		const uint32 vend = vbegin + factor;

		for(const auto& [syms, base] : symSources) {
			auto it = std::ranges::lower_bound(
				syms,
				vbegin,
				[](uint32 bin1, uint32 bin2) {
					return bin1 < bin2;
				},
				[base](const ATDefaultSymbolInfo& sym) {
					return sym.mAddress + base;
				}
			);

			auto itEnd = syms.end();

			for(; it != itEnd; ++it) {
				const auto& cand = *it;
				const uint32 address = cand.mAddress + base;

				if (address >= vend)
					break;

				switch(cand.mType) {
					case ATDefaultSymbolType::Default:
						break;

					case ATDefaultSymbolType::ReadOnly:
						if (poke)
							continue;
						break;

					case ATDefaultSymbolType::WriteOnly:
						if (!poke)
							continue;
						break;
				}

				ResultInfo ri;
				ri.mInsertionText.sprintf("%0*u", factor >= 100 ? 2 : 1, address % factor);
				ri.mItemText.sprintf(L"%u", address);
				ri.mDescriptionText.sprintf(L"%u ($%04X) %hs", address, address, cand.mName);

				resultSink(ri);
			}
		}
	}
}

void ATAutoSuggestEngine::HandleCIOPath(const ResultSink& resultSink, const MatchInfo& mi) {
	if (mi.mMatchedText.size() < 3) {
		VDFAIL("Regex should not have matched such a short string");
		return;
	}

	const char cioDevice = mi.mMatchedText[1];
	const char cioUnitChar = mi.mMatchedText[2];
	const uint8 unit = (cioUnitChar >= '1' && cioUnitChar <= '9') ? (uint8)(cioUnitChar - '0') : 1;

	const VDStringA cioRelativePath(mi.mMatchedGroup);

	class AutoSuggestSink final : public IATDeviceAutoSuggestSink {
	public:
		AutoSuggestSink(const ResultSink& rs) : mResultSink(rs) {}

		void AddSuggestion(const char *insertText, const wchar_t *itemText, const wchar_t *descriptionText) override {
			ResultInfo ri;
			ri.mInsertionText = insertText;
			ri.mItemText = itemText;
			ri.mDescriptionText = descriptionText;
			mResultSink(ri);
		}

		const ResultSink& mResultSink;
	} sink(resultSink);

	for(IATDeviceAutoSuggest *devAS : g_sim.GetDeviceManager()->GetInterfaces<IATDeviceAutoSuggest>(false, false, false)) {
		devAS->AutoSuggestCIOPaths(cioDevice, unit, cioRelativePath, sink);
	}
}

void ATAutoSuggestEngine::HandleStatementKeyword(const ResultSink& resultSink, const MatchInfo& mi) {
	if (!mOptions.mbStatementKeywords)
		return;

	EmitBasicCandidates(
		kAtariBasicStatements,
		sizeof(kAtariBasicStatements) / sizeof(kAtariBasicStatements[0]),
		mi.mMatchedGroup,
		resultSink
	);
}

void ATAutoSuggestEngine::HandleExpressionKeyword(const ResultSink& resultSink, const MatchInfo& mi) {
	const BasicSlot slot = mLineState.mSlot;

	// In FOR/LET variable slots, the only sensible suggestion is a
	// variable name — appended with '=' so the next character the user
	// types belongs to the right-hand side ("FOR I" → accept → "FOR I=").
	// We still surface clause/function candidates beneath in case the
	// user wanted (e.g.) "FOR I=ABS(X)..." with the cursor on "AB" —
	// but variables outrank them.
	const bool inVarSlot =
		(slot == BasicSlot::AfterForVar) ||
		(slot == BasicSlot::AfterLetVar);

	auto sinkWithAssignSuffix = [&resultSink](const ResultInfo& ri) {
		// Wrap the original result info, appending '=' to the insertion
		// text so the user lands ready to type the RHS.  The display
		// (item) text stays clean.
		ResultInfo augmented = ri;
		augmented.mInsertionText += '=';
		resultSink(augmented);
	};

	if (mOptions.mbVariableNames) {
		EmitVariableMatches(
			mi.mMatchedGroup,
			inVarSlot ? (ResultSink)sinkWithAssignSuffix : resultSink
		);
	}

	if (mOptions.mbFunctionKeywords) {
		EmitBasicCandidates(
			kAtariBasicFunctions,
			sizeof(kAtariBasicFunctions) / sizeof(kAtariBasicFunctions[0]),
			mi.mMatchedGroup,
			resultSink
		);
		EmitBasicCandidates(
			kAtariBasicClauses,
			sizeof(kAtariBasicClauses) / sizeof(kAtariBasicClauses[0]),
			mi.mMatchedGroup,
			resultSink
		);
	}
}

void ATAutoSuggestEngine::EmitVariableMatches(const VDStringSpanA& prefix, const ResultSink& resultSink) {
	// Only walk the VNT when the BASIC pointer chain is consistent.
	// Otherwise $82/$84 contain garbage from whatever runs in immediate
	// mode (DOS, monitor, OSS Action!) and we'd emit nonsense names.
	if (!ATIsBasicMemoryLayoutValid())
		return;

	// Atari BASIC variable name table layout (per reference manual §
	// "Variable name table"):
	//   VNTP  ($82-83) — first byte of the table.
	//   VNTD  ($84-85) — one past the last byte of the table.
	//   Each name: ATASCII bytes, last byte has bit 7 set.
	//   Trailing '(' marks numeric array; trailing '$' marks string array.
	//   Whole table terminated by a $00 byte at VNTD.
	//
	// We walk forward but emit in REVERSE order so the most-recently-
	// declared variables (typically what the user is actively editing)
	// appear at the top of the popup.  This is the BASIC analogue of
	// "most-recently-used".

	const uint16 vntp = g_sim.DebugReadWord(0x82);
	const uint16 vntd = g_sim.DebugReadWord(0x84);

	if (vntd <= vntp || (uint32)(vntd - vntp) > 4096)
		return;

	struct VarRecord {
		VDStringA mName;     // canonical uppercase
		char      mTypeChar; // 0 numeric scalar, '(' numeric array, '$' string array
	};
	vdvector<VarRecord> vars;
	vars.reserve(32);

	VarRecord cur;
	cur.mTypeChar = 0;

	for(uint32 a = vntp; a < vntd && vars.size() < 128; ++a) {
		uint8 b = g_sim.DebugReadByte((uint16)a);
		if (b == 0)
			break;

		const bool last = (b & 0x80) != 0;
		const char c = (char)(b & 0x7F);

		if (last) {
			// Distinguish the terminator character: '(' = numeric array,
			// '$' = string array, anything else is part of the scalar
			// name's final letter.
			if (c == '(' || c == '$')
				cur.mTypeChar = c;
			else
				cur.mName.push_back(c);
			vars.push_back(cur);
			cur.mName.clear();
			cur.mTypeChar = 0;
		} else {
			cur.mName.push_back(c);
		}
	}

	// Emit reverse-order (most recently declared first).
	const size_t prefLen = prefix.size();
	for (auto it = vars.rbegin(); it != vars.rend(); ++it) {
		const VarRecord& v = *it;
		if (v.mName.size() < prefLen)
			continue;
		bool matches = true;
		for (size_t i = 0; i < prefLen; ++i) {
			char p = prefix[i];
			if (p >= 'a' && p <= 'z') p = (char)(p - 32);
			char n = v.mName[i];
			if (n >= 'a' && n <= 'z') n = (char)(n - 32);
			if (p != n) { matches = false; break; }
		}
		if (!matches)
			continue;

		ResultInfo ri;
		// Insertion = remainder of the name, then the type-disambiguating
		// trailing character if any (so "ABC$" inserts as expected when
		// the user typed "AB").
		ri.mInsertionText.assign(v.mName.data() + prefLen, v.mName.size() - prefLen);
		if (v.mTypeChar)
			ri.mInsertionText.push_back(v.mTypeChar);

		// Display: full name + type sigil + a small "variable" tag so the
		// user can tell variables apart from built-in keywords at a glance.
		VDStringA display = v.mName;
		if (v.mTypeChar)
			display.push_back(v.mTypeChar);
		ri.mItemText = VDTextU8ToW(VDStringSpanA(display.data(), display.data() + display.size()));
		ri.mDescriptionText = (v.mTypeChar == '$') ? L"string variable"
		                    : (v.mTypeChar == '(') ? L"numeric array"
		                                           : L"numeric variable";
		resultSink(ri);
	}
}

////////////////////////////////////////////////////////////////////////////////

bool ATIsBasicMemoryLayoutValid() {
	// Atari BASIC layout (per reference manual §"Variable name table" /
	// §"Program format"):
	//   VNTP   $82-83  start of variable name table
	//   VNTD   $84-85  one past end of VNT (== VVTP in well-formed state)
	//   VVTP   $86-87  start of variable value table
	//   STMTAB $88-89  start of statement table
	//   STMCUR $8A-8B  current statement (within or just past STMTAB)
	//   STARP  $8C-8D  start of string/array area
	//
	// All five must form a non-decreasing chain inside user RAM
	// ($0700-$BFFF on a typical XL/XE; we use a slightly relaxed upper
	// bound so 800/400 setups with extended-RAM modules still pass).
	// We also cap the total span at 48 KB which covers every realistic
	// BASIC program even on 64K+ extended systems.
	const uint16 vntp  = g_sim.DebugReadWord(0x82);
	const uint16 vntd  = g_sim.DebugReadWord(0x84);
	const uint16 vvtp  = g_sim.DebugReadWord(0x86);
	const uint16 stmt  = g_sim.DebugReadWord(0x88);
	const uint16 stmc  = g_sim.DebugReadWord(0x8A);
	const uint16 starp = g_sim.DebugReadWord(0x8C);

	if (vntp < 0x0700 || vntp >= 0xC000)
		return false;
	if (vntd  < vntp)
		return false;
	if (vvtp  < vntd)
		return false;
	if (stmt  < vvtp)
		return false;
	if (stmc  < stmt)
		return false;
	if (starp < stmc)
		return false;
	if (starp > 0xC000)
		return false;
	if ((uint32)(starp - vntp) > 49152u)
		return false;
	return true;
}

vdrefptr<IATAutoSuggestEngine> ATCreateAutoSuggestEngine() {
	return vdmakerefcounted<ATAutoSuggestEngine>();
}

// ---------------------------------------------------------------------------
// BASIC statement-table walker
// ---------------------------------------------------------------------------
//
// STMTAB layout (Atari BASIC reference manual §"Statement table"):
//   bytes 0-1  line number (little-endian, 0..32767; 32768 == direct mode)
//   byte  2    offset to next line from start of this one (line length)
//   byte  3    offset to next statement on this line (first statement's start)
//   bytes 4..  tokenised statements
//
// Lines are stored in ascending order; the direct-mode sentinel (32768)
// is always the last entry.  A well-formed STMTAB always fits between
// STMTAB ($88) and STARP ($8C) bytes; if either pointer is stale or the
// line-length byte is zero we bail to avoid an infinite loop.
namespace {
	struct StmtTabRange {
		uint16 mAddr  = 0;
		uint16 mEnd   = 0;
		bool   mValid = false;
	};

	StmtTabRange ReadStmtTabRange() {
		StmtTabRange r;
		if (!ATIsBasicMemoryLayoutValid())
			return r;
		r.mAddr  = g_sim.DebugReadWord(0x88);
		r.mEnd   = g_sim.DebugReadWord(0x8C);  // STARP
		if (r.mEnd <= r.mAddr)
			return r;
		r.mValid = true;
		return r;
	}
}

bool ATBasicProgramLineExists(uint16 lineNumber) {
	if (lineNumber > 32767u)
		return false;
	const StmtTabRange r = ReadStmtTabRange();
	if (!r.mValid)
		return false;

	uint16 addr = r.mAddr;
	while (addr + 3 <= r.mEnd) {
		const uint16 ln = g_sim.DebugReadWord(addr);
		if (ln == lineNumber)
			return true;
		if (ln == 32768u)
			break;   // direct-mode sentinel — end of program
		const uint8 len = g_sim.DebugReadByte((uint16)(addr + 2));
		if (len < 4)
			break;   // malformed; avoid spin
		const uint32 next = (uint32)addr + len;
		if (next > 0x10000u || (uint16)next <= addr)
			break;
		addr = (uint16)next;
		if (ln > lineNumber)
			break;   // sorted table — passed our target
	}
	return false;
}

