//	Altirra - Atari 800/800XL/5200 emulator
//	Copyright (C) 2022 Avery Lee
//
//	This program is free software; you can redistribute it and/or modify
//	it under the terms of the GNU General Public License as published by
//	the Free Software Foundation; either version 2 of the License, or
//	(at your option) any later version.
//
//	This program is distributed in the hope that it will be useful,
//	but WITHOUT ANY WARRANTY; without even the implied warranty of
//	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//	GNU General Public License for more details.
//
//	You should have received a copy of the GNU General Public License along
//	with this program. If not, see <http://www.gnu.org/licenses/>.
//
//	As a special exception, this library can also be redistributed and/or
//	modified under an alternate license. See COPYING.RMT in the same source
//	archive for details.

#include <stdafx.h>
#include <vd2/system/error.h>
#include <vd2/system/vdalloc.h>
#include <at/atcore/propertyset.h>
#include <at/atcore/enumparseimpl.h>

ATPropertySet::ATPropertySet() {
}

vdnothrow ATPropertySet::ATPropertySet(ATPropertySet&& src) noexcept
	: mProperties(std::move(src.mProperties))
{
	src.mProperties.clear();
}

ATPropertySet::ATPropertySet(const ATPropertySet& src) {
	operator=(src);
}

ATPropertySet::~ATPropertySet() {
	Clear();
}

ATPropertySet& ATPropertySet::operator=(const ATPropertySet& src) {
	if (&src != this) {
		Clear();

		auto it = src.mProperties.begin(), itEnd = src.mProperties.end();

		for(; it != itEnd; ++it) {
			const char *name = it->first;
			size_t len = strlen(name);

			vdautoarrayptr<char> newname(new char[len+1]);
			memcpy(newname.get(), name, len+1);

			vdautoarrayptr<wchar_t> newstr;
			if (it->second.mType == kATPropertyType_String16) {
				size_t len2 = wcslen(it->second.mValStr16);
				newstr = new wchar_t[len2 + 1];
				memcpy(newstr.get(), it->second.mValStr16, sizeof(wchar_t)*(len2 + 1));
			}

			ATPropertyValue& newVal = mProperties.insert(newname.get()).first->second;
			newname.release();

			newVal = it->second;

			if (it->second.mType == kATPropertyType_String16) {
				newVal.mValStr16 = newstr.get();
				newstr.release();
			}
		}
	}

	return *this;
}

vdnothrow ATPropertySet& ATPropertySet::operator=(ATPropertySet&& src) noexcept {
	if (&src != this) {
		mProperties = std::move(src.mProperties);
		src.mProperties.clear();
	}

	return *this;
}

void ATPropertySet::Clear() {
	auto it = mProperties.begin(), itEnd = mProperties.end();

	while(it != itEnd) {
		const char *name = it->first;

		if (it->second.mType == kATPropertyType_String16) {
			wchar_t *s = it->second.mValStr16;
			it->second.mValStr16 = nullptr;

			delete[] s;
		}

		it = mProperties.erase(it);

		delete[] name;
	}
}

void ATPropertySet::EnumProperties(void (*fn)(const char *name, const ATPropertyValue& val, void *data), void *data) const {
	for(auto it = mProperties.begin(), itEnd = mProperties.end();
		it != itEnd;
		++it)
	{
		fn(it->first, it->second, data);
	}
}

void ATPropertySet::Unset(const char *name) {
	auto it = mProperties.find(name);

	if (it != mProperties.end()) {
		const char *name = it->first;

		mProperties.erase(it);
		delete[] name;
	}
}

void ATPropertySet::SetBool(const char *name, bool val) {
	CreateProperty(name, kATPropertyType_Bool).mValBool = val;
}

void ATPropertySet::SetInt32(const char *name, sint32 val) {
	CreateProperty(name, kATPropertyType_Int32).mValI32 = val;
}

void ATPropertySet::SetUint32(const char *name, uint32 val) {
	CreateProperty(name, kATPropertyType_Uint32).mValU32 = val;
}

