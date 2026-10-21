//	AltirraSDL - HTTPS client implementation
//	See http_client.h for the contract and the per-platform rationale.

#include <stdafx.h>

#include "http_client.h"

#include <cstdio>
#include <chrono>
#include <cstring>
#include <mutex>

namespace ATHttp {

// ---------------------------------------------------------------------------
// Shared helpers
// ---------------------------------------------------------------------------

void UrlEncode(const char* s, size_t n, std::string& out) {
	static const char kHex[] = "0123456789ABCDEF";
	for (size_t i = 0; i < n; ++i) {
		const unsigned char c = (unsigned char)s[i];
		const bool unreserved =
			(c >= 'A' && c <= 'Z') ||
			(c >= 'a' && c <= 'z') ||
			(c >= '0' && c <= '9') ||
			c == '-' || c == '_' || c == '.' || c == '~';
		if (unreserved) {
			out.push_back((char)c);
		} else {
			out.push_back('%');
			out.push_back(kHex[c >> 4]);
			out.push_back(kHex[c & 15]);
		}
	}
}

void UrlEncode(const std::string& s, std::string& out) {
	UrlEncode(s.data(), s.size(), out);
}

namespace {

// Normalise a Content-Type header value: lower-case, parameters stripped.
// "image/PNG; charset=binary" -> "image/png"
std::string NormalizeContentType(const char* raw) {
	std::string ct;
	if (!raw)
		return ct;
	for (const char* p = raw; *p && *p != ';'; ++p) {
		char c = *p;
		if (c >= 'A' && c <= 'Z')
			c = (char)(c - 'A' + 'a');
		ct.push_back(c);
	}
	while (!ct.empty() && (ct.back() == ' ' || ct.back() == '\t'))
		ct.pop_back();
	size_t lead = 0;
	while (lead < ct.size() && (ct[lead] == ' ' || ct[lead] == '\t'))
		++lead;
	if (lead)
		ct.erase(0, lead);
	return ct;
}

bool IsCancelled(const Request& in) {
	return in.cancel && in.cancel->load(std::memory_order_relaxed);
}

}  // namespace

}  // namespace ATHttp

// ===========================================================================
// Windows — WinHTTP
// ===========================================================================
#if defined(_WIN32)

#include <windows.h>
#include <winhttp.h>

namespace ATHttp {

bool Available() { return true; }
const char* BackendName() { return "WinHTTP"; }

namespace {

std::wstring WidenUtf8(const std::string& s) {
	if (s.empty())
		return std::wstring();
	int need = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(),
		nullptr, 0);
	std::wstring w;
	if (need <= 0)
		return w;
	w.resize((size_t)need);
	MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), &w[0], need);
	return w;
}

std::string NarrowUtf8(const wchar_t* s, size_t n) {
	if (!s || !n)
		return std::string();
	int need = WideCharToMultiByte(CP_UTF8, 0, s, (int)n,
		nullptr, 0, nullptr, nullptr);
	std::string a;
	if (need <= 0)
		return a;
	a.resize((size_t)need);
	WideCharToMultiByte(CP_UTF8, 0, s, (int)n, &a[0], need, nullptr, nullptr);
	return a;
}

struct HInternetGuard {
	HINTERNET h = nullptr;
	~HInternetGuard() { if (h) WinHttpCloseHandle(h); }
};

std::string LastErrorText(const char* where) {
	char buf[160];
	snprintf(buf, sizeof buf, "%s failed (WinHTTP error %lu)", where,
		(unsigned long)GetLastError());
	return std::string(buf);
}

}  // namespace

