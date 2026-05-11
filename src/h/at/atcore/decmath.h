//	Altirra - Atari 800/800XL/5200 emulator
//	Copyright (C) 2009-2019 Avery Lee
//	Decimal math support
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

#ifndef f_AT_ATCORE_DECMATH_H
#define f_AT_ATCORE_DECMATH_H

#include <optional>
#include <string.h>		// memcpy — see docs/merging-with-altirra-mainline.md
#include <vd2/system/vdtypes.h>
#include <vd2/system/VDString.h>

struct ATDecFloatInvalid {};

struct ATDecFloatText final : public VDStringSpanA {
	ATDecFloatText() = default;

	vdnothrow ATDecFloatText(const ATDecFloatText& src) vdnoexcept
		: VDStringSpanA(mBuf.ch, mBuf.ch + src.size())
		, mBuf(src.mBuf)
	{
	}

	vdnothrow ATDecFloatText& operator=(const ATDecFloatText& src) vdnoexcept {
		if (&src != this) {
			mBuf = src.mBuf;
			static_cast<VDStringSpanA&>(*this) = VDStringSpanA(mBuf.ch, mBuf.ch + src.size());
		}

		return *this;
	}

	vdnothrow ATDecFloatText(ATDecFloatInvalid) vdnoexcept
		: VDStringSpanA(mBuf.ch, mBuf.ch + 7)
		, mBuf{}
	{
		memcpy(mBuf.ch, "invalid", 8);
	}

	const char *c_str() const { return mBuf.ch; }

	struct Buffer {
		char ch[18];
	} mBuf;
};

struct ATDecFloat {
	uint8	mSignExp;
	uint8	mMantissa[5];

	static ATDecFloat FromBytes(const void *fp6) {
		ATDecFloat x;
		memcpy(&x, fp6, 6);

		return x;
	}
	
	static ATDecFloat FromDouble(double x) {
		ATDecFloat d;
		d.SetDouble(x);
		return d;
	}

	static constexpr ATDecFloat Zero() {
		constexpr ATDecFloat zero { 0x00, { 0x00, 0x00, 0x00, 0x00, 0x00 } };
		return zero;
	}

	static constexpr ATDecFloat One() {
		constexpr ATDecFloat one { 0x40, { 0x01, 0x00, 0x00, 0x00, 0x00 } };
		return one;
	}

	static const ATDecFloat kLog10E;

	void SetZero() { *this = Zero(); }
	void SetOne() { *this = One(); }

	bool SetDouble(double d);

	ATDecFloat operator-() const;

	ATDecFloat Abs() const;
	std::optional<ATDecFloat> Log() const;
	std::optional<ATDecFloat> Log10() const;
	std::optional<ATDecFloat> Exp() const;
	std::optional<ATDecFloat> Exp10() const;

	std::optional<ATDecFloat> PlyEvl(const ATDecFloat& x, vdspan<const ATDecFloat> coeffs) const;

	ATDecFloatText ToString() const;
	double ToDouble() const;
};

// log10(e) = 0.43 42 94 48 19 0325182765112891891661
inline const constinit ATDecFloat ATDecFloat::kLog10E { 0x3F, { 0x43, 0x42, 0x94, 0x48, 0x19 } };


bool operator< (const ATDecFloat& x, const ATDecFloat& y);
bool operator==(const ATDecFloat& x, const ATDecFloat& y);
bool operator> (const ATDecFloat& x, const ATDecFloat& y);
bool operator!=(const ATDecFloat& x, const ATDecFloat& y);
bool operator<=(const ATDecFloat& x, const ATDecFloat& y);
bool operator>=(const ATDecFloat& x, const ATDecFloat& y);

bool ATDecFloatAdd(ATDecFloat& dst, const ATDecFloat& x, const ATDecFloat& y);
bool ATDecFloatMul(ATDecFloat& dst, const ATDecFloat& x, const ATDecFloat& y);
bool ATDecFloatDiv(ATDecFloat& dst, const ATDecFloat& x, const ATDecFloat& y);

struct ATDecFloatOpt {
	ATDecFloatOpt() = default;
	ATDecFloatOpt(const ATDecFloat& v) : mValue(v), mbValid(true) {};

	ATDecFloatOpt& operator=(const ATDecFloat& v) {
		mValue = v;
		mbValid = true;
		return *this;
	}

	bool IsValid() const { return mbValid; }
	const ATDecFloat& Value() const { return mValue; }

	ATDecFloatOpt operator-() const {
		return mbValid ? ATDecFloatOpt(-mValue) : ATDecFloatOpt();
	}
	
	ATDecFloatOpt& operator+=(const ATDecFloatOpt& other);
	ATDecFloatOpt& operator-=(const ATDecFloatOpt& other);
	ATDecFloatOpt& operator*=(const ATDecFloatOpt& other);
	ATDecFloatOpt& operator/=(const ATDecFloatOpt& other);

	ATDecFloatOpt operator+(const ATDecFloatOpt& other) const;
	ATDecFloatOpt operator-(const ATDecFloatOpt& other) const;
	ATDecFloatOpt operator*(const ATDecFloatOpt& other) const;
	ATDecFloatOpt operator/(const ATDecFloatOpt& other) const;

	ATDecFloatOpt PlyEvl(vdspan<const ATDecFloat> coeffs) const;
	ATDecFloatOpt Log() const;
	ATDecFloatOpt Log10() const;
	ATDecFloatOpt Exp() const;
	ATDecFloatOpt Exp10() const;

	ATDecFloatText ToString() const;

	bool operator==(const ATDecFloatOpt& other) const {
		return mbValid ? other.mbValid && mValue == other.mValue : !other.mbValid;
	}

	ATDecFloat mValue {};
	bool mbValid = false;
};

inline ATDecFloatOpt operator+(const ATDecFloat& x, const ATDecFloatOpt& y) { return ATDecFloatOpt(x) + y; }
inline ATDecFloatOpt operator-(const ATDecFloat& x, const ATDecFloatOpt& y) { return ATDecFloatOpt(x) - y; }
inline ATDecFloatOpt operator*(const ATDecFloat& x, const ATDecFloatOpt& y) { return ATDecFloatOpt(x) * y; }
inline ATDecFloatOpt operator/(const ATDecFloat& x, const ATDecFloatOpt& y) { return ATDecFloatOpt(x) / y; }

#endif
