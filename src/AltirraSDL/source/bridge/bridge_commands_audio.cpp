// AltirraBridge - audio capture command (impl)
//
// AUDIO_RECORD start path=<file> [raw] [stereo]
// AUDIO_RECORD stop
// AUDIO_RECORD status
//
// Captures the mixed POKEY output to a WAV (or raw float) file by attaching
// an ATAudioWriter as the audio output's tap. The tap reads the pre-resample
// source mix in audiooutput_sdl3.cpp, so it works in --headless (dummy audio
// device) just as well as with a real device. The writer resamples to
// 44100 Hz 16-bit; mono by default (one ATAudioWriter, stereo=false).
//
// This command is intentionally isolated in its own translation unit and is
// compiled ONLY into the GUI AltirraSDL target (which links ATAudioWriter via
// ui_recording.cpp). The headless AltirraBridgeServer target builds audio with
// -DALTIRRA_AUDIO_NULL and does not link ATAudioWriter, so the dispatch in
// bridge_server.cpp is guarded by ALTIRRA_BRIDGE_AUDIO_REC.

#include <stdafx.h>

#include "bridge_protocol.h"

#include "simulator.h"
#include "constants.h"
#include "audiowriter.h"

#include <at/ataudio/audiooutput.h>

#include <string>
#include <vector>

namespace ATBridge {

namespace {

// Local copies of the small JSON-builder helpers (the shared bridge_protocol.h
// only exposes JsonOk/JsonError/JsonEscape; the field builders live file-scope
// in each command TU). Kept identical to bridge_commands_debug.cpp.
void AddField(std::string& out, const char* key, const std::string& valueLiteral) {
	out += '"';  out += key;  out += "\":";  out += valueLiteral;  out += ',';
}
void AddU32(std::string& out, const char* key, uint32_t value) {
	out += '"';  out += key;  out += "\":";  out += std::to_string(value);  out += ',';
}
void AddStr(std::string& out, const char* key, const std::string& value) {
	out += '"';  out += key;  out += "\":\"";  out += JsonEscape(value);  out += "\",";
}
void AddBool(std::string& out, const char* key, bool value) {
	out += '"';  out += key;  out += "\":";  out += (value ? "true" : "false");  out += ',';
}
void StripTrailingComma(std::string& s) {
	if (!s.empty() && s.back() == ',') s.pop_back();
}

// Live recording session. Single client / single recording at a time, which
// matches the bridge's single-client contract.
ATAudioWriter *g_pBridgeAudioWriter = nullptr;
std::string    g_bridgeAudioPath;
bool           g_bridgeAudioRaw = false;
bool           g_bridgeAudioStereo = false;

std::wstring Widen(const std::string& s) {
	// Path is ASCII in practice (test harness controls it); widen byte-wise.
	std::wstring w;
	w.reserve(s.size());
	for (unsigned char c : s)
		w.push_back((wchar_t)c);
	return w;
}

void StopRecordingInternal(ATSimulator& sim) {
	if (!g_pBridgeAudioWriter)
		return;
	sim.GetAudioOutput()->SetAudioTap(nullptr);
	try {
		g_pBridgeAudioWriter->Finalize();
	} catch (...) {
		// best-effort flush; the file may still be usable
	}
	delete g_pBridgeAudioWriter;
	g_pBridgeAudioWriter = nullptr;
}

} // namespace

std::string CmdAudioRecord(ATSimulator& sim, const std::vector<std::string>& tokens) {
	if (tokens.size() < 2)
		return JsonError("AUDIO_RECORD: need a subcommand (start|stop|status)");

	const std::string& sub = tokens[1];

	if (sub == "status") {
		std::string payload;
		AddBool (payload, "recording", g_pBridgeAudioWriter != nullptr);
		if (g_pBridgeAudioWriter) {
			AddStr  (payload, "path",   g_bridgeAudioPath);
			AddBool (payload, "raw",    g_bridgeAudioRaw);
			AddBool (payload, "stereo", g_bridgeAudioStereo);
		}
		AddU32  (payload, "sample_rate", 44100);
		StripTrailingComma(payload);
		return JsonOk(payload);
	}

	if (sub == "stop") {
		const bool was = (g_pBridgeAudioWriter != nullptr);
		StopRecordingInternal(sim);
		std::string payload;
		AddBool (payload, "was_recording", was);
		AddStr  (payload, "path", g_bridgeAudioPath);
		StripTrailingComma(payload);
		g_bridgeAudioPath.clear();
		return JsonOk(payload);
	}

	if (sub == "start") {
		// Parse path=<file> and optional raw / stereo flags.
		std::string path;
		bool raw = false;
		bool stereo = false;
		for (size_t i = 2; i < tokens.size(); ++i) {
			const std::string& t = tokens[i];
			if (t.rfind("path=", 0) == 0)
				path = t.substr(5);
			else if (t == "raw")
				raw = true;
			else if (t == "stereo")
				stereo = true;
			else
				return JsonError("AUDIO_RECORD start: unknown option: " + t);
		}
		if (path.empty())
			return JsonError("AUDIO_RECORD start: path= is required");

		// Replace any in-flight recording.
		StopRecordingInternal(sim);

		const bool pal = (sim.GetVideoStandard() == kATVideoStandard_PAL);
		try {
			g_pBridgeAudioWriter = new ATAudioWriter(Widen(path).c_str(), raw, stereo, pal, nullptr);
			sim.GetAudioOutput()->SetAudioTap(g_pBridgeAudioWriter);
		} catch (...) {
			delete g_pBridgeAudioWriter;
			g_pBridgeAudioWriter = nullptr;
			return JsonError("AUDIO_RECORD start: failed to open " + path);
		}

		g_bridgeAudioPath = path;
		g_bridgeAudioRaw = raw;
		g_bridgeAudioStereo = stereo;

		std::string payload;
		AddStr  (payload, "path",   path);
		AddBool (payload, "raw",    raw);
		AddBool (payload, "stereo", stereo);
		AddBool (payload, "pal",    pal);
		AddU32  (payload, "sample_rate", 44100);
		StripTrailingComma(payload);
		return JsonOk(payload);
	}

	return JsonError("AUDIO_RECORD: unknown subcommand: " + sub);
}

} // namespace ATBridge
