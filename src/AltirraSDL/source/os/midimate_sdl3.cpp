//	Altirra SDL3 frontend - MidiMate device with portable MIDI backend
//
//	Cross-platform port of src/Altirra/source/midimate.cpp. The MIDI
//	parser (ATMidiOutput::Write/ProcessMessageByte/...) is a verbatim
//	copy of the Windows source; only the host-side MIDI delivery is
//	swapped per-platform:
//
//	  - Linux:     ALSA sequencer client (libasound).
//	  - macOS:     CoreMIDI virtual source.
//	  - Windows:   Win32 winmm midiOut* (matches Altirra.sln build).
//	  - WASM/Android/no-MIDI: short-msg dispatch is a no-op so the
//	    parser still validates traffic but nothing reaches a synth.
//
//	The Atari side sees a fully functional MidiMate either way; users
//	on platforms with a backend get audible MIDI, users on others get
//	a working device that quietly drops output.

#include <stdafx.h>
#include <at/atcore/deviceimpl.h>
#include <at/atcore/deviceparentimpl.h>
#include <at/atcore/deviceserial.h>
#include <at/atcore/devicesioimpl.h>
#include <at/atcore/propertyset.h>
#include <at/atcore/scheduler.h>
#include "debuggerlog.h"

#if defined(__linux__) && !defined(__ANDROID__) && !defined(__EMSCRIPTEN__)
	#define ATMIDI_BACKEND_ALSA 1
	#include <alsa/asoundlib.h>
#elif defined(__APPLE__)
	#define ATMIDI_BACKEND_COREMIDI 1
	#include "midimate_coremidi.h"
#elif defined(_WIN32)
	#define ATMIDI_BACKEND_WINMM 1
	#include <windows.h>
	#include <mmsystem.h>
	#pragma comment(lib, "winmm.lib")
#else
	#define ATMIDI_BACKEND_NULL 1
#endif

ATDebuggerLogChannel g_ATLCMIDI(false, false, "MIDI", "MIDI command activity");

////////////////////////////////////////////////////////////////////////////////
// Platform-specific MIDI sink. Each backend exposes the same three
// operations: open on first use, send a 1-3 byte short message, reset
// state, close on shutdown.
////////////////////////////////////////////////////////////////////////////////

namespace {
#if ATMIDI_BACKEND_ALSA
	class ATMidiSink {
	public:
		ATMidiSink() {
			if (snd_seq_open(&mSeq, "default", SND_SEQ_OPEN_OUTPUT, 0) != 0) {
				mSeq = nullptr;
				return;
			}
			snd_seq_set_client_name(mSeq, "Altirra");
			mPort = snd_seq_create_simple_port(mSeq, "MidiMate Out",
				SND_SEQ_PORT_CAP_READ | SND_SEQ_PORT_CAP_SUBS_READ,
				SND_SEQ_PORT_TYPE_MIDI_GENERIC | SND_SEQ_PORT_TYPE_APPLICATION);
		}

		~ATMidiSink() {
			if (mSeq) {
				snd_seq_close(mSeq);
				mSeq = nullptr;
			}
		}

		void SendShort(uint32 packed) {
			if (!mSeq || mPort < 0)
				return;

			snd_midi_event_t *parser = nullptr;
			if (snd_midi_event_new(3, &parser) != 0)
				return;
			snd_midi_event_init(parser);
			snd_midi_event_no_status(parser, 1);

			snd_seq_event_t ev {};
			snd_seq_ev_set_source(&ev, mPort);
			snd_seq_ev_set_subs(&ev);
			snd_seq_ev_set_direct(&ev);

			uint8 bytes[3] = {
				(uint8)(packed       & 0xff),
				(uint8)((packed >> 8) & 0xff),
				(uint8)((packed >> 16) & 0xff),
			};
			// Determine real length from status byte.
			size_t len = 1;
			uint8 status = bytes[0];
			if (status >= 0xF8) {
				len = 1;
			} else if (status >= 0xF0) {
				len = 1;	// system common are odd; only our 0xF6 path comes here
			} else {
				switch (status & 0xF0) {
				case 0xC0: case 0xD0:
					len = 2; break;
				default:
					len = 3; break;
				}
			}

			for (size_t i = 0; i < len; ++i) {
				if (snd_midi_event_encode_byte(parser, bytes[i], &ev) == 1)
					break;
			}
			snd_seq_event_output_direct(mSeq, &ev);
			snd_seq_drain_output(mSeq);

			snd_midi_event_free(parser);
		}

