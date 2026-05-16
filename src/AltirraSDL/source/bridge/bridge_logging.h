// AltirraBridge - persistent diagnostics log

#ifndef ALTIRRASDL_BRIDGE_LOGGING_H
#define ALTIRRASDL_BRIDGE_LOGGING_H

#include <cstdarg>

namespace ATBridgeLog {

void Init(const char *path = nullptr);
void Shutdown();
const char *GetPath();
bool IsOpen();

void Write(const char *level, const char *tag, const char *fmt, ...);
void WriteV(const char *level, const char *tag, const char *fmt, va_list ap);

} // namespace ATBridgeLog

#define BRIDGE_LOG_ERROR(tag, fmt, ...) \
	ATBridgeLog::Write("ERROR", tag, fmt __VA_OPT__(,) __VA_ARGS__)

#define BRIDGE_LOG_WARN(tag, fmt, ...) \
	ATBridgeLog::Write("WARN", tag, fmt __VA_OPT__(,) __VA_ARGS__)

#define BRIDGE_LOG_INFO(tag, fmt, ...) \
	ATBridgeLog::Write("INFO", tag, fmt __VA_OPT__(,) __VA_ARGS__)

#endif
