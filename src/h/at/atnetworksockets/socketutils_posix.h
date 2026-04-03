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

#ifndef f_ATNETWORKSOCKETS_SOCKETUTILS_POSIX_H
#define f_ATNETWORKSOCKETS_SOCKETUTILS_POSIX_H

#include <sys/socket.h>
#include <netinet/in.h>

struct ATSocketAddress;

struct ATSocketNativeAddress {
	ATSocketNativeAddress(const ATSocketAddress& addr);

	const sockaddr *GetSockAddr() const { return mpAddr; }
	socklen_t GetSockAddrLen() const { return mAddrLen; }

	const sockaddr *mpAddr = nullptr;
	socklen_t mAddrLen = 0;

	union {
		sockaddr_in mIPv4;
		sockaddr_in6 mIPv6;
	};
};

ATSocketAddress ATSocketFromNativeAddress(const sockaddr *saddr);

#endif