void ATPropertySet::SetFloat(const char *name, float val) {
	CreateProperty(name, kATPropertyType_Float).mValF = val;
}

void ATPropertySet::SetDouble(const char *name, double val) {
	CreateProperty(name, kATPropertyType_Double).mValD = val;
}

void ATPropertySet::SetString(const char *name, const wchar_t *val) {
	size_t len = wcslen(val);
	vdautoarrayptr<wchar_t> newstr(new wchar_t[len+1]);
	memcpy(newstr.get(), val, sizeof(wchar_t)*(len+1));

	ATPropertyValue& propVal = CreateProperty(name, kATPropertyType_String16);
	propVal.mValStr16 = newstr.get();
	newstr.release();
}

void ATPropertySet::SetEnum(const ATEnumLookupTable& enumTable, const char *name, uint32 val) {
	const char *s = ATEnumToString(enumTable, val);

	size_t len = strlen(s);
	vdautoarrayptr<wchar_t> newstr(new wchar_t[len+1]);
	for(size_t i = 0; i <= len; ++i)
		newstr[i] = (wchar_t)(unsigned char)s[i];

	ATPropertyValue& propVal = CreateProperty(name, kATPropertyType_String16);
	propVal.mValStr16 = newstr.get();
	newstr.release();
}

bool ATPropertySet::GetBool(const char *name, bool def) const {
	bool val = def;
	TryGetBool(name, val);
	return val;
}

sint32 ATPropertySet::GetInt32(const char *name, sint32 def) const {
	sint32 val = def;
	TryGetInt32(name, val);
	return val;
}

uint32 ATPropertySet::GetUint32(const char *name, uint32 def) const {
	uint32 val = def;
	TryGetUint32(name, val);
	return val;
}

float ATPropertySet::GetFloat(const char *name, float def) const {
	float val = def;
	TryGetFloat(name, val);
	return val;
}

double ATPropertySet::GetDouble(const char *name, double def) const {
	double val = def;
	TryGetDouble(name, val);
	return val;
}

const wchar_t *ATPropertySet::GetString(const char *name, const wchar_t *def) const {
	const wchar_t *val = def;
	TryGetString(name, val);
	return val;
}

uint32 ATPropertySet::GetEnum(const ATEnumLookupTable& enumTable, const char *name) const {
	uint32 v;
	return TryGetEnum(enumTable, name, v) ? v : enumTable.mDefaultValue;
}

uint32 ATPropertySet::GetEnum(const ATEnumLookupTable& enumTable, const char *name, uint32 table) const {
	uint32 v;
	return TryGetEnum(enumTable, name, v) ? v : table;
}

bool ATPropertySet::TryGetBool(const char *name, bool& val) const {
	const ATPropertyValue *propVal = GetProperty(name);

	if (!propVal)
		return false;

	switch(propVal->mType) {
		case kATPropertyType_Float:
			val = propVal->mValF != 0;
			return true;

		case kATPropertyType_Double:
			val = propVal->mValD != 0;
			return true;

		case kATPropertyType_Int32:
			val = propVal->mValI32 != 0;
			return true;

		case kATPropertyType_Uint32:
			val = propVal->mValU32 != 0;
			return true;

		case kATPropertyType_Bool:
			val = propVal->mValBool;
			return true;

		case kATPropertyType_String16: {
			const wchar_t *s = propVal->mValStr16;

			while(*s == L' ')
				++s;

			if (*s == L'0') {
				++s;
				val = false;
			} else if (*s == L'1') {
				++s;
				val = true;
			} else if (*s == L'f') {
				if (s[1] != L'a' || s[2] != L'l' || s[3] != 's' || s[4] != 'e')
					return false;
				s += 5;
				val = false;
			} else if (*s == L't') {
				if (s[1] != L'r' || s[2] != L'u' || s[3] != 'e')
					return false;
				s += 4;

				val = true;
			} else
				return false;

			while(*s == L' ')
				++s;

			if (*s)
				return false;

			return true;
		}

		default:
			return false;
	}
}

