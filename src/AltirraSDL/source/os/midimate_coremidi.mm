//	Altirra SDL3 frontend — CoreMIDI backend (Objective-C++ implementation)
//
//	Compiled with Clang even when the main CXX compiler is GCC, because
//	CoreMIDI headers use Objective-C blocks (^) that only Clang supports.

#include "midimate_coremidi.h"
#include <CoreMIDI/CoreMIDI.h>
#include <CoreFoundation/CoreFoundation.h>

struct ATCoreMidiSink {
	MIDIClientRef   mClient = 0;
	MIDIEndpointRef mSource = 0;
};

ATCoreMidiSink *ATCoreMidiSink_Create(void) {
	auto *sink = new ATCoreMidiSink;
	MIDIClientCreate(CFSTR("Altirra"), nullptr, nullptr, &sink->mClient);
	if (sink->mClient)
		MIDISourceCreate(sink->mClient, CFSTR("MidiMate Out"), &sink->mSource);
	return sink;
}

void ATCoreMidiSink_Destroy(ATCoreMidiSink *sink) {
	if (!sink) return;
	if (sink->mSource) {
		MIDIEndpointDispose(sink->mSource);
		sink->mSource = 0;
	}
	if (sink->mClient) {
		MIDIClientDispose(sink->mClient);
		sink->mClient = 0;
	}
	delete sink;
}

void ATCoreMidiSink_SendShort(ATCoreMidiSink *sink, uint32_t packed) {
	if (!sink || !sink->mSource)
		return;
	Byte buf[3] = {
		(Byte)(packed       & 0xff),
		(Byte)((packed >> 8) & 0xff),
		(Byte)((packed >> 16) & 0xff),
	};
	Byte status = buf[0];
	ByteCount len = 1;
	if (status < 0xF0) {
		switch (status & 0xF0) {
		case 0xC0: case 0xD0: len = 2; break;
		default:               len = 3; break;
		}
	}

	MIDIPacketList packetList;
	MIDIPacket *pkt = MIDIPacketListInit(&packetList);
	pkt = MIDIPacketListAdd(&packetList, sizeof(packetList),
		pkt, 0, len, buf);
	if (pkt)
		MIDIReceived(sink->mSource, &packetList);
}

void ATCoreMidiSink_Reset(ATCoreMidiSink *sink) {
	if (!sink) return;
	for (uint8_t ch = 0; ch < 16; ++ch) {
		uint32_t m = 0xB0 | ch;
		m |= ((uint32_t)123 << 8);
		ATCoreMidiSink_SendShort(sink, m);
	}
}
