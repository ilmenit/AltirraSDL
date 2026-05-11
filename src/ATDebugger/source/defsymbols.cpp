//	Altirra - Atari 800/800XL emulator
//	Copyright (C) 2008-2022 Avery Lee
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
#include <vd2/system/constexpr.h>
#include <at/atcore/ksyms.h>
#include <at/atdebugger/symbols.h>
#include <at/atdebugger/internal/symstore.h>

template<size_t N>
consteval VDCxArray<ATDefaultSymbolInfo, N> ATPreSortDefaultSymbolArray(const ATDefaultSymbolInfo (&src)[N]) {
	VDCxArray<ATDefaultSymbolInfo, N> dst {};

	for(size_t i = 0; i < N; ++i) {
		dst.v[i] = src[i];
	}

	std::sort(
		dst.begin(),
		dst.end(),
		[](const ATDefaultSymbolInfo& x, const ATDefaultSymbolInfo& y) {
			return x.mAddress < y.mAddress;
		}
	);

	return dst;
};

static constexpr auto kATDefaultSymbolsForOSVariables = [] {
	static constexpr ATDefaultSymbolInfo kVariableSymbols[] = {
		{ ATKernelSymbols::CASINI, 2, "CASINI" },
		{ ATKernelSymbols::RAMLO , 2, "RAMLO"  },
		{ ATKernelSymbols::TRAMSZ, 1, "TRAMSZ" },
		{ ATKernelSymbols::WARMST, 1, "WARMST" },
		{ ATKernelSymbols::DOSVEC, 2, "DOSVEC" },
		{ ATKernelSymbols::DOSINI, 2, "DOSINI" },
		{ ATKernelSymbols::APPMHI, 2, "APPMHI" },
		{ ATKernelSymbols::POKMSK, 1, "POKMSK" },
		{ ATKernelSymbols::BRKKEY, 1, "BRKKEY" },
		{ ATKernelSymbols::RTCLOK, 3, "RTCLOK" },
		{ ATKernelSymbols::BUFADR, 2, "BUFADR" },
		{ ATKernelSymbols::ICHIDZ, 1, "ICHIDZ" },
		{ ATKernelSymbols::ICDNOZ, 1, "ICDNOZ" },
		{ ATKernelSymbols::ICCOMZ, 1, "ICCOMZ" },
		{ ATKernelSymbols::ICSTAZ, 1, "ICSTAZ" },
		{ ATKernelSymbols::ICBALZ, 1, "ICBALZ" },
		{ ATKernelSymbols::ICBAHZ, 1, "ICBAHZ" },
		{ ATKernelSymbols::ICBLLZ, 1, "ICBLLZ" },
		{ ATKernelSymbols::ICBLHZ, 1, "ICBLHZ" },
		{ ATKernelSymbols::ICAX1Z, 1, "ICAX1Z" },
		{ ATKernelSymbols::ICAX2Z, 1, "ICAX2Z" },
		{ ATKernelSymbols::ICAX3Z, 1, "ICAX3Z" },
		{ ATKernelSymbols::ICAX4Z, 1, "ICAX4Z" },
		{ ATKernelSymbols::ICAX5Z, 1, "ICAX5Z" },
		{ ATKernelSymbols::CIOCHR, 1, "CIOCHR" },
		{ ATKernelSymbols::STATUS, 1, "STATUS" },
		{ ATKernelSymbols::CHKSUM, 1, "CHKSUM" },
		{ ATKernelSymbols::BUFRLO, 1, "BUFRLO" },
		{ ATKernelSymbols::BUFRHI, 1, "BUFRHI" },
		{ ATKernelSymbols::BFENLO, 1, "BFENLO" },
		{ ATKernelSymbols::BFENHI, 1, "BFENHI" },
		{ ATKernelSymbols::BUFRFL, 1, "BUFRFL" },
		{ ATKernelSymbols::RECVDN, 1, "RECVDN" },
		{ ATKernelSymbols::XMTDON, 1, "XMTDON" },
		{ ATKernelSymbols::CHKSNT, 1, "CHKSNT" },
		{ ATKernelSymbols::SOUNDR, 1, "SOUNDR" },
		{ ATKernelSymbols::CRITIC, 1, "CRITIC" },
		{ ATKernelSymbols::CKEY  , 1, "CKEY"   },
		{ ATKernelSymbols::CASSBT, 1, "CASSBT" },
		{ ATKernelSymbols::ATRACT, 1, "ATRACT" },
		{ ATKernelSymbols::DRKMSK, 1, "DRKMSK" },
		{ ATKernelSymbols::COLRSH, 1, "COLRSH" },
		{ ATKernelSymbols::HOLD1 , 1, "HOLD1"  },
		{ ATKernelSymbols::LMARGN, 1, "LMARGN" },
		{ ATKernelSymbols::RMARGN, 1, "RMARGN" },
		{ ATKernelSymbols::ROWCRS, 1, "ROWCRS" },
		{ ATKernelSymbols::COLCRS, 2, "COLCRS" },
		{ ATKernelSymbols::OLDROW, 1, "OLDROW" },
		{ ATKernelSymbols::OLDCOL, 2, "OLDCOL" },
		{ ATKernelSymbols::OLDCHR, 1, "OLDCHR" },
		{ ATKernelSymbols::DINDEX, 1, "DINDEX" },
		{ ATKernelSymbols::SAVMSC, 2, "SAVMSC" },
		{ ATKernelSymbols::OLDADR, 2, "OLDADR" },
		{ ATKernelSymbols::PALNTS, 1, "PALNTS" },
		{ ATKernelSymbols::LOGCOL, 1, "LOGCOL" },
		{ ATKernelSymbols::ADRESS, 2, "ADRESS" },
		{ ATKernelSymbols::TOADR , 2, "TOADR"  },
		{ ATKernelSymbols::RAMTOP, 1, "RAMTOP" },
		{ ATKernelSymbols::BUFCNT, 1, "BUFCNT" },
		{ ATKernelSymbols::BUFSTR, 2, "BUFSTR" },
		{ ATKernelSymbols::BITMSK, 1, "BITMSK" },
		{ ATKernelSymbols::DELTAR, 1, "DELTAR" },
		{ ATKernelSymbols::DELTAC, 2, "DELTAC" },
		{ ATKernelSymbols::ROWINC, 1, "ROWINC" },
		{ ATKernelSymbols::COLINC, 1, "COLINC" },
		{ ATKernelSymbols::KEYDEF, 2, "KEYDEF" },	// XL/XE
		{ ATKernelSymbols::SWPFLG, 1, "SWPFLG" },
		{ ATKernelSymbols::COUNTR, 2, "COUNTR" },

		{ ATKernelSymbols::FR0   , 6, "FR0"    },
		{ ATKernelSymbols::FR1   , 6, "FR1"    },
		{ ATKernelSymbols::CIX   , 1, "CIX"    },

		{ ATKernelSymbols::INBUFF, 1, "INBUFF" },
		{ ATKernelSymbols::FLPTR , 1, "FLPTR"  },

		{ ATKernelSymbols::VDSLST, 2, "VDSLST" },
		{ ATKernelSymbols::VPRCED, 2, "VPRCED" },
		{ ATKernelSymbols::VINTER, 2, "VINTER" },
		{ ATKernelSymbols::VBREAK, 2, "VBREAK" },
		{ ATKernelSymbols::VKEYBD, 2, "VKEYBD" },
		{ ATKernelSymbols::VSERIN, 2, "VSERIN" },
		{ ATKernelSymbols::VSEROR, 2, "VSEROR" },
		{ ATKernelSymbols::VSEROC, 2, "VSEROC" },
		{ ATKernelSymbols::VTIMR1, 2, "VTIMR1" },
		{ ATKernelSymbols::VTIMR2, 2, "VTIMR2" },
		{ ATKernelSymbols::VTIMR4, 2, "VTIMR4" },
		{ ATKernelSymbols::VIMIRQ, 2, "VIMIRQ" },
		{ ATKernelSymbols::CDTMV1, 2, "CDTMV1" },
		{ ATKernelSymbols::CDTMV2, 2, "CDTMV2" },
		{ ATKernelSymbols::CDTMV3, 2, "CDTMV3" },
		{ ATKernelSymbols::CDTMV4, 2, "CDTMV4" },
		{ ATKernelSymbols::CDTMV5, 2, "CDTMV5" },
		{ ATKernelSymbols::VVBLKI, 2, "VVBLKI" },
		{ ATKernelSymbols::VVBLKD, 2, "VVBLKD" },
		{ ATKernelSymbols::CDTMA1, 2, "CDTMA1" },
		{ ATKernelSymbols::CDTMA2, 2, "CDTMA2" },
		{ ATKernelSymbols::CDTMF3, 1, "CDTMF3" },
		{ ATKernelSymbols::CDTMF4, 1, "CDTMF4" },
		{ ATKernelSymbols::CDTMF5, 1, "CDTMF5" },
		{ ATKernelSymbols::SDMCTL, 1, "SDMCTL" },
		{ ATKernelSymbols::SDLSTL, 1, "SDLSTL" },
		{ ATKernelSymbols::SDLSTH, 1, "SDLSTH" },
		{ ATKernelSymbols::SSKCTL, 1, "SSKCTL" },
		{ ATKernelSymbols::LPENH , 1, "LPENH"  },
		{ ATKernelSymbols::LPENV , 1, "LPENV"  },
		{ ATKernelSymbols::BRKKY , 2, "BRKKY"  },
		{ ATKernelSymbols::VPIRQ , 2, "VPIRQ"  },	// XL/XE
		{ ATKernelSymbols::DFLAGS, 1, "DFLAGS" },
		{ ATKernelSymbols::DBSECT, 1, "DBSECT" },
		{ ATKernelSymbols::BOOTAD, 2, "BOOTAD" },
		{ ATKernelSymbols::COLDST, 1, "COLDST" },
		{ ATKernelSymbols::DSKTIM, 1, "DSKTIM" },
		{ ATKernelSymbols::PDVMSK, 1, "PDVMSK" },
		{ ATKernelSymbols::SHPDVS, 1, "SHPDVS" },
		{ ATKernelSymbols::PDMSK , 1, "PDMSK"  },	// XL/XE
		{ ATKernelSymbols::CHSALT, 1, "CHSALT" },	// XL/XE
		{ ATKernelSymbols::GPRIOR, 1, "GPRIOR" },
		{ ATKernelSymbols::PADDL0, 1, "PADDL0" },
		{ ATKernelSymbols::PADDL1, 1, "PADDL1" },
		{ ATKernelSymbols::PADDL2, 1, "PADDL2" },
		{ ATKernelSymbols::PADDL3, 1, "PADDL3" },
		{ ATKernelSymbols::PADDL4, 1, "PADDL4" },
		{ ATKernelSymbols::PADDL5, 1, "PADDL5" },
		{ ATKernelSymbols::PADDL6, 1, "PADDL6" },
		{ ATKernelSymbols::PADDL7, 1, "PADDL7" },
		{ ATKernelSymbols::STICK0, 1, "STICK0" },
		{ ATKernelSymbols::STICK1, 1, "STICK1" },
		{ ATKernelSymbols::STICK2, 1, "STICK2" },
		{ ATKernelSymbols::STICK3, 1, "STICK3" },
		{ ATKernelSymbols::PTRIG0, 1, "PTRIG0" },
		{ ATKernelSymbols::PTRIG1, 1, "PTRIG1" },
		{ ATKernelSymbols::PTRIG2, 1, "PTRIG2" },
		{ ATKernelSymbols::PTRIG3, 1, "PTRIG3" },
		{ ATKernelSymbols::PTRIG4, 1, "PTRIG4" },
		{ ATKernelSymbols::PTRIG5, 1, "PTRIG5" },
		{ ATKernelSymbols::PTRIG6, 1, "PTRIG6" },
		{ ATKernelSymbols::PTRIG7, 1, "PTRIG7" },
		{ ATKernelSymbols::STRIG0, 1, "STRIG0" },
		{ ATKernelSymbols::STRIG1, 1, "STRIG1" },
		{ ATKernelSymbols::STRIG2, 1, "STRIG2" },
		{ ATKernelSymbols::STRIG3, 1, "STRIG3" },
		{ ATKernelSymbols::JVECK , 2, "JVECK"  },
		{ ATKernelSymbols::TXTROW, 1, "TXTROW" },
		{ ATKernelSymbols::TXTCOL, 2, "TXTCOL" },
		{ ATKernelSymbols::TINDEX, 1, "TINDEX" },
		{ ATKernelSymbols::TXTMSC, 2, "TXTMSC" },
		{ ATKernelSymbols::TXTOLD, 2, "TXTOLD" },
		{ ATKernelSymbols::CRETRY, 1, "CRETRY" },
		{ ATKernelSymbols::HOLD2 , 1, "HOLD2"  },
		{ ATKernelSymbols::DMASK , 1, "DMASK"  },
		{ ATKernelSymbols::ESCFLG, 1, "ESCFLG" },
		{ ATKernelSymbols::TABMAP,15, "TABMAP" },
		{ ATKernelSymbols::LOGMAP, 4, "LOGMAP" },
		{ ATKernelSymbols::DRETRY, 1, "DRETRY" },
		{ ATKernelSymbols::SHFLOK, 1, "SHFLOK" },
		{ ATKernelSymbols::BOTSCR, 1, "BOTSCR" },
		{ ATKernelSymbols::PCOLR0, 1, "PCOLR0" },
		{ ATKernelSymbols::PCOLR1, 1, "PCOLR1" },
		{ ATKernelSymbols::PCOLR2, 1, "PCOLR2" },
		{ ATKernelSymbols::PCOLR3, 1, "PCOLR3" },
		{ ATKernelSymbols::COLOR0, 1, "COLOR0" },
		{ ATKernelSymbols::COLOR1, 1, "COLOR1" },
		{ ATKernelSymbols::COLOR2, 1, "COLOR2" },
		{ ATKernelSymbols::COLOR3, 1, "COLOR3" },
		{ ATKernelSymbols::COLOR4, 1, "COLOR4" },
		{ ATKernelSymbols::DSCTLN, 1, "DSCTLN" },	// XL/XE
		{ ATKernelSymbols::KRPDEL, 1, "KRPDEL" },	// XL/XE
		{ ATKernelSymbols::KEYREP, 1, "KEYREP" },	// XL/XE
		{ ATKernelSymbols::NOCLIK, 1, "NOCLIK" },	// XL/XE
		{ ATKernelSymbols::HELPFG, 1, "HELPFG" },	// XL/XE
		{ ATKernelSymbols::DMASAV, 1, "DMASAV" },	// XL/XE
		{ ATKernelSymbols::RUNAD , 2, "RUNAD"  },
		{ ATKernelSymbols::INITAD, 2, "INITAD" },
		{ ATKernelSymbols::MEMTOP, 2, "MEMTOP" },
		{ ATKernelSymbols::MEMLO , 2, "MEMLO"  },
		{ ATKernelSymbols::DVSTAT, 4, "DVSTAT" },
		{ ATKernelSymbols::CBAUDL, 1, "CBAUDL" },
		{ ATKernelSymbols::CBAUDH, 1, "CBAUDH" },
		{ ATKernelSymbols::CRSINH, 1, "CRSINH" },
		{ ATKernelSymbols::KEYDEL, 1, "KEYDEL" },
		{ ATKernelSymbols::CH1   , 1, "CH1"    },
		{ ATKernelSymbols::CHACT , 1, "CHACT"  },
		{ ATKernelSymbols::CHBAS , 1, "CHBAS"  },
		{ ATKernelSymbols::ATACHR, 1, "ATACHR" },
		{ ATKernelSymbols::CH    , 1, "CH"     },
		{ ATKernelSymbols::FILDAT, 1, "FILDAT" },
		{ ATKernelSymbols::DSPFLG, 1, "DSPFLG" },
		{ ATKernelSymbols::DDEVIC, 1, "DDEVIC" },
		{ ATKernelSymbols::DUNIT , 1, "DUNIT"  },
		{ ATKernelSymbols::DCOMND, 1, "DCOMND" },
		{ ATKernelSymbols::DSTATS, 1, "DSTATS" },
		{ ATKernelSymbols::DBUFLO, 1, "DBUFLO" },
		{ ATKernelSymbols::DBUFHI, 1, "DBUFHI" },
		{ ATKernelSymbols::DTIMLO, 1, "DTIMLO" },
		{ ATKernelSymbols::DBYTLO, 1, "DBYTLO" },
		{ ATKernelSymbols::DBYTHI, 1, "DBYTHI" },
		{ ATKernelSymbols::DAUX1 , 1, "DAUX1"  },
		{ ATKernelSymbols::DAUX2 , 1, "DAUX2"  },
		{ ATKernelSymbols::TIMER1, 2, "TIMER1" },
		{ ATKernelSymbols::CASFLG, 1, "CASFLG" },
		{ ATKernelSymbols::TIMER2, 2, "TIMER2" },
		{ ATKernelSymbols::TIMFLG, 1, "TIMFLG" },
		{ ATKernelSymbols::STACKP, 1, "STACKP" },
		{ ATKernelSymbols::HATABS,38, "HATABS" },
		{ ATKernelSymbols::ICHID , 1, "ICHID"  },
		{ ATKernelSymbols::ICDNO , 1, "ICDNO"  },
		{ ATKernelSymbols::ICCMD , 1, "ICCMD"  },
		{ ATKernelSymbols::ICSTA , 1, "ICSTA"  },
		{ ATKernelSymbols::ICBAL , 1, "ICBAL"  },
		{ ATKernelSymbols::ICBAH , 1, "ICBAH"  },
		{ ATKernelSymbols::ICPTL , 1, "ICPTL"  },
		{ ATKernelSymbols::ICPTH , 1, "ICPTH"  },
		{ ATKernelSymbols::ICBLL , 1, "ICBLL"  },
		{ ATKernelSymbols::ICBLH , 1, "ICBLH"  },
		{ ATKernelSymbols::ICAX1 , 1, "ICAX1"  },
		{ ATKernelSymbols::ICAX2 , 1, "ICAX2"  },
		{ ATKernelSymbols::ICAX3 , 1, "ICAX3"  },
		{ ATKernelSymbols::ICAX4 , 1, "ICAX4"  },
		{ ATKernelSymbols::ICAX5 , 1, "ICAX5"  },
		{ ATKernelSymbols::ICAX6 , 1, "ICAX6"  },
		{ ATKernelSymbols::BASICF, 1, "BASICF" },
		{ ATKernelSymbols::GINTLK, 1, "GINTLK" },
		{ ATKernelSymbols::CASBUF, 131, "CASBUF" },
		{ ATKernelSymbols::LBUFF , 128, "LBUFF"  },
	};

	return ATPreSortDefaultSymbolArray(kVariableSymbols);
}();