bool ATPropertySet::TryGetInt32(const char *name, sint32& val) const {
	const ATPropertyValue *propVal = GetProperty(name);

	if (!propVal)
		return false;

	switch(propVal->mType) {
		case kATPropertyType_Bool:
			val = propVal->mValBool ? 1 : 0;
			return true;

		case kATPropertyType_Float:
			if ((double)propVal->mValF < -0x7FFFFFFF-1 || (double)propVal->mValF > 0x7FFFFFFF)
				return false;

			val = (sint32)propVal->mValF;
			return true;

		case kATPropertyType_Double:
			if (propVal->mValD < -0x7FFFFFFF-1 || propVal->mValD > 0x7FFFFFFF)
				return false;

			val = (sint32)propVal->mValD;
			return true;

		case kATPropertyType_Int32:
			val = propVal->mValI32;
			return true;

		case kATPropertyType_Uint32:
			if (propVal->mValU32 > 0x7FFFFFFF)
				return false;

			val = (sint32)propVal->mValU32;
			return true;

		case kATPropertyType_String16: {
			wchar_t *s = propVal->mValStr16;

			while(*s == L' ')
				++s;

			wchar_t *t = s;
			int base = 0;
			
			errno = 0;

			if (*s == '$') {
				++s;
				base = 16;

				errno = 0;
				const unsigned long long vh = wcstoull(s, &t, 16);
				if (errno)
					return false;

				if (std::cmp_greater(vh, INT32_MAX))
					return false;

				val = (sint32)vh;
			} else {
				const long long v = wcstoll(s, &t, base);
				if (errno)
					return false;

				if (std::cmp_less(v, INT32_MIN) || std::cmp_greater(v, INT32_MAX))
					return false;

				val = (sint32)v;
			}

			while(*t == L' ')
				++t;

			if (*t)
				return false;

			return true;
		}

		default:
			return false;
	}
}

bool ATPropertySet::TryGetUint32(const char *name, uint32& val) const {
	const ATPropertyValue *propVal = GetProperty(name);

	if (!propVal)
		return false;

	switch(propVal->mType) {
		case kATPropertyType_Bool:
			val = propVal->mValBool ? 1 : 0;
			return true;

		case kATPropertyType_Float:
			if (propVal->mValF < 0 || (double)propVal->mValF > 0xFFFFFFFF)
				return false;

			val = (uint32)propVal->mValF;
			return true;

		case kATPropertyType_Double:
			if (propVal->mValD < 0 || propVal->mValD > 0xFFFFFFFF)
				return false;

			val = (uint32)propVal->mValD;
			return true;

		case kATPropertyType_Int32:
			if (propVal->mValI32 < 0)
				return false;

			val = (uint32)propVal->mValI32;
			return true;

		case kATPropertyType_Uint32:
			val = propVal->mValU32;
			return true;

		case kATPropertyType_String16: {
			wchar_t *s = propVal->mValStr16;

			while(*s == L' ')
				++s;

			int base = 0;
			if (*s == '$') {
				++s;
				base = 16;
			}

			wchar_t *t = s;

			errno = 0;
			const unsigned long long v = wcstoull(s, &t, base);
			if (errno)
				return false;

			if (v > UINT32_MAX)
				return false;

			while(*t == L' ')
				++t;

			if (*t)
				return false;

			val = (uint32)v;
			return true;
		}

		default:
			return false;
	}
}

bool ATPropertySet::TryGetFloat(const char *name, float& val) const {
	const ATPropertyValue *propVal = GetProperty(name);

	if (!propVal)
		return false;

	switch(propVal->mType) {
		case kATPropertyType_Bool:
			val = propVal->mValBool ? 1 : 0;
			return true;

		case kATPropertyType_Int32:
			val = (float)propVal->mValI32;
			return true;

		case kATPropertyType_Uint32:
			val = (float)propVal->mValU32;
			return true;

		case kATPropertyType_Double:
			val = (float)propVal->mValD;
			return true;

		case kATPropertyType_Float:
			val = propVal->mValF;
			return true;

		case kATPropertyType_String16:
			if (!IsValidFPNumber(propVal->mValStr16))
				return false;

			errno = 0;
			val = wcstof(propVal->mValStr16, nullptr);
			if (errno)
				return false;

			return true;

		default:
			return false;
	}
}

