//	VirtualDub - Video processing and capture application
//	System library component
//	Copyright (C) 2026 Avery Lee, All Rights Reserved.
//
//	Beginning with 1.6.0, the VirtualDub system library is licensed
//	differently than the remainder of VirtualDub.  This particular file is
//	thus licensed as follows (the "zlib" license):
//
//	This software is provided 'as-is', without any express or implied
//	warranty.  In no event will the authors be held liable for any
//	damages arising from the use of this software.
//
//	Permission is granted to anyone to use this software for any purpose,
//	including commercial applications, and to alter it and redistribute it
//	freely, subject to the following restrictions:
//
//	1.	The origin of this software must not be misrepresented; you must
//		not claim that you wrote the original software. If you use this
//		software in a product, an acknowledgment in the product
//		documentation would be appreciated but is not required.
//	2.	Altered source versions must be plainly marked as such, and must
//		not be misrepresented as being the original software.
//	3.	This notice may not be removed or altered from any source
//		distribution.

class vddouble2 {
public:
	typedef vddouble2 self_type;
	typedef double value_type;

	void set(value_type x2, value_type y2) { x=x2; y=y2; }

	value_type	lensq() const							{ return x*x + y*y; }
	value_type	length() const							{ return sqrtf(x*x + y*y); }

	self_type	operator-() const						{ self_type a = {-x, -y}; return a; }

	self_type	operator+(value_type s) const			{ return self_type { x+s, y+s }; }
	self_type	operator-(value_type s) const			{ return self_type { x-s, y-s }; }

	self_type	operator+(const self_type& r) const		{ self_type a = {x+r.x, y+r.y}; return a; }
	self_type	operator-(const self_type& r) const		{ self_type a = {x-r.x, y-r.y}; return a; }

	self_type&	operator+=(const self_type& r)			{ x+=r.x; y+=r.y; return *this; }
	self_type&	operator-=(const self_type& r)			{ x-=r.x; y-=r.y; return *this; }

	self_type	operator*(value_type s) const			{ self_type a = {x*s, y*s}; return a; }
	self_type&	operator*=(value_type s)				{ x*=s; y*=s; return *this; }

	self_type	operator/(value_type s) const			{ const value_type inv(value_type(1)/s); self_type a = {x*inv, y*inv}; return a; }
	self_type&	operator/=(value_type s)				{ const value_type inv(value_type(1)/s); x*=inv; y*=inv; return *this; }

	self_type	operator*(const self_type& r) const		{ self_type a = {x*r.x, y*r.y}; return a; }
	self_type&	operator*=(const self_type& r)			{ x*=r.x; y*=r.y; return *this; }

	self_type	operator/(const self_type& r) const		{ self_type a = {x/r.x, y/r.y}; return a; }
	self_type&	operator/=(const self_type& r)			{ x/=r.x; y/=r.y; return *this; }

	value_type x;
	value_type y;
};

VDFORCEINLINE vddouble2 operator*(double s, const vddouble2& v) { return v*s; }
VDFORCEINLINE vddouble2 operator/(double s, const vddouble2& v) { return vddouble2 { s / v.x, s / v.y }; }

inline bool operator==(const vddouble2& a, const vddouble2& b) {
	return a.x == b.x && a.y == b.y;
}

inline bool operator!=(const vddouble2& a, const vddouble2& b) {
	return a.x != b.x || a.y != b.y;
}

///////////////////////////////////////////////////////////////////////////

class vddouble3 {
public:
	typedef vddouble3 self_type;
	typedef double value_type;

	void set(double x2, double y2, double z2) { x=x2; y=y2; z=z2; }

	constexpr double		lensq() const							{ return x*x + y*y + z*z; }

	constexpr vddouble2	project() const							{ const value_type inv(value_type(1)/z); const vddouble2 a = {x*inv, y*inv}; return a; }
	constexpr vddouble2	as2d() const							{ const vddouble2 a = {x, y}; return a; }

	constexpr self_type	operator+() const						{ return *this; }
	constexpr self_type	operator-() const						{ return vddouble3 {-x, -y, -z}; }