vdspan<const ATDefaultSymbolInfo> ATGetSortedSymbolsForOSVariables() {
	return kATDefaultSymbolsForOSVariables;
}

void ATCreateDefaultVariableSymbolStore(IATSymbolStore **ppStore) {
	vdrefptr<ATSymbolStore> symstore(new ATSymbolStore);

	symstore->Init(0x0000, 0x0400);
	symstore->AddDefaultSymbols(kATDefaultSymbolsForOSVariables);

	*ppStore = symstore.release();
}

void ATCreateDefaultVariableSymbolStore5200(IATSymbolStore **ppStore) {
	vdrefptr<ATSymbolStore> symstore(new ATSymbolStore);

	symstore->Init(0x0000, 0x0400);

	using namespace ATKernelSymbols5200;

	static constexpr ATDefaultSymbolInfo kSymbols[] = {
		{ POKMSK, 1, "POKMSK" },
		{ RTCLOK, 1, "RTCLOK" },
		{ CRITIC, 1, "CRITIC" },
		{ ATRACT, 1, "ATRACT" },
		{ SDMCTL, 1, "SDMCTL" },
		{ SDLSTL, 1, "SDLSTL" },
		{ SDLSTH, 1, "SDLSTH" },
		{ PCOLR0, 1, "PCOLR0" },
		{ PCOLR1, 1, "PCOLR1" },
		{ PCOLR2, 1, "PCOLR2" },
		{ PCOLR3, 1, "PCOLR3" },
		{ COLOR0, 1, "COLOR0" },
		{ COLOR1, 1, "COLOR1" },
		{ COLOR2, 1, "COLOR2" },
		{ COLOR3, 1, "COLOR3" },
		{ COLOR4, 1, "COLOR4" },

		{ VIMIRQ, 2, "VIMIRQ" },
		{ VVBLKI, 2, "VVBLKI" },
		{ VVBLKD, 2, "VVBLKD" },
		{ VDSLST, 2, "VDSLST" },
		{ VTRIGR, 2, "VTRIGR" },
		{ VBRKOP, 2, "VBRKOP" },
		{ VKYBDI, 2, "VKYBDI" },
		{ VKYBDF, 2, "VKYBDF" },
		{ VSERIN, 2, "VSERIN" },
		{ VSEROR, 2, "VSEROR" },
		{ VSEROC, 2, "VSEROC" },
		{ VTIMR1, 2, "VTIMR1" },
		{ VTIMR2, 2, "VTIMR2" },
		{ VTIMR4, 2, "VTIMR4" },
	};

	symstore->AddDefaultSymbols(kSymbols);

	*ppStore = symstore.release();
}

