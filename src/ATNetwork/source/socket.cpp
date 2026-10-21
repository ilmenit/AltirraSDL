//	Altirra - Atari 800/800XL/5200 emulator
//	Copyright (C) 2009-2023 Avery Lee
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
#include <vd2/system/binary.h>
#include <at/atnetwork/socket.h>

// Fork: the public comparison was declared without a definition. Native
// NetSIO uses it to reject packets from a different peer. Compare fields,
// rather than union storage or structure padding, on every platform.
bool ATSocketAddress::operator==(const ATSocketAddress& other) const {
	if (mType != other.mType || mPort != other.mPort)
		return false;
	switch (mType) {
		case ATSocketAddressType::IPv4:
			return mIPv4Address == other.mIPv4Address;
		case ATSocketAddressType::IPv6:
			return mIPv6.mScopeId == other.mIPv6.mScopeId
				&& !memcmp(mIPv6.mAddress, other.mIPv6.mAddress,
					sizeof mIPv6.mAddress);
		default:
			return true;
	}
}

ATSocketAddress ATSocketAddress::CreateIPv4InIPv6(const ATSocketAddress& ipv4) {
	ATSocketAddress addr;
	addr.mType = ATSocketAddressType::IPv6;
	addr.mPort = ipv4.mPort;
	memset(addr.mIPv6.mAddress, 0, sizeof addr.mIPv6.mAddress);
	VDWriteUnalignedBEU32(addr.mIPv6.mAddress + 12, ipv4.mIPv4Address);
	addr.mIPv6.mScopeId = 0;

	return addr;
}

bool ATSocketAddress::IsNonZero() const {
	switch(mType) {
		case ATSocketAddressType::IPv4:
			return mIPv4Address != 0 || mPort != 0;

		case ATSocketAddressType::IPv6:
			if (mPort != 0)
				return true;

			for(const uint8& v : mIPv6.mAddress) {
				if (v != 0)
					return true;
			}

			return false;

		default:
			return false;
	}
}

VDStringA ATSocketAddress::ToString(bool includePort) const {
	VDStringA s;

	if (mType == ATSocketAddressType::IPv4) {
		s.sprintf("%u.%u.%u.%u",
			(mIPv4Address >> 24) & 0xFF,
			(mIPv4Address >> 16) & 0xFF,
			(mIPv4Address >>  8) & 0xFF,
			(mIPv4Address >>  0) & 0xFF
		);

		if (includePort)
			s.append_sprintf(":%u", mPort);
	} else if (mType == ATSocketAddressType::IPv6) {
		uint16 words[8];
		int zerorun = 0;
		int maxzerorun = 1;
		int maxzerorunpos = -1;

		for(int i=0; i<8; ++i) {
			words[i] = VDReadUnalignedBEU16(&mIPv6.mAddress[i*2]);

			if (words[i]) {
				zerorun = 0;
			} else {
				++zerorun;

				if (maxzerorun < zerorun) {
					maxzerorun = zerorun;
					maxzerorunpos = (i + 1) - zerorun;
				}
			}
		}

		s = "[";

		for(int i=0; i<8; ++i) {
			if (i == maxzerorunpos) {
				s += "::";

				i += maxzerorun - 1;
			} else {
				if (s.back() != L':' && s.back() != L'[')
					s += ':';

				s.append_sprintf("%x", words[i]);
			}
		}

		if (mIPv6.mScopeId)
			s.append_sprintf("%%%u", (unsigned)mIPv6.mScopeId);

		if (includePort)
			s.append_sprintf("]:%u", mPort);
		else
			s.append("]");
	}

	return s;
}