		void Reset() {
			// Send "All Notes Off" (CC 123) on every channel.
			if (!mSeq || mPort < 0) return;
			for (uint8 ch = 0; ch < 16; ++ch) {
				uint32 m = 0xB0 | ch;
				m |= ((uint32)123 << 8);
				m |= ((uint32)0   << 16);
				SendShort(m);
			}
		}

	private:
		snd_seq_t *mSeq = nullptr;
		int mPort = -1;
	};

#elif ATMIDI_BACKEND_COREMIDI
	class ATMidiSink {
	public:
		ATMidiSink()  { mSink = ATCoreMidiSink_Create(); }
		~ATMidiSink() { ATCoreMidiSink_Destroy(mSink); }

		void SendShort(uint32 packed) { ATCoreMidiSink_SendShort(mSink, packed); }
		void Reset()                  { ATCoreMidiSink_Reset(mSink); }

	private:
		ATCoreMidiSink *mSink = nullptr;
	};

#elif ATMIDI_BACKEND_WINMM
	class ATMidiSink {
	public:
		ATMidiSink() {
			midiOutOpen(&mhmo, MIDI_MAPPER, 0, 0, CALLBACK_NULL);
		}

		~ATMidiSink() {
			if (mhmo) {
				midiOutClose(mhmo);
				mhmo = nullptr;
			}
		}

		void SendShort(uint32 packed) {
			if (mhmo)
				midiOutShortMsg(mhmo, packed);
		}

		void Reset() {
			if (mhmo)
				midiOutReset(mhmo);
		}

	private:
		HMIDIOUT mhmo = nullptr;
	};

#else  // ATMIDI_BACKEND_NULL — WASM / Android / unknown
	class ATMidiSink {
	public:
		void SendShort(uint32) {}
		void Reset() {}
	};
#endif
}

////////////////////////////////////////////////////////////////////////////////

class ATMidiOutput final : public vdrefcounted<IATDeviceSerial> {
	ATMidiOutput(const ATMidiOutput&) = delete;
	ATMidiOutput& operator=(const ATMidiOutput&) = delete;
public:
	ATMidiOutput() {
		ResetState();
	}

	~ATMidiOutput() = default;

	void Reset() {
		ResetState();
		mSink.Reset();
	}

	void *AsInterface(uint32 id) override {
		if (id == IATDeviceSerial::kTypeID)
			return static_cast<IATDeviceSerial *>(this);
		return nullptr;
	}

	void SetOnStatusChange(const vdfunction<void(const ATDeviceSerialStatus&)>&) override {}
	void SetTerminalState(const ATDeviceSerialTerminalState&) override {}
	ATDeviceSerialStatus GetStatus() override { return {}; }
	void SetOnReadReady(vdfunction<void()>) override {}
	bool Read(uint32, uint8&, bool&) override { return false; }
	bool Read(uint32&, uint8&) override { return false; }
	void FlushBuffers() override {}

	void Write(uint32 baudRate, uint8 c) override {
		if (baudRate < 29687 || baudRate > 32812) {
			mState = 0;
			return;
		}

		if (c >= 0xF8) {
			switch (c) {
			case 0xF8: case 0xFA: case 0xFB:
			case 0xFC: case 0xFE: case 0xFF:
				g_ATLCMIDI("Message out: %02X\n", mMsgStatus);
				mSink.SendShort(mMsgStatus);
				break;
			}
			return;
		}

		ProcessMessageByte(c);
	}

private:
	void ResetState() {
		mbInSysEx = false;
		mState = 0;
		mMsgStatus = 0;
		mMsgData1 = 0;
		mMsgData2 = 0;
		mSysExLength = 0;
	}