bool ATPropertySet::TryGetDouble(const char *name, double& val) const {
	const ATPropertyValue *propVal = GetProperty(name);

	if (!propVal)
		return false;

	switch(propVal->mType) {
		case kATPropertyType_Bool:
			val = propVal->mValBool ? 1 : 0;
			return true;

		case kATPropertyType_Int32:
			val = propVal->mValI32;
			return true;

		case kATPropertyType_Uint32:
			val = propVal->mValU32;
			return true;

		case kATPropertyType_Float:
			val = propVal->mValF;
			return true;

		case kATPropertyType_Double:
			val = propVal->mValD;
			return true;

		case kATPropertyType_String16:
			// pre-scan the string and make sure it is a valid simple number
			if (!IsValidFPNumber(propVal->mValStr16))
				return false;

			// convert the string now that we know it's good
			errno = 0;
			val = wcstod(propVal->mValStr16, nullptr);

			// reject infinites
			if (errno != 0)
				return false;

			return true;

		default:
			return false;
	}
}

bool ATPropertySet::TryGetString(const char *name, const wchar_t *& val) const {
	const ATPropertyValue *propVal = GetProperty(name);

	if (!propVal || propVal->mType != kATPropertyType_String16)
		return false;

	val = propVal->mValStr16;
	return true;
}

bool ATPropertySet::TryGetEnum(const ATEnumLookupTable& table, const char *name, uint32& val) const {
	val = 0;

	const ATPropertyValue *propVal = GetProperty(name);
	if (!propVal || propVal->mType != kATPropertyType_String16)
		return false;

	auto result = ATParseEnum(table, VDStringSpanW(propVal->mValStr16));
	
	val = result.mValue;
	return result.mValid;
}

bool ATPropertySet::TryConvertToString(const char *name, VDStringW& s) const {
	s.clear();

	const ATPropertyValue *propVal = GetProperty(name);
	if (!propVal)
		return false;

	switch(propVal->mType) {
		case kATPropertyType_Bool:
			s = propVal->mValBool ? L"1" : L"0";
			break;

		case kATPropertyType_Int32:
			s.sprintf(L"%d", (int)propVal->mValI32);
			break;

		case kATPropertyType_Uint32:
			s.sprintf(L"%u", (unsigned)propVal->mValU32);
			break;

		case kATPropertyType_Float:
			s.sprintf(L"%.8g", (unsigned)propVal->mValF);
			break;

		case kATPropertyType_Double:
			s.sprintf(L"%.17g", (unsigned)propVal->mValD);
			break;

		case kATPropertyType_String16:
			s = propVal->mValStr16;
			break;

		default:
			return false;
	}

	return true;
}

VDStringW ATPropertySet::ToCommandLineString() const {
	VDStringW s;

	EnumProperties(
		[&](const char *name, const ATPropertyValue& v) {
			if (!s.empty())
				s += L',';

			s += VDTextAToW(name);

			VDStringW value;
			if (TryConvertToString(name, value)) {
				const bool needQuotes = value.find(L',') != value.npos;

				s += L'=';

				VDStringW quotedValue;

				if (needQuotes)
					quotedValue += L'"';

				bool valid = true;

				for(wchar_t c : value) {
					if (c < 0x20) {
						valid = false;
						break;
					}

					if (c == L'"')
						s += c;
					
					s += c;
				}

				if (valid) {
					if (needQuotes)
						quotedValue += L'"';

					size_t backslashes = 0;
					for(wchar_t c : quotedValue) {

						if (c == L'\\') {
							++backslashes;
							continue;
						}

						if (c == L'"') {
							while(backslashes--)
								s += L"\\\\";

							s += L"\\\"";
							continue;
						}

						while(backslashes--)
							s += L'\\';

						s += c;
					}

					return;
				}
			}

			throw VDException(L"The settings for this device cannot be specified from the command line and must be imported as a file instead.");
		}
	);

	return s;
}

