//	Altirra - Atari 800/800XL/5200 emulator
//	Copyright (C) 2025 Avery Lee
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
#include <at/atcore/audiomixer.h>
#include <at/atcore/deviceport.h>
#include <at/atcore/deviceprinter.h>
#include <at/atcore/deviceserial.h>
#include <at/atcore/logging.h>
#include <at/atui/uicommandmanager.h>
#include "customdevicevmtypes.h"
#include "customdevice.h"
#include "customdevice_win32.h"	// IATDeviceCustomNetworkEngine full type required for vdrefptr<> dtor instantiation; some clang/Android targets are stricter about needing this where Linux GCC accepts the forward decl
#include "memorymanager.h"
#include "simulator.h"
#include "irqcontroller.h"
#include "uiaccessors.h"
#include "uiqueue.h"

extern ATSimulator g_sim;
extern ATLogChannel g_ATLCCustomDev;

////////////////////////////////////////////////////////////////////////////////

const ATVMObjectClass ATDeviceCustomScriptThread::kVMObjectClass {
	"Thread",
	{
		ATVMExternalMethod::Bind<&ATDeviceCustomScriptThread::VMCallIsRunning>("is_running"),
		ATVMExternalMethod::Bind<&ATDeviceCustomScriptThread::VMCallRun>("run"),
		ATVMExternalMethod::Bind<&ATDeviceCustomScriptThread::VMCallInterrupt>("interrupt"),
		ATVMExternalMethod::Bind<&ATDeviceCustomScriptThread::VMCallSleep>("sleep", ATVMFunctionFlags::AsyncAll),
		ATVMExternalMethod::Bind<&ATDeviceCustomScriptThread::VMCallJoin>("join", ATVMFunctionFlags::AsyncAll),
	}
};

sint32 ATDeviceCustom::ScriptThread::VMCallIsRunning() {
	return mVMThread.mbSuspended || mpParent->mVMDomain.mpActiveThread == &mVMThread;
}

void ATDeviceCustom::ScriptThread::VMCallRun(const ATVMFunction *function) {
	if (mpParent->mVMDomain.mpActiveThread == &mVMThread)
		return;

	mVMThread.Abort();
	mVMThread.StartVoid(*function);

	mpParent->ScheduleThread(mVMThread);
}

void ATDeviceCustom::ScriptThread::VMCallInterrupt() {
	if (mpParent->mVMDomain.mpActiveThread == &mVMThread)
		return;

	if (mVMThread.IsStarted()) {
		mVMThread.Abort();

		mpParent->ScheduleThreads(mVMThread.mJoinQueue);
	}
}

void ATDeviceCustom::ScriptThread::VMCallSleep(sint32 cycles, ATVMDomain& domain) {
	if (cycles <= 0)
		return;

	ATDeviceCustom& self = *static_cast<ATDeviceCustomDomain&>(domain).mpParent;
	domain.mpActiveThread->Suspend(&self.mpSleepAbortFn);
	self.mSleepHeap.push_back(ATDeviceCustomSleepInfo { domain.mpActiveThread->mThreadIndex, self.mpScheduler->GetTick64() + (uint32)cycles });
	std::push_heap(self.mSleepHeap.begin(), self.mSleepHeap.end(), ATDeviceCustomSleepInfoPred());

	self.UpdateThreadSleep();
}

void ATDeviceCustom::ScriptThread::VMCallJoin(ATVMDomain& domain) {
	if (mVMThread.IsStarted())
		mVMThread.mJoinQueue.Suspend(*domain.mpActiveThread);
}

////////////////////////////////////////////////////////////////////////////////

const ATVMObjectClass ATDeviceCustomScriptCondition::kVMObjectClass {
	"Condition",
	{
		ATVMExternalMethod::Bind<&ATDeviceCustomScriptCondition::VMCallWait>("wait", ATVMFunctionFlags::AsyncAll),
		ATVMExternalMethod::Bind<&ATDeviceCustomScriptCondition::VMCallSignal>("signal"),
		ATVMExternalMethod::Bind<&ATDeviceCustomScriptCondition::VMCallBroadcast>("broadcast")
	}
};

void ATDeviceCustomScriptCondition::VMCallWait(ATVMDomain& domain) {
	mWaitQueue.Suspend(*domain.mpActiveThread);
}

void ATDeviceCustomScriptCondition::VMCallSignal() {
	mpParent->ScheduleNextThread(mWaitQueue);
}

void ATDeviceCustomScriptCondition::VMCallBroadcast() {
	mpParent->ScheduleThreads(mWaitQueue);
}

////////////////////////////////////////////////////////////////////////////////

const ATVMObjectClass ATDeviceCustomSegment::kVMObjectClass {
	"Segment",
	{
		ATVMExternalMethod::Bind<&ATDeviceCustomSegment::VMCallGetLength>("get_length"),
		ATVMExternalMethod::Bind<&ATDeviceCustomSegment::VMCallClear>("clear"),
		ATVMExternalMethod::Bind<&ATDeviceCustomSegment::VMCallFill>("fill"),
		ATVMExternalMethod::Bind<&ATDeviceCustomSegment::VMCallXorConst>("xor_const"),
		ATVMExternalMethod::Bind<&ATDeviceCustomSegment::VMCallReverseBits>("reverse_bits"),
		ATVMExternalMethod::Bind<&ATDeviceCustomSegment::VMCallTranslate>("translate"),
		ATVMExternalMethod::Bind<&ATDeviceCustomSegment::VMCallCopy>("copy"),
		ATVMExternalMethod::Bind<&ATDeviceCustomSegment::VMCallCopyRect>("copy_rect"),
		ATVMExternalMethod::Bind<&ATDeviceCustomSegment::VMCallReadByte>("read_byte"),
		ATVMExternalMethod::Bind<&ATDeviceCustomSegment::VMCallWriteByte>("write_byte"),
		ATVMExternalMethod::Bind<&ATDeviceCustomSegment::VMCallReadWord>("read_word"),
		ATVMExternalMethod::Bind<&ATDeviceCustomSegment::VMCallWriteWord>("write_word"),
		ATVMExternalMethod::Bind<&ATDeviceCustomSegment::VMCallReadRevWord>("read_rev_word"),
		ATVMExternalMethod::Bind<&ATDeviceCustomSegment::VMCallWriteRevWord>("write_rev_word")
	}
};

sint32 ATDeviceCustomSegment::VMCallGetLength() {
	return mSize;
}

void ATDeviceCustomSegment::VMCallClear(sint32 value) {
	if (mbReadOnly)
		return;

	memset(mpData, value, mSize);
}

void ATDeviceCustomSegment::VMCallFill(sint32 offset, sint32 value, sint32 size) {
	if (offset < 0 || size <= 0 || mbReadOnly)
		return;

	if ((uint32)offset >= mSize || mSize - (uint32)offset < (uint32)size)
		return;

	memset(mpData + offset, value, size);
}

void ATDeviceCustomSegment::VMCallXorConst(sint32 offset, sint32 value, sint32 size) {
	if (offset < 0 || size <= 0 || mbReadOnly)
		return;

	if ((uint32)offset >= mSize || mSize - (uint32)offset < (uint32)size)
		return;

	for(sint32 i = 0; i < size; ++i)
		mpData[i + offset] ^= (uint8)value;
}

void ATDeviceCustomSegment::VMCallReverseBits(sint32 offset, sint32 size) {
	if (offset < 0 || size <= 0 || mbReadOnly)
		return;

	if ((uint32)offset >= mSize || mSize - (uint32)offset < (uint32)size)
		return;

	static constexpr struct RevBitsTable {
		constexpr RevBitsTable()
			: mTable{}
		{
			for(int i=0; i<256; ++i) {
				uint8 v = (uint8)i;

				v = ((v & 0x0F) << 4) + ((v & 0xF0) >> 4);
				v = ((v & 0x33) << 2) + ((v & 0xC0) >> 2);
				v = ((v & 0x55) << 1) + ((v & 0xAA) >> 1);

				mTable[i] = v;
			}
		}

		uint8 mTable[256];
	} kRevBitsTable;

	for(sint32 i = 0; i < size; ++i)
		mpData[i + offset] = kRevBitsTable.mTable[mpData[i + offset]];
}