	void ProcessMessageByte(uint8 c) {
		if (mState) {
			if (c >= 0x80) {
				mState = 0;
			} else {
				ProcessDataByte(c);
				return;
			}
		}

		if (mbInSysEx) {
			if (c >= 0x80) {
				if (c == 0xF7) {
					g_ATLCMIDI("SysEx message (ignored)\n", mMsgStatus);
				} else {
					return;
				}
			}

			mSysExBuffer[mSysExLength++] = c;
			if (!mSysExLength)
				mbInSysEx = false;
			return;
		}

		if (c < 0x80) {
			if (mMsgStatus < 0x80 || mMsgStatus >= 0xF0)
				return;
			ProcessVoiceMessageStatus(mMsgStatus);
			ProcessDataByte(c);
			return;
		}

		if (c < 0xF0 && mbInSysEx)
			return;

		mMsgStatus = c;

		if (c < 0xF0)
			ProcessVoiceMessageStatus(c);
		else
			ProcessSystemCommonMessageStatus(c);
	}

	void ProcessVoiceMessageStatus(uint8 c) {
		switch (c & 0xF0) {
		case 0x80: case 0x90: case 0xA0: case 0xB0: case 0xE0:
			mState = 1; break;
		case 0xC0: case 0xD0:
			mState = 3; break;
		}
	}

	void ProcessSystemCommonMessageStatus(uint8 c) {
		switch (c) {
		case 0xF0: mbInSysEx = true; break;
		case 0xF1: mState = 1; break;
		case 0xF2: mState = 1; break;
		case 0xF3: mState = 3; break;
		case 0xF7: mbInSysEx = false; break;
		case 0xF6:
			g_ATLCMIDI("Message out: %02X\n", mMsgStatus);
			mSink.SendShort(mMsgStatus);
			break;
		}
	}

	void ProcessDataByte(uint8 c) {
		switch (mState) {
		case 1:
			mMsgData1 = c;
			++mState;
			break;
		case 2:
			mMsgData2 = c;
			g_ATLCMIDI("Message out: %02X %02X %02X\n", mMsgStatus, mMsgData1, mMsgData2);
			mState = 0;
			mSink.SendShort(mMsgStatus
				+ ((uint32)mMsgData1 << 8)
				+ ((uint32)mMsgData2 << 16));
			break;
		case 3:
			mMsgData1 = c;
			g_ATLCMIDI("Message out: %02X %02X\n", mMsgStatus, mMsgData1);
			mSink.SendShort(mMsgStatus + ((uint32)mMsgData1 << 8));
			mState = 0;
			break;
		}
	}

	bool mbInSysEx {};
	uint32 mState {};
	uint8 mMsgStatus {};
	uint8 mMsgData1 {};
	uint8 mMsgData2 {};
	uint8 mSysExLength {};
	uint8 mSysExBuffer[256] {};

	ATMidiSink mSink;
};

////////////////////////////////////////////////////////////////////////////////
// ATDeviceMidiMate — verbatim port of the Windows class. The only
// difference is that mpMidiOutput uses our portable ATMidiOutput.
////////////////////////////////////////////////////////////////////////////////

class ATDeviceMidiMate final
	: public ATDevice
	, public ATDeviceSIO
	, public IATDeviceRawSIO
	, public IATSchedulerCallback
{
	ATDeviceMidiMate(const ATDeviceMidiMate&) = delete;
	ATDeviceMidiMate& operator=(const ATDeviceMidiMate&) = delete;
public:
	ATDeviceMidiMate() = default;

	void *AsInterface(uint32 id) override;

	void GetDeviceInfo(ATDeviceInfo& info) override;
	void GetSettings(ATPropertySet&) override {}
	bool SetSettings(const ATPropertySet&) override { return true; }
	void Init() override;
	void Shutdown() override;
	void ColdReset() override;

	void InitSIO(IATDeviceSIOManager *mgr) override;

	void OnCommandStateChanged(bool asserted) override;
	void OnMotorStateChanged(bool asserted) override;
	void OnReceiveByte(uint8 c, bool command, uint32 cyclesPerBit) override;
	void OnSendReady() override;

	void OnScheduledEvent(uint32 id) override;

protected:
	IATDeviceSIOManager *mpSIOMgr = nullptr;
	vdrefptr<IATDeviceSIOInterface> mpSIOInterface;
	bool mbActive {};

	vdrefptr<ATMidiOutput> mpMidiOutput;
	IATDeviceSerial *mpCurrentOutput = nullptr;

	ATScheduler *mpScheduler = nullptr;
	ATEvent *mpEvent = nullptr;

	ATDeviceParentSingleChild mMidiPort;
};

