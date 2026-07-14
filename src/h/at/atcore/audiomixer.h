//	Altirra - Atari 800/800XL/5200 emulator
//	Core library - audio mixing system definitions
//	Copyright (C) 2008-2018 Avery Lee
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
//
//	As a special exception, this library can also be redistributed and/or
//	modified under an alternate license. See COPYING.RMT in the same source
//	archive for details.

//=========================================================================
// Synchronous audio mixer output
//
// The synchronous audio mixer combines audio from multiple sources in
// emulation synchronous time. The mixer divides time into discrete mixing
// chunks and then polls all sources to mix audio into the buffer for each
// chunk. This is always done after the emulation time has passed. More
// details are in audiosource.h.
//
// For simple looping or one-shot sounds, the mixer also provides a sample
// player that automatically tracks sound lifetimes.
//

#ifndef f_AT_ATCORE_AUDIOMIXER_H
#define f_AT_ATCORE_AUDIOMIXER_H

#include <vd2/system/vdtypes.h>
#include <vd2/system/refcount.h>
#include <vd2/system/unknown.h>
#include <vd2/system/vdstl.h>

class IATAudioAsyncSource;
class IATAudioAsyncSamplePlayer;
class IATSyncAudioSource;
class IATSyncAudioSamplePlayer;
class IATSyncAudioEdgePlayer;
class IVDRefCount;

// The audio mix a sound participates in. Currently selects the mix volume used for the sound.
enum ATAudioMix {
	kATAudioMix_Drive,
	kATAudioMix_Covox,
	kATAudioMix_Modem,
	kATAudioMix_Cassette,
	kATAudioMix_Other,
	kATAudioMixCount
};

enum ATAudioSampleId : uint32 {
	kATAudioSampleId_None,
	kATAudioSampleId_DiskRotation,
	kATAudioSampleId_DiskStep1,
	kATAudioSampleId_DiskStep2,
	kATAudioSampleId_DiskStep2H,
	kATAudioSampleId_DiskStep3,
	kATAudioSampleId_SpeakerStep,
	kATAudioSampleId_1030Relay,
	kATAudioSampleId_Printer1029Pin,
	kATAudioSampleId_Printer1029Platen,
	kATAudioSampleId_Printer1029Retract,
	kATAudioSampleId_Printer1029Home,
	kATAudioSampleId_Printer1025Feed,
	kATAudioSampleId_Printer1020HeadMove,
	kATAudioSampleId_Printer1020HeadReverse,
	kATAudioSampleId_Printer1020PaperFeed,
	kATAudioSampleId_Printer1020PenDown,
	kATAudioSampleId_Printer1020PenUp,
};

enum class ATSoundId : uint32 {
	Invalid = 0
};

class IATInternalAudioTap {
public:
	virtual void WriteInternalAudio(const float *samples, uint32 count, uint32 timestamp) = 0;
};

class IATAudioMixer {
public:
	static constexpr uint32 kTypeID = "IATAudioMixer"_vdtypeid;

	virtual void AddSyncAudioSource(IATSyncAudioSource *src) = 0;
	virtual void RemoveSyncAudioSource(IATSyncAudioSource *src) = 0;

	virtual void AddAsyncAudioSource(IATAudioAsyncSource& src) = 0;
	virtual void RemoveAsyncAudioSource(IATAudioAsyncSource& src) = 0;

	virtual IATSyncAudioSamplePlayer& GetSamplePlayer() = 0;
	virtual IATSyncAudioSamplePlayer& GetEdgeSamplePlayer() = 0;
	virtual IATSyncAudioEdgePlayer& GetEdgePlayer() = 0;
	virtual IATSyncAudioSamplePlayer& GetAsyncSamplePlayer() = 0;

	// Add/remove internal audio taps, which receive the raw audio output from the computer. Currently this
	// receives just the main POKEY output.
	virtual void AddInternalAudioTap(IATInternalAudioTap *tap) = 0;
	virtual void RemoveInternalAudioTap(IATInternalAudioTap *tap) = 0;

	// Enable or disable internal audio. Unblock must be called once for each
	// call to Block. This does not block the internal audio tap, which receives
	// the internal audio regardless.
	virtual void BlockInternalAudio() = 0;
	virtual void UnblockInternalAudio() = 0;
};

