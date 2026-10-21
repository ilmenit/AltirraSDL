//	Altirra - Atari 800/800XL/5200 emulator
//	SDL3 frontend - crash report persistence & viewer

#include <stdafx.h>
#include "crash_report.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/stat.h>

#include <SDL3/SDL.h>
#include <imgui.h>

#include <vd2/system/VDString.h>
#include <vd2/system/text.h>

// Provided by src/system/source/configdir_sdl3.cpp.
extern VDStringA ATGetConfigDir();

namespace {

// In-memory buffer that backs the ImGui viewer.  Populated by either
// ATCrashReportLoadPrevious() (from a previous session) or
// ATCrashReportShowNow() (current session).  Empty means "no report
// pending — do not open the window".
VDStringA s_pendingReport;

// Sticky open flag for the ImGui window.  Goes false when the user hits
// the close button and stays false for the remainder of the session.
bool s_viewerOpen = false;

const char *CrashLogPath() {
	static VDStringA s_path;
	if (s_path.empty()) {
		s_path = ATGetConfigDir();
		s_path += "/last_crash.txt";
	}
	return s_path.c_str();
}

const char *CrashLogSeenPath() {
	static VDStringA s_path;
	if (s_path.empty()) {
		s_path = ATGetConfigDir();
		s_path += "/last_crash.txt.seen";
	}
	return s_path.c_str();
}

// The config directory is UTF-8; narrow fopen on Windows would use the
// legacy code page and fail for non-ASCII user names.
FILE *OpenUtf8(const char *path, const char *mode) {
#ifdef _WIN32
	return _wfopen(VDTextU8ToW(VDStringSpanA(path)).c_str(),
		VDTextU8ToW(VDStringSpanA(mode)).c_str());
#else
	return fopen(path, mode);
#endif
}

} // namespace

void ATCrashReportWrite(const char *phase, const char *message) {
	// Best-effort.  Must not throw.  Must not depend on anything that
	// might itself be in a broken state at the moment we are called.
	FILE *f = OpenUtf8(CrashLogPath(), "a");
	if (!f)
		return;

	time_t now = time(nullptr);
	char timebuf[64];
	timebuf[0] = 0;
	struct tm tmbuf;
#ifdef _WIN32
	if (localtime_s(&tmbuf, &now) == 0)
		strftime(timebuf, sizeof timebuf, "%Y-%m-%d %H:%M:%S", &tmbuf);
#else
	if (localtime_r(&now, &tmbuf))
		strftime(timebuf, sizeof timebuf, "%Y-%m-%d %H:%M:%S", &tmbuf);
#endif

	fprintf(f, "=== Altirra crash report ===\n");
	fprintf(f, "Time:    %s\n", timebuf[0] ? timebuf : "?");
	fprintf(f, "Phase:   %s\n", phase ? phase : "?");
	fprintf(f, "Message: %s\n", message ? message : "?");
	fprintf(f, "\n");
	fclose(f);
}

void ATCrashReportLoadPrevious() {
	FILE *f = OpenUtf8(CrashLogPath(), "rb");
	if (!f)
		return;

	if (fseek(f, 0, SEEK_END) != 0) { fclose(f); return; }
	long len = ftell(f);
	if (len < 0) { fclose(f); return; }
	if (len > 256 * 1024) len = 256 * 1024;  // sanity clamp
	rewind(f);

	s_pendingReport.resize((size_t)len);
	if (len > 0) {
		size_t got = fread(&s_pendingReport[0], 1, (size_t)len, f);
		if (got != (size_t)len)
			s_pendingReport.resize(got);
	}
	fclose(f);

	// Rename so we show it at most once.  If rename fails (readonly FS,
	// etc.) fall back to unlink; if that also fails we will show it
	// again next launch, which is annoying but not dangerous.
	// SDL's path functions take UTF-8 on every platform.
	SDL_RemovePath(CrashLogSeenPath());  // clear any prior "seen" copy
	if (!SDL_RenamePath(CrashLogPath(), CrashLogSeenPath()))
		SDL_RemovePath(CrashLogPath());

	if (!s_pendingReport.empty()) {
		s_viewerOpen = true;
		SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,
			"Altirra: previous-session crash report loaded (%zu bytes)",
			(size_t)s_pendingReport.size());
	}
}

void ATCrashReportShowNow(const char *phase, const char *message) {
	char header[256];
	SDL_snprintf(header, sizeof header,
		"=== Altirra crash report (this session) ===\n"
		"Phase:   %s\n"
		"Message: %s\n\n",
		phase ? phase : "?", message ? message : "?");

	if (!s_pendingReport.empty())
		s_pendingReport += "\n";
	s_pendingReport += header;
	s_viewerOpen = true;
}

void ATCrashReportRender() {
	if (!s_viewerOpen || s_pendingReport.empty())
		return;

	ImGuiIO& io = ImGui::GetIO();
	const ImVec2 center = ImVec2(io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f);
	ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));

	// Cap the window size to the display so it works on phones.
	const float maxW = io.DisplaySize.x * 0.95f;
	const float maxH = io.DisplaySize.y * 0.85f;
	float w = 700.0f, h = 500.0f;
	if (w > maxW) w = maxW;
	if (h > maxH) h = maxH;
	ImGui::SetNextWindowSize(ImVec2(w, h), ImGuiCond_Appearing);

	if (ImGui::Begin("Crash report", &s_viewerOpen,
			ImGuiWindowFlags_NoSavedSettings))
	{
		ImGui::TextWrapped(
			"Altirra recorded a fatal error. The full log below has also "
			"been written to the Android system log (logcat, tag \"SDL\"). "
			"You can long-press / drag inside the text area to select, "
			"or hit \"Copy to clipboard\" to grab the whole thing.");
		ImGui::Separator();

		if (ImGui::Button("Copy to clipboard")) {
			ImGui::SetClipboardText(s_pendingReport.c_str());
		}
		ImGui::SameLine();
		if (ImGui::Button("Close")) {
			s_viewerOpen = false;
		}

		ImGui::Separator();

		// Read-only multiline input: ImGui renders this with selection,
		// cursor, Ctrl+C, and on Android the SDL3 clipboard bridge lets
		// the selection go to the system clipboard.
		//
		// The buffer is const-cast because ImGui::InputTextMultiline does
		// not have a read-only-only overload; the ReadOnly flag guarantees
		// it is not actually modified.
		const ImVec2 avail = ImGui::GetContentRegionAvail();
		ImGui::InputTextMultiline("##crashtext",
			const_cast<char*>(s_pendingReport.c_str()),
			s_pendingReport.size() + 1,
			avail,
			ImGuiInputTextFlags_ReadOnly);
	}
	ImGui::End();
}