void ATCreateDeviceMidiMate(const ATPropertySet&, IATDevice **dev) {
	vdrefptr<ATDeviceMidiMate> p(new ATDeviceMidiMate);
	*dev = p.release();
}

extern const ATDeviceDefinition g_ATDeviceDefMidiMate = {
	"midimate", nullptr, L"MidiMate", ATCreateDeviceMidiMate
};

void *ATDeviceMidiMate::AsInterface(uint32 id) {
	switch (id) {
	case IATDeviceSIO::kTypeID:    return static_cast<IATDeviceSIO *>(this);
	case IATDeviceRawSIO::kTypeID: return static_cast<IATDeviceRawSIO *>(this);
	case IATDeviceParent::kTypeID: return static_cast<IATDeviceParent *>(&mMidiPort);
	}
	return ATDevice::AsInterface(id);
}

void ATDeviceMidiMate::GetDeviceInfo(ATDeviceInfo& info) {
	info.mpDef = &g_ATDeviceDefMidiMate;
}

void ATDeviceMidiMate::Init() {
	mMidiPort.Init(IATDeviceSerial::kTypeID, "serial", L"MIDI I/O port", "midiport", this);
	mMidiPort.SetOnAttach(
		[this] {
			mpCurrentOutput = mMidiPort.GetChild<IATDeviceSerial>();
			mpMidiOutput = nullptr;

			if (mpCurrentOutput) {
				mpCurrentOutput->SetOnReadReady(
					[this] {
						if (!mpEvent)
							mpScheduler->SetEvent(1, this, 1, mpEvent);
					}
				);
			}
		}
	);

	mMidiPort.SetOnDetach(
		[this] {
			if (mpCurrentOutput) {
				mpCurrentOutput->SetOnReadReady(nullptr);
				mpCurrentOutput = nullptr;
			}
		}
	);

	mpScheduler = GetService<IATDeviceSchedulingService>()->GetMachineScheduler();
}

void ATDeviceMidiMate::Shutdown() {
	mpCurrentOutput = nullptr;
	mpMidiOutput = nullptr;

	mMidiPort.Shutdown();

	if (mpScheduler) {
		mpScheduler->UnsetEvent(mpEvent);
		mpScheduler = nullptr;
	}

	mpSIOInterface = nullptr;

	if (mpSIOMgr) {
		mpSIOMgr->RemoveRawDevice(this);
		mpSIOMgr = nullptr;
	}
}

void ATDeviceMidiMate::ColdReset() {
	if (mpMidiOutput)
		mpMidiOutput->Reset();

	mpScheduler->SetEvent(1, this, 1, mpEvent);
}

void ATDeviceMidiMate::InitSIO(IATDeviceSIOManager *mgr) {
	mpSIOMgr = mgr;
	mpSIOMgr->AddRawDevice(this);
}

void ATDeviceMidiMate::OnCommandStateChanged(bool) {}

void ATDeviceMidiMate::OnMotorStateChanged(bool asserted) {
	mbActive = asserted;
	mpSIOMgr->SetExternalClock(this, 0, asserted ? 57 : 0);
}

void ATDeviceMidiMate::OnReceiveByte(uint8 c, bool, uint32 cyclesPerBit) {
	if (!mbActive || !cyclesPerBit)
		return;

	if (!mpCurrentOutput) {
		mpMidiOutput = new ATMidiOutput;
		mpCurrentOutput = mpMidiOutput;
	}

	mpCurrentOutput->Write((uint32)(0.5f + 1789772.5f / (float)cyclesPerBit), c);
}

void ATDeviceMidiMate::OnSendReady() {}

void ATDeviceMidiMate::OnScheduledEvent(uint32) {
	mpEvent = nullptr;

	if (mpCurrentOutput) {
		uint8 c = 0;
		bool framingError = false;
		if (mpCurrentOutput->Read(31250, c, framingError)) {
			mpScheduler->SetEvent(573, this, 1, mpEvent);

			if (mbActive)
				mpSIOMgr->SendRawByte(c, 57, true);
		}
	}
}