void ATDeviceCustomSegment::VMCallTranslate(sint32 destOffset, ATDeviceCustomSegment *srcSegment, sint32 srcOffset, sint32 size, ATDeviceCustomSegment *translateTable, sint32 translateOffset) {
	if (destOffset < 0 || srcOffset < 0 || translateOffset < 0 || size <= 0 || mbReadOnly)
		return;

	uint32 udoffset = (uint32)destOffset;
	uint32 usoffset = (uint32)srcOffset;
	uint32 utoffset = (uint32)translateOffset;
	uint32 usize = (uint32)size;

	if (udoffset >= mSize || mSize - udoffset < usize)
		return;

	if (usoffset >= srcSegment->mSize || srcSegment->mSize - usoffset < usize)
		return;

	if (utoffset >= translateTable->mSize || translateTable->mSize - utoffset < 256)
		return;

	// check if translation table overlaps destination (not allowed)
	if (translateTable == this) {
		uint32 delta = udoffset - utoffset;
		uint32 span = usize + 256;

		if (delta < span || delta > (uint32)((uint32)0 - span))
			return;
	}

	// check if source overlaps destination (not allowed unless in-place)
	uint8 *VDRESTRICT dst = mpData + udoffset;
	const uint8 *VDRESTRICT tbl = translateTable->mpData + utoffset;

	if (srcSegment == this) {
		uint32 delta = udoffset - usoffset;

		if (delta && (delta < usize || delta > (uint32)((uint32)0 - usize)))
			return;

		for(uint32 i=0; i<usize; ++i)
			dst[i] = tbl[dst[i]];
	} else {
		const uint8 *VDRESTRICT src = srcSegment->mpData + usoffset;

		for(uint32 i=0; i<usize; ++i)
			dst[i] = tbl[src[i]];
	}
}

void ATDeviceCustomSegment::VMCallCopy(sint32 destOffset, ATDeviceCustomSegment *srcSegment, sint32 srcOffset, sint32 size) {
	if (destOffset < 0 || srcOffset < 0 || size <= 0 || mbReadOnly)
		return;

	uint32 udoffset = (uint32)destOffset;
	uint32 usoffset = (uint32)srcOffset;
	uint32 usize = (uint32)size;

	if (udoffset >= mSize || mSize - udoffset < usize)
		return;

	if (usoffset >= srcSegment->mSize || srcSegment->mSize - usoffset < usize)
		return;

	memmove(mpData + udoffset, srcSegment->mpData + usoffset, usize);
}

void ATDeviceCustomSegment::VMCallCopyRect(sint32 dstOffset, sint32 dstSkip, ATDeviceCustomSegment *srcSegment, sint32 srcOffset, sint32 srcSkip, sint32 width, sint32 height) {
	if (dstOffset < 0
		|| dstSkip < 0
		|| srcOffset < 0
		|| srcSkip < 0
		|| width <= 0
		|| height <= 0)
		return;

	const uint64 dstEnd64 = (uint64)dstOffset + width + ((uint64)dstSkip + width) * (height - 1);
	if (dstEnd64 > mSize)
		return;

	const uint64 srcEnd64 = (uint64)srcOffset + width + ((uint64)srcSkip + width) * (height - 1);
	if (srcEnd64 > srcSegment->mSize)
		return;

	// check if we may be doing an overlapping copy
	if (srcSegment == this) {
		uint32 srcStart = (uint32)srcOffset;
		uint32 srcEnd = (uint32)srcEnd64;
		uint32 dstStart = (uint32)dstOffset;
		uint32 dstEnd = (uint32)dstEnd64;

		if (srcStart < dstEnd && dstStart < srcEnd) {
			// We have an overlapping code, so we can't use the generic rect routine, we have to
			// enforce the copy direction. Prefer ascending copy when possible and use descending
			// copy only when the destination address is higher.
			//
			// Note that we may need to either flip the horizontal or vertical directions. For
			// simplicity, we assume that if the rects overlap that they have the same pitch,
			// and therefore only one of the axes matters -- if there is a vertical scroll there
			// can be no horizontal overlap. Things get weird and generally not useful with
			// mismatched pitches and it's too expensive to check it while still allowing
			// useful cases like multiple surfaces packed into the same pitch.

			uint8 *dst = mpData + dstOffset;
			const uint8 *src = srcSegment->mpData + srcOffset;
			ptrdiff_t dstPitch = width + dstSkip;
			ptrdiff_t srcPitch = width + srcSkip;

			if (dstStart > srcStart) {
				dst += dstPitch * (height - 1);
				src += srcPitch * (height - 1);

				for(;;) {
					memmove(dst, src, width);
					if (!--height)
						break;

					dst -= dstPitch;
					src -= srcPitch;
				}
			} else {
				while(height--) {
					memmove(dst, src, width);
					dst += width + dstSkip;
					src += width + srcSkip;
				}
			}
		}
	}

	// non-overlapping copy
	VDMemcpyRect(mpData + dstOffset, width + dstSkip, srcSegment->mpData + srcOffset, width + srcSkip, width, height);
}

void ATDeviceCustomSegment::VMCallWriteByte(sint32 offset, sint32 value) {
	if (offset >= 0 && (uint32)offset < mSize && !mbReadOnly)
		mpData[offset] = (uint8)value;
}

sint32 ATDeviceCustomSegment::VMCallReadByte(sint32 offset) {
	if (offset >= 0 && (uint32)offset < mSize)
		return mpData[offset];
	else
		return 0;
}

void ATDeviceCustomSegment::VMCallWriteWord(sint32 offset, sint32 value) {
	if (offset >= 0 && (uint32)offset + 1 < mSize && !mbReadOnly)
		VDWriteUnalignedLEU16(&mpData[offset], (uint16)value);
}

sint32 ATDeviceCustomSegment::VMCallReadWord(sint32 offset) {
	if (offset >= 0 && (uint32)offset + 1 < mSize)
		return VDReadUnalignedLEU16(&mpData[offset]);
	else
		return 0;
}

void ATDeviceCustomSegment::VMCallWriteRevWord(sint32 offset, sint32 value) {
	if (offset >= 0 && (uint32)offset + 1 < mSize && !mbReadOnly)
		VDWriteUnalignedBEU16(&mpData[offset], (uint16)value);
}

sint32 ATDeviceCustomSegment::VMCallReadRevWord(sint32 offset) {
	if (offset >= 0 && (uint32)offset + 1 < mSize)
		return VDReadUnalignedBEU16(&mpData[offset]);
	else
		return 0;
}

////////////////////////////////////////////////////////////////////////////////

const ATVMObjectClass ATDeviceCustomMemoryLayer::kVMObjectClass {
	"MemoryLayer",
	{
		ATVMExternalMethod::Bind<&ATDeviceCustomMemoryLayer::VMCallSetOffset>("set_offset"),
		ATVMExternalMethod::Bind<&ATDeviceCustomMemoryLayer::VMCallSetSegmentAndOffset>("set_segment_and_offset"),
		ATVMExternalMethod::Bind<&ATDeviceCustomMemoryLayer::VMCallSetModes>("set_modes"),
		ATVMExternalMethod::Bind<&ATDeviceCustomMemoryLayer::VMCallSetReadOnly>("set_readonly"),
		ATVMExternalMethod::Bind<&ATDeviceCustomMemoryLayer::VMCallSetBaseAddress>("set_base_address"),
	}
};

void ATDeviceCustom::MemoryLayer::VMCallSetOffset(sint32 offset) {
	if (mpPhysLayer && mpSegment) {
		if (!(offset & 0xFF) && offset >= 0 && (uint32)offset <= mMaxOffset) {
			mSegmentOffset = offset;

			if (!mbIsWriteThrough)
				mpParent->mpMemMan->SetLayerMemory(mpPhysLayer, mpSegment->mpData + offset);
		}
	}
}

void ATDeviceCustom::MemoryLayer::VMCallSetSegmentAndOffset(ATDeviceCustomSegment *seg, sint32 offset) {
	if (!seg)
		return;

	if (mpPhysLayer && mpSegment && !(offset & 0xFF) && offset >= 0 && (uint32)offset < seg->mSize && seg->mSize - (uint32)offset >= mSize) {
		mpSegment = seg;
		mMaxOffset = seg->mSize - mSize;

		mSegmentOffset = offset;

		if (!mbIsWriteThrough)
			mpParent->mpMemMan->SetLayerMemory(mpPhysLayer, seg->mpData + offset);
	}
}

void ATDeviceCustom::MemoryLayer::VMCallSetModes(sint32 read, sint32 write) {
	if (!mpPhysLayer)
		return;

	uint32 modes = 0;

	if (write)
		modes |= kATMemoryAccessMode_W;

	if (read)
		modes |= kATMemoryAccessMode_AR;

	if (mEnabledModes != modes) {
		mEnabledModes = modes;

		mpParent->UpdateLayerModes(*this);
	}
}

void ATDeviceCustom::MemoryLayer::VMCallSetReadOnly(sint32 ro) {
	if (mpPhysLayer)
		mpParent->mpMemMan->SetLayerReadOnly(mpPhysLayer, ro != 0);
}

void ATDeviceCustom::MemoryLayer::VMCallSetBaseAddress(sint32 baseAddr) {
	if (!mpPhysLayer || !mpSegment)
		return;

	if (baseAddr < 0 || baseAddr >= 0x10000 || (baseAddr & 0xFF))
		return;

	if ((uint32)baseAddr > 0x10000 - mSize)
		return;

	if (mAddressBase == baseAddr)
		return;

	mAddressBase = baseAddr;

	mpParent->mpMemMan->SetLayerAddressRange(mpPhysLayer, baseAddr >> 8, mSize >> 8);
}

////////////////////////////////////////////////////////////////////////////////

