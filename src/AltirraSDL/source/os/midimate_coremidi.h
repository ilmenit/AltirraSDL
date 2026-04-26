//	Altirra SDL3 frontend — CoreMIDI backend (C-linkage wrapper)
//
//	CoreMIDI headers use Objective-C blocks which GCC cannot parse.
//	This header exposes an opaque, plain-C interface so that
//	midimate_sdl3.cpp (compiled by any C++ compiler) can drive the
//	CoreMIDI virtual source without including <CoreMIDI/CoreMIDI.h>.

#ifndef MIDIMATE_COREMIDI_H
#define MIDIMATE_COREMIDI_H

#include <cstdint>

#ifdef __cplusplus
extern "C" {
#endif

// Opaque handle to the CoreMIDI client + virtual source pair.
typedef struct ATCoreMidiSink ATCoreMidiSink;

ATCoreMidiSink *ATCoreMidiSink_Create(void);
void            ATCoreMidiSink_Destroy(ATCoreMidiSink *sink);
void            ATCoreMidiSink_SendShort(ATCoreMidiSink *sink, uint32_t packed);
void            ATCoreMidiSink_Reset(ATCoreMidiSink *sink);

#ifdef __cplusplus
}
#endif

#endif // MIDIMATE_COREMIDI_H