void ATCreateDefaultMathPackSymbolStore(IATSymbolStore **ppStore) {
	using namespace ATKernelSymbols;

	vdrefptr<ATSymbolStore> symstore(new ATSymbolStore);

	symstore->Init(0xD800, 0x0800);

	static constexpr ATDefaultSymbolInfo kSymbols[] = {
		{ AFP   , 1, "AFP"    },
		{ FASC  , 1, "FASC"   },
		{ IFP   , 1, "IFP"    },
		{ FPI   , 1, "FPI"    },
		{ ZFR0  , 1, "ZFR0"   },
		{ ZF1   , 1, "ZF1"    },
		{ FADD  , 1, "FADD"   },
		{ FSUB  , 1, "FSUB"   },
		{ FMUL  , 1, "FMUL"   },
		{ FDIV  , 1, "FDIV"   },
		{ PLYEVL, 1, "PLYEVL" },
		{ FLD0R , 1, "FLD0R"  },
		{ FLD0P , 1, "FLD0P"  },
		{ FLD1R , 1, "FLD1R"  },
		{ FLD1P , 1, "FLD1P"  },
		{ FST0R , 1, "FST0R"  },
		{ FST0P , 1, "FST0P"  },
		{ FMOVE , 1, "FMOVE"  },
		{ EXP   , 1, "EXP"    },
		{ EXP10 , 1, "EXP10"  },
		{ LOG   , 1, "LOG"    },
		{ LOG10 , 1, "LOG10"  },
	};

	symstore->AddDefaultSymbols(kSymbols);

	*ppStore = symstore.release();
}

