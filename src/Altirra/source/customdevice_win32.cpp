//	Altirra - Atari 800/800XL/5200 emulator
//	Copyright (C) 2009-2020 Avery Lee
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
#include <at/atcore/timerservice.h>
#include <at/atnetworksockets/nativesockets.h>
#include "customdevice_win32.h"

class ATDeviceCustomNetworkEngine final : public vdrefcounted<IATDeviceCustomNetworkEngine> {
public:
	ATDeviceCustomNetworkEngine(uint16 port, IATTimerService& timerService, vdfunction<void()> recvNotifyFn);
	~ATDeviceCustomNetworkEngine();

	bool Init();
	void Shutdown() override;

	bool IsConnected() override;
	bool WaitForFirstConnectionAttempt() override;
	bool Restore() override;
	bool Send(const void *data, uint32 len) override;
	bool Recv(void *data, uint32 len) override;
	bool SetRecvNotifyEnabled() override;

private:
	void Connect();
	void QueueReconnect();

	const uint16 mPort;
	vdfunction<void()> mReceiveHandler;

	vdrefptr<IATStreamSocket> mpSocket;
	IATTimerService& mTimerService;
	uint64 mReconnectAsyncToken = 0;

	VDCriticalSection mMutex;

	VDSignal mSocketEvent;
	ATSocketStatus mSocketStatus;
	bool mbConnectionAttempted = false;
	bool mbInitialNotifyNeeded = false;
	bool mbRecvNotifyNeeded = false;
	bool mbAllowReconnect = true;
	bool mbRestoreRequired = true;
};

////////////////////////////////////////////////////////////////////////////

ATDeviceCustomNetworkEngine::ATDeviceCustomNetworkEngine(uint16 port, IATTimerService& timerService, vdfunction<void()> recvNotifyFn)
	: mPort(port)
	, mTimerService(timerService)
	, mReceiveHandler(std::move(recvNotifyFn))
{
}

ATDeviceCustomNetworkEngine::~ATDeviceCustomNetworkEngine() {
	Shutdown();
}

bool ATDeviceCustomNetworkEngine::Init() {
	Connect();
	return true;
}

void ATDeviceCustomNetworkEngine::Shutdown() {
	mTimerService.Cancel(&mReconnectAsyncToken);

	vdsynchronized(mMutex) {
		mbRecvNotifyNeeded = false;
		mbAllowReconnect = false;
	}

	if (mpSocket) {
		mpSocket->SetOnEvent(nullptr, nullptr, false);
		mpSocket->CloseSocket(true);
		mpSocket = nullptr;
	}
}

bool ATDeviceCustomNetworkEngine::IsConnected() {
	mMutex.Lock();
	bool success = mpSocket && (!mSocketStatus.mbConnecting && !mSocketStatus.mbClosed && !mSocketStatus.mbRemoteClosed);
	mMutex.Unlock();

	return success;
}

bool ATDeviceCustomNetworkEngine::WaitForFirstConnectionAttempt() {
	bool success;

	for(;;) {
		vdsynchronized(mMutex) {
			if (!mpSocket)
				return false;
			success = !mSocketStatus.mbConnecting;
		}

		if (success)
			break;

		mSocketEvent.wait();
	}

	return success;
}

bool ATDeviceCustomNetworkEngine::Restore() {
	bool success = false;

	if (mpSocket) {
		vdsynchronized(mMutex) {
			if (mbRestoreRequired) {
				if (!mSocketStatus.mbConnecting && !mSocketStatus.mbClosed && !mSocketStatus.mbRemoteClosed) {
					mbRestoreRequired = false;
					success = true;
				}
			}
		}
	}

	return success;
}

