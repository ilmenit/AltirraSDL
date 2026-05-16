// AltirraBridge - persistent diagnostics log

#include <stdafx.h>

#include "bridge_logging.h"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <mutex>
#include <string>

#if defined(_WIN32)
#  include <process.h>
#  define BRIDGE_LOG_GETPID() _getpid()
#else
#  include <unistd.h>
#  define BRIDGE_LOG_GETPID() getpid()
#endif

namespace ATBridgeLog {
namespace {

std::mutex g_mutex;
FILE *g_file = nullptr;
std::string g_path;
bool g_initialised = false;

std::string TmpDir() {
	const char* env = std::getenv("TMPDIR");
	if (!env || !*env) env = std::getenv("TEMP");
	if (!env || !*env) env = std::getenv("TMP");
#if defined(_WIN32)
	if (!env || !*env) env = ".";
#else
	if (!env || !*env) env = "/tmp";
#endif
	return std::string(env);
}

std::string DefaultLogPath() {
	char name[80];
	std::snprintf(name, sizeof name, "altirra-bridge-%d.log",
		(int)BRIDGE_LOG_GETPID());

	std::string path = TmpDir();
	if (!path.empty()) {
		const char last = path.back();
		if (last != '/' && last != '\\')
#if defined(_WIN32)
			path += "\\";
#else
			path += "/";
#endif
	}
	path += name;
	return path;
}

void FormatTimestamp(char *buf, size_t len) {
	using namespace std::chrono;
	const auto now = system_clock::now();
	const auto ms = duration_cast<milliseconds>(
		now.time_since_epoch()).count() % 1000;
	const std::time_t t = system_clock::to_time_t(now);

	std::tm tmv {};
#if defined(_WIN32)
	localtime_s(&tmv, &t);
#else
	localtime_r(&t, &tmv);
#endif

	char base[32];
	std::strftime(base, sizeof base, "%Y-%m-%d %H:%M:%S", &tmv);
	std::snprintf(buf, len, "%s.%03lld", base, (long long)ms);
}

void WriteLineLocked(const char *level, const char *tag, const char *line) {
	char ts[48];
	FormatTimestamp(ts, sizeof ts);

	std::fprintf(stderr, "[%s] %s\n", tag ? tag : "?", line ? line : "");

	if (g_file) {
		std::fprintf(g_file, "%s [%s] [%s] %s\n",
			ts,
			level ? level : "INFO",
			tag ? tag : "?",
			line ? line : "");
		std::fflush(g_file);
	}
}

} // namespace

void Init(const char *path) {
	std::lock_guard<std::mutex> lock(g_mutex);
	if (g_initialised)
		return;

	g_initialised = true;
	g_path = (path && *path) ? std::string(path) : DefaultLogPath();
	g_file = std::fopen(g_path.c_str(), "w");

	if (g_file) {
		WriteLineLocked("INFO", "BridgeLog", "persistent log opened");
		WriteLineLocked("INFO", "BridgeLog", g_path.c_str());
	} else {
		char msg[1024];
		std::snprintf(msg, sizeof msg,
			"could not open persistent log file: %s", g_path.c_str());
		WriteLineLocked("ERROR", "BridgeLog", msg);
	}
}

void Shutdown() {
	std::lock_guard<std::mutex> lock(g_mutex);
	if (g_file) {
		WriteLineLocked("INFO", "BridgeLog", "persistent log closed");
		std::fclose(g_file);
		g_file = nullptr;
	}
	g_initialised = false;
}

const char *GetPath() {
	return g_path.c_str();
}

bool IsOpen() {
	std::lock_guard<std::mutex> lock(g_mutex);
	return g_file != nullptr;
}

void WriteV(const char *level, const char *tag, const char *fmt, va_list ap) {
	char stack[2048];
	va_list ap2;
	va_copy(ap2, ap);
	int n = std::vsnprintf(stack, sizeof stack, fmt, ap2);
	va_end(ap2);

	std::lock_guard<std::mutex> lock(g_mutex);
	if (!g_initialised) {
		g_initialised = true;
		g_path.clear();
	}

	if (n < 0) {
		WriteLineLocked(level, tag, fmt ? fmt : "(format error)");
	} else if ((size_t)n < sizeof stack) {
		WriteLineLocked(level, tag, stack);
	} else {
		std::string big;
		big.resize((size_t)n + 1);
		std::vsnprintf(big.data(), big.size(), fmt, ap);
		big.resize((size_t)n);
		WriteLineLocked(level, tag, big.c_str());
	}
}

void Write(const char *level, const char *tag, const char *fmt, ...) {
	va_list ap;
	va_start(ap, fmt);
	WriteV(level, tag, fmt, ap);
	va_end(ap);
}

} // namespace ATBridgeLog