void ATCreateDefaultKernelSymbolStore(IATSymbolStore **ppStore) {
	using namespace ATKernelSymbols;

	vdrefptr<ATSymbolStore> symstore(new ATSymbolStore);

	symstore->Init(0xE400, 0x0100);

	static constexpr ATDefaultSymbolInfo kSymbols[] = {
		{ 0xE400, 3, "EDITRV" },
		{ 0xE410, 3, "SCRENV" },
		{ 0xE420, 3, "KEYBDV" },
		{ 0xE430, 3, "PRINTV" },
		{ 0xE440, 3, "CASETV" },
		{ 0xE450, 3, "DISKIV" },
		{ 0xE453, 3, "DSKINV" },
		{ 0xE456, 3, "CIOV"   },
		{ 0xE459, 3, "SIOV"   },
		{ 0xE45C, 3, "SETVBV" },
		{ 0xE45F, 3, "SYSVBV" },
		{ 0xE462, 3, "XITVBV" },
		{ 0xE465, 3, "SIOINV" },
		{ 0xE468, 3, "SENDEV" },
		{ 0xE46B, 3, "INTINV" },
		{ 0xE46E, 3, "CIOINV" },
		{ 0xE471, 3, "BLKBDV" },
		{ 0xE474, 3, "WARMSV" },
		{ 0xE477, 3, "COLDSV" },
		{ 0xE47A, 3, "RBLOKV" },
		{ 0xE47D, 3, "CSOPIV" },
		{ 0xE480, 3, "VCTABL" },
	};

	symstore->AddDefaultSymbols(kSymbols);

	*ppStore = symstore.release();
}