const ATVMObjectClass ATDeviceCustomNetwork::kVMObjectClass {
	"Network",
	{
		ATVMExternalMethod::Bind<&ATDeviceCustomNetwork::VMCallSendMessage>("send_message"),
		ATVMExternalMethod::Bind<&ATDeviceCustomNetwork::VMCallPostMessage>("post_message"),
	}
};

sint32 ATDeviceCustom::Network::VMCallSendMessage(sint32 param1, sint32 param2) {
	return mpParent->SendNetCommand(param1, param2, ATDeviceCustomNetworkCommand::ScriptEventSend);
}

sint32 ATDeviceCustom::Network::VMCallPostMessage(sint32 param1, sint32 param2) {
	return mpParent->PostNetCommand(param1, param2, ATDeviceCustomNetworkCommand::ScriptEventPost);
}

////////////////////////////////////////////////////////////////////////////////

const ATVMObjectClass ATDeviceCustomSIO::kVMObjectClass {
	"SIO",
	{
		ATVMExternalMethod::Bind<&ATDeviceCustomSIO::VMCallAck>("ack"),
		ATVMExternalMethod::Bind<&ATDeviceCustomSIO::VMCallNak>("nak"),
		ATVMExternalMethod::Bind<&ATDeviceCustomSIO::VMCallError>("error"),
		ATVMExternalMethod::Bind<&ATDeviceCustomSIO::VMCallComplete>("complete"),
		ATVMExternalMethod::Bind<&ATDeviceCustomSIO::VMCallSendFrame>("send_frame", ATVMFunctionFlags::AsyncSIO),
		ATVMExternalMethod::Bind<&ATDeviceCustomSIO::VMCallRecvFrame>("recv_frame", ATVMFunctionFlags::AsyncSIO),
		ATVMExternalMethod::Bind<&ATDeviceCustomSIO::VMCallDelay>("delay", ATVMFunctionFlags::AsyncSIO),
		ATVMExternalMethod::Bind<&ATDeviceCustomSIO::VMCallEnableRaw>("enable_raw"),
		ATVMExternalMethod::Bind<&ATDeviceCustomSIO::VMCallSetProceed>("set_proceed"),
		ATVMExternalMethod::Bind<&ATDeviceCustomSIO::VMCallSetInterrupt>("set_interrupt"),
		ATVMExternalMethod::Bind<&ATDeviceCustomSIO::VMCallCommandAsserted>("command_asserted"),
		ATVMExternalMethod::Bind<&ATDeviceCustomSIO::VMCallMotorAsserted>("motor_asserted"),
		ATVMExternalMethod::Bind<&ATDeviceCustomSIO::VMCallSendRawByte>("send_raw_byte", ATVMFunctionFlags::AsyncRawSIO),
		ATVMExternalMethod::Bind<&ATDeviceCustomSIO::VMCallRecvRawByte>("recv_raw_byte", ATVMFunctionFlags::AsyncRawSIO),
		ATVMExternalMethod::Bind<&ATDeviceCustomSIO::VMCallWaitCommand>("wait_command", ATVMFunctionFlags::AsyncRawSIO),
		ATVMExternalMethod::Bind<&ATDeviceCustomSIO::VMCallWaitCommandOff>("wait_command_off", ATVMFunctionFlags::AsyncRawSIO),
		ATVMExternalMethod::Bind<&ATDeviceCustomSIO::VMCallWaitMotorChanged>("wait_motor_changed", ATVMFunctionFlags::AsyncRawSIO),
		ATVMExternalMethod::Bind<&ATDeviceCustomSIO::VMCallResetRecvChecksum>("reset_recv_checksum"),
		ATVMExternalMethod::Bind<&ATDeviceCustomSIO::VMCallResetSendChecksum>("reset_send_checksum"),
		ATVMExternalMethod::Bind<&ATDeviceCustomSIO::VMCallGetRecvChecksum>("get_recv_checksum"),
		ATVMExternalMethod::Bind<&ATDeviceCustomSIO::VMCallCheckRecvChecksum>("check_recv_checksum"),
		ATVMExternalMethod::Bind<&ATDeviceCustomSIO::VMCallGetSendChecksum>("get_send_checksum"),
		ATVMExternalMethod::Bind<&ATDeviceCustomSIO::VMCallSetExternalClock>("set_external_clock"),
	}
};

void ATDeviceCustom::SIO::Reset() {
	mSendChecksum = 0;
	mRecvChecksum = 0;
	mRecvLast = 0;
}

void ATDeviceCustom::SIO::VMCallAck() {
	if (!mbValid)
		return;

	if (mbDataFrameReceived) {
		mbDataFrameReceived = false;

		// enforce data-to-ACK delay (850us minimum)
		mpParent->mpSIOInterface->Delay(1530);
	}

	mpParent->mpSIOInterface->SendACK();
}

void ATDeviceCustom::SIO::VMCallNak() {
	if (!mbValid)
		return;

	if (mbDataFrameReceived) {
		mbDataFrameReceived = false;

		// enforce data-to-NAK delay (850us minimum)
		mpParent->mpSIOInterface->Delay(1530);
	}

	mpParent->mpSIOInterface->SendNAK();
}

void ATDeviceCustom::SIO::VMCallError() {
	if (!mbValid)
		return;

	mpParent->mpSIOInterface->SendError();
}

void ATDeviceCustom::SIO::VMCallComplete() {
	if (!mbValid)
		return;

	mpParent->mpSIOInterface->SendComplete();
}

void ATDeviceCustom::SIO::VMCallSendFrame(ATDeviceCustomSegment *seg, sint32 offset, sint32 length) {
	if (!mbValid)
		return;

	if (!seg || offset < 0 || length <= 0 || length > 8192)
		return;

	uint32 uoffset = (uint32)offset;
	uint32 ulength = (uint32)length;
	if (uoffset > seg->mSize || seg->mSize - uoffset < ulength)
		return;

	mpParent->mpSIOInterface->SendData(seg->mpData + offset, ulength, true);
	mpParent->mpSIOInterface->InsertFence((uint32)ATDeviceCustomSerialFenceId::ScriptDelay);
	mpParent->mVMThreadSIO.Suspend();
}

void ATDeviceCustom::SIO::VMCallRecvFrame(sint32 length) {
	if (!mbValid)
		return;

	if (length <= 0 || length > 8192)
		return;

	mbDataFrameReceived = true;
	mpParent->mpSIOInterface->ReceiveData((uint32)ATDeviceCustomSerialFenceId::ScriptReceive, (uint32)length, true);
	mpParent->mVMThreadSIO.Suspend();
}

void ATDeviceCustom::SIO::VMCallDelay(sint32 cycles) {
	if (!mbValid)
		return;

	if (cycles <= 0 || cycles > 64*1024*1024)
		return;

	mpParent->mpSIOInterface->Delay((uint32)cycles);
	mpParent->mpSIOInterface->InsertFence((uint32)ATDeviceCustomSerialFenceId::ScriptDelay);
	mpParent->mVMThreadSIO.Suspend();
}

void ATDeviceCustom::SIO::VMCallEnableRaw(sint32 enable0) {
	if (!mpParent->mbInitedSIO)
		return;

	const bool enable = (enable0 != 0);

	if (mpParent->mbInitedRawSIO != enable) {
		mpParent->mbInitedRawSIO = enable;

		if (enable)
			mpParent->mpSIOMgr->AddRawDevice(mpParent);
		else
			mpParent->mpSIOMgr->RemoveRawDevice(mpParent);
	}
}

void ATDeviceCustom::SIO::VMCallSetProceed(sint32 asserted) {
	if (!mpParent->mbInitedRawSIO)
		return;

	mpParent->mpSIOMgr->SetSIOProceed(mpParent, asserted != 0);
}

void ATDeviceCustom::SIO::VMCallSetInterrupt(sint32 asserted) {
	if (!mpParent->mbInitedRawSIO)
		return;

	mpParent->mpSIOMgr->SetSIOInterrupt(mpParent, asserted != 0);
}

sint32 ATDeviceCustom::SIO::VMCallCommandAsserted() {
	if (!mpParent->mbInitedSIO)
		return false;

	return mpParent->mpSIOMgr->IsSIOCommandAsserted();
}

sint32 ATDeviceCustom::SIO::VMCallMotorAsserted() {
	if (!mpParent->mbInitedSIO)
		return false;

	return mpParent->mpSIOMgr->IsSIOMotorAsserted();
}

void ATDeviceCustom::SIO::VMCallSendRawByte(sint32 c, sint32 cyclesPerBit, ATVMDomain& domain) {
	if (!mpParent->mbInitedRawSIO)
		return;

	if (cyclesPerBit < 4 || cyclesPerBit > 100000)
		return;

	ATDeviceCustom& self = *static_cast<ATDeviceCustomDomain&>(domain).mpParent;
	domain.mpActiveThread->Suspend(&self.mpRawSendAbortFn);

	const uint32 threadIndex = domain.mpActiveThread->mThreadIndex;

	mpParent->mRawSendQueue.push_back(ATDeviceCustomRawSendInfo { (sint32)threadIndex, (uint32)cyclesPerBit, (uint8)c });

	if (!mpParent->mpEventRawSend)
		mpParent->SendNextRawByte();
}