class IATAudioSampleSource {
public:
	// Mix samples from the source into the destination buffer, using the
	// given volume (sample scale). Offset is the number of samples (not
	// cycles!) from the beginning of the sound to the mixing position.
	virtual void MixAudio(float *dst, uint32 len, float volume, uint64 offset, float mixingRate) = 0;
};

struct ATAudioGroupDesc {
	// Audio mix that sounds in the group should participate in.
	ATAudioMix mAudioMix = kATAudioMix_Other;

	// If true, each new sound that is queued stops any previously queued sounds that would begin
	// on or after the same start time -- the new sound supercedes previous sounds queued past that point.
	// This is used to allow sound sequences to be pre-queued and then preempted. Sounds that have
	// already started by that point are not affected.
	bool mbRemoveSupercededSounds = false;

	ATAudioGroupDesc&& Mix(ATAudioMix mix) && { mAudioMix = mix; return static_cast<ATAudioGroupDesc&&>(*this); }
	ATAudioGroupDesc&& RemoveSupercededSounds() && { mbRemoveSupercededSounds = true; return static_cast<ATAudioGroupDesc&&>(*this); }
};

class IATAudioSoundGroup : public IVDRefCount {
public:
	virtual bool IsAnySoundQueued() const = 0;

	virtual void StopAllSounds() = 0;
};

// Convolution player
//
// The convolution player is a special subclass of the regular sample player that
// mixes copies of a single sound by FFT convolution instead of regular sample mixing.
// It is significantly faster when hundreds of instances of the same sound sample
// are played in the same audio frame.
//
// Considerations:
//
// - There is a cost of 1 2K FFT per audio frame per convolution player that has
//   impulses, and another FFT for the output when any impulses are present. Thus,
//   convolution players are slower until around dozens of sounds are played
//   per frame. However, convolution players have a very low cost per sound, and
//   there is no cost for convolution players that do not currently have impulses.
//
// - Each convolution player can only handle one sample. (Convolution players will
//   be shared if allocated for stock samples.)
//
// - Sounds are limited to 2560 samples.
//
// - Convolution players can be used both on the normal and edge sample players.
//   Edge convolution players are useful for convolving samples with square waves.
//
// - Convolution players currently only support samples played within the current
//   audio frame. They cannot be used to play sounds more than one frame in the
//   future.
//
// - Sounds cannot be stopped on a convolution player. Once a sound has started,
//   it must play to completion.
//
class IATSyncAudioConvolutionPlayer : public IVDRefCount {
public:
	virtual void Play(uint32 t, float volume) = 0;
};

class IATAudioSampleHandle : public IVDRefCount {};

enum class ATAudioSamplingRateUnit {
	Hz,
	PokeyMixingRate
};

struct ATAudioSoundSamplingRate {
	float mValue {};
	ATAudioSamplingRateUnit mUnit {};

	ATAudioSoundSamplingRate(float val = 0, ATAudioSamplingRateUnit unit = ATAudioSamplingRateUnit::Hz)
		: mValue(val), mUnit(unit) {}
};

struct ATSoundParams {
	float mDelay = 0.0f;
	float mRateScale = 1.0f;
	float mVolume = 1.0f;
	float mPan = 0;
	bool mbLooping = false;

	ATSoundParams&& Delay(float delay) && { mDelay = delay; return static_cast<ATSoundParams&&>(*this); }
	ATSoundParams&& RateScale(float rate) && { mRateScale = rate; return static_cast<ATSoundParams&&>(*this); }
	ATSoundParams&& Volume(float vol) && { mVolume = vol; return static_cast<ATSoundParams&&>(*this); }
	ATSoundParams&& Pan(float pan) && { mPan = pan; return static_cast<ATSoundParams&&>(*this); }
	ATSoundParams&& Loop(bool enabled = true) && { mbLooping = enabled; return static_cast<ATSoundParams&&>(*this); }
};

class IATSyncAudioSamplePlayer {
public:
	virtual IATSyncAudioSource& AsSource() = 0;