namespace {
	static constexpr auto kGTIASymbols = [] {
		static constexpr ATDefaultSymbolInfo kRawGTIASymbols[]={
			{ 0x00, 1, "HPOSP0", ATDefaultSymbolType::WriteOnly },
			{ 0x01, 1, "HPOSP1", ATDefaultSymbolType::WriteOnly },
			{ 0x02, 1, "HPOSP2", ATDefaultSymbolType::WriteOnly },
			{ 0x03, 1, "HPOSP3", ATDefaultSymbolType::WriteOnly },
			{ 0x04, 1, "HPOSM0", ATDefaultSymbolType::WriteOnly },
			{ 0x05, 1, "HPOSM1", ATDefaultSymbolType::WriteOnly },
			{ 0x06, 1, "HPOSM2", ATDefaultSymbolType::WriteOnly },
			{ 0x07, 1, "HPOSM3", ATDefaultSymbolType::WriteOnly },
			{ 0x08, 1, "SIZEP0", ATDefaultSymbolType::WriteOnly },
			{ 0x09, 1, "SIZEP1", ATDefaultSymbolType::WriteOnly },
			{ 0x0A, 1, "SIZEP2", ATDefaultSymbolType::WriteOnly },
			{ 0x0B, 1, "SIZEP3", ATDefaultSymbolType::WriteOnly },
			{ 0x0C, 1, "SIZEM" , ATDefaultSymbolType::WriteOnly },
			{ 0x0D, 1, "GRAFP0", ATDefaultSymbolType::WriteOnly },
			{ 0x0E, 1, "GRAFP1", ATDefaultSymbolType::WriteOnly },
			{ 0x0F, 1, "GRAFP2", ATDefaultSymbolType::WriteOnly },
			{ 0x10, 1, "GRAFP3", ATDefaultSymbolType::WriteOnly },
			{ 0x11, 1, "GRAFM" , ATDefaultSymbolType::WriteOnly },
			{ 0x12, 1, "COLPM0", ATDefaultSymbolType::WriteOnly },
			{ 0x13, 1, "COLPM1", ATDefaultSymbolType::WriteOnly },
			{ 0x14, 1, "COLPM2", ATDefaultSymbolType::WriteOnly },
			{ 0x15, 1, "COLPM3", ATDefaultSymbolType::WriteOnly },
			{ 0x16, 1, "COLPF0", ATDefaultSymbolType::WriteOnly },
			{ 0x17, 1, "COLPF1", ATDefaultSymbolType::WriteOnly },
			{ 0x18, 1, "COLPF2", ATDefaultSymbolType::WriteOnly },
			{ 0x19, 1, "COLPF3", ATDefaultSymbolType::WriteOnly },
			{ 0x1A, 1, "COLBK" , ATDefaultSymbolType::WriteOnly },
			{ 0x1B, 1, "PRIOR" , ATDefaultSymbolType::WriteOnly },
			{ 0x1C, 1, "VDELAY", ATDefaultSymbolType::WriteOnly },
			{ 0x1D, 1, "GRACTL", ATDefaultSymbolType::WriteOnly },
			{ 0x1E, 1, "HITCLR", ATDefaultSymbolType::WriteOnly },

			{ 0x00, 1, "M0PF"  , ATDefaultSymbolType::ReadOnly },
			{ 0x01, 1, "M1PF"  , ATDefaultSymbolType::ReadOnly },
			{ 0x02, 1, "M2PF"  , ATDefaultSymbolType::ReadOnly },
			{ 0x03, 1, "M3PF"  , ATDefaultSymbolType::ReadOnly },
			{ 0x04, 1, "P0PF"  , ATDefaultSymbolType::ReadOnly },
			{ 0x05, 1, "P1PF"  , ATDefaultSymbolType::ReadOnly },
			{ 0x06, 1, "P2PF"  , ATDefaultSymbolType::ReadOnly },
			{ 0x07, 1, "P3PF"  , ATDefaultSymbolType::ReadOnly },
			{ 0x08, 1, "M0PL"  , ATDefaultSymbolType::ReadOnly },
			{ 0x09, 1, "M1PL"  , ATDefaultSymbolType::ReadOnly },
			{ 0x0A, 1, "M2PL"  , ATDefaultSymbolType::ReadOnly },
			{ 0x0B, 1, "M3PL"  , ATDefaultSymbolType::ReadOnly },
			{ 0x0C, 1, "P0PL"  , ATDefaultSymbolType::ReadOnly },
			{ 0x0D, 1, "P1PL"  , ATDefaultSymbolType::ReadOnly },
			{ 0x0E, 1, "P2PL"  , ATDefaultSymbolType::ReadOnly },
			{ 0x0F, 1, "P3PL"  , ATDefaultSymbolType::ReadOnly },
			{ 0x10, 1, "TRIG0" , ATDefaultSymbolType::ReadOnly },
			{ 0x11, 1, "GRAFM" , ATDefaultSymbolType::ReadOnly },
			{ 0x12, 1, "COLPM0", ATDefaultSymbolType::ReadOnly },
			{ 0x13, 1, "COLPM1", ATDefaultSymbolType::ReadOnly },
			{ 0x14, 1, "COLPM2", ATDefaultSymbolType::ReadOnly },
			{ 0x15, 1, "COLPM3", ATDefaultSymbolType::ReadOnly },
			{ 0x16, 1, "COLPF0", ATDefaultSymbolType::ReadOnly },
			{ 0x17, 1, "COLPF1", ATDefaultSymbolType::ReadOnly },
			{ 0x18, 1, "COLPF2", ATDefaultSymbolType::ReadOnly },
			{ 0x19, 1, "COLPF3", ATDefaultSymbolType::ReadOnly },
			{ 0x1A, 1, "COLBK" , ATDefaultSymbolType::ReadOnly },
			{ 0x1B, 1, "PRIOR" , ATDefaultSymbolType::ReadOnly },
			{ 0x1C, 1, "VDELAY", ATDefaultSymbolType::ReadOnly },
			{ 0x1D, 1, "GRACTL", ATDefaultSymbolType::ReadOnly },
			{ 0x1E, 1, "HITCLR", ATDefaultSymbolType::ReadOnly },

			{ 0x1F, 1, "CONSOL", ATDefaultSymbolType::NoExecute },
		};

		return ATPreSortDefaultSymbolArray(kRawGTIASymbols);
	}();