sint32 ATDeviceCustom::SIO::VMCallRecvRawByte(ATVMDomain& domain) {
	if (!mpParent->mbInitedRawSIO)
		return 0;

	mpParent->mVMThreadRawRecvQueue.Suspend(*domain.mpActiveThread);
	return 0;
}

sint32 ATDeviceCustom::SIO::VMCallWaitCommand(ATVMDomain& domain) {
	if (!mpParent->mbInitedRawSIO)
		return 0;

	mpParent->mVMThreadSIOCommandAssertQueue.Suspend(*domain.mpActiveThread);

	return 0;
}

sint32 ATDeviceCustom::SIO::VMCallWaitCommandOff(ATVMDomain& domain) {
	if (!mpParent->mbInitedRawSIO)
		return 0;

	if (mpParent->mpSIOMgr->IsSIOCommandAsserted())
		mpParent->mVMThreadSIOCommandOffQueue.Suspend(*domain.mpActiveThread);

	return 0;
}

sint32 ATDeviceCustom::SIO::VMCallWaitMotorChanged(ATVMDomain& domain) {
	if (!mpParent->mbInitedRawSIO)
		return 0;

	mpParent->mVMThreadSIOMotorChangedQueue.Suspend(*domain.mpActiveThread);

	return 0;
}

void ATDeviceCustom::SIO::VMCallResetRecvChecksum() {
	mRecvChecksum = 0;
	mRecvLast = 0;
}

void ATDeviceCustom::SIO::VMCallResetSendChecksum() {
	mSendChecksum = 0;
}

sint32 ATDeviceCustom::SIO::VMCallGetRecvChecksum() {
	return mRecvChecksum ? (mRecvChecksum - 1) % 255 + 1 : 0;
}

sint32 ATDeviceCustom::SIO::VMCallCheckRecvChecksum() {
	uint8 computedChk = mRecvChecksum - mRecvLast ? (uint8)((mRecvChecksum - mRecvLast - 1) % 255 + 1) : (uint8)0;

	return mRecvLast == computedChk;
}

sint32 ATDeviceCustom::SIO::VMCallGetSendChecksum() {
	return mSendChecksum ? (mSendChecksum - 1) % 255 + 1 : 0;
}

void ATDeviceCustom::SIO::VMCallSetExternalClock(sint32 initialOffset, sint32 period) {
	if (!mpParent->mbInitedRawSIO)
		return;

	if (period <= 0 || period > 100000) {
		mpParent->mpSIOMgr->SetExternalClock(mpParent, 0, 0);
	} else {
		initialOffset %= period;

		if (initialOffset < 0)
			initialOffset += period;

		mpParent->mpSIOMgr->SetExternalClock(mpParent, initialOffset, period);
	}
}

/////////////////////////////////////////////////////////////////////////////

const ATVMObjectClass ATDeviceCustomSIODevice::kVMObjectClass {
	"SIODevice",
	{}
};

/////////////////////////////////////////////////////////////////////////////

const ATVMObjectClass ATDeviceCustomPBIDevice::kVMObjectClass {
	"PBIDevice",
	{
		ATVMExternalMethod::Bind<&ATDeviceCustomPBIDevice::VMCallAssertIrq>("assert_irq"),
		ATVMExternalMethod::Bind<&ATDeviceCustomPBIDevice::VMCallNegateIrq>("negate_irq"),
	}
};

void ATDeviceCustomPBIDevice::VMCallAssertIrq() {
	if (mParent.mpPBIDeviceIrqController && mParent.mPBIDeviceIrqBit && !mParent.mbPBIDeviceIrqAsserted) {
		mParent.mbPBIDeviceIrqAsserted = true;
		mParent.mpPBIDeviceIrqController->Assert(mParent.mPBIDeviceIrqBit, false);
	}
}

void ATDeviceCustomPBIDevice::VMCallNegateIrq() {
	if (mParent.mpPBIDeviceIrqController && mParent.mPBIDeviceIrqBit && mParent.mbPBIDeviceIrqAsserted) {
		mParent.mbPBIDeviceIrqAsserted = false;
		mParent.mpPBIDeviceIrqController->Negate(mParent.mPBIDeviceIrqBit, false);
	}
}

////////////////////////////////////////////////////////////////////////////////

const ATVMObjectClass ATDeviceCustomClock::kVMObjectClass {
	"Clock",
	{
		ATVMExternalMethod::Bind<&ATDeviceCustomClock::VMCallCaptureLocalTime>("capture_local_time"),
		ATVMExternalMethod::Bind<&ATDeviceCustomClock::VMCallLocalYear>("local_year"),
		ATVMExternalMethod::Bind<&ATDeviceCustomClock::VMCallLocalMonth>("local_month"),
		ATVMExternalMethod::Bind<&ATDeviceCustomClock::VMCallLocalDay>("local_day"),
		ATVMExternalMethod::Bind<&ATDeviceCustomClock::VMCallLocalDayOfWeek>("local_day_of_week"),
		ATVMExternalMethod::Bind<&ATDeviceCustomClock::VMCallLocalHour>("local_hour"),
		ATVMExternalMethod::Bind<&ATDeviceCustomClock::VMCallLocalMinute>("local_minute"),
		ATVMExternalMethod::Bind<&ATDeviceCustomClock::VMCallLocalSecond>("local_second"),
	}
};

void ATDeviceCustomClock::Reset() {
	mLocalTimeCaptureTimestamp = 0;
	mLocalTime = {};
}

void ATDeviceCustomClock::VMCallCaptureLocalTime() {
	uint64 t = mpParent->mpScheduler->GetTick64();

	if (mLocalTimeCaptureTimestamp != t) {
		mLocalTimeCaptureTimestamp = t;

		mLocalTime = VDGetLocalDate(VDGetCurrentDate());
	}
}

sint32 ATDeviceCustomClock::VMCallLocalYear() {
	return mLocalTime.mYear;
}

sint32 ATDeviceCustomClock::VMCallLocalMonth() {
	return mLocalTime.mMonth;
}

sint32 ATDeviceCustomClock::VMCallLocalDay() {
	return mLocalTime.mDay;
}

sint32 ATDeviceCustomClock::VMCallLocalDayOfWeek() {
	return mLocalTime.mDayOfWeek;
}

sint32 ATDeviceCustomClock::VMCallLocalHour() {
	return mLocalTime.mHour;
}

sint32 ATDeviceCustomClock::VMCallLocalMinute() {
	return mLocalTime.mMinute;
}

sint32 ATDeviceCustomClock::VMCallLocalSecond() {
	return mLocalTime.mSecond;
}

////////////////////////////////////////////////////////////////////////////////

const ATVMObjectClass ATDeviceCustomConsole::kVMObjectClass {
	"Console",
	{
		ATVMExternalMethod::Bind<&ATDeviceCustomConsole::VMCallSetConsoleButtonState>("set_console_button_state"),
		ATVMExternalMethod::Bind<&ATDeviceCustomConsole::VMCallSetKeyState>("set_key_state"),
		ATVMExternalMethod::Bind<&ATDeviceCustomConsole::VMCallPushBreak>("push_break"),
	}
};

void ATDeviceCustomConsole::VMCallSetConsoleButtonState(sint32 button, sint32 state) {
	if (button != 1 && button != 2 && button != 4)
		return;

	g_sim.GetGTIA().SetConsoleSwitch((uint8)button, state != 0);
}

void ATDeviceCustomConsole::VMCallSetKeyState(sint32 key, sint32 state) {
	if (key != (uint8)key)
		return;

	auto& pokey = g_sim.GetPokey();

	if (state)
		pokey.PushRawKey((uint8)key, false);
	else
		pokey.ReleaseRawKey((uint8)key, false);
}

void ATDeviceCustomConsole::VMCallPushBreak() {
	auto& pokey = g_sim.GetPokey();

	pokey.PushBreak();
}

////////////////////////////////////////////////////////////////////////////////

const ATVMObjectClass ATDeviceCustomControllerPort::kVMObjectClass {
	"ControllerPort",
	{
		ATVMExternalMethod::Bind<&ATDeviceCustomControllerPort::VMCallSetPaddleA>("set_paddle_a"),
		ATVMExternalMethod::Bind<&ATDeviceCustomControllerPort::VMCallSetPaddleB>("set_paddle_b"),
		ATVMExternalMethod::Bind<&ATDeviceCustomControllerPort::VMCallSetTrigger>("set_trigger"),
		ATVMExternalMethod::Bind<&ATDeviceCustomControllerPort::VMCallSetDirs>("set_dirs"),
	}
};

void ATDeviceCustomControllerPort::Init() {
}

void ATDeviceCustomControllerPort::Shutdown() {
	mpControllerPort = nullptr;
}

