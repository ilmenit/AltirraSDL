//	AltirraSDL - embedded MCP server: JSON helpers
//	See mcp_json.h.

#include <stdafx.h>

#include "mcp_json.h"
#include "bridge_protocol.h"    // ATBridge::JsonEscape

#include <vd2/system/text.h>
#include <vd2/system/VDString.h>
#include <vd2/vdjson/jsonvalue.h>

#include <cmath>
#include <cstdio>

namespace ATMCP {

std::string JsonStr(const std::string& s) {
	return "\"" + ATBridge::JsonEscape(s) + "\"";
}

std::string ValueToU8(const VDJSONValueRef& v) {
	if (!v.IsString())
		return std::string();

	const VDStringSpanW span = v.AsStringSpan();
	const VDStringA u8 = VDTextWToU8(span);
	return std::string(u8.data(), u8.size());
}

std::string SerializeValue(const VDJSONValueRef& v) {
	switch (v->mType) {
		case VDJSONValue::kTypeNull:
		default:
			return "null";

		case VDJSONValue::kTypeBool:
			return v.AsBool() ? "true" : "false";

		case VDJSONValue::kTypeInt:
			return std::to_string((long long)v.AsInt64());

		case VDJSONValue::kTypeReal: {
			const double d = v.AsDouble();
			if (!std::isfinite(d))
				return "null";
			char buf[64];
			std::snprintf(buf, sizeof buf, "%.17g", d);
			return buf;
		}

		case VDJSONValue::kTypeString:
			return JsonStr(ValueToU8(v));

		case VDJSONValue::kTypeArray: {
			std::string out = "[";
			bool first = true;
			for (const VDJSONValueRef& e : v.AsArray()) {
				if (!first)
					out += ',';
				first = false;
				out += SerializeValue(e);
			}
			return out + "]";
		}

		case VDJSONValue::kTypeObject: {
			std::string out = "{";
			bool first = true;
			for (const VDJSONMemberRef& m : v.AsObject()) {
				if (!first)
					out += ',';
				first = false;
				const VDStringA name = VDTextWToU8(VDStringSpanW(m.GetName()));
				out += JsonStr(std::string(name.data(), name.size()));
				out += ':';
				out += SerializeValue(m.GetValue());
			}
			return out + "}";
		}
	}
}

VDJSONValueRef Member(const VDJSONValueRef& v, const char *name) {
	if (!v.IsObject())
		return VDJSONValueRef(v.GetDocument(), &VDJSONValue::null);

	return v[name];
}

void JsonObject::Key(const char *key) {
	if (!mBody.empty())
		mBody += ',';
	mBody += JsonStr(key);
	mBody += ':';
}

JsonObject& JsonObject::Raw(const char *key, const std::string& rawJson) {
	Key(key);
	mBody += rawJson;
	return *this;
}

JsonObject& JsonObject::Str(const char *key, const std::string& value) {
	Key(key);
	mBody += JsonStr(value);
	return *this;
}

JsonObject& JsonObject::Int(const char *key, int64_t value) {
	Key(key);
	mBody += std::to_string((long long)value);
	return *this;
}

JsonObject& JsonObject::Bool(const char *key, bool value) {
	Key(key);
	mBody += value ? "true" : "false";
	return *this;
}

}  // namespace ATMCP