	static constexpr auto kPOKEYSymbols = [] {
		static constexpr ATDefaultSymbolInfo kRawPOKEYSymbols[]={
			{ 0x00, 1, "AUDF1" , ATDefaultSymbolType::WriteOnly },
			{ 0x01, 1, "AUDC1" , ATDefaultSymbolType::WriteOnly },
			{ 0x02, 1, "AUDF2" , ATDefaultSymbolType::WriteOnly },
			{ 0x03, 1, "AUDC2" , ATDefaultSymbolType::WriteOnly },
			{ 0x04, 1, "AUDF3" , ATDefaultSymbolType::WriteOnly },
			{ 0x05, 1, "AUDC3" , ATDefaultSymbolType::WriteOnly },
			{ 0x06, 1, "AUDF4" , ATDefaultSymbolType::WriteOnly },
			{ 0x07, 1, "AUDC4" , ATDefaultSymbolType::WriteOnly },
			{ 0x08, 1, "AUDCTL", ATDefaultSymbolType::WriteOnly },
			{ 0x09, 1, "STIMER", ATDefaultSymbolType::WriteOnly },
			{ 0x0A, 1, "SKRES" , ATDefaultSymbolType::WriteOnly },
			{ 0x0B, 1, "POTGO" , ATDefaultSymbolType::WriteOnly },
			{ 0x0D, 1, "SEROUT", ATDefaultSymbolType::WriteOnly },
			{ 0x0E, 1, "IRQEN" , ATDefaultSymbolType::WriteOnly },
			{ 0x0F, 1, "SKCTL" , ATDefaultSymbolType::WriteOnly },

			{ 0x00, 1, "POT0"  , ATDefaultSymbolType::ReadOnly },
			{ 0x01, 1, "POT1"  , ATDefaultSymbolType::ReadOnly },
			{ 0x02, 1, "POT2"  , ATDefaultSymbolType::ReadOnly },
			{ 0x03, 1, "POT3"  , ATDefaultSymbolType::ReadOnly },
			{ 0x04, 1, "POT4"  , ATDefaultSymbolType::ReadOnly },
			{ 0x05, 1, "POT5"  , ATDefaultSymbolType::ReadOnly },
			{ 0x06, 1, "POT6"  , ATDefaultSymbolType::ReadOnly },
			{ 0x07, 1, "POT7"  , ATDefaultSymbolType::ReadOnly },
			{ 0x08, 1, "ALLPOT", ATDefaultSymbolType::ReadOnly },
			{ 0x09, 1, "KBCODE", ATDefaultSymbolType::ReadOnly },
			{ 0x0A, 1, "RANDOM", ATDefaultSymbolType::ReadOnly },
			{ 0x0D, 1, "SERIN" , ATDefaultSymbolType::ReadOnly },
			{ 0x0E, 1, "IRQST" , ATDefaultSymbolType::ReadOnly },
			{ 0x0F, 1, "SKSTAT", ATDefaultSymbolType::ReadOnly },
		};

		return ATPreSortDefaultSymbolArray(kRawPOKEYSymbols);
	}();