void ATDeviceCustomControllerPort::Enable() {
	mpControllerPort->SetEnabled(true);
}

void ATDeviceCustomControllerPort::Disable() {
	mpControllerPort->SetEnabled(false);
}

void ATDeviceCustomControllerPort::ResetPotPositions() {
	mpControllerPort->ResetPotPosition(false);
	mpControllerPort->ResetPotPosition(true);
}

void ATDeviceCustomControllerPort::VMCallSetPaddleA(sint32 pos) {
	mpControllerPort->SetPotPosition(false, pos);
}

void ATDeviceCustomControllerPort::VMCallSetPaddleB(sint32 pos) {
	mpControllerPort->SetPotPosition(true, pos);
}

void ATDeviceCustomControllerPort::VMCallSetTrigger(sint32 asserted) {
	mpControllerPort->SetTriggerDown(asserted);
}

void ATDeviceCustomControllerPort::VMCallSetDirs(sint32 mask) {
	mpControllerPort->SetDirInput(mask & 15);
}

/////////////////////////////////////////////////////////////////////////////

const ATVMObjectClass ATDeviceCustomDebug::kVMObjectClass {
	"Debug",
	{
		ATVMExternalMethod::Bind<&ATDeviceCustomDebug::VMCallLog>("log"),
		ATVMExternalMethod::Bind<&ATDeviceCustomDebug::VMCallLogInt>("log_int"),
	}
};

void ATDeviceCustomDebug::VMCallLog(const char *str) {
	VDStringA s;
	s = str;
	s += '\n';
	g_ATLCCustomDev <<= s.c_str();
}

void ATDeviceCustomDebug::VMCallLogInt(const char *str, sint32 v) {
	VDStringA s;
	s = str;
	s.append_sprintf("%d\n", v);
	g_ATLCCustomDev <<= s.c_str();
}

/////////////////////////////////////////////////////////////////////////////

const ATVMObjectClass ATDeviceCustomImage::kVMObjectClass {
	"Image",
	{
		ATVMExternalMethod::Bind<&ATDeviceCustomImage::VMCallClear>("clear"),
		ATVMExternalMethod::Bind<&ATDeviceCustomImage::VMCallGetPixel>("get_pixel"),
		ATVMExternalMethod::Bind<&ATDeviceCustomImage::VMCallPutPixel>("put_pixel"),
		ATVMExternalMethod::Bind<&ATDeviceCustomImage::VMCallFillRect>("fill_rect"),
		ATVMExternalMethod::Bind<&ATDeviceCustomImage::VMCallInvertRect>("invert_rect"),
		ATVMExternalMethod::Bind<&ATDeviceCustomImage::VMCallBlt>("blt"),
		ATVMExternalMethod::Bind<&ATDeviceCustomImage::VMCallBltExpand1>("blt_expand_1"),
		ATVMExternalMethod::Bind<&ATDeviceCustomImage::VMCallBltTileMap>("blt_tile_map"),
	}
};

void ATDeviceCustomImage::Init(sint32 w, sint32 h) {
	mPixmapBuffer.init(w, h, nsVDPixmap::kPixFormat_XRGB8888);
	VDMemset32(mPixmapBuffer.base(), 0xFFFF00FF, mPixmapBuffer.size() >> 2);
}

void ATDeviceCustomImage::VMCallClear(sint32 color) {
	VDMemset32(mPixmapBuffer.base(), (uint32)color | 0xFF000000, mPixmapBuffer.size() >> 2);
	mChangeCounter |= 1;
}

int ATDeviceCustomImage::VMCallGetPixel(sint32 x, sint32 y) {
	if (x < 0 || y < 0 || x >= mPixmapBuffer.w || y >= mPixmapBuffer.h)
		return 0;

	return ((const uint32 *)((const char *)mPixmapBuffer.data + mPixmapBuffer.pitch * y))[x] & 0xFFFFFF;
}

void ATDeviceCustomImage::VMCallPutPixel(sint32 x, sint32 y, sint32 color) {
	if (x < 0 || y < 0 || x >= mPixmapBuffer.w || y >= mPixmapBuffer.h)
		return;

	((uint32 *)((char *)mPixmapBuffer.data + mPixmapBuffer.pitch * y))[x] = (uint32)color | 0xFF000000;
	mChangeCounter |= 1;
}

void ATDeviceCustomImage::VMCallFillRect(sint32 x, sint32 y, sint32 w, sint32 h, sint32 color) {
	sint32 x1 = std::max<sint32>(x, 0);
	sint32 y1 = std::max<sint32>(y, 0);
	sint32 x2 = std::min<sint32>(x+w, mPixmapBuffer.w);
	sint32 y2 = std::min<sint32>(y+h, mPixmapBuffer.h);
	sint32 dx = x2 - x1;
	sint32 dy = y2 - y1;

	if (dx <= 0 || dy <= 0)
		return;

	VDMemset32Rect((char *)mPixmapBuffer.data + mPixmapBuffer.pitch * y1 + x1*4, mPixmapBuffer.pitch, (uint32)color | 0xFF000000, dx, dy);
	mChangeCounter |= 1;
}

void ATDeviceCustomImage::VMCallInvertRect(sint32 x, sint32 y, sint32 w, sint32 h) {
	sint32 x1 = std::max<sint32>(x, 0);
	sint32 y1 = std::max<sint32>(y, 0);
	sint32 x2 = std::min<sint32>(x+w, mPixmapBuffer.w);
	sint32 y2 = std::min<sint32>(y+h, mPixmapBuffer.h);
	sint32 dx = x2 - x1;
	sint32 dy = y2 - y1;

	if (dx <= 0 || dy <= 0)
		return;

	char *dst = (char *)mPixmapBuffer.data + mPixmapBuffer.pitch * y1 + x1*4;

	do {
		uint32 *dst2 = (uint32 *)dst;

		for(sint32 x=0; x<w; ++x)
			dst2[x] ^= 0xFFFFFF;

		dst += mPixmapBuffer.pitch;
	} while(--dy);

	mChangeCounter |= 1;
}

void ATDeviceCustomImage::VMCallBlt(sint32 dx, sint32 dy, ATDeviceCustomImage *src, sint32 sx, sint32 sy, sint32 w, sint32 h) {
	const sint32 dw = mPixmapBuffer.w;
	const sint32 dh = mPixmapBuffer.h;
	const sint32 sw = src->mPixmapBuffer.w;
	const sint32 sh = src->mPixmapBuffer.h;

	// trivial reject -- this also ensures that we have enough headroom for partial
	// clipping calcs to prevent overflows
	if (w <= 0 || h <= 0)
		return;

	if (std::min(dx, sx) <= -w || std::min(dy, sy) <= -h)
		return;

	if (dx >= dw || dy >= dh || sx >= sw || sy >+ sh)
		return;

	// right/bottom clip
	w = std::min(std::min(w, sw - sx), dw - dx);
	h = std::min(std::min(h, sh - sy), dh - dy);

	// top/left clip
	sint32 xoff = std::min(0, std::min(dx, sx));
	sint32 yoff = std::min(0, std::min(dy, sy));

	sx -= xoff;
	sy -= yoff;
	dx -= xoff;
	dy -= yoff;
	w -= xoff;
	h -= yoff;

	if (w <= 0 || h <= 0)
		return;

	char *dstBits = (char *)mPixmapBuffer.data + mPixmapBuffer.pitch * dy + 4 * dx;
	const char *srcBits = (char *)src->mPixmapBuffer.data + src->mPixmapBuffer.pitch * sy + 4 * sx;
	size_t bpr = 4 * w;

	// check if we are doing a self-copy and need to do a memmove
	if (src == this && (abs(sx - dx) < w || abs(sy - dy) < h)) {
		// Check if we need to do a descending copy vertically; otherwise, use ascending copy vertically.
		// Let memmove() handle the horizontal as it can do platform-specific optimizations.
		ptrdiff_t dstStep = mPixmapBuffer.pitch;
		ptrdiff_t srcStep = src->mPixmapBuffer.pitch;
		if (dy > sy) {
			srcBits += srcStep * (h - 1);
			dstBits += dstStep * (h - 1);
			srcStep = -srcStep;
			dstStep = -dstStep;
		}

		do {
			memmove(dstBits, srcBits, bpr);
			dstBits += dstStep;
			srcBits += srcStep;
		} while(--h);
	} else {
		// finally, safe to blit
		VDMemcpyRect(
			dstBits,
			mPixmapBuffer.pitch,
			srcBits,
			src->mPixmapBuffer.pitch,
			4 * w,
			h);
	}

	mChangeCounter |= 1;
}