void Get(const Request& in, Response& out) {
	out = Response();
	const auto deadline = std::chrono::steady_clock::now() +
		std::chrono::milliseconds(in.timeoutMs ? in.timeoutMs : 20000);

	if (in.url.empty()) {
		out.error = "empty URL";
		return;
	}

	const std::wstring wurl = WidenUtf8(in.url);

	// Crack the URL into components.  WinHttpCrackUrl wants writable
	// component buffers when the lengths are zero-initialised, so give
	// it explicit storage.
	wchar_t hostBuf[256] = {};
	wchar_t pathBuf[2048] = {};
	wchar_t extraBuf[2048] = {};

	URL_COMPONENTSW uc = {};
	uc.dwStructSize = sizeof(uc);
	uc.lpszHostName = hostBuf;      uc.dwHostNameLength = 255;
	uc.lpszUrlPath = pathBuf;       uc.dwUrlPathLength = 2047;
	uc.lpszExtraInfo = extraBuf;    uc.dwExtraInfoLength = 2047;

	if (!WinHttpCrackUrl(wurl.c_str(), (DWORD)wurl.size(), 0, &uc)) {
		out.error = LastErrorText("WinHttpCrackUrl");
		return;
	}

	const std::wstring wagent = in.userAgent.empty()
		? std::wstring(L"AltirraSDL")
		: WidenUtf8(in.userAgent);

	HInternetGuard session;
	session.h = WinHttpOpen(wagent.c_str(),
		WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
		WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
	if (!session.h) {
		// AUTOMATIC_PROXY honours WPAD, which is what a corporate or
		// captive network needs, but it is not available everywhere.
		// Fall back to the machine's configured proxy rather than
		// failing outright.
		session.h = WinHttpOpen(wagent.c_str(),
			WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
			WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
	}
	if (!session.h) {
		out.error = LastErrorText("WinHttpOpen");
		return;
	}

	const DWORD tmo = in.timeoutMs ? in.timeoutMs : 20000;
	// Keep cancellation/shutdown bounded even while a synchronous OS
	// request is waiting on an unresponsive server. The complete transfer
	// retains its longer deadline, checked between network operations.
	const int wait = (int)(tmo < 5000 ? tmo : 5000);
	WinHttpSetTimeouts(session.h, wait, wait, wait, wait);

	HInternetGuard connect;
	connect.h = WinHttpConnect(session.h, hostBuf, uc.nPort, 0);
	if (!connect.h) {
		out.error = LastErrorText("WinHttpConnect");
		return;
	}

	// "https://host" cracks to an empty path; WinHttpOpenRequest needs
	// "/" for that, not an empty object name.
	std::wstring objectName(pathBuf);
	objectName += extraBuf;
	if (objectName.empty())
		objectName = L"/";

	DWORD reqFlags = WINHTTP_FLAG_REFRESH;
	if (uc.nScheme == INTERNET_SCHEME_HTTPS)
		reqFlags |= WINHTTP_FLAG_SECURE;

	HInternetGuard request;
	request.h = WinHttpOpenRequest(connect.h, L"GET", objectName.c_str(),
		nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES,
		reqFlags);
	if (!request.h) {
		out.error = LastErrorText("WinHttpOpenRequest");
		return;
	}

	// gzip/deflate for the JSON responses; WinHTTP decodes transparently.
	DWORD decomp = WINHTTP_DECOMPRESSION_FLAG_ALL;
	WinHttpSetOption(request.h, WINHTTP_OPTION_DECOMPRESSION,
		&decomp, sizeof decomp);

	if (IsCancelled(in)) { out.error = "cancelled"; return; }

	if (!WinHttpSendRequest(request.h, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
		WINHTTP_NO_REQUEST_DATA, 0, 0, 0))
	{
		out.error = LastErrorText("WinHttpSendRequest");
		return;
	}
	if (IsCancelled(in)) { out.error = "cancelled"; return; }

	if (!WinHttpReceiveResponse(request.h, nullptr)) {
		out.error = LastErrorText("WinHttpReceiveResponse");
		return;
	}
	if (IsCancelled(in)) { out.error = "cancelled"; return; }

	DWORD statusCode = 0;
	DWORD statusSize = sizeof statusCode;
	if (WinHttpQueryHeaders(request.h,
		WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
		WINHTTP_HEADER_NAME_BY_INDEX, &statusCode, &statusSize,
		WINHTTP_NO_HEADER_INDEX))
	{
		out.status = (int)statusCode;
	}

	// Content-Type (optional).
	{
		DWORD ctSize = 0;
		WinHttpQueryHeaders(request.h, WINHTTP_QUERY_CONTENT_TYPE,
			WINHTTP_HEADER_NAME_BY_INDEX, nullptr, &ctSize,
			WINHTTP_NO_HEADER_INDEX);
		if (ctSize > 0 && GetLastError() == ERROR_INSUFFICIENT_BUFFER) {
			std::wstring ctw;
			ctw.resize(ctSize / sizeof(wchar_t));
			if (WinHttpQueryHeaders(request.h, WINHTTP_QUERY_CONTENT_TYPE,
				WINHTTP_HEADER_NAME_BY_INDEX, &ctw[0], &ctSize,
				WINHTTP_NO_HEADER_INDEX))
			{
				while (!ctw.empty() && ctw.back() == L'\0')
					ctw.pop_back();
				out.contentType =
					NormalizeContentType(NarrowUtf8(ctw.data(),
						ctw.size()).c_str());
			}
		}
	}

	// Body.
	for (;;) {
		if (IsCancelled(in) || std::chrono::steady_clock::now() >= deadline) {
			out.status = 0;
			out.body.clear();
			out.error = IsCancelled(in) ? "cancelled" : "transfer timed out";
			return;
		}

		DWORD avail = 0;
		if (!WinHttpQueryDataAvailable(request.h, &avail)) {
			out.error = LastErrorText("WinHttpQueryDataAvailable");
			out.status = 0;
			return;
		}
		if (!avail)
			break;
		if (IsCancelled(in)) { out.error = "cancelled"; return; }

		if (in.maxBodyBytes && out.body.size() + avail > in.maxBodyBytes) {
			out.status = 0;
			out.body.clear();
			out.error = "response too large";
			return;
		}

		const size_t base = out.body.size();
		out.body.resize(base + avail);
		DWORD read = 0;
		if (!WinHttpReadData(request.h, out.body.data() + base, avail,
			&read))
		{
			out.error = LastErrorText("WinHttpReadData");
			out.status = 0;
			return;
		}
		out.body.resize(base + read);
		if (in.bytesReceived)
			in.bytesReceived->store(out.body.size(), std::memory_order_relaxed);
		if (!read)
			break;
	}
}

}  // namespace ATHttp

// ===========================================================================
// Android — java.net.HttpsURLConnection over JNI
// ===========================================================================
#elif defined(__ANDROID__)

#include <jni.h>
#include <SDL3/SDL.h>
#include <SDL3/SDL_system.h>

namespace ATHttp {

bool Available() { return true; }
const char* BackendName() { return "Android"; }

namespace {

// Scoped local-ref delete.  Mirrors the JLocal helper in
// app/android_platform.cpp; duplicated rather than shared because that
// file's helpers are file-local and this module must not depend on it.
struct JLocal {
	JNIEnv* env;
	jobject obj;
	JLocal(JNIEnv* e, jobject o) : env(e), obj(o) {}
	~JLocal() { if (obj) env->DeleteLocalRef(obj); }
	JLocal(const JLocal&) = delete;
	JLocal& operator=(const JLocal&) = delete;
	operator jobject() const { return obj; }
	explicit operator bool() const { return obj != nullptr; }
};

bool ClearException(JNIEnv* env) {
	if (!env->ExceptionCheck())
		return false;
	env->ExceptionClear();
	return true;
}

std::string JStringToUtf8(JNIEnv* env, jstring s) {
	std::string out;
	if (!s)
		return out;
	const char* chars = env->GetStringUTFChars(s, nullptr);
	if (chars) {
		out = chars;
		env->ReleaseStringUTFChars(s, chars);
	}
	return out;
}

}  // namespace

void Get(const Request& in, Response& out) {
	out = Response();
	const auto deadline = std::chrono::steady_clock::now() +
		std::chrono::milliseconds(in.timeoutMs ? in.timeoutMs : 20000);

	if (in.url.empty()) {
		out.error = "empty URL";
		return;
	}

	// SDL attaches the calling thread to the VM if needed, so this is
	// safe from the scraper's worker threads.
	JNIEnv* env = (JNIEnv*)SDL_GetAndroidJNIEnv();
	if (!env) {
		out.error = "no JNI environment";
		return;
	}

	jclass urlClass = env->FindClass("java/net/URL");
	if (!urlClass) { ClearException(env); out.error = "no java.net.URL"; return; }
	JLocal urlClassRef(env, urlClass);

	jmethodID urlCtor = env->GetMethodID(urlClass, "<init>",
		"(Ljava/lang/String;)V");
	jmethodID openConn = env->GetMethodID(urlClass, "openConnection",
		"()Ljava/net/URLConnection;");
	if (!urlCtor || !openConn) {
		ClearException(env);
		out.error = "java.net.URL methods missing";
		return;
	}

	// NewStringUTF expects Java's modified UTF-8, which differs from
	// standard UTF-8 only for embedded NULs and supplementary-plane
	// characters.  URLs reaching here are percent-encoded ASCII, so the
	// distinction cannot bite; anything else would be a provider bug.
	JLocal jurlStr(env, (jobject)env->NewStringUTF(in.url.c_str()));
	if (!jurlStr) { ClearException(env); out.error = "bad URL string"; return; }

	JLocal jurl(env, env->NewObject(urlClass, urlCtor, (jstring)jurlStr.obj));
	if (ClearException(env) || !jurl) {
		out.error = "malformed URL";
		return;
	}

	JLocal conn(env, env->CallObjectMethod(jurl, openConn));
	if (ClearException(env) || !conn) {
		out.error = "openConnection failed";
		return;
	}

	jclass httpConnClass = env->FindClass("java/net/HttpURLConnection");
	if (!httpConnClass) { ClearException(env); out.error = "no HttpURLConnection"; return; }
	JLocal httpConnClassRef(env, httpConnClass);

	const jint tmo = (jint)(in.timeoutMs && in.timeoutMs < 5000
		? in.timeoutMs : 5000);

	jmethodID setConnectTimeout = env->GetMethodID(httpConnClass,
		"setConnectTimeout", "(I)V");
	jmethodID setReadTimeout = env->GetMethodID(httpConnClass,
		"setReadTimeout", "(I)V");
	jmethodID setRequestProperty = env->GetMethodID(httpConnClass,
		"setRequestProperty", "(Ljava/lang/String;Ljava/lang/String;)V");
	jmethodID setFollow = env->GetMethodID(httpConnClass,
		"setInstanceFollowRedirects", "(Z)V");
	jmethodID getResponseCode = env->GetMethodID(httpConnClass,
		"getResponseCode", "()I");
	jmethodID getInputStream = env->GetMethodID(httpConnClass,
		"getInputStream", "()Ljava/io/InputStream;");
	jmethodID getErrorStream = env->GetMethodID(httpConnClass,
		"getErrorStream", "()Ljava/io/InputStream;");
	jmethodID getContentType = env->GetMethodID(httpConnClass,
		"getContentType", "()Ljava/lang/String;");
	jmethodID disconnect = env->GetMethodID(httpConnClass,
		"disconnect", "()V");

	if (!setConnectTimeout || !setReadTimeout || !getResponseCode
		|| !getInputStream || !disconnect)
	{
		ClearException(env);
		out.error = "HttpURLConnection methods missing";
		return;
	}

	env->CallVoidMethod(conn, setConnectTimeout, tmo);
	env->CallVoidMethod(conn, setReadTimeout, tmo);
	if (setFollow)
		env->CallVoidMethod(conn, setFollow, JNI_TRUE);
	if (setRequestProperty && !in.userAgent.empty()) {
		JLocal key(env, (jobject)env->NewStringUTF("User-Agent"));
		JLocal val(env, (jobject)env->NewStringUTF(in.userAgent.c_str()));
		env->CallVoidMethod(conn, setRequestProperty,
			(jstring)key.obj, (jstring)val.obj);
	}
	ClearException(env);

	// Ensure the connection is always torn down, on every exit path.
	struct Disconnector {
		JNIEnv* env; jobject conn; jmethodID mid;
		~Disconnector() {
			if (mid) { env->CallVoidMethod(conn, mid); env->ExceptionClear(); }
		}
	} disconnector{env, conn.obj, disconnect};

	if (IsCancelled(in)) { out.error = "cancelled"; return; }

	const jint code = env->CallIntMethod(conn, getResponseCode);
	if (ClearException(env)) {
		out.error = "connection failed";
		return;
	}
	out.status = (int)code;

	if (getContentType) {
		JLocal ct(env, env->CallObjectMethod(conn, getContentType));
		env->ExceptionClear();
		if (ct)
			out.contentType = NormalizeContentType(
				JStringToUtf8(env, (jstring)ct.obj).c_str());
	}

	// 4xx/5xx bodies arrive on the error stream; the provider encodes
	// its quota / not-found messages there, so we must read it.
	jobject rawStream = env->CallObjectMethod(conn,
		(code >= 400 && getErrorStream) ? getErrorStream : getInputStream);
	if (ClearException(env))
		rawStream = nullptr;
	JLocal stream(env, rawStream);
	if (!stream)
		return;   // status is set; empty body is legitimate here

	jclass isClass = env->FindClass("java/io/InputStream");
	if (!isClass) { ClearException(env); return; }
	JLocal isClassRef(env, isClass);

	jmethodID readMid = env->GetMethodID(isClass, "read", "([BII)I");
	jmethodID closeMid = env->GetMethodID(isClass, "close", "()V");
	if (!readMid) { ClearException(env); return; }

	const jint kChunk = 32768;
	JLocal buf(env, (jobject)env->NewByteArray(kChunk));
	if (!buf) { ClearException(env); return; }

	for (;;) {
		if (IsCancelled(in) || std::chrono::steady_clock::now() >= deadline) {
			out.status = 0;
			out.body.clear();
			out.error = IsCancelled(in) ? "cancelled" : "transfer timed out";
			break;
		}

		const jint n = env->CallIntMethod(stream, readMid,
			(jbyteArray)buf.obj, 0, kChunk);
		if (ClearException(env)) {
			out.status = 0;
			out.body.clear();
			out.error = "read failed";
			break;
		}
		if (n <= 0)
			break;

		if (in.maxBodyBytes
			&& out.body.size() + (size_t)n > in.maxBodyBytes)
		{
			out.status = 0;
			out.body.clear();
			out.error = "response too large";
			break;
		}

		const size_t base = out.body.size();
		out.body.resize(base + (size_t)n);
		env->GetByteArrayRegion((jbyteArray)buf.obj, 0, n,
			(jbyte*)(out.body.data() + base));
		if (in.bytesReceived)
			in.bytesReceived->store(out.body.size(), std::memory_order_relaxed);
		if (ClearException(env)) {
			out.status = 0;
			out.body.clear();
			out.error = "read failed";
			break;
		}
	}

	if (closeMid) {
		env->CallVoidMethod(stream, closeMid);
		env->ExceptionClear();
	}
}

}  // namespace ATHttp

// ===========================================================================
// WASM — unsupported
//
// Emscripten offers only asynchronous fetch, which cannot back a
// blocking Get().  Rather than fake it, the backend reports itself
// unavailable and the metadata UI degrades with an explanation.  A WASM
// build serves a curated game-pack library where metadata is better
// baked in when the pack is built than fetched at runtime.
// ===========================================================================
#elif defined(__EMSCRIPTEN__)

namespace ATHttp {

bool Available() { return false; }
const char* BackendName() { return "none"; }

void Get(const Request&, Response& out) {
	out = Response();
	out.error = "HTTPS is not available in the browser build";
}

}  // namespace ATHttp

// ===========================================================================
// Linux / macOS / other Unix — libcurl, loaded at runtime
//
// dlopen() rather than link:
//   - no build-time dependency, so no CI package install and no change
//     to the "SDL3 + Dear ImGui only" dependency rule;
//   - the portable AppImage does NOT bundle libcurl (linuxdeploy only
//     picks up what ldd reports), so it uses the host's libcurl and
//     therefore the host's correctly-configured CA store.  A bundled
//     libcurl+OpenSSL would look for CA certificates at paths that
//     differ across distributions and fail TLS verification on some.
// ===========================================================================
#else

#include <dlfcn.h>

namespace ATHttp {

namespace {

// libcurl option/info constants.  Reproduced here because we never
// include curl.h — see the file header for why the library is loaded
// at runtime.  Values are part of libcurl's ABI and never change.
enum : int {
	kOptLong          = 0,
	kOptObjectPoint   = 10000,
	kOptFunctionPoint = 20000,

	kOptURL               = kOptObjectPoint + 2,
	kOptWriteData         = kOptObjectPoint + 1,
	kOptWriteFunction     = kOptFunctionPoint + 11,
	kOptUserAgent         = kOptObjectPoint + 18,
	kOptAcceptEncoding    = kOptObjectPoint + 102,
	kOptXferInfoData      = kOptObjectPoint + 57,
	kOptXferInfoFunction  = kOptFunctionPoint + 219,
	kOptFollowLocation    = kOptLong + 52,
	kOptMaxRedirs         = kOptLong + 68,
	kOptNoSignal          = kOptLong + 99,
	kOptNoProgress        = kOptLong + 43,
	kOptTimeoutMs         = kOptLong + 155,
	kOptConnectTimeoutMs  = kOptLong + 156,

	kInfoString       = 0x100000,
	kInfoLong         = 0x200000,
	kInfoResponseCode = kInfoLong + 2,
	kInfoContentType  = kInfoString + 18,

	kGlobalDefault = 3,
	kErrOK         = 0,
	kErrAbortedByCallback = 42,
};

typedef void* (*fn_easy_init)(void);
typedef int   (*fn_easy_setopt)(void*, int, ...);
typedef int   (*fn_easy_perform)(void*);
typedef int   (*fn_easy_getinfo)(void*, int, ...);
typedef void  (*fn_easy_cleanup)(void*);
typedef int   (*fn_global_init)(long);
typedef const char* (*fn_easy_strerror)(int);

struct Curl {
	void* lib = nullptr;
	fn_easy_init     easy_init = nullptr;
	fn_easy_setopt   easy_setopt = nullptr;
	fn_easy_perform  easy_perform = nullptr;
	fn_easy_getinfo  easy_getinfo = nullptr;
	fn_easy_cleanup  easy_cleanup = nullptr;
	fn_easy_strerror easy_strerror = nullptr;
	bool ok = false;
};

Curl g_curl;
std::once_flag g_curlOnce;

void LoadCurl() {
	static const char* kCandidates[] = {
#if defined(__APPLE__)
		"libcurl.4.dylib",
		"/usr/lib/libcurl.4.dylib",
		"libcurl.dylib",
#else
		"libcurl.so.4",
		"libcurl.so.3",
		"libcurl.so",
#endif
	};

	for (const char* name : kCandidates) {
		g_curl.lib = dlopen(name, RTLD_LAZY | RTLD_LOCAL);
		if (g_curl.lib)
			break;
	}
	if (!g_curl.lib)
		return;

	auto sym = [](const char* n) { return dlsym(g_curl.lib, n); };

	g_curl.easy_init     = (fn_easy_init)sym("curl_easy_init");
	g_curl.easy_setopt   = (fn_easy_setopt)sym("curl_easy_setopt");
	g_curl.easy_perform  = (fn_easy_perform)sym("curl_easy_perform");
	g_curl.easy_getinfo  = (fn_easy_getinfo)sym("curl_easy_getinfo");
	g_curl.easy_cleanup  = (fn_easy_cleanup)sym("curl_easy_cleanup");
	g_curl.easy_strerror = (fn_easy_strerror)sym("curl_easy_strerror");
	fn_global_init globalInit = (fn_global_init)sym("curl_global_init");

	if (!g_curl.easy_init || !g_curl.easy_setopt || !g_curl.easy_perform
		|| !g_curl.easy_getinfo || !g_curl.easy_cleanup || !globalInit)
	{
		dlclose(g_curl.lib);
		g_curl.lib = nullptr;
		return;
	}

	// curl_global_init is not thread-safe; std::call_once around
	// LoadCurl() is what makes this correct when several worker threads
	// race to the first request.
	if (globalInit(kGlobalDefault) != kErrOK) {
		dlclose(g_curl.lib);
		g_curl.lib = nullptr;
		return;
	}

	g_curl.ok = true;
}

const Curl& GetCurl() {
	std::call_once(g_curlOnce, LoadCurl);
	return g_curl;
}

struct WriteCtx {
	std::vector<uint8_t>* body;
	size_t maxBytes;
	bool overflow;
	const Request* request;
};

size_t WriteCallback(char* ptr, size_t size, size_t nmemb, void* userdata) {
	WriteCtx* ctx = (WriteCtx*)userdata;
	const size_t n = size * nmemb;
	if (ctx->maxBytes && ctx->body->size() + n > ctx->maxBytes) {
		ctx->overflow = true;
		return 0;   // signals an error to libcurl, aborting the transfer
	}
	const size_t base = ctx->body->size();
	ctx->body->resize(base + n);
	memcpy(ctx->body->data() + base, ptr, n);
	if (ctx->request->bytesReceived)
		ctx->request->bytesReceived->store(base + n, std::memory_order_relaxed);
	return n;
}

int XferCallback(void* userdata, int64_t total, int64_t, int64_t, int64_t) {
	const Request& request = *(const Request*)userdata;
	if (request.bytesTotal)
		request.bytesTotal->store(total > 0 ? (uint64_t)total : 0,
			std::memory_order_relaxed);
	// Non-zero aborts the transfer with CURLE_ABORTED_BY_CALLBACK.
	return IsCancelled(request) ? 1 : 0;
}

}  // namespace

bool Available() { return GetCurl().ok; }

const char* BackendName() { return GetCurl().ok ? "libcurl" : "none"; }

void Get(const Request& in, Response& out) {
	out = Response();

	const Curl& c = GetCurl();
	if (!c.ok) {
		out.error = "libcurl is not installed on this system";
		return;
	}
	if (in.url.empty()) {
		out.error = "empty URL";
		return;
	}
	if (IsCancelled(in)) {
		out.error = "cancelled";
		return;
	}

	void* eh = c.easy_init();
	if (!eh) {
		out.error = "curl_easy_init failed";
		return;
	}

	WriteCtx ctx{&out.body, in.maxBodyBytes, false, &in};

	c.easy_setopt(eh, kOptURL, in.url.c_str());
	c.easy_setopt(eh, kOptWriteFunction, (void*)&WriteCallback);
	c.easy_setopt(eh, kOptWriteData, (void*)&ctx);
	c.easy_setopt(eh, kOptFollowLocation, (long)1);
	c.easy_setopt(eh, kOptMaxRedirs, (long)5);
	// CURLOPT_PROTOCOLS / CURLOPT_REDIR_PROTOCOLS (curl's stable ABI).
	// A redirect from a media server must not access file:// or other
	// protocols supported by the host's libcurl.
	c.easy_setopt(eh, kOptLong + 181, (long)3); // HTTP | HTTPS
	c.easy_setopt(eh, kOptLong + 182, (long)3);
	// Required when libcurl is used from threads: without it libcurl
	// installs signal handlers / uses alarm() for DNS timeouts.
	c.easy_setopt(eh, kOptNoSignal, (long)1);
	c.easy_setopt(eh, kOptTimeoutMs, (long)(in.timeoutMs ? in.timeoutMs : 20000));
	c.easy_setopt(eh, kOptConnectTimeoutMs, (long)10000);
	// Empty string = "every encoding libcurl was built with".
	c.easy_setopt(eh, kOptAcceptEncoding, "");
	if (!in.userAgent.empty())
		c.easy_setopt(eh, kOptUserAgent, in.userAgent.c_str());

	if (in.cancel || in.bytesTotal) {
		c.easy_setopt(eh, kOptNoProgress, (long)0);
		c.easy_setopt(eh, kOptXferInfoFunction, (void*)&XferCallback);
		c.easy_setopt(eh, kOptXferInfoData, (void*)&in);
	}

	const int rc = c.easy_perform(eh);

	if (rc == kErrOK) {
		long code = 0;
		c.easy_getinfo(eh, kInfoResponseCode, &code);
		out.status = (int)code;

		const char* ct = nullptr;
		c.easy_getinfo(eh, kInfoContentType, &ct);
		out.contentType = NormalizeContentType(ct);
	} else {
		out.status = 0;
		out.body.clear();
		if (ctx.overflow)
			out.error = "response too large";
		else if (rc == kErrAbortedByCallback)
			out.error = "cancelled";
		else if (c.easy_strerror)
			out.error = c.easy_strerror(rc);
		else
			out.error = "transfer failed";
	}

	c.easy_cleanup(eh);
}

}  // namespace ATHttp

#endif
