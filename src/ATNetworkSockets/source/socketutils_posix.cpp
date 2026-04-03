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
#include <cstring>
#include <at/atnetwork/socket.h>
#include <at/atnetworksockets/socketutils_posix.h>

ATSocketNativeAddress::ATSocketNativeAddress(const ATSocketAddress& addr) {
	if (addr.mType == ATSocketAddressType::IPv4) {
		memset(&mIPv4, 0, sizeof mIPv4);

		mIPv4.sin_family = AF_INET;
		mIPv4.sin_port = htons(addr.mPort);
		mIPv4.sin_addr.s_addr = htonl(addr.mIPv4Address);

		mpAddr = (const sockaddr *)&mIPv4;
		mAddrLen = sizeof mIPv4;
	} else if (addr.mType == ATSocketAddressType::IPv6) {
		memset(&mIPv6, 0, sizeof mIPv6);

		mIPv6.sin6_family = AF_INET6;
		mIPv6.sin6_port = htons(addr.mPort);
		mIPv6.sin6_flowinfo = 0;
		memcpy(mIPv6.sin6_addr.s6_addr, addr.mIPv6.mAddress, sizeof mIPv6.sin6_addr.s6_addr);
		mIPv6.sin6_scope_id = addr.mIPv6.mScopeId;

		mpAddr = (const sockaddr *)&mIPv6;
		mAddrLen = sizeof mIPv6;
	}
}

ATSocketAddress ATSocketFromNativeAddress(const sockaddr *saddr) {
	ATSocketAddress addr;

	if (saddr) {
		if (saddr->sa_family == AF_INET) {
			sockaddr_in saddr4 {};
			memcpy(&saddr4, saddr, sizeof saddr4);

			addr.mType = ATSocketAddressType::IPv4;
			addr.mIPv4Address = ntohl(saddr4.sin_addr.s_addr);
			addr.mPort = ntohs(saddr4.sin_port);
		} else if (saddr->sa_family == AF_INET6) {
			sockaddr_in6 saddr6 {};
			memcpy(&saddr6, saddr, sizeof saddr6);

			addr.mType = ATSocketAddressType::IPv6;
			memcpy(addr.mIPv6.mAddress, saddr6.sin6_addr.s6_addr, sizeof addr.mIPv6.mAddress);
			addr.mIPv6.mScopeId = saddr6.sin6_scope_id;
			addr.mPort = ntohs(saddr6.sin6_port);
		}
	}

	return addr;
}