void ATDeviceCustomImage::VMCallBltExpand1(sint32 dx, sint32 dy, ATDeviceCustomSegment *srcSegment, sint32 srcOffset, sint32 srcPitch, sint32 w, sint32 h, sint32 c0, sint32 c1) {
	const sint32 dw = mPixmapBuffer.w;
	const sint32 dh = mPixmapBuffer.h;

	// trivial reject -- this also ensures that we have enough headroom for partial
	// clipping calcs to prevent overflows
	if (w <= 0 || h <= 0)
		return;

	// check for destination clipping
	if ((dx | dy) < 0)
		return;

	if (dx >= dw || dy >= dh)
		return;

	if (dw - dx < w || dh - dy < h)
		return;

	// check for source clipping
	const sint32 rowBytes = (w + 7) >> 3;
	const sint32 srcSize = srcSegment->mSize;

	if (srcOffset < 0 || srcOffset >= srcSize)
		return;

	if (srcSize - srcOffset < rowBytes)
		return;

	if (h) {
		sint64 srcLastLineOffset = srcOffset + (sint64)srcPitch * (h - 1);
		if (srcPitch < 0) {
			if (srcLastLineOffset < 0)
				return;
		} else if (srcPitch > 0) {
			if (srcLastLineOffset > srcSize - rowBytes)
				return;
		}
	}

	// finally, safe to blit
	uint32 *dst = (uint32 *)((char *)mPixmapBuffer.data + mPixmapBuffer.pitch * dy + 4 * dx);
	const uint8 *src = srcSegment->mpData + srcOffset;

	do {
		uint32 *dst2 = dst;
		const uint8 *src2 = src;
		uint8 bit = 0x80;

		for (sint32 x=0; x<w; ++x) {
			dst2[x] = (*src2 & bit) ? c1 : c0;

			bit >>= 1;
			if (bit == 0) {
				bit = 0x01;
				++src2;
			}
		}

		dst = (uint32 *)((char *)dst + mPixmapBuffer.pitch);
		src += srcPitch;
	} while(--h);

	mChangeCounter |= 1;
}

void ATDeviceCustomImage::VMCallBltTileMap(
	sint32 x,
	sint32 y,
	ATDeviceCustomImage *imageSrc,
	sint32 tileWidth,
	sint32 tileHeight,
	ATDeviceCustomSegment *tileSrc,
	sint32 tileMapOffset,
	sint32 tileMapSkip,
	sint32 tileMapWidth,
	sint32 tileMapHeight
) {
	if (tileWidth <= 0 || tileHeight <= 0)
		return;

	if (tileMapWidth <= 0 || tileMapHeight <= 0)
		return;

	if (x < 0 || y < 0)
		return;

	if (x >= mPixmapBuffer.w || y >= mPixmapBuffer.h)
		return;

	if (x + (uint64)tileWidth * tileMapWidth > mPixmapBuffer.w
		|| y + (uint64)tileHeight * tileMapHeight > mPixmapBuffer.h)
		return;

	if (imageSrc == this)
		return;

	if (imageSrc->mPixmapBuffer.w < tileWidth || imageSrc->mPixmapBuffer.h < tileHeight * 256)
		return;

	// bounds-check tilemap
	if (tileMapOffset < 0 || tileMapSkip < 0)
		return;

	if ((uint32)tileMapOffset >= tileSrc->mSize)
		return;

	if (tileMapHeight > 1 && (uint32)tileMapSkip >= tileSrc->mSize)
		return;

	if ((uint64)(uint32)tileMapOffset + (uint32)tileMapWidth - 1 + (uint64)(uint32)(tileMapSkip + tileMapWidth) * (uint32)(tileMapHeight - 1) >= tileSrc->mSize)
		return;

	// blit
	const uint8 *tileMapRow = tileSrc->mpData + tileMapOffset;
	uint8 *dstTileRow = (uint8 *)mPixmapBuffer.data + mPixmapBuffer.pitch * y + 4 * x;

	for(sint32 ytile = 0; ytile < tileMapHeight; ++ytile) {
		uint8 *dstTile = dstTileRow;
		dstTileRow += mPixmapBuffer.pitch * tileHeight;

		for(sint32 xtile = 0; xtile < tileMapWidth; ++xtile) {
			const uint8 tile = tileMapRow[xtile];

			VDMemcpyRect(dstTile, mPixmapBuffer.pitch, (char *)imageSrc->mPixmapBuffer.data + imageSrc->mPixmapBuffer.pitch * tileHeight * (size_t)tile, imageSrc->mPixmapBuffer.pitch, tileWidth*4, tileHeight);
			dstTile += tileWidth * 4;
		}

		tileMapRow += tileMapWidth + tileMapSkip;
	}

	mChangeCounter |= 1;
}

/////////////////////////////////////////////////////////////////////////////

const ATVMObjectClass ATDeviceCustomVideoOutput::kVMObjectClass {
	"VideoOutput",
	{
		ATVMExternalMethod::Bind<&ATDeviceCustomVideoOutput::VMCallSetPAR>("set_par"),
		ATVMExternalMethod::Bind<&ATDeviceCustomVideoOutput::VMCallSetImage>("set_image"),
		ATVMExternalMethod::Bind<&ATDeviceCustomVideoOutput::VMCallMarkActive>("mark_active"),
		ATVMExternalMethod::Bind<&ATDeviceCustomVideoOutput::VMCallSetPassThrough>("set_pass_through"),
		ATVMExternalMethod::Bind<&ATDeviceCustomVideoOutput::VMCallSetTextArea>("set_text_area"),
		ATVMExternalMethod::Bind<&ATDeviceCustomVideoOutput::VMCallSetCopyTextSource>("set_copy_text_source"),
	}
};

ATDeviceCustomVideoOutput::ATDeviceCustomVideoOutput(ATDeviceCustom *parent, IATDeviceVideoManager *vidMgr, const char *tag, const wchar_t *displayName) {
	mpParent = parent;
	mpVideoMgr = vidMgr;
	mTag = tag;
	mDisplayName = displayName;

	static constexpr uint32 kBlackPixel = 0;
	mFrameBuffer.data = (void *)&kBlackPixel;
	mFrameBuffer.pitch = sizeof(uint32);
	mFrameBuffer.w = 1;
	mFrameBuffer.h = 1;
	mFrameBuffer.format = nsVDPixmap::kPixFormat_XRGB8888;

	mVideoInfo.mbSignalValid = false;
	mVideoInfo.mbSignalPassThrough = false;
	mVideoInfo.mHorizScanRate = 15735.0f;
	mVideoInfo.mVertScanRate = 60.0f;
	mVideoInfo.mFrameBufferLayoutChangeCount = 0;
	mVideoInfo.mFrameBufferChangeCount = 0;
	mVideoInfo.mTextRows = 0;
	mVideoInfo.mTextColumns = 0;
	mVideoInfo.mPixelAspectRatio = 1.0;
	mVideoInfo.mDisplayArea = vdrect32(0, 0, 1, 1);
	mVideoInfo.mBorderColor = 0;
	mVideoInfo.mbForceExactPixels = false;

	vidMgr->AddVideoOutput(this);
}

ATDeviceCustomVideoOutput::~ATDeviceCustomVideoOutput() {
	mpVideoMgr->RemoveVideoOutput(this);
}

void ATDeviceCustomVideoOutput::VMCallSetPAR(sint32 numerator, sint32 denominator) {
	if (numerator > 0 && denominator > 0)
		mVideoInfo.mPixelAspectRatio = std::clamp((double)numerator / (double)denominator, 0.01, 100.0);
}

void ATDeviceCustomVideoOutput::VMCallSetImage(ATDeviceCustomImage *image) {
	mpFrameBufferImage = image;
	mFrameBuffer = image->mPixmapBuffer;
	++mVideoInfo.mFrameBufferLayoutChangeCount;
	mVideoInfo.mbSignalValid = true;
	mVideoInfo.mDisplayArea = vdrect32(0, 0, mFrameBuffer.w, mFrameBuffer.h);
}

void ATDeviceCustomVideoOutput::VMCallMarkActive() {
	mActivityCounter |= 1;
}

void ATDeviceCustomVideoOutput::VMCallSetPassThrough(sint32 enabled) {
	mVideoInfo.mbSignalPassThrough = enabled != 0;
}

void ATDeviceCustomVideoOutput::VMCallSetTextArea(sint32 cols, sint32 rows) {
	if (cols > 0 && rows > 0) {
		cols = std::min<sint32>(cols, 2048);
		rows = std::min<sint32>(rows, 2048);

		mVideoInfo.mTextRows = rows;
		mVideoInfo.mTextColumns = cols;
	} else {
		mVideoInfo.mTextRows = 0;
		mVideoInfo.mTextColumns = 0;
	}
}

void ATDeviceCustomVideoOutput::VMCallSetCopyTextSource(ATDeviceCustomSegment *seg, sint32 offset, sint32 skip) {
	if (offset >= 0 && (uint32)offset < seg->mSize && skip >= 0) {
		mpCopySource = seg;
		mCopyOffset = offset;
		mCopySkip = skip;
	}
}

const char *ATDeviceCustomVideoOutput::GetName() const {
	return mTag.c_str();
}

const wchar_t *ATDeviceCustomVideoOutput::GetDisplayName() const {
	return mDisplayName.c_str();
}

void ATDeviceCustomVideoOutput::Tick(uint32 hz300ticks) {
}