	static constexpr auto kPIASymbols = [] {
		static constexpr ATDefaultSymbolInfo kRawPIASymbols[]={
			{ 0xD300, 1, "PORTA", ATDefaultSymbolType::NoExecute },
			{ 0xD301, 1, "PORTB", ATDefaultSymbolType::NoExecute },
			{ 0xD302, 1, "PACTL", ATDefaultSymbolType::NoExecute },
			{ 0xD303, 1, "PBCTL", ATDefaultSymbolType::NoExecute },
		};

		return ATPreSortDefaultSymbolArray(kRawPIASymbols);
	}();

	static constexpr auto kANTICSymbols = [] {
		static constexpr ATDefaultSymbolInfo kRawANTICSymbols[]={
			{ 0xD400, 1, "DMACTL", ATDefaultSymbolType::WriteOnly },
			{ 0xD401, 1, "CHACTL", ATDefaultSymbolType::WriteOnly },
			{ 0xD402, 1, "DLISTL", ATDefaultSymbolType::WriteOnly },
			{ 0xD403, 1, "DLISTH", ATDefaultSymbolType::WriteOnly },
			{ 0xD404, 1, "HSCROL", ATDefaultSymbolType::WriteOnly },
			{ 0xD405, 1, "VSCROL", ATDefaultSymbolType::WriteOnly },
			{ 0xD407, 1, "PMBASE", ATDefaultSymbolType::WriteOnly },
			{ 0xD409, 1, "CHBASE", ATDefaultSymbolType::WriteOnly },
			{ 0xD40A, 1, "WSYNC" , ATDefaultSymbolType::WriteOnly },
			{ 0xD40B, 1, "VCOUNT", ATDefaultSymbolType::ReadOnly  },
			{ 0xD40C, 1, "PENH"  , ATDefaultSymbolType::ReadOnly  },
			{ 0xD40D, 1, "PENV"  , ATDefaultSymbolType::ReadOnly  },
			{ 0xD40E, 1, "NMIEN" , ATDefaultSymbolType::WriteOnly },
			{ 0xD40F, 1, "NMIRES", ATDefaultSymbolType::WriteOnly },
			{ 0xD40F, 1, "NMIST" , ATDefaultSymbolType::ReadOnly  },
		};
		return ATPreSortDefaultSymbolArray(kRawANTICSymbols);
	}();
}