	constexpr self_type	operator+(double s) const				{ return vddouble3 { x+s, y+s, z+s }; }
	constexpr self_type	operator-(double s) const				{ return vddouble3 { x-s, y-s, z-s }; }

	constexpr self_type	operator+(const self_type& r) const		{ return vddouble3 { x+r.x, y+r.y, z+r.z }; }
	constexpr self_type	operator-(const self_type& r) const		{ return vddouble3 { x-r.x, y-r.y, z-r.z }; }

	constexpr self_type&	operator+=(const self_type& r)		{ x+=r.x; y+=r.y; z+=r.z; return *this; }
	constexpr self_type&	operator-=(const self_type& r)		{ x-=r.x; y-=r.y; z-=r.z; return *this; }

	constexpr self_type	operator*(const double s) const			{ return vddouble3 { x*s, y*s, z*s }; }
	constexpr self_type&	operator*=(const double s)			{ x*=s; y*=s; z*=s; return *this; }

	constexpr self_type	operator/(const double s) const			{ const value_type inv(value_type(1)/s); return vddouble3 { x*inv, y*inv, z*inv }; }
	constexpr self_type&	operator/=(const double s)			{ const value_type inv(value_type(1)/s); x*=inv; y*=inv; z*=inv; return *this; }

	constexpr self_type	operator*(const self_type& r) const		{ return vddouble3 { x*r.x, y*r.y, z*r.z }; }
	constexpr self_type&	operator*=(const self_type& r)		{ x*=r.x; y*=r.y; z*=r.z; return *this; }

	constexpr self_type	operator/(const self_type& r) const		{ return vddouble3 { x/r.x, y/r.y, z/r.z }; }
	constexpr self_type&	operator/=(const self_type& r)		{ x/=r.x; y/=r.y; z/=r.z; return *this; }

	value_type x;
	value_type y;
	value_type z;
};

VDFORCEINLINE constexpr vddouble3 operator+(double s, const vddouble3& v) { return vddouble3 { s+v.x, s+v.y, s+v.z }; }
VDFORCEINLINE constexpr vddouble3 operator-(double s, const vddouble3& v) { return vddouble3 { s-v.x, s-v.y, s-v.z }; }
VDFORCEINLINE constexpr vddouble3 operator*(double s, const vddouble3& v) { return vddouble3 { s*v.x, s*v.y, s*v.z }; }
VDFORCEINLINE constexpr vddouble3 operator/(double s, const vddouble3& v) { return vddouble3 { s/v.x, s/v.y, s/v.z }; }

///////////////////////////////////////////////////////////////////////////

class vddouble4 {
public:
	typedef vddouble4 self_type;
	typedef double value_type;

	static vddouble4 splat(double v) { return vddouble4 { v, v, v, v }; }

	void setzero() { x=y=z=w = 0; }
	void set(double x2, double y2, double z2, double w2) { x=x2; y=y2; z=z2; w=w2; }

	static constexpr vddouble4 zero() { return vddouble4 { 0.0f, 0.0f, 0.0f, 0.0f }; }

	double		lensq() const							{ return x*x + y*y + z*z + w*w; }

	vddouble3	project() const							{ const double inv(double(1)/w); const vddouble3 a = {x*inv, y*inv, z*inv}; return a; }

	self_type	operator-() const						{ const self_type a = {-x, -y, -z, -w}; return a; }

	self_type	operator+(const self_type& r) const		{ const self_type a = {x+r.x, y+r.y, z+r.z, w+r.w}; return a; }
	self_type	operator-(const self_type& r) const		{ const self_type a = {x-r.x, y-r.y, z-r.z, w-r.w}; return a; }

	self_type&	operator+=(const self_type& r)			{ x+=r.x; y+=r.y; z+=r.z; w+=r.w; return *this; }
	self_type&	operator-=(const self_type& r)			{ x-=r.x; y-=r.y; z-=r.z; w-=r.w; return *this; }

	self_type	operator*(const double factor) const	{ const self_type a = {x*factor, y*factor, z*factor, w*factor}; return a; }
	self_type	operator/(const double factor) const	{ const double inv(double(1) / factor); const self_type a = {x*inv, y*inv, z*inv, w*inv}; return a; }