void ATDeviceCustomVideoOutput::UpdateFrame() {
	if (mpOnComposite) {
		mpParent->mVMThread.mThreadVariables[(int)ATDeviceCustom::ThreadVarIndex::Timestamp] = ATSCHEDULER_GETTIME(mpParent->mpScheduler);
		mpParent->mVMThread.RunVoid(*mpOnComposite);
	}
}

const VDPixmap& ATDeviceCustomVideoOutput::GetFrameBuffer() {
	return mFrameBuffer;
}

const ATDeviceVideoInfo& ATDeviceCustomVideoOutput::GetVideoInfo() {
	mVideoInfo.mFrameBufferChangeCount = mVideoInfo.mFrameBufferLayoutChangeCount;

	if (mpFrameBufferImage) {
		mVideoInfo.mFrameBufferChangeCount += mpFrameBufferImage->mChangeCounter;
		if (mpFrameBufferImage->mChangeCounter & 1)
			++mpFrameBufferImage->mChangeCounter;
	}

	return mVideoInfo;
}

vdpoint32 ATDeviceCustomVideoOutput::PixelToCaretPos(const vdpoint32& pixelPos) {
	if (mVideoInfo.mDisplayArea.empty())
		return {};

	sint32 x = VDRoundToInt32(((double)(pixelPos.x + 0.5) - mVideoInfo.mDisplayArea.left) * (double)mVideoInfo.mTextColumns / (double)mVideoInfo.mDisplayArea.width());
	sint32 y = VDFloorToInt(((double)(pixelPos.y + 0.5) - mVideoInfo.mDisplayArea.top) * (double)mVideoInfo.mTextRows / (double)mVideoInfo.mDisplayArea.height());

	if (y < 0) {
		x = 0;
		y = 0;
	} else if (y >= mVideoInfo.mTextRows) {
		x = mVideoInfo.mTextColumns;
		y = mVideoInfo.mTextRows - 1;
	} else {
		x = std::clamp<sint32>(x, 0, mVideoInfo.mTextColumns);
	}

	return {x, y};
}

vdrect32 ATDeviceCustomVideoOutput::CharToPixelRect(const vdrect32& r) {
	if (mVideoInfo.mDisplayArea.empty() || mVideoInfo.mTextRows <= 0 || mVideoInfo.mTextColumns <= 0)
		return {};
	
	double scaleX = (double)mVideoInfo.mDisplayArea.width() / (double)mVideoInfo.mTextColumns;
	double scaleY = (double)mVideoInfo.mDisplayArea.height() / (double)mVideoInfo.mTextRows;

	return vdrect32 {
		mVideoInfo.mDisplayArea.left + VDRoundToInt32((double)r.left * scaleX),
		mVideoInfo.mDisplayArea.top + VDRoundToInt32((double)r.top * scaleY),
		mVideoInfo.mDisplayArea.left + VDRoundToInt32((double)r.right * scaleX),
		mVideoInfo.mDisplayArea.top + VDRoundToInt32((double)r.bottom * scaleY)
	};
}

int ATDeviceCustomVideoOutput::ReadRawText(uint8 *dst, int x, int y, int n) {
	if (!mpOnPreCopy)
		return 0;

	if (x < 0 || y < 0 || x >= mVideoInfo.mTextColumns || y >= mVideoInfo.mTextRows)
		return 0;

	mpParent->mVMThread.mThreadVariables[(int)ATDeviceCustom::ThreadVarIndex::Timestamp] = ATSCHEDULER_GETTIME(mpParent->mpScheduler);
	mpParent->mVMThread.RunVoid(*mpOnPreCopy);

	if (mpCopySource
		&& mCopyOffset >= 0
		&& (uint32)mCopyOffset < mpCopySource->mSize
		&& mpCopySource->mSize - (uint32)mCopyOffset >= (uint32)(mVideoInfo.mTextRows * mVideoInfo.mTextColumns + mCopySkip * (mVideoInfo.mTextColumns - 1)))
	{
		n = std::min<int>(n, mVideoInfo.mTextColumns - x);

		memcpy(dst, mpCopySource->mpData + mCopyOffset + (mVideoInfo.mTextColumns + mCopySkip) * y + x, n);

		return n;
	}

	return 0;
}

uint32 ATDeviceCustomVideoOutput::GetActivityCounter() {
	if (mActivityCounter & 1)
		++mActivityCounter;

	return mActivityCounter;
}

////////////////////////////////////////////////////////////////////////////////

const ATVMObjectClass ATDeviceCustomSoundParams::kVMObjectClass {
	"SoundParams",
	{
		ATVMExternalMethod::Bind<&ATDeviceCustomSoundParams::VMCallSetPan>("set_pan"),
		ATVMExternalMethod::Bind<&ATDeviceCustomSoundParams::VMCallSetVolume>("set_volume"),
		ATVMExternalMethod::Bind<&ATDeviceCustomSoundParams::VMCallSetRate>("set_rate"),
		ATVMExternalMethod::Bind<&ATDeviceCustomSoundParams::VMCallSetLooping>("set_looping"),
	}
};

void ATDeviceCustomSoundParams::VMCallSetPan(sint32 x, sint32 y) {
	if (y == 0)
		mPan = 0.0f;
	else
		mPan = std::clamp((float)x / (float)y, -1.0f, 1.0f);
}

void ATDeviceCustomSoundParams::VMCallSetVolume(sint32 x, sint32 y) {
	if (y <= 0 || x >= y)
		mVolume = 1.0f;
	else if (x < 0)
		mVolume = 0.0f;
	else
		mVolume = (float)x / (float)y;
}

void ATDeviceCustomSoundParams::VMCallSetRate(sint32 x, sint32 y) {
	if (x < 0 || y <= 0)
		mRate = 1.0f;
	else
		mRate = std::clamp((float)x / (float)y, 0.1f, 10.0f);
}

void ATDeviceCustomSoundParams::VMCallSetLooping(int enabled) {
	mbLooping = enabled != 0;
}

////////////////////////////////////////////////////////////////////////////////

const ATVMObjectClass ATDeviceCustomSound::kVMObjectClass {
	"Sound",
	{
		ATVMExternalMethod::Bind<&ATDeviceCustomSound::VMCallPlay>("play"),
		ATVMExternalMethod::Bind<&ATDeviceCustomSound::VMCallStopAll>("stop_all"),
	}
};

ATDeviceCustomSound::ATDeviceCustomSound(ATDeviceCustom& parent)
	: mParent(parent)
{
	IATAudioMixer& mixer = *mParent.GetService<IATAudioMixer>();
	IATSyncAudioSamplePlayer& player = mixer.GetAsyncSamplePlayer();

	ATAudioGroupDesc desc;
	mpSoundGroup = player.CreateGroup(desc);
}

void ATDeviceCustomSound::SetSoundData(vdspan<const sint16> data, float samplingRate) {
	IATAudioMixer& mixer = *mParent.GetService<IATAudioMixer>();
	IATSyncAudioSamplePlayer& player = mixer.GetAsyncSamplePlayer();

	mpAudioSample = player.RegisterSample(data, ATAudioSoundSamplingRate(samplingRate), 1.0f);
}

void ATDeviceCustomSound::VMCallPlay(const ATDeviceCustomSoundParams *params) {
	if (mpAudioSample) {
		IATSyncAudioSamplePlayer& player = mParent.GetService<IATAudioMixer>()->GetAsyncSamplePlayer();

		mpSoundGroup->StopAllSounds();

		player.AddSound(
			*mpSoundGroup,
			0,
			*mpAudioSample,
			ATSoundParams().Volume(params->mVolume).RateScale(params->mRate).Pan(params->mPan).Loop(params->mbLooping)
		);
	}
}

void ATDeviceCustomSound::VMCallStopAll() {
	if (mpSoundGroup)
		mpSoundGroup->StopAllSounds();
}

////////////////////////////////////////////////////////////////////////////////

const ATVMObjectClass ATDeviceCustomSerialPort::kVMObjectClass {
	"SerialPort",
	{
		ATVMExternalMethod::Bind<&ATDeviceCustomSerialPort::VMCallRead>("read"),
		ATVMExternalMethod::Bind<&ATDeviceCustomSerialPort::VMCallReadWait>("read_wait", ATVMFunctionFlags::AsyncAll),
		ATVMExternalMethod::Bind<&ATDeviceCustomSerialPort::VMCallWrite>("write"),
		ATVMExternalMethod::Bind<&ATDeviceCustomSerialPort::VMCallWaitError>("wait_error", ATVMFunctionFlags::AsyncAll),
		ATVMExternalMethod::Bind<&ATDeviceCustomSerialPort::VMCallBaudToCyclesPerByte>("baud_to_cycles_per_byte"),
	}
};