vdspan<const ATDefaultSymbolInfo> ATGetSortedSymbolsForANTICVariables() {
	return kANTICSymbols;
}

vdspan<const ATDefaultSymbolInfo> ATGetSortedSymbolsForGTIAVariables() {
	return kGTIASymbols;
}

vdspan<const ATDefaultSymbolInfo> ATGetSortedSymbolsForPIAVariables() {
	return kPIASymbols;
}

vdspan<const ATDefaultSymbolInfo> ATGetSortedSymbolsForPOKEYVariables() {
	return kPOKEYSymbols;
}

void ATCreateDefaultHardwareSymbolStore(IATSymbolStore **ppStore) {
	vdrefptr<ATSymbolStore> symstore(new ATSymbolStore);

	symstore->Init(0xD000, 0x0500);
	symstore->AddDefaultSymbols(kGTIASymbols , 0xD000);
	symstore->AddDefaultSymbols(kPOKEYSymbols, 0xD200);
	symstore->AddDefaultSymbols(kPIASymbols);
	symstore->AddDefaultSymbols(kANTICSymbols);

	*ppStore = symstore.release();
}

void ATCreateDefault5200HardwareSymbolStore(IATSymbolStore **ppStore) {
	vdrefptr<ATSymbolStore> symstore(new ATSymbolStore);

	symstore->Init(0xC000, 0x3000);
	symstore->AddDefaultSymbols(kGTIASymbols , 0xC000);
	symstore->AddDefaultSymbols(kPOKEYSymbols, 0xE800);
	symstore->AddDefaultSymbols(kANTICSymbols);

	*ppStore = symstore.release();
}