	virtual vdrefptr<IATAudioSampleHandle> RegisterSample(vdspan<const sint16> soundData, const ATAudioSoundSamplingRate& samplingRate, float volume) = 0;

	virtual ATSoundId AddSound(IATAudioSoundGroup& soundGroup, uint32 delay, ATAudioSampleId sampleId, float volume) = 0;
	virtual ATSoundId AddLoopingSound(IATAudioSoundGroup& soundGroup, uint32 delay, ATAudioSampleId sampleId, float volume) = 0;

	virtual ATSoundId AddSound(IATAudioSoundGroup& soundGroup, uint32 delay, IATAudioSampleSource *src, IVDRefCount *owner, uint32 len, float volume) = 0;
	virtual ATSoundId AddLoopingSound(IATAudioSoundGroup& soundGroup, uint32 delay, IATAudioSampleSource *src, IVDRefCount *owner, float volume) = 0;

	virtual ATSoundId AddSound(IATAudioSoundGroup& soundGroup, uint32 delay, IATAudioSampleHandle& sample, const ATSoundParams& params) = 0;

	// Create a sound group. All sounds placed into the group are subject to the policies of that group.
	// All sounds in a group are automatically stopped when the last reference to the group is released,
	// so the group must be actively held to keep the sounds alive (this is a safety mechanism).
	virtual vdrefptr<IATAudioSoundGroup> CreateGroup(const ATAudioGroupDesc& desc) = 0;

	// Stop a sound immediately in real time. This may stop it up to one
	// mixing frame before the current simulated time.
	virtual void ForceStopSound(ATSoundId id) = 0;

	// Stop a sound immediately in simulation time.
	virtual void StopSound(ATSoundId id) = 0;

	// Stop a sound at the given time. If the time has already passed, the
	// sound is stopped immediately. Otherwise, the sound will be truncated
	// to the given time.
	virtual void StopSound(ATSoundId id, uint64 time) = 0;

	virtual vdrefptr<IATSyncAudioConvolutionPlayer> CreateConvolutionPlayer(ATAudioSampleId sampleId) = 0;
	virtual vdrefptr<IATSyncAudioConvolutionPlayer> CreateConvolutionPlayer(const sint16 *sample, uint32 len) = 0;
};

struct ATSyncAudioEdge {
	uint32 mTime;
	float mDeltaValue;
};

class ATSyncAudioEdgeBuffer final : public vdrefcount {
public:
	vdfastvector<ATSyncAudioEdge> mEdges;
	float mLeftVolume = 0;
	float mRightVolume = 0;
	const char *mpDebugLabel = nullptr;
};

// Audio edge player
//
// The audio edge player converts a series of edge transitions into a normal audio waveform.
// This has several advantages over direct mixing:
//
//	- It is single pass for all sources, instead of each source having to individually fill
//	  in pulses.
//
//	- Edges do not have to be sorted.
//
//	- The edge player takes care of downsampling from 1.7MHz to 64KHz.
//
// Unlike the sample player, the edge player does not allow future buffering of edges. Edges
// can only be submitted during the mixing process within the current frame. The edge player
// will accommodate edges slightly beyond the mixing window but beyond the cycle window,
// automatically carrying them over to the next mixing frame. This avoids the need for
// callers to buffer edges between the end of the mixing frame and the end of the simulation
// frame.
//
// The edge player takes advantage of the fact that the end of the mixing pipeline has a
// high-pass filter which normally consists of a differencing stage followed by an
// integration stage. The edge player is inserted between the two, allowing it to take
// advantage of 'free' integration.
//
class IATSyncAudioEdgePlayer {
public:
	// Add edges to be mixed in. The edges are copied.
	virtual void AddEdges(const ATSyncAudioEdge *edges, size_t numEdges, float volume) = 0;

	// Submit a buffer of edges to be mixed in. The edges in the buffer will be mixed in
	// at the end of the next or current in-progress mixing phase, and then cleared. The
	// edge player will hold a reference on the buffer until this happens. The buffer
	// can be reused afterward but will need to be resubmitted.
	virtual void AddEdgeBuffer(ATSyncAudioEdgeBuffer *buffer) = 0;
};

#endif