ATDeviceCustomSerialPort::ATDeviceCustomSerialPort(ATDeviceCustom& parent)
	: mParent(parent)
{
	const sint32 busId = mParent.AddDeviceBus(mDeviceBus);

	mDeviceBus.SetOnAttach(std::bind_front(&ATDeviceCustomSerialPort::OnDeviceAttach, this));
	mDeviceBus.SetOnDetach(std::bind_front(&ATDeviceCustomSerialPort::OnDeviceDetach, this));
	mDeviceBus.Init(&mParent, busId, IATDeviceSerial::kTypeID, "serial", L"Serial Port", "serial");
}

ATDeviceCustomSerialPort::~ATDeviceCustomSerialPort() {
	Shutdown();
}

void ATDeviceCustomSerialPort::Shutdown() {
	mParent.RemoveDeviceBus(mDeviceBus);
	mDeviceBus.Shutdown();
}

sint32 ATDeviceCustomSerialPort::VMCallRead(sint32 baud) {
	IATDeviceSerial *dev = mDeviceBus.GetChild<IATDeviceSerial>();

	if (!dev)
		return -1;

	uint32 actualbaudRate = 0;
	uint8 c = 0;
	bool framingError = false;

	if (baud <= 0) {
		if (!dev->Read(actualbaudRate, c))
			return -1;
	} else {
		if (!dev->Read(baud, c, framingError))
			return -1;

		if (framingError)
			RaiseError(ErrorType::Framing);
	}

	return c;
}

sint32 ATDeviceCustomSerialPort::VMCallReadWait(sint32 baud, ATVMDomain& domain) {
	sint32 c = VMCallRead(baud);

	if (c < 0) {
		mWaitBaudRate = baud;
		mReadQueue.Suspend(*domain.mpActiveThread);
	}

	return c;
}

void ATDeviceCustomSerialPort::VMCallWrite(sint32 byte, sint32 baud) {
	IATDeviceSerial *dev = mDeviceBus.GetChild<IATDeviceSerial>();

	if (dev)
		dev->Write((uint32)std::max<sint32>(baud, 0), (uint8)byte);
}

sint32 ATDeviceCustomSerialPort::VMCallWaitError(ATVMDomain& domain) {
	if (!mPendingErrorMask) {
		mErrorQueue.Suspend(*domain.mpActiveThread);
		return 0;
	}

	sint32 errorId = 0;
	uint32 errorMask = mPendingErrorMask;

	do {
		++errorId;
		errorMask >>= 1;
	} while(errorMask);

	mPendingErrorMask &= (mPendingErrorMask - 1);

	return errorId;
}

sint32 ATDeviceCustomSerialPort::VMCallBaudToCyclesPerByte(sint32 baud) {
	const double bytesPerSecond = (double)std::max<sint32>(10, baud) * 0.1;
	const double cyclesPerSecond = mParent.mpScheduler->GetRate().asDouble();

	sint32 cpb = VDRoundToInt32(cyclesPerSecond / bytesPerSecond);

	return std::max<sint32>(1, cpb);
}

void ATDeviceCustomSerialPort::RaiseError(ErrorType errorType) {
	if (mErrorQueue.GetNext()) {
		// we should not have waiting threads if we had pending errors
		VDASSERT(!mPendingErrorMask);

		mErrorQueue.ResumeInt((sint32)errorType);
		return;
	}

	const uint32 errorBit = UINT32_C(1) << ((int)errorType - 1);
	mPendingErrorMask |= errorBit;
}

void ATDeviceCustomSerialPort::OnDeviceAttach() {
	IATDeviceSerial *dev = mDeviceBus.GetChild<IATDeviceSerial>();
	if (dev)
		dev->SetOnReadReady([this] { OnReadReady(); });
}

void ATDeviceCustomSerialPort::OnDeviceDetach() {
	IATDeviceSerial *dev = mDeviceBus.GetChild<IATDeviceSerial>();
	if (dev)
		dev->SetOnReadReady(nullptr);
}

void ATDeviceCustomSerialPort::OnReadReady() {
	if (!mReadQueue.GetNext())
		return;

	sint32 c = VMCallRead(mWaitBaudRate);
	if (c >= 0)
		mReadQueue.ResumeInt(c);
}

////////////////////////////////////////////////////////////////////////////////

const ATVMObjectClass ATDeviceCustomPrinterPort::kVMObjectClass {
	"PrinterPort",
	{
		ATVMExternalMethod::Bind<&ATDeviceCustomPrinterPort::VMCallWriteRaw>("write_raw"),
		ATVMExternalMethod::Bind<&ATDeviceCustomPrinterPort::VMCallWriteUnicode>("write_unicode"),
	}
};

ATDeviceCustomPrinterPort::ATDeviceCustomPrinterPort(ATDeviceCustom& parent)
	: mParent(parent)
{
	const sint32 busId = mParent.AddDeviceBus(mDeviceBus);

	mDeviceBus.SetOnAttach(std::bind_front(&ATDeviceCustomPrinterPort::OnDeviceAttach, this));
	mDeviceBus.SetOnDetach(std::bind_front(&ATDeviceCustomPrinterPort::OnDeviceDetach, this));
	mDeviceBus.Init(&mParent, busId, IATPrinterOutput::kTypeID, "printer", L"Printer Port", "printer");
}

ATDeviceCustomPrinterPort::~ATDeviceCustomPrinterPort() {
	Shutdown();
}

void ATDeviceCustomPrinterPort::Shutdown() {
	mpPrinterOutput = nullptr;
	mParent.RemoveDeviceBus(mDeviceBus);
	mDeviceBus.Shutdown();
}

void ATDeviceCustomPrinterPort::VMCallWriteRaw(sint32 ch) {
	IATPrinterOutput *dev = GetPrinterOutput();

	if (dev) {
		uint8 ch2 = (uint8)ch;

		dev->WriteRaw(&ch2, 1);
	}
}

void ATDeviceCustomPrinterPort::VMCallWriteUnicode(sint32 ch) {
	if (ch < 0 || ch > 0x10FFFF || (ch >= 0xD800 && ch < 0xDFFF))
		return;

	IATPrinterOutput *dev = GetPrinterOutput();
	if (dev) {
		// wchar_t is UTF-16 on Windows (2 bytes) and UTF-32 on
		// Linux/macOS (4 bytes). Encode the codepoint accordingly so
		// supplementary-plane characters survive the round trip.
		if constexpr (sizeof(wchar_t) >= 4) {
			wchar_t ch2 = (wchar_t)ch;
			dev->WriteUnicode(&ch2, 1);
		} else {
			wchar_t ch2[2] {};

			if (ch < 0x10000) {
				ch2[0] = (wchar_t)ch;
				dev->WriteUnicode(ch2, 1);
			} else {
				ch2[0] = (wchar_t)(0xD800 + ((ch >> 10) & 0x3FF));
				ch2[1] = (wchar_t)(0xDC00 + (ch & 0x3FF));
				dev->WriteUnicode(ch2, 2);
			}
		}
	}
}

void ATDeviceCustomPrinterPort::OnDeviceAttach() {
	IATPrinterOutput *dev = mDeviceBus.GetChild<IATPrinterOutput>();
	
	if (dev)
		mpPrinterOutput = dev;
}

void ATDeviceCustomPrinterPort::OnDeviceDetach() {
	IATPrinterOutput *dev = mDeviceBus.GetChild<IATPrinterOutput>();

	if (mpPrinterOutput == dev)
		mpPrinterOutput = nullptr;
}

IATPrinterOutput *ATDeviceCustomPrinterPort::GetPrinterOutput() {
	if (!mpPrinterOutput)
		mpPrinterOutput = mParent.GetService<IATPrinterOutputManager>()->CreatePrinterOutput(mParent.mDeviceName.c_str());

	return mpPrinterOutput;
}

////////////////////////////////////////////////////////////////////////////////

const ATVMObjectClass ATDeviceCustomEmulatorObj::kVMObjectClass {
	"Emulator",
	{
		ATVMExternalMethod::Bind<&ATDeviceCustomEmulatorObj::VMCallRunCommand>("run_command", ATVMFunctionFlags::AsyncAll | ATVMFunctionFlags::Unsafe)
	}
};

ATDeviceCustomEmulatorObj::ATDeviceCustomEmulatorObj() {
	mpCancelFn = [this](ATVMThread& thread) {
		mPendingCommand.clear();
	};
}

void ATDeviceCustomEmulatorObj::Shutdown() {
	mbValid = false;
}

void ATDeviceCustomEmulatorObj::VMCallRunCommand(const char *cmd, ATVMDomain& domain) {
	domain.mpActiveThread->Suspend(&mpCancelFn);

	mPendingCommand = cmd;

	ATUIGetQueue().PushStep(
		[self = vdrefptr(this)] {
			if (!self->mPendingCommand.empty()) {
				ATUICommandOptions opts;

				if (self->mPendingCommand.back() == '!') {
					self->mPendingCommand.pop_back();

					opts.mbQuiet = true;
				}

				ATUIExecuteCommandStringAndShowErrors(self->mPendingCommand.c_str(), &opts);
			}
		}
	);

	g_sim.PostInterruptingEvent(kATSimEvent_AnonymousPause);
}