void ATPropertySet::ParseFromCommandLineString(const wchar_t *s) {
	const auto isValidName = [](VDStringSpanW name) {
		if (name.empty())
			return false;

		wchar_t c = name[0];

		if (c != '_' && (c < L'a' || c > L'z') && (c < L'A' || c > L'Z'))
			return false;

		for(wchar_t ch : name.subspan(1)) {
			if (ch != '_' && (ch < L'a' || ch > L'z') && (ch < L'A' || ch > L'Z') && (ch < L'0' || ch > L'9'))
				return false;
		}

		return true;
	};

	for(;;) {
		while(*s == L' ')
			++s;

		if (!*s)
			break;

		const wchar_t *nameStart = s;
		while(*s != L'=') {
			if (!*s)
				throw VDException(L"Invalid device parameter: %ls", nameStart);

			++s;
		}

		// validate name
		const wchar_t *nameEnd = s;
		while(nameEnd != nameStart && nameEnd[-1] == L' ')
			--nameEnd;

		VDStringSpanW nameView(nameStart, nameEnd);

		if (!isValidName(nameView))
			throw VDException(L"Invalid device parameter name: %.*ls", (int)nameView.size(), nameView.data());

		VDStringA name = VDTextWToA(nameView);

		// parse value
		++s;

		while(*s == L' ')
			++s;

		// check for quoting
		const wchar_t *valueStart = s;
		bool quoted = false;

		if (*s == L'"') {
			++s;
			quoted = true;
		}

		// parse out value, converting double-escaped quotes
		VDStringW value;
		bool valid = true;

		for(;;) {
			if (!*s) {
				if (quoted)
					valid = false;
				break;
			}

			if (*s == L',' && !quoted)
				break;

			if (*s == L'"') {
				++s;

				if (*s != L'"') {
					if (!quoted)
						valid = false;

					break;
				}
			}

			value += *s;
			++s;
		}

		if (!valid)
			throw VDException(L"Invalid device parameter: %.*ls", (int)(s - valueStart), valueStart);

		SetString(name.c_str(), value.c_str());

		if (!*s)
			break;

		++s;
	}
}

const ATPropertyValue *ATPropertySet::GetProperty(const char *name) const {
	auto it = mProperties.find(name);

	return it != mProperties.end() ? &it->second : nullptr;
}

ATPropertyValue& ATPropertySet::CreateProperty(const char *name, ATPropertyType type) {
	auto it = mProperties.find(name);

	if (it != mProperties.end()) {
		if (it->second.mType == kATPropertyType_String16)
			delete[] it->second.mValStr16;

		it->second.mType = type;
		return it->second;
	}

	size_t len = strlen(name);
	vdautoarrayptr<char> newName(new char[len + 1]);
	memcpy(newName.get(), name, len+1);

	ATPropertyValue& newVal = mProperties.insert(newName.get()).first->second;
	newName.release();

	newVal.mType = type;
	return newVal;
}

bool ATPropertySet::IsValidFPNumber(const wchar_t *s) {
	while(*s == L' ')
		++s;

	// eat sign
	if (*s == L'+' || *s == L'-')
		++s;

	// eat integer digits
	bool hasDigits = false;

	while(*s >= L'0' && *s <= L'9') {
		++s;
		hasDigits = true;
	}

	// eat fractional digits
	if (*s == L'.') {
		++s;

		while(*s >= L'0' && *s <= L'9') {
			++s;
			hasDigits = true;
		}
	}

	// fail if we have no integer or fractional digits
	if (!hasDigits)
		return false;

	// check for exponential
	if (*s == L'e' || *s == L'E') {
		++s;

		if (*s == L'+' || *s == L'-')
			++s;

		if (*s < L'0' || *s > L'9')
			return false;

		do {
			++s;
		} while(*s >= L'0' && *s <= L'9');
	}

	// eat trailing spaces
	while(*s == L' ')
		++s;

	// fail if there are unconsumed characters
	if (*s)
		return false;

	// all good
	return true;
}