bool ATDeviceCustomNetworkEngine::Send(const void *data, uint32 len) {
	if (!mpSocket)
		return false;

	if (!len)
		return true;

	bool success = true;
	while(len) {
		vdsynchronized(mMutex) {
			if (mbRestoreRequired) {
				success = false;
				break;
			}
		}

		sint32 actual = mpSocket->Send(data, len);

		if (actual < 0) {
			success = false;
			break;
		}

		if (actual) {
			data = (const char *)data + actual;
			len -= actual;
		} else {
			mSocketEvent.wait();
		}
	}

	return success;
}

bool ATDeviceCustomNetworkEngine::Recv(void *data, uint32 len) {
	if (!len)
		return true;

	bool success = true;

	while(len) {
		vdsynchronized(mMutex) {
			if (mbRestoreRequired) {
				success = false;
				break;
			}
		}

		sint32 actual = mpSocket->Recv(data, len);

		if (actual < 0) {
			success = false;
			break;
		}

		if (actual) {
			data = (char *)data + actual;
			len -= actual;
		} else {
			mSocketEvent.wait();
		}
	}

	return success;
}

bool ATDeviceCustomNetworkEngine::SetRecvNotifyEnabled() {
	bool hadDataAlready;

	vdsynchronized(mMutex) {
		hadDataAlready = mpSocket && mpSocket->GetSocketStatus().mbCanRead;

		if (!hadDataAlready)
			mbRecvNotifyNeeded = true;
	}
	return !hadDataAlready;
}

void ATDeviceCustomNetworkEngine::Connect() {
	vdsynchronized(mMutex) {
		mSocketStatus = ATSocketStatus();
		mSocketStatus.mbConnecting = true;

		mbRecvNotifyNeeded = false;
		mbInitialNotifyNeeded = true;
	}

	ATSocketAddress addr;
	addr.mType = ATSocketAddressType::IPv4;
	addr.mIPv4Address = 0x7F000001;
	addr.mPort = mPort;

	mpSocket = ATNetConnect(addr);

	if (mpSocket) {
		mpSocket->SetOnEvent(
			nullptr,
			[this, self = vdrefptr(this)](const ATSocketStatus& status) {
				bool notifyRecv = false;

				vdsynchronized(mMutex) {
					mSocketStatus = status;

					if (status.mbClosed || status.mbRemoteClosed)
						mbRestoreRequired = true;
					else if ((status.mbCanRead && mbRecvNotifyNeeded) || (!status.mbConnecting && mbInitialNotifyNeeded)) {
						mbRecvNotifyNeeded = false;
						mbInitialNotifyNeeded = false;
						notifyRecv = true;
					}
				}

				if (notifyRecv)
					mReceiveHandler();

				mSocketEvent.signal();

				if (status.mbClosed || status.mbRemoteClosed)
					QueueReconnect();
			},
			true
		);
	} else {
		vdsynchronized(mMutex) {
			mbConnectionAttempted = true;
			mbRestoreRequired = true;
			mSocketStatus.mbClosed = true;
		}

		QueueReconnect();
	}
}

void ATDeviceCustomNetworkEngine::QueueReconnect() {
	mTimerService.Request(&mReconnectAsyncToken, 1.0f,
		[this] {
			if (mpSocket) {
				bool socketClosed;

				vdsynchronized(mMutex) {
					socketClosed = mSocketStatus.mbClosed || mSocketStatus.mbRemoteClosed;
				}

				if (socketClosed) {
					mpSocket = nullptr;
					mbRestoreRequired = true;
				}
			}

			if (!mpSocket)
				Connect();
		}
	);
}

////////////////////////////////////////////////////////////////////////////

vdrefptr<IATDeviceCustomNetworkEngine> ATCreateDeviceCustomNetworkEngine(uint16 localhostPort, IATTimerService& timerService, vdfunction<void()> recvNotifyFn) {
	vdrefptr<ATDeviceCustomNetworkEngine> p(new ATDeviceCustomNetworkEngine(localhostPort, timerService, std::move(recvNotifyFn)));

	if (!p->Init())
		return nullptr;

	return vdrefptr<IATDeviceCustomNetworkEngine>(p.get());
}
