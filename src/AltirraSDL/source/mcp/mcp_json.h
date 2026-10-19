//	AltirraSDL - embedded MCP server: JSON helpers
//
//	Parsing uses the in-tree vdjson library; output is built by string
//	concatenation in the same style as the bridge (bridge_protocol.h).

#pragma once

#include <cstdint>
#include <string>

struct VDJSONValueRef;

namespace ATMCP {

// Quoted, escaped JSON string literal.
std::string JsonStr(const std::string& s);

// UTF-8 copy of a vdjson string value ("" for non-strings).
std::string ValueToU8(const VDJSONValueRef& v);

// Re-serialise any vdjson value (used to echo JSON-RPC ids verbatim).
std::string SerializeValue(const VDJSONValueRef& v);

// Lookup helper: v[name] for a UTF-8 name, tolerating non-objects.
VDJSONValueRef Member(const VDJSONValueRef& v, const char *name);

// Minimal object builder: appends "key":value pairs.
class JsonObject {
public:
	JsonObject& Raw(const char *key, const std::string& rawJson);
	JsonObject& Str(const char *key, const std::string& value);
	JsonObject& Int(const char *key, int64_t value);
	JsonObject& Bool(const char *key, bool value);
	std::string Build() const { return "{" + mBody + "}"; }

private:
	void Key(const char *key);
	std::string mBody;
};

}  // namespace ATMCP
