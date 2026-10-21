// Altirra SDL netplay lobby transport adapter.
#include <stdafx.h>
#include "http_minimal.h"
#include "../media/http_client.h"
#include <cstring>
#include <utility>

namespace ATNetplay {

void UrlPercentEncode(const char* s, size_t n, std::string& out) {
	static const char hex[] = "0123456789ABCDEF";
	for (size_t i = 0; i < n; ++i) {
		unsigned char c = (unsigned char)s[i];
		bool unreserved =
			(c >= 'A' && c <= 'Z') ||
			(c >= 'a' && c <= 'z') ||
			(c >= '0' && c <= '9') ||
			c == '-' || c == '_' || c == '.' || c == '~';
		if (unreserved) {
			out.push_back((char)c);
		} else {
			out.push_back('%');
			out.push_back(hex[c >> 4]);
			out.push_back(hex[c & 0xF]);
		}
	}
}

void HttpRequestSync(const HttpRequest& in, HttpResponse& out) {
	out = HttpResponse();
	if (!in.host || !*in.host || !in.path || in.path[0] != '/' || !in.method) {
		out.error = "invalid lobby request";
		return;
	}
	ATHttp::Request request;
	request.url = in.secure ? "https://" : "http://";
	request.url += in.host;
	request.url += ":" + std::to_string(in.port);
	if (in.basePath) request.url += in.basePath;
	request.url += in.path;
	if (!std::strcmp(in.method, "POST")) request.method = ATHttp::Method::Post;
	else if (!std::strcmp(in.method, "DELETE")) request.method = ATHttp::Method::Delete;
	else if (std::strcmp(in.method, "GET")) { out.error = "unsupported lobby request method"; return; }
	request.timeoutMs = in.timeoutMs;
	request.cancel = in.cancel;
	request.maxBodyBytes = 1024 * 1024; // bounded full lobby listing
	request.userAgent = "AltirraSDL-netplay/2";
	request.headers.emplace_back("Accept", "application/json");
	if (in.body && in.bodyLen)
		request.requestBody.assign(reinterpret_cast<const char*>(in.body), in.bodyLen);
	if (in.contentType) request.requestContentType = in.contentType;
	if (in.extraHeader && *in.extraHeader) {
		const std::string header = in.extraHeader;
		const auto colon = header.find(':');
		if (colon == std::string::npos || header.find_first_of("\r\n") != std::string::npos) {
			out.error = "invalid lobby request header"; return;
		}
		request.headers.emplace_back(header.substr(0, colon), header.substr(colon + 1));
	}
	ATHttp::Response response;
	ATHttp::Perform(request, response);
	out.status = !response.error.empty() && response.status >= 200 && response.status < 300 ? 0 : response.status;
	out.body = std::move(response.body);
	out.contentType = std::move(response.contentType);
	out.error = std::move(response.error);
}

} // namespace ATNetplay
