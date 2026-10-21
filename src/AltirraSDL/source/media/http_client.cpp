//	AltirraSDL - HTTPS client implementation
//	See http_client.h for the contract and the per-platform rationale.

#include <stdafx.h>

#include "http_client.h"

#include <algorithm>
#include <cstdio>
#include <chrono>
#include <cstring>
#include <limits>
#include <mutex>
#include <string_view>

namespace ATHttp {

void Get(const Request& in, Response& out) {
	if (in.method == Method::Get) { Perform(in, out); return; }
	Request request = in;
	request.method = Method::Get;
	request.requestBody.clear();
	Perform(request, out);
}
void Post(const Request& in, Response& out) {
	if (in.method == Method::Post) { Perform(in, out); return; }
	Request request = in;
	request.method = Method::Post;
	Perform(request, out);
}

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

bool ValidateHeaders(const Request& in, Response& out) {
	size_t bytes = 0;
	if (in.headers.size() > 32) {
		out = Response(); out.error = "too many HTTP request headers"; return false;
	}
	for (const auto& header : in.headers) {
		bytes += header.first.size() + header.second.size();
		bool valid = !header.first.empty() && bytes <= 16384;
		for (unsigned char c : header.first)
			valid = valid && ((c >= 'A' && c <= 'Z') ||
				(c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-');
		for (unsigned char c : header.second)
			valid = valid && c >= 0x20 && c != 0x7f;
		std::string name = header.first;
		for (char& c : name) if (c >= 'A' && c <= 'Z') c += 'a' - 'A';
		valid = valid && name != "host" && name != "content-length"
			&& name != "transfer-encoding" && name != "connection";
		if (!valid) {
			out = Response(); out.error = "invalid HTTP request header"; return false;
		}
	}
	return true;
}

// The backend resolves Location first. Comparing the resulting absolute
// authorities conservatively avoids sending private lobby tokens elsewhere.
std::string RequestOrigin(const std::string& url) {
	const auto schemeEnd = url.find("://");
	if (schemeEnd == std::string::npos) return {};
	const auto end = url.find_first_of("/?#", schemeEnd + 3);
	std::string origin = url.substr(0, end);
	if (origin.find('@') != std::string::npos) return {};
	for (char& c : origin) {
		if ((unsigned char)c <= 0x20 || c == 0x7f) return {};
		if (c >= 'A' && c <= 'Z') c += 'a' - 'A';
	}
	return origin;
}

bool AllowRedirect(const Request& in, const std::string& target, Response& out) {
	if (in.headers.empty()) return true;
	const auto origin = RequestOrigin(in.url);
	if (!origin.empty() && origin == RequestOrigin(target)) return true;
	out = Response(); out.error = "authenticated HTTP redirect crosses origin";
	return false;
}

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

// One decoded-body accounting/error path shared by all native backends.
// No exception may cross libcurl's C callback boundary, including errors
// from a user-provided sink or vector allocation.
struct BodyReceiver {
	const Request& request;
	Response& response;
	uint64_t received = 0;
	uint64_t expected = 0;
	bool haveExpected = false;
	bool encoded = false;
	bool rejectedError = false;
	bool rejectedRedirect = false;
	std::string redirectLocation;
	enum Failure { None, Cancelled, TooLarge, SinkFailed, OutOfMemory,
		Incomplete } failure = None;

	BodyReceiver(const Request& in, Response& out) : request(in), response(out) {
		if (request.bytesReceived) request.bytesReceived->store(0, std::memory_order_relaxed);
		if (request.bytesTotal) request.bytesTotal->store(0, std::memory_order_relaxed);
	}
	void Begin(int status) noexcept {
		response.status = status;
		response.body.clear();
		received = expected = 0;
		haveExpected = encoded = false;
		rejectedError = rejectedRedirect = false;
		redirectLocation.clear();
		if (request.bytesReceived) request.bytesReceived->store(0, std::memory_order_relaxed);
		if (request.bytesTotal) request.bytesTotal->store(0, std::memory_order_relaxed);
	}
	bool SkipBody() const noexcept {
		return request.bodySink && (response.status < 200 || response.status >= 300);
	}
	void Length(std::string_view text) noexcept {
		while (!text.empty() && (text.front() == ' ' || text.front() == '\t')) text.remove_prefix(1);
		while (!text.empty() && (text.back() == ' ' || text.back() == '\t' ||
			text.back() == '\r' || text.back() == '\n')) text.remove_suffix(1);
		uint64_t value = 0;
		if (text.empty()) return;
		for (const char c : text) {
			if (c < '0' || c > '9' || value > (UINT64_MAX - (c - '0')) / 10) return;
			value = value * 10 + (c - '0');
		}
		expected = value;
		haveExpected = true;
	}
	void Encoded() noexcept {
		encoded = true;
		if (request.bytesTotal) request.bytesTotal->store(0, std::memory_order_relaxed);
	}
	bool HeadersDone() noexcept {
		const bool accepted = response.status >= 200 && response.status < 300;
		if (accepted && haveExpected && !encoded) {
			if (request.bytesTotal) request.bytesTotal->store(expected, std::memory_order_relaxed);
			if (request.maxBodyBytes && expected > request.maxBodyBytes) {
				failure = TooLarge;
				return false;
			}
		}
		return true;
	}
	bool Append(const void* data, size_t size) noexcept {
		if (IsCancelled(request)) { failure = Cancelled; return false; }
		// Streaming archive consumers must never receive login/error pages
		// or intermediate redirects. Memory clients still receive error JSON.
		if (SkipBody()) return true;
		if (size > UINT64_MAX - received ||
			(request.maxBodyBytes && size > request.maxBodyBytes - received)) {
			failure = TooLarge;
			return false;
		}
		try {
			if (request.bodySink) {
				if (!request.bodySink(data, size)) { failure = SinkFailed; return false; }
			} else {
				const auto* bytes = (const uint8_t*)data;
				response.body.insert(response.body.end(), bytes, bytes + size);
			}
		} catch (...) {
			failure = request.bodySink ? SinkFailed : OutOfMemory;
			return false;
		}
		received += size;
		if (request.bytesReceived) request.bytesReceived->store(received, std::memory_order_relaxed);
		return true;
	}
	bool Finish() noexcept {
		if (IsCancelled(request)) failure = Cancelled;
		if (failure == None && response.status >= 200 && response.status < 300 &&
			haveExpected && !encoded && received != expected) failure = Incomplete;
		return failure == None;
	}
	void ReportFailure() {
		response.status = 0;
		response.body.clear();
		switch (failure) {
			case Cancelled: response.error = "cancelled"; break;
			case TooLarge: response.error = "response too large"; break;
			case SinkFailed: response.error = "download sink failed"; break;
			case OutOfMemory: response.error = "unable to allocate HTTP response"; break;
			case Incomplete: response.error = "incomplete HTTP response"; break;
			default: break;
		}
	}
};

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

static void GetWindowsOnce(const Request& in, Response& out,
	std::string& redirectURL) {
	out = Response();
	BodyReceiver receiver(in, out);
	const auto deadline = std::chrono::steady_clock::now() +
		std::chrono::milliseconds(in.timeoutMs ? in.timeoutMs : 20000);

	if (in.url.empty()) {
		out.error = "empty URL";
		return;
	}

	const std::wstring wurl = WidenUtf8(in.url);

	// Borrow components from wurl; fixed buffers would reject long signed
	// download URLs. wurl stays alive throughout the request.
	URL_COMPONENTSW uc = {};
	uc.dwStructSize = sizeof(uc);
	uc.dwHostNameLength = (DWORD)-1;
	uc.dwUrlPathLength = (DWORD)-1;
	uc.dwExtraInfoLength = (DWORD)-1;

	if (!WinHttpCrackUrl(wurl.c_str(), (DWORD)wurl.size(), 0, &uc)) {
		out.error = LastErrorText("WinHttpCrackUrl");
		return;
	}

	if (uc.nScheme != INTERNET_SCHEME_HTTP && uc.nScheme != INTERNET_SCHEME_HTTPS) {
		out.error = "only HTTP and HTTPS URLs are supported";
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
	const int wait = (int)(tmo < 2000 ? tmo : 2000);
	if (!WinHttpSetTimeouts(session.h, wait, wait, wait, wait)) {
		out.error = LastErrorText("WinHttpSetTimeouts");
		return;
	}

	HInternetGuard connect;
	const std::wstring host(uc.lpszHostName ? uc.lpszHostName : L"", uc.dwHostNameLength);
	connect.h = WinHttpConnect(session.h, host.c_str(), uc.nPort, 0);
	if (!connect.h) {
		out.error = LastErrorText("WinHttpConnect");
		return;
	}

	// "https://host" cracks to an empty path; WinHttpOpenRequest needs
	// "/" for that, not an empty object name.
	std::wstring objectName;
	if (uc.dwUrlPathLength) objectName.append(uc.lpszUrlPath, uc.dwUrlPathLength);
	if (uc.dwExtraInfoLength) objectName.append(uc.lpszExtraInfo, uc.dwExtraInfoLength);
	const auto fragment = objectName.find(L'#');
	if (fragment != std::wstring::npos) objectName.resize(fragment);
	if (objectName.empty())
		objectName = L"/";

	DWORD reqFlags = WINHTTP_FLAG_REFRESH;
	if (uc.nScheme == INTERNET_SCHEME_HTTPS)
		reqFlags |= WINHTTP_FLAG_SECURE;

	HInternetGuard request;
	request.h = WinHttpOpenRequest(connect.h,
		in.method == Method::Post ? L"POST" :
		in.method == Method::Delete ? L"DELETE" : L"GET", objectName.c_str(),
		nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES,
		reqFlags);
	if (!request.h) {
		out.error = LastErrorText("WinHttpOpenRequest");
		return;
	}
	// Do not let WinHTTP drain a redirect's arbitrary-size body before it
	// follows Location (its default drain limit is only 1 MiB). The outer
	// loop closes this request and follows a bounded number of headers only.
	DWORD redirectPolicy = WINHTTP_OPTION_REDIRECT_POLICY_NEVER;
	if (!WinHttpSetOption(request.h, WINHTTP_OPTION_REDIRECT_POLICY,
		&redirectPolicy, sizeof redirectPolicy)) {
		out.error = LastErrorText("WinHttpSetOption redirect policy");
		return;
	}
	// Apply on the request as well: the body must use this cancellation
	// bound independently of proxy/session defaults.
	if (!WinHttpSetTimeouts(request.h, wait, wait, wait, wait)) {
		out.error = LastErrorText("WinHttpSetTimeouts");
		return;
	}

	// gzip/deflate for the JSON responses; WinHTTP decodes transparently.
	DWORD decomp = WINHTTP_DECOMPRESSION_FLAG_ALL;
	WinHttpSetOption(request.h, WINHTTP_OPTION_DECOMPRESSION,
		&decomp, sizeof decomp);

	if (IsCancelled(in)) { out.error = "cancelled"; return; }

	std::wstring headers;
	void* upload = WINHTTP_NO_REQUEST_DATA;
	DWORD uploadSize = 0;
	if (in.method == Method::Post) {
		if (in.requestBody.size() > MAXDWORD ||
			in.requestContentType.find_first_of("\r\n") != std::string::npos) {
			out.error = "invalid HTTP request body or content type";
			return;
		}
		headers = L"Content-Type: " + WidenUtf8(in.requestContentType.empty()
			? "application/octet-stream" : in.requestContentType) + L"\r\n";
		upload = const_cast<char*>(in.requestBody.data());
		uploadSize = (DWORD)in.requestBody.size();
	}
	for (const auto& header : in.headers)
		headers += WidenUtf8(header.first) + L": " + WidenUtf8(header.second) + L"\r\n";
	if (!WinHttpSendRequest(request.h, headers.empty() ? WINHTTP_NO_ADDITIONAL_HEADERS : headers.c_str(),
		(DWORD)headers.size(), upload, uploadSize, uploadSize, 0))
	{
		out.error = IsCancelled(in) ? "cancelled" : LastErrorText("WinHttpSendRequest");
		return;
	}
	if (IsCancelled(in)) { out.error = "cancelled"; return; }

	if (!WinHttpReceiveResponse(request.h, nullptr)) {
		out.error = IsCancelled(in) ? "cancelled" : LastErrorText("WinHttpReceiveResponse");
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
	if (out.status == 301 || out.status == 302 || out.status == 303 ||
		out.status == 307 || out.status == 308) {
		DWORD size = 0;
		WinHttpQueryHeaders(request.h, WINHTTP_QUERY_LOCATION,
			WINHTTP_HEADER_NAME_BY_INDEX, nullptr, &size, WINHTTP_NO_HEADER_INDEX);
		if (!size || size > 65536 || GetLastError() != ERROR_INSUFFICIENT_BUFFER) {
			out.error = "redirect has no valid Location";
			return;
		}
		std::wstring location(size / sizeof(wchar_t), L'\0');
		if (!WinHttpQueryHeaders(request.h, WINHTTP_QUERY_LOCATION,
			WINHTTP_HEADER_NAME_BY_INDEX, location.data(), &size, WINHTTP_NO_HEADER_INDEX)) {
			out.error = LastErrorText("WinHttpQueryHeaders Location");
			return;
		}
		// Use Windows' URI resolver for relative paths, queries and dot
		// segments. Load this system component without adding a link-time
		// dependency to standalone HTTP clients.
		struct Module {
			HMODULE h = LoadLibraryExW(L"shlwapi.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
			~Module() { if (h) FreeLibrary(h); }
		} module;
		using Combine = HRESULT (WINAPI*)(LPCWSTR, LPCWSTR, LPWSTR, LPDWORD, DWORD);
		const auto combine = module.h ? reinterpret_cast<Combine>(
			GetProcAddress(module.h, "UrlCombineW")) : nullptr;
		if (!combine) { out.error = "Windows URL resolver unavailable"; return; }
		std::wstring target(32768, L'\0');
		DWORD length = (DWORD)target.size();
		if (FAILED(combine(wurl.c_str(), location.c_str(), target.data(), &length, 0))) {
			out.error = "invalid redirect URL";
			return;
		}
		target.resize(length);
		const auto fragment = target.find(L'#');
		if (fragment != std::wstring::npos) target.resize(fragment);
		redirectURL = NarrowUtf8(target.data(), target.size());
		return;
	}
	const auto header = [&](DWORD type) {
		wchar_t text[256] {};
		DWORD size = sizeof text;
		if (!WinHttpQueryHeaders(request.h, type, WINHTTP_HEADER_NAME_BY_INDEX,
			text, &size, WINHTTP_NO_HEADER_INDEX)) return std::string();
		return NarrowUtf8(text, wcslen(text));
	};
	receiver.Length(header(WINHTTP_QUERY_CONTENT_LENGTH));
	receiver.encoded = !header(WINHTTP_QUERY_CONTENT_ENCODING).empty() ||
		!header(WINHTTP_QUERY_TRANSFER_ENCODING).empty();
	if (!receiver.HeadersDone()) { receiver.ReportFailure(); return; }

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
	if (receiver.SkipBody()) return;

	// Read directly into a fixed chunk. QueryDataAvailable is unnecessary
	// for streaming and can wait for the remote server even while the user
	// has cancelled. A bounded ReadData wait gives us a cancellation check
	// between each receive, including when the server has stopped sending.
	uint8_t chunk[32768];
	for (;;) {
		if (IsCancelled(in) || std::chrono::steady_clock::now() >= deadline) {
			out.status = 0;
			out.body.clear();
			out.error = IsCancelled(in) ? "cancelled" : "transfer timed out";
			return;
		}

		DWORD read = 0;
		if (!WinHttpReadData(request.h, chunk, sizeof chunk, &read)) {
			out.error = IsCancelled(in) ? "cancelled" : LastErrorText("WinHttpReadData");
			out.status = 0;
			return;
		}
		if (!read)
			break;
		if (IsCancelled(in)) { out.error = "cancelled"; return; }

		if (!receiver.Append(chunk, read)) { receiver.ReportFailure(); return; }
	}
	if (!receiver.Finish()) receiver.ReportFailure();
}

void Perform(const Request& in, Response& out) {
	if (!ValidateHeaders(in, out)) return;
	const auto deadline = std::chrono::steady_clock::now() +
		std::chrono::milliseconds(in.timeoutMs ? in.timeoutMs : 20000);
	Request hop = in;
	for (unsigned redirects = 0;; ++redirects) {
		const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(
			deadline - std::chrono::steady_clock::now()).count();
		if (IsCancelled(in) || remaining <= 0) {
			out = Response();
			out.error = IsCancelled(in) ? "cancelled" : "transfer timed out";
			return;
		}
		hop.timeoutMs = (uint32_t)remaining;
		std::string redirectURL;
		GetWindowsOnce(hop, out, redirectURL);
		if (IsCancelled(in)) { out = Response(); out.error = "cancelled"; return; }
		if (!out.error.empty() || redirectURL.empty()) return;
		if (!AllowRedirect(in, redirectURL, out)) return;
		if (redirects == 5) {
			out = Response();
			out.error = "too many redirects";
			return;
		}
		if (out.status == 303 ||
			((out.status == 301 || out.status == 302) && hop.method == Method::Post)) {
			hop.method = Method::Get;
			hop.requestBody.clear();
		}
		hop.url = std::move(redirectURL);
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

static void GetAndroidOnce(const Request& in, Response& out,
	std::string& redirectURL) {
	out = Response();
	BodyReceiver receiver(in, out);
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
	if (!env->IsInstanceOf(conn, httpConnClass)) {
		out.error = "only HTTP and HTTPS URLs are supported";
		return;
	}

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
		env->CallVoidMethod(conn, setFollow, JNI_FALSE);
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

	for (const auto& header : in.headers) {
		if (!setRequestProperty) { out.error = "HTTP request headers unavailable"; return; }
		JLocal key(env, (jobject)env->NewStringUTF(header.first.c_str()));
		JLocal value(env, (jobject)env->NewStringUTF(header.second.c_str()));
		if (ClearException(env) || !key || !value) { out.error = "HTTP request header allocation failed"; return; }
		env->CallVoidMethod(conn, setRequestProperty, key.obj, value.obj);
		if (ClearException(env)) { out.error = "HTTP request header setup failed"; return; }
	}
	if (in.method == Method::Delete) {
		const auto setMethod = env->GetMethodID(httpConnClass, "setRequestMethod", "(Ljava/lang/String;)V");
		JLocal method(env, (jobject)env->NewStringUTF("DELETE"));
		if (ClearException(env) || !setMethod || !method) { out.error = "HTTP DELETE unavailable"; return; }
		env->CallVoidMethod(conn, setMethod, method.obj);
		if (ClearException(env)) { out.error = "HTTP DELETE setup failed"; return; }
	}

	if (in.method == Method::Post) {
		if (in.requestContentType.find_first_of("\r\n") != std::string::npos) {
			out.error = "invalid HTTP content type";
			return;
		}
		const auto setMethod = env->GetMethodID(httpConnClass, "setRequestMethod", "(Ljava/lang/String;)V");
		const auto setOutput = env->GetMethodID(httpConnClass, "setDoOutput", "(Z)V");
		const auto setLength = env->GetMethodID(httpConnClass, "setFixedLengthStreamingMode", "(J)V");
		const auto getOutput = env->GetMethodID(httpConnClass, "getOutputStream", "()Ljava/io/OutputStream;");
		if (ClearException(env) || !setMethod || !setOutput || !setLength || !getOutput || !setRequestProperty) {
			out.error = "HTTP POST methods unavailable";
			return;
		}
		JLocal method(env, (jobject)env->NewStringUTF("POST"));
		JLocal key(env, (jobject)env->NewStringUTF("Content-Type"));
		JLocal value(env, (jobject)env->NewStringUTF(in.requestContentType.empty()
			? "application/octet-stream" : in.requestContentType.c_str()));
		if (ClearException(env) || !method || !key || !value) {
			out.error = "HTTP POST headers unavailable";
			return;
		}
		env->CallVoidMethod(conn, setMethod, method.obj);
		env->CallVoidMethod(conn, setOutput, JNI_TRUE);
		env->CallVoidMethod(conn, setLength, (jlong)in.requestBody.size());
		env->CallVoidMethod(conn, setRequestProperty, key.obj, value.obj);
		if (ClearException(env)) { out.error = "HTTP POST setup failed"; return; }
		JLocal output(env, env->CallObjectMethod(conn, getOutput));
		if (ClearException(env) || !output) { out.error = "HTTP POST connection failed"; return; }
		JLocal outputClass(env, env->FindClass("java/io/OutputStream"));
		const auto write = outputClass ? env->GetMethodID((jclass)outputClass.obj, "write", "([BII)V") : nullptr;
		const auto close = outputClass ? env->GetMethodID((jclass)outputClass.obj, "close", "()V") : nullptr;
		if (ClearException(env) || !write || !close) { out.error = "HTTP POST stream unavailable"; return; }
		struct OutputCloser {
			JNIEnv* env; jobject stream; jmethodID close;
			~OutputCloser() { env->CallVoidMethod(stream, close); env->ExceptionClear(); }
		} outputCloser{env, output.obj, close};
		JLocal buffer(env, (jobject)env->NewByteArray(32768));
		if (ClearException(env) || !buffer) { out.error = "HTTP POST buffer unavailable"; return; }
		for (size_t offset = 0; offset < in.requestBody.size();) {
			if (IsCancelled(in) || std::chrono::steady_clock::now() >= deadline) {
				out.error = IsCancelled(in) ? "cancelled" : "transfer timed out";
				return;
			}
			const auto count = (jint)std::min<size_t>(32768, in.requestBody.size() - offset);
			env->SetByteArrayRegion((jbyteArray)buffer.obj, 0, count,
				(const jbyte*)in.requestBody.data() + offset);
			if (ClearException(env)) { out.error = "HTTP POST buffer failed"; return; }
			env->CallVoidMethod(output, write, buffer.obj, 0, count);
			if (ClearException(env)) { out.error = "HTTP POST upload failed"; return; }
			offset += (size_t)count;
		}
	}

	const jint code = env->CallIntMethod(conn, getResponseCode);
	if (ClearException(env)) {
		out.error = "connection failed";
		return;
	}
	out.status = (int)code;

	// Android's automatic redirect handling refuses HTTP/HTTPS changes.
	// Resolve Location with java.net.URL, then follow a bounded number of
	// HTTP(S) hops in Get(), sharing the original transfer deadline.
	if (code == 301 || code == 302 || code == 303 || code == 307 || code == 308) {
		jmethodID getHeader = env->GetMethodID(httpConnClass,
			"getHeaderField", "(Ljava/lang/String;)Ljava/lang/String;");
		jmethodID resolve = env->GetMethodID(urlClass, "<init>",
			"(Ljava/net/URL;Ljava/lang/String;)V");
		jmethodID protocol = env->GetMethodID(urlClass, "getProtocol",
			"()Ljava/lang/String;");
		jmethodID external = env->GetMethodID(urlClass, "toExternalForm",
			"()Ljava/lang/String;");
		if (ClearException(env) || !getHeader || !resolve || !protocol || !external) {
			out.error = "redirect methods unavailable";
			return;
		}
		JLocal name(env, (jobject)env->NewStringUTF("Location"));
		JLocal location(env, env->CallObjectMethod(conn, getHeader, name.obj));
		if (ClearException(env) || !location) {
			out.error = "redirect has no Location";
			return;
		}
		JLocal target(env, env->NewObject(urlClass, resolve, jurl.obj, location.obj));
		if (ClearException(env) || !target) {
			out.error = "invalid redirect URL";
			return;
		}
		JLocal scheme(env, env->CallObjectMethod(target, protocol));
		if (ClearException(env) || !scheme) {
			out.error = "invalid redirect scheme";
			return;
		}
		const auto schemeText = JStringToUtf8(env, (jstring)scheme.obj);
		if (schemeText != "http" && schemeText != "https") {
			out.error = "redirect must use HTTP or HTTPS";
			return;
		}
		JLocal urlText(env, env->CallObjectMethod(target, external));
		if (ClearException(env) || !urlText) {
			out.error = "invalid redirect URL";
			return;
		}
		redirectURL = JStringToUtf8(env, (jstring)urlText.obj);
		return;
	}

	if (getContentType) {
		JLocal ct(env, env->CallObjectMethod(conn, getContentType));
		env->ExceptionClear();
		if (ct)
			out.contentType = NormalizeContentType(
				JStringToUtf8(env, (jstring)ct.obj).c_str());
	}
	jmethodID getHeader = env->GetMethodID(httpConnClass,
		"getHeaderField", "(Ljava/lang/String;)Ljava/lang/String;");
	if (!ClearException(env) && getHeader) {
		const auto header = [&](const char* key) {
			JLocal name(env, (jobject)env->NewStringUTF(key));
			if (ClearException(env) || !name) return std::string();
			JLocal value(env, env->CallObjectMethod(conn, getHeader, name.obj));
			if (ClearException(env) || !value) return std::string();
			return JStringToUtf8(env, (jstring)value.obj);
		};
		receiver.Length(header("Content-Length"));
		receiver.encoded = !header("Content-Encoding").empty() ||
			!header("Transfer-Encoding").empty();
	}
	if (!receiver.HeadersDone()) { receiver.ReportFailure(); return; }
	if (receiver.SkipBody()) return;

	// 4xx/5xx bodies arrive on the error stream; the provider encodes
	// its quota / not-found messages there, so we must read it.
	jobject rawStream = env->CallObjectMethod(conn,
		(code >= 400 && getErrorStream) ? getErrorStream : getInputStream);
	if (ClearException(env))
		rawStream = nullptr;
	JLocal stream(env, rawStream);
	if (!stream)
	{
		if (!receiver.Finish()) receiver.ReportFailure();
		return;   // status is set; empty body is legitimate here
	}

	jclass isClass = env->FindClass("java/io/InputStream");
	if (!isClass) { ClearException(env); return; }
	JLocal isClassRef(env, isClass);

	jmethodID readMid = env->GetMethodID(isClass, "read", "([BII)I");
	jmethodID closeMid = env->GetMethodID(isClass, "close", "()V");
	if (!readMid) { ClearException(env); return; }

	const jint kChunk = 32768;
	JLocal buf(env, (jobject)env->NewByteArray(kChunk));
	if (!buf) { ClearException(env); return; }
	jbyte chunk[kChunk];

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

		env->GetByteArrayRegion((jbyteArray)buf.obj, 0, n, chunk);
		if (ClearException(env)) {
			out.status = 0;
			out.body.clear();
			out.error = "read failed";
			break;
		}
		if (!receiver.Append(chunk, (size_t)n)) { receiver.ReportFailure(); break; }
	}
	if (out.error.empty() && !receiver.Finish()) receiver.ReportFailure();

	if (closeMid) {
		env->CallVoidMethod(stream, closeMid);
		env->ExceptionClear();
	}
}

void Perform(const Request& in, Response& out) {
	if (!ValidateHeaders(in, out)) return;
	const auto deadline = std::chrono::steady_clock::now() +
		std::chrono::milliseconds(in.timeoutMs ? in.timeoutMs : 20000);
	Request hop = in;
	for (unsigned redirects = 0;; ++redirects) {
		const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(
			deadline - std::chrono::steady_clock::now()).count();
		if (IsCancelled(in) || remaining <= 0) {
			out = Response();
			out.error = IsCancelled(in) ? "cancelled" : "transfer timed out";
			return;
		}
		hop.timeoutMs = (uint32_t)remaining;
		std::string redirectURL;
		GetAndroidOnce(hop, out, redirectURL);
		if (IsCancelled(in)) {
			out = Response();
			out.error = "cancelled";
			return;
		}
		if (!out.error.empty() || redirectURL.empty())
			return;
		if (!AllowRedirect(in, redirectURL, out)) return;
		if (redirects == 5) {
			out = Response();
			out.error = "too many redirects";
			return;
		}
		if (out.status == 303 ||
			((out.status == 301 || out.status == 302) && hop.method == Method::Post)) {
			hop.method = Method::Get;
			hop.requestBody.clear();
		}
		hop.url = std::move(redirectURL);
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

void Perform(const Request&, Response& out) {
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
	kOptHeaderData        = kOptObjectPoint + 29,
	kOptHeaderFunction    = kOptFunctionPoint + 79,
	kOptPost              = kOptLong + 47,
	kOptPostFields        = kOptObjectPoint + 15,
	kOptPostFieldSize     = 30000 + 120,
	kOptHttpHeader        = kOptObjectPoint + 23,
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
typedef void* (*fn_slist_append)(void*, const char*);
typedef void (*fn_slist_free_all)(void*);
typedef void* (*fn_url)();
typedef int (*fn_url_set)(void*, int, const char*, unsigned);
typedef int (*fn_url_get)(void*, int, char**, unsigned);
typedef void (*fn_url_cleanup)(void*);
typedef void (*fn_free)(void*);

struct Curl {
	void* lib = nullptr;
	fn_easy_init     easy_init = nullptr;
	fn_easy_setopt   easy_setopt = nullptr;
	fn_easy_perform  easy_perform = nullptr;
	fn_easy_getinfo  easy_getinfo = nullptr;
	fn_easy_cleanup  easy_cleanup = nullptr;
	fn_easy_strerror easy_strerror = nullptr;
	fn_slist_append slist_append = nullptr;
	fn_slist_free_all slist_free_all = nullptr;
	fn_url url = nullptr;
	fn_url_set url_set = nullptr;
	fn_url_get url_get = nullptr;
	fn_url_cleanup url_cleanup = nullptr;
	fn_free free = nullptr;
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
	g_curl.slist_append = (fn_slist_append)sym("curl_slist_append");
	g_curl.slist_free_all = (fn_slist_free_all)sym("curl_slist_free_all");
	g_curl.url = (fn_url)sym("curl_url");
	g_curl.url_set = (fn_url_set)sym("curl_url_set");
	g_curl.url_get = (fn_url_get)sym("curl_url_get");
	g_curl.url_cleanup = (fn_url_cleanup)sym("curl_url_cleanup");
	g_curl.free = (fn_free)sym("curl_free");
	fn_global_init globalInit = (fn_global_init)sym("curl_global_init");

	if (!g_curl.easy_init || !g_curl.easy_setopt || !g_curl.easy_perform
		|| !g_curl.easy_getinfo || !g_curl.easy_cleanup || !globalInit
		|| !g_curl.slist_append || !g_curl.slist_free_all)
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

size_t WriteCallback(char* ptr, size_t size, size_t nmemb, void* userdata) {
	auto& receiver = *(BodyReceiver*)userdata;
	if (size && nmemb > SIZE_MAX / size) {
		receiver.failure = BodyReceiver::TooLarge;
		return 0;
	}
	const size_t n = size * nmemb;
	return receiver.Append(ptr, n) ? n : 0;
}

bool HeaderStarts(std::string_view text, std::string_view prefix) noexcept {
	if (text.size() < prefix.size()) return false;
	for (size_t i = 0; i < prefix.size(); ++i) {
		char c = text[i];
		if (c >= 'A' && c <= 'Z') c += 'a' - 'A';
		if (c != prefix[i]) return false;
	}
	return true;
}

size_t HeaderCallback(char* ptr, size_t size, size_t nmemb, void* userdata) {
	auto& receiver = *(BodyReceiver*)userdata;
	if (size && nmemb > SIZE_MAX / size) return 0;
	const size_t n = size * nmemb;
	const std::string_view line(ptr, n);
	if (HeaderStarts(line, "http/")) {
		const auto space = line.find(' ');
		if (space != std::string_view::npos && space + 3 < line.size()) {
			int status = 0;
			for (size_t i = space + 1; i <= space + 3; ++i) {
				if (line[i] < '0' || line[i] > '9') return 0;
				status = status * 10 + line[i] - '0';
			}
			receiver.Begin(status);
		}
	} else if (HeaderStarts(line, "location:")) {
		auto location = line.substr(9);
		while (!location.empty() && (location.front() == ' ' || location.front() == '\t')) location.remove_prefix(1);
		while (!location.empty() && (location.back() == '\r' || location.back() == '\n' ||
			location.back() == ' ' || location.back() == '\t')) location.remove_suffix(1);
		try { receiver.redirectLocation.assign(location.data(), location.size()); }
		catch (...) { receiver.failure = BodyReceiver::OutOfMemory; return 0; }
	} else if (HeaderStarts(line, "content-length:")) {
		receiver.Length(line.substr(15));
	} else if (HeaderStarts(line, "content-encoding:") || HeaderStarts(line, "transfer-encoding:")) {
		// Content-Length counts encoded bytes, not the decoded chunks
		// delivered by libcurl. Chunked framing also supersedes length.
		receiver.Encoded();
	} else if (line == "\r\n" || line == "\n") {
		if (!receiver.HeadersDone()) return 0;
		const auto status = receiver.response.status;
		if (status == 301 || status == 302 || status == 303 || status == 307 || status == 308) {
			// Stop at the headers. Automatic redirects can drain an
			// arbitrarily large or stalled body before following Location.
			receiver.rejectedRedirect = true;
			return 0;
		}
		if (receiver.request.bodySink && receiver.response.status >= 400) {
			// Keep the HTTP status, but do not download a potentially huge
			// error page into an archive destination or wait for its body.
			receiver.rejectedError = true;
			return 0;
		}
	}
	return n;
}

int XferCallback(void* userdata, int64_t total, int64_t, int64_t, int64_t) {
	const auto& receiver = *(const BodyReceiver*)userdata;
	const Request& request = receiver.request;
	// HeadersDone owns an advertised decoded length. Some curl versions
	// report dltotal=0 after a header rejection; do not erase that known
	// size (or replace it with a wire-size estimate during a transfer).
	if (request.bytesTotal && !receiver.encoded && !receiver.haveExpected)
		request.bytesTotal->store(total > 0 ? (uint64_t)total : 0,
			std::memory_order_relaxed);
	// Non-zero aborts the transfer with CURLE_ABORTED_BY_CALLBACK.
	return IsCancelled(request) ? 1 : 0;
}

}  // namespace

bool Available() { return GetCurl().ok; }

const char* BackendName() { return GetCurl().ok ? "libcurl" : "none"; }

static void GetCurlOnce(const Request& in, Response& out,
	std::string& redirectURL) {
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

	struct Cleanup {
		const Curl& curl;
		void* handle;
		void* headers = nullptr;
		~Cleanup() {
			curl.easy_cleanup(handle);
			if (headers) curl.slist_free_all(headers);
		}
	} cleanup{c, eh, nullptr};
	BodyReceiver receiver(in, out);

	c.easy_setopt(eh, kOptURL, in.url.c_str());
	if (in.method == Method::Delete)
		c.easy_setopt(eh, kOptObjectPoint + 36, "DELETE");
	if (in.method == Method::Post) {
		if (in.requestContentType.find_first_of("\r\n") != std::string::npos) {
			out.error = "invalid HTTP content type";
			return;
		}
		const std::string contentType = "Content-Type: " + (in.requestContentType.empty()
			? "application/octet-stream" : in.requestContentType);
		cleanup.headers = c.slist_append(nullptr, contentType.c_str());
		if (!cleanup.headers) { out.error = "unable to allocate HTTP headers"; return; }
		c.easy_setopt(eh, kOptHttpHeader, cleanup.headers);
		c.easy_setopt(eh, kOptPost, (long)1);
		c.easy_setopt(eh, kOptPostFields, in.requestBody.c_str());
		c.easy_setopt(eh, kOptPostFieldSize, (int64_t)in.requestBody.size());
	}
	for (const auto& header : in.headers) {
		const std::string line = header.first + ": " + header.second;
		void* updated = c.slist_append(cleanup.headers, line.c_str());
		if (!updated) { out.error = "unable to allocate HTTP headers"; return; }
		cleanup.headers = updated;
	}
	if (cleanup.headers) c.easy_setopt(eh, kOptHttpHeader, cleanup.headers);
	c.easy_setopt(eh, kOptWriteFunction, (void*)&WriteCallback);
	c.easy_setopt(eh, kOptWriteData, (void*)&receiver);
	c.easy_setopt(eh, kOptHeaderFunction, (void*)&HeaderCallback);
	c.easy_setopt(eh, kOptHeaderData, (void*)&receiver);
	c.easy_setopt(eh, kOptFollowLocation, (long)0);
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
		c.easy_setopt(eh, kOptXferInfoData, (void*)&receiver);
	}

	const int rc = c.easy_perform(eh);

	if (rc == kErrOK) {
		long code = 0;
		c.easy_getinfo(eh, kInfoResponseCode, &code);
		out.status = (int)code;

		const char* ct = nullptr;
		c.easy_getinfo(eh, kInfoContentType, &ct);
		out.contentType = NormalizeContentType(ct);
		if (!receiver.Finish()) receiver.ReportFailure();
	} else if (receiver.rejectedRedirect) {
		// CURLINFO_REDIRECT_URL is not set when aborting at headers. Use
		// libcurl's URI API to resolve Location without draining its body.
		if (!c.url || !c.url_set || !c.url_get || !c.url_cleanup || !c.free) {
			out.error = "HTTP redirects require libcurl 7.62 or newer";
			return;
		}
		struct URL {
			const Curl& curl;
			void* handle;
			char* target = nullptr;
			~URL() { if (target) curl.free(target); if (handle) curl.url_cleanup(handle); }
		} url{c, c.url()};
		// CURLUPART_URL = 0. Setting a relative URL resolves it against
		// the URL already stored in the handle (curl's stable URL API).
		if (!url.handle || receiver.redirectLocation.empty() ||
			c.url_set(url.handle, 0, in.url.c_str(), 0) ||
			c.url_set(url.handle, 0, receiver.redirectLocation.c_str(), 0) ||
			c.url_get(url.handle, 0, &url.target, 0) || !url.target)
			out.error = "redirect has no valid HTTP Location";
		else redirectURL = url.target;
	} else if (receiver.rejectedError) {
		out.body.clear();
	} else {
		out.status = 0;
		out.body.clear();
		if (receiver.failure != BodyReceiver::None)
			receiver.ReportFailure();
		else if (rc == kErrAbortedByCallback)
			out.error = "cancelled";
		else if (c.easy_strerror)
			out.error = c.easy_strerror(rc);
		else
			out.error = "transfer failed";
	}

}

void Perform(const Request& in, Response& out) {
	if (!ValidateHeaders(in, out)) return;
	const auto deadline = std::chrono::steady_clock::now() +
		std::chrono::milliseconds(in.timeoutMs ? in.timeoutMs : 20000);
	Request hop = in;
	for (unsigned redirects = 0;; ++redirects) {
		const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(
			deadline - std::chrono::steady_clock::now()).count();
		if (IsCancelled(in) || remaining <= 0) {
			out = Response();
			out.error = IsCancelled(in) ? "cancelled" : "transfer timed out";
			return;
		}
		hop.timeoutMs = (uint32_t)remaining;
		std::string redirectURL;
		GetCurlOnce(hop, out, redirectURL);
		if (IsCancelled(in)) { out = Response(); out.error = "cancelled"; return; }
		if (!out.error.empty() || redirectURL.empty()) return;
		if (!AllowRedirect(in, redirectURL, out)) return;
		if (redirects == 5) {
			out = Response();
			out.error = "too many redirects";
			return;
		}
		if (out.status == 303 ||
			((out.status == 301 || out.status == 302) && hop.method == Method::Post)) {
			hop.method = Method::Get;
			hop.requestBody.clear();
		}
		hop.url = std::move(redirectURL);
	}
}

}  // namespace ATHttp

#endif