	self_type&	operator*=(const double factor)			{ x *= factor; y *= factor; z *= factor; w *= factor; return *this; }
	self_type&	operator/=(const double factor)			{ const double inv(double(1) / factor); x *= inv; y *= inv; z *= inv; w *= inv; return *this; }

	self_type	operator*(const self_type& r) const		{ self_type a = {x*r.x, y*r.y, z*r.z, w*r.w}; return a; }
	self_type&	operator*=(const self_type& r)			{ x*=r.x; y*=r.y; z*=r.z; w*=r.w; return *this; }

	self_type	operator/(const self_type& r) const		{ self_type a = {x/r.x, y/r.y, z/r.z, w*r.w}; return a; }
	self_type&	operator/=(const self_type& r)			{ x/=r.x; y/=r.y; z/=r.z; w/=r.w; return *this; }

	value_type x;
	value_type y;
	value_type z;
	value_type w;
};

VDFORCEINLINE vddouble4 operator*(double s, const vddouble4& v) { return v*s; }

///////////////////////////////////////////////////////////////////////////

namespace nsVDMath {
	VDFORCEINLINE double length(const vddouble2& a) {
		return sqrtf(a.x*a.x + a.y*a.y);
	}

	VDFORCEINLINE double length(const vddouble3& a) {
		return sqrtf(a.x*a.x + a.y*a.y + a.z*a.z);
	}

	VDFORCEINLINE double length(const vddouble4& a) {
		return sqrtf(a.x*a.x + a.y*a.y + a.z*a.z + a.w*a.w);
	}

	VDFORCEINLINE vddouble2 normalize(const vddouble2& a) {
		return a / length(a);
	}

	VDFORCEINLINE vddouble3 normalize(const vddouble3& a) {
		return a / length(a);
	}

	VDFORCEINLINE vddouble4 normalize(const vddouble4& a) {
		return a / length(a);
	}

	VDFORCEINLINE constexpr double dot(const vddouble2& a, const vddouble2& b) {
		return a.x*b.x + a.y*b.y;
	}

	VDFORCEINLINE constexpr double dot(const vddouble3& a, const vddouble3& b) {
		return a.x*b.x + a.y*b.y + a.z*b.z;
	}

	VDFORCEINLINE constexpr double dot(const vddouble4& a, const vddouble4& b) {
		return a.x*b.x + a.y*b.y + a.z*b.z + a.w*b.w;
	}

	VDFORCEINLINE constexpr vddouble2 rot(const vddouble2& a) {
		return vddouble2{a.y, -a.x};
	}

	VDFORCEINLINE constexpr vddouble3 cross(const vddouble3& a, const vddouble3& b) {
		const vddouble3 r = {a.y*b.z - a.z*b.y, a.z*b.x - a.x*b.z, a.x*b.y - a.y*b.x};
		return r;
	}

	VDFORCEINLINE constexpr vddouble2 min(const vddouble2& a, const vddouble2& b) {
		return vddouble2 {
			a.x < b.x ? a.x : b.x,
			a.y < b.y ? a.y : b.y
		};
	}

	VDFORCEINLINE constexpr vddouble2 max(const vddouble2& a, const vddouble2& b) {
		return vddouble2 {
			a.x > b.x ? a.x : b.x,
			a.y > b.y ? a.y : b.y
		};
	}

	VDFORCEINLINE constexpr vddouble4 min(const vddouble4& a, const vddouble4& b) {
		return vddouble4 {
			a.x < b.x ? a.x : b.x,
			a.y < b.y ? a.y : b.y,
			a.z < b.z ? a.z : b.z,
			a.w < b.w ? a.w : b.w,
		};
	}

	VDFORCEINLINE constexpr vddouble4 max(const vddouble4& a, const vddouble4& b) {
		return vddouble4 {
			a.x > b.x ? a.x : b.x,
			a.y > b.y ? a.y : b.y,
			a.z > b.z ? a.z : b.z,
			a.w > b.w ? a.w : b.w,
		};
	}
};
