//	VirtualDub - Video processing and capture application
//	System library component
//	Copyright (C) 1998-2019 Avery Lee, All Rights Reserved.
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

#ifndef f_VD2_SYSTEM_VECMATH_REF_H
#define f_VD2_SYSTEM_VECMATH_REF_H

#include <string.h>		// memcpy — see docs/merging-with-altirra-mainline.md
#include <vd2/system/vdtypes.h>
#include <vd2/system/vectors.h>

namespace nsVDVecMath {
	struct vdmask32x3 {
		alignas(16) uint32 v[4];

		static vdmask32x3 set(bool x, bool y, bool z) {
			return vdmask32x3 {
				{
					x ? UINT32_C(0xFFFFFFFF) : 0,
					y ? UINT32_C(0xFFFFFFFF) : 0,
					z ? UINT32_C(0xFFFFFFFF) : 0,
					0
				}
			};
		}
	};

	inline bool operator==(vdmask32x3 a, vdmask32x3 b) { return a.v[0] == b.v[0] && a.v[1] == b.v[1] && a.v[2] == b.v[2]; }
	inline bool operator!=(vdmask32x3 a, vdmask32x3 b) { return a.v[0] != b.v[0] || a.v[1] != b.v[1] || a.v[2] != b.v[2]; }

	inline vdmask32x3 operator~(vdmask32x3 a) { return vdmask32x3 { ~a.v[0], ~a.v[1], ~a.v[2] }; }
	inline vdmask32x3 operator&(vdmask32x3 a, vdmask32x3 b) { return vdmask32x3 { a.v[0] & b.v[0], a.v[1] & b.v[1], a.v[2] & b.v[2] }; }
	inline vdmask32x3 operator|(vdmask32x3 a, vdmask32x3 b) { return vdmask32x3 { a.v[0] | b.v[0], a.v[1] | b.v[1], a.v[2] | b.v[2] }; }
	inline vdmask32x3 operator^(vdmask32x3 a, vdmask32x3 b) { return vdmask32x3 { a.v[0] ^ b.v[0], a.v[1] ^ b.v[1], a.v[2] ^ b.v[2] }; }

	//////////////////////////////////////////////////////////////////////////

	struct vdmask32x4 {
		alignas(16) uint32 v[4];

		static vdmask32x4 set(bool x, bool y, bool z, bool w) {
			return vdmask32x4 {
				{
					x ? UINT32_C(0xFFFFFFFF) : 0,
					y ? UINT32_C(0xFFFFFFFF) : 0,
					z ? UINT32_C(0xFFFFFFFF) : 0,
					w ? UINT32_C(0xFFFFFFFF) : 0
				}
			};
		}
	};

	inline bool operator==(vdmask32x4 a, vdmask32x4 b) { return a.v[0] == b.v[0] && a.v[1] == b.v[1] && a.v[2] == b.v[2] && a.v[3] == b.v[3]; }
	inline bool operator!=(vdmask32x4 a, vdmask32x4 b) { return a.v[0] != b.v[0] || a.v[1] != b.v[1] || a.v[2] != b.v[2] || a.v[3] != b.v[3]; }

	inline vdmask32x4 operator~(vdmask32x4 a) { return vdmask32x4 { ~a.v[0], ~a.v[1], ~a.v[2], ~a.v[3] }; }
	inline vdmask32x4 operator&(vdmask32x4 a, vdmask32x4 b) { return vdmask32x4 { a.v[0] & b.v[0], a.v[1] & b.v[1], a.v[2] & b.v[2], a.v[3] & b.v[3] }; }
	inline vdmask32x4 operator|(vdmask32x4 a, vdmask32x4 b) { return vdmask32x4 { a.v[0] | b.v[0], a.v[1] | b.v[1], a.v[2] | b.v[2], a.v[3] | b.v[3] }; }
	inline vdmask32x4 operator^(vdmask32x4 a, vdmask32x4 b) { return vdmask32x4 { a.v[0] ^ b.v[0], a.v[1] ^ b.v[1], a.v[2] ^ b.v[2], a.v[3] ^ b.v[3] }; }

	//////////////////////////////////////////////////////////////////////////

	struct vdfloat32x3 {
		alignas(16) float v[4];

		static vdfloat32x3 zero() { return vdfloat32x3{}; }

		static vdfloat32x3 set1(float x) {
			return vdfloat32x3 { x, x, x, x };
		}

		static vdfloat32x3 set(float x, float y, float z) {
			return vdfloat32x3 { x, y, z, z };
		}

		float x() const { return v[0]; }
		float y() const { return v[1]; }
		float z() const { return v[2]; }
	};

	inline vdfloat32x3 operator+(vdfloat32x3 x) { return x; }
	inline vdfloat32x3 operator-(vdfloat32x3 x) { return vdfloat32x3 { -x.v[0], -x.v[1], -x.v[2] }; }

	inline vdfloat32x3 operator+(vdfloat32x3 x, vdfloat32x3 y) { return vdfloat32x3 { x.v[0] + y.v[0], x.v[1] + y.v[1], x.v[2] + y.v[2]  }; }
	inline vdfloat32x3 operator-(vdfloat32x3 x, vdfloat32x3 y) { return vdfloat32x3 { x.v[0] - y.v[0], x.v[1] - y.v[1], x.v[2] - y.v[2]  }; }
	inline vdfloat32x3 operator*(vdfloat32x3 x, vdfloat32x3 y) { return vdfloat32x3 { x.v[0] * y.v[0], x.v[1] * y.v[1], x.v[2] * y.v[2]  }; }
	inline vdfloat32x3 operator/(vdfloat32x3 x, vdfloat32x3 y) { return vdfloat32x3 { x.v[0] / y.v[0], x.v[1] / y.v[1], x.v[2] / y.v[2]  }; }

	inline vdfloat32x3 operator+(vdfloat32x3 x, float y) { return vdfloat32x3 { x.v[0] + y, x.v[1] + y, x.v[2] + y  }; }
	inline vdfloat32x3 operator-(vdfloat32x3 x, float y) { return vdfloat32x3 { x.v[0] - y, x.v[1] - y, x.v[2] - y  }; }
	inline vdfloat32x3 operator*(vdfloat32x3 x, float y) { return vdfloat32x3 { x.v[0] * y, x.v[1] * y, x.v[2] * y  }; }
	inline vdfloat32x3 operator/(vdfloat32x3 x, float y) { return vdfloat32x3 { x.v[0] / y, x.v[1] / y, x.v[2] / y  }; }

	inline vdfloat32x3& operator+=(vdfloat32x3& x, vdfloat32x3 y) { x.v[0]+=y.v[0]; x.v[1]+=y.v[1]; x.v[2]+=y.v[2]; return x; }
	inline vdfloat32x3& operator-=(vdfloat32x3& x, vdfloat32x3 y) { x.v[0]-=y.v[0]; x.v[1]-=y.v[1]; x.v[2]-=y.v[2]; return x; }
	inline vdfloat32x3& operator*=(vdfloat32x3& x, vdfloat32x3 y) { x.v[0]*=y.v[0]; x.v[1]*=y.v[1]; x.v[2]*=y.v[2]; return x; }
	inline vdfloat32x3& operator/=(vdfloat32x3& x, vdfloat32x3 y) { x.v[0]/=y.v[0]; x.v[1]/=y.v[1]; x.v[2]/=y.v[2]; return x; }

	inline vdfloat32x3& operator+=(vdfloat32x3& x, float y) { x.v[0]+=y, x.v[1]+=y, x.v[2]+=y; return x; }
	inline vdfloat32x3& operator-=(vdfloat32x3& x, float y) { x.v[0]-=y, x.v[1]-=y, x.v[2]-=y; return x; }
	inline vdfloat32x3& operator*=(vdfloat32x3& x, float y) { x.v[0]*=y, x.v[1]*=y, x.v[2]*=y; return x; }
	inline vdfloat32x3& operator/=(vdfloat32x3& x, float y) { x.v[0]/=y, x.v[1]/=y, x.v[2]/=y; return x; }

	inline vdfloat32x3 operator+(float x, vdfloat32x3 y) { return vdfloat32x3 { x + y.v[0], x + y.v[1], x + y.v[2]  }; }
	inline vdfloat32x3 operator-(float x, vdfloat32x3 y) { return vdfloat32x3 { x - y.v[0], x - y.v[1], x - y.v[2]  }; }
	inline vdfloat32x3 operator*(float x, vdfloat32x3 y) { return vdfloat32x3 { x * y.v[0], x * y.v[1], x * y.v[2]  }; }
	inline vdfloat32x3 operator/(float x, vdfloat32x3 y) { return vdfloat32x3 { x / y.v[0], x / y.v[1], x / y.v[2]  }; }

	inline bool operator==(vdfloat32x3 x, vdfloat32x3 y) { return x.v[0] == y.v[0] && x.v[1] == y.v[1] && x.v[2] == y.v[2]; }
	inline bool operator!=(vdfloat32x3 x, vdfloat32x3 y) { return x.v[0] != y.v[0] || x.v[1] != y.v[1] || x.v[2] != y.v[2]; }

	inline vdmask32x3 cmplt(vdfloat32x3 x, vdfloat32x3 y) { return vdmask32x3 { x.v[0] <  y.v[0] ? ~0U : 0U, x.v[1] <  y.v[1] ? ~0U : 0U, x.v[2] <  y.v[2] ? ~0U : 0U, 0U }; }
	inline vdmask32x3 cmple(vdfloat32x3 x, vdfloat32x3 y) { return vdmask32x3 { x.v[0] <= y.v[0] ? ~0U : 0U, x.v[1] <= y.v[1] ? ~0U : 0U, x.v[2] <= y.v[2] ? ~0U : 0U, 0U }; }
	inline vdmask32x3 cmpgt(vdfloat32x3 x, vdfloat32x3 y) { return vdmask32x3 { x.v[0] >  y.v[0] ? ~0U : 0U, x.v[1] >  y.v[1] ? ~0U : 0U, x.v[2] >  y.v[2] ? ~0U : 0U, 0U }; }
	inline vdmask32x3 cmpge(vdfloat32x3 x, vdfloat32x3 y) { return vdmask32x3 { x.v[0] >= y.v[0] ? ~0U : 0U, x.v[1] >= y.v[1] ? ~0U : 0U, x.v[2] >= y.v[2] ? ~0U : 0U, 0U }; }
	inline vdmask32x3 cmpeq(vdfloat32x3 x, vdfloat32x3 y) { return vdmask32x3 { x.v[0] == y.v[0] ? ~0U : 0U, x.v[1] == y.v[1] ? ~0U : 0U, x.v[2] == y.v[2] ? ~0U : 0U, 0U }; }
	inline vdmask32x3 cmpne(vdfloat32x3 x, vdfloat32x3 y) { return vdmask32x3 { x.v[0] != y.v[0] ? ~0U : 0U, x.v[1] != y.v[1] ? ~0U : 0U, x.v[2] != y.v[2] ? ~0U : 0U, 0U }; }

	//////////////////////////////////////////////////////////////////////////

	struct vdfloat32x4 {
		alignas(16) float v[4];

		[[nodiscard]] static vdfloat32x4 zero() { return vdfloat32x4{}; }

		[[nodiscard]] static vdfloat32x4 set1(float x) {
			return vdfloat32x4 { x, x, x, x };
		}

		[[nodiscard]] static vdfloat32x4 set(float x, float y, float z, float w) {
			return vdfloat32x4 { x, y, z, w };
		}

		[[nodiscard]] static vdfloat32x4 set(vdfloat32x3 xyz, float w) {
			return vdfloat32x4 { xyz.v[0], xyz.v[1], xyz.v[2], w };
		}

		[[nodiscard]] static vdfloat32x4 unpacku8(uint32 v8) {
			return vdfloat32x4 {
				(float)((v8 >>  0) & 0xff),
				(float)((v8 >>  8) & 0xff),
				(float)((v8 >> 16) & 0xff),
				(float)((v8 >> 24) & 0xff)
			};
		}

		[[nodiscard]] float x() const { return v[0]; }
		[[nodiscard]] float y() const { return v[1]; }
		[[nodiscard]] float z() const { return v[2]; }
		[[nodiscard]] float w() const { return v[3]; }
		[[nodiscard]] vdfloat32x3 xyz() const { return vdfloat32x3 { { v[0], v[1], v[2], v[3] } }; }
	};

	inline vdfloat32x4 operator+(vdfloat32x4 x) { return x; }
	inline vdfloat32x4 operator-(vdfloat32x4 x) { return vdfloat32x4 { -x.v[0], -x.v[1], -x.v[2], -x.v[3] }; }

	inline vdfloat32x4 operator+(vdfloat32x4 x, vdfloat32x4 y) { return vdfloat32x4 { x.v[0] + y.v[0], x.v[1] + y.v[1], x.v[2] + y.v[2], x.v[3] + y.v[3] }; }
	inline vdfloat32x4 operator-(vdfloat32x4 x, vdfloat32x4 y) { return vdfloat32x4 { x.v[0] - y.v[0], x.v[1] - y.v[1], x.v[2] - y.v[2], x.v[3] - y.v[3] }; }
	inline vdfloat32x4 operator*(vdfloat32x4 x, vdfloat32x4 y) { return vdfloat32x4 { x.v[0] * y.v[0], x.v[1] * y.v[1], x.v[2] * y.v[2], x.v[3] * y.v[3] }; }
	inline vdfloat32x4 operator/(vdfloat32x4 x, vdfloat32x4 y) { return vdfloat32x4 { x.v[0] / y.v[0], x.v[1] / y.v[1], x.v[2] / y.v[2], x.v[3] / y.v[3] }; }

	inline vdfloat32x4 operator+(vdfloat32x4 x, float y) { return vdfloat32x4 { x.v[0] + y, x.v[1] + y, x.v[2] + y, x.v[3] + y }; }
	inline vdfloat32x4 operator-(vdfloat32x4 x, float y) { return vdfloat32x4 { x.v[0] - y, x.v[1] - y, x.v[2] - y, x.v[3] - y }; }
	inline vdfloat32x4 operator*(vdfloat32x4 x, float y) { return vdfloat32x4 { x.v[0] * y, x.v[1] * y, x.v[2] * y, x.v[3] * y }; }
	inline vdfloat32x4 operator/(vdfloat32x4 x, float y) { return vdfloat32x4 { x.v[0] / y, x.v[1] / y, x.v[2] / y, x.v[3] / y }; }

	inline vdfloat32x4 operator+(float x, vdfloat32x4 y) { return vdfloat32x4 { x + y.v[0], x + y.v[1], x + y.v[2], x + y.v[3] }; }
	inline vdfloat32x4 operator-(float x, vdfloat32x4 y) { return vdfloat32x4 { x - y.v[0], x - y.v[1], x - y.v[2], x - y.v[3] }; }
	inline vdfloat32x4 operator*(float x, vdfloat32x4 y) { return vdfloat32x4 { x * y.v[0], x * y.v[1], x * y.v[2], x * y.v[3] }; }
	inline vdfloat32x4 operator/(float x, vdfloat32x4 y) { return vdfloat32x4 { x / y.v[0], x / y.v[1], x / y.v[2], x / y.v[3] }; }

	inline vdfloat32x4& operator+=(vdfloat32x4& x, vdfloat32x4 y) { x.v[0] += y.v[0]; x.v[1] += y.v[1]; x.v[2] += y.v[2]; x.v[3] += y.v[3]; return x; }
	inline vdfloat32x4& operator-=(vdfloat32x4& x, vdfloat32x4 y) { x.v[0] -= y.v[0]; x.v[1] -= y.v[1]; x.v[2] -= y.v[2]; x.v[3] -= y.v[3]; return x; }
	inline vdfloat32x4& operator*=(vdfloat32x4& x, vdfloat32x4 y) { x.v[0] *= y.v[0]; x.v[1] *= y.v[1]; x.v[2] *= y.v[2]; x.v[3] *= y.v[3]; return x; }
	inline vdfloat32x4& operator/=(vdfloat32x4& x, vdfloat32x4 y) { x.v[0] /= y.v[0]; x.v[1] /= y.v[1]; x.v[2] /= y.v[2]; x.v[3] /= y.v[3]; return x; }

	inline vdfloat32x4& operator+=(vdfloat32x4& x, float y) { x.v[0] += y; x.v[1] += y; x.v[2] += y; x.v[3] += y; return x; }
	inline vdfloat32x4& operator-=(vdfloat32x4& x, float y) { x.v[0] -= y; x.v[1] -= y; x.v[2] -= y; x.v[3] -= y; return x; }
	inline vdfloat32x4& operator*=(vdfloat32x4& x, float y) { x.v[0] *= y; x.v[1] *= y; x.v[2] *= y; x.v[3] *= y; return x; }
	inline vdfloat32x4& operator/=(vdfloat32x4& x, float y) { x.v[0] /= y; x.v[1] /= y; x.v[2] /= y; x.v[3] /= y; return x; }
	
	inline bool operator==(vdfloat32x4 x, vdfloat32x4 y) { return x.v[0] == y.v[0] && x.v[1] == y.v[1] && x.v[2] == y.v[2] && x.v[3] == y.v[3]; }
	inline bool operator!=(vdfloat32x4 x, vdfloat32x4 y) { return x.v[0] != y.v[0] || x.v[1] != y.v[1] || x.v[2] != y.v[2] || x.v[3] != y.v[3]; }

	inline vdmask32x4 cmplt(vdfloat32x4 x, vdfloat32x4 y) { return vdmask32x4 { x.v[0] <  y.v[0] ? ~0U : 0U, x.v[1] <  y.v[1] ? ~0U : 0U, x.v[2] <  y.v[2] ? ~0U : 0U, x.v[3] <  y.v[3] ? ~0U : 0U }; }
	inline vdmask32x4 cmple(vdfloat32x4 x, vdfloat32x4 y) { return vdmask32x4 { x.v[0] <= y.v[0] ? ~0U : 0U, x.v[1] <= y.v[1] ? ~0U : 0U, x.v[2] <= y.v[2] ? ~0U : 0U, x.v[3] <= y.v[3] ? ~0U : 0U }; }
	inline vdmask32x4 cmpgt(vdfloat32x4 x, vdfloat32x4 y) { return vdmask32x4 { x.v[0] >  y.v[0] ? ~0U : 0U, x.v[1] >  y.v[1] ? ~0U : 0U, x.v[2] >  y.v[2] ? ~0U : 0U, x.v[3] >  y.v[3] ? ~0U : 0U }; }
	inline vdmask32x4 cmpge(vdfloat32x4 x, vdfloat32x4 y) { return vdmask32x4 { x.v[0] >= y.v[0] ? ~0U : 0U, x.v[1] >= y.v[1] ? ~0U : 0U, x.v[2] >= y.v[2] ? ~0U : 0U, x.v[3] >= y.v[3] ? ~0U : 0U }; }
	inline vdmask32x4 cmpeq(vdfloat32x4 x, vdfloat32x4 y) { return vdmask32x4 { x.v[0] == y.v[0] ? ~0U : 0U, x.v[1] == y.v[1] ? ~0U : 0U, x.v[2] == y.v[2] ? ~0U : 0U, x.v[3] == y.v[3] ? ~0U : 0U }; }
	inline vdmask32x4 cmpne(vdfloat32x4 x, vdfloat32x4 y) { return vdmask32x4 { x.v[0] != y.v[0] ? ~0U : 0U, x.v[1] != y.v[1] ? ~0U : 0U, x.v[2] != y.v[2] ? ~0U : 0U, x.v[3] != y.v[3] ? ~0U : 0U }; }

	///////////////////////////////////////////////////////////////////////

	struct vdfloat32x3x3 {
		vdfloat32x3 x, y, z;
	};

	inline vdfloat32x3x3 loadu(const vdfloat3x3& m) {
		return vdfloat32x3x3 {
			{ m.x.x, m.x.y, m.x.z },
			{ m.y.x, m.y.y, m.y.z },
			{ m.z.x, m.z.y, m.z.z },
		};
	}

	inline vdfloat32x3 mul(vdfloat32x3 a, const vdfloat32x3x3& b) {
		return a.x() * b.x
			+ a.y() * b.y
			+ a.z() * b.z;
	}

	inline vdfloat32x3x3 mul(vdfloat32x3x3 a, vdfloat32x3x3 b) {
		return vdfloat32x3x3 {
			a.x.x() * b.x + a.x.y() * b.y + a.x.z() * b.z,
			a.y.x() * b.x + a.y.y() * b.y + a.y.z() * b.z,
			a.z.x() * b.x + a.z.y() * b.y + a.z.z() * b.z,
		};
	}

	///////////////////////////////////////////////////////////////////////

	struct vdfloat32x3x4 {
		vdfloat32x3 x, y, z, w;
	};

	inline vdfloat32x3 mul1(vdfloat32x3 a, const vdfloat32x3x4& b) {
		return a.x() * b.x
			+ a.y() * b.y
			+ a.z() * b.z
			+ b.w;
	}

	//////////////////////////////////////////////////////////////////////////

	struct vdint32x4 {
		sint32 v[4];

		static vdint32x4 set(sint32 x, sint32 y, sint32 z, sint32 w) {
			return vdint32x4 { { x, y, z, w } };
		}

		[[nodiscard]] bool operator==(const vdint32x4&) const = default;
	};

	//////////////////////////////////////////////////////////////////////////

	[[nodiscard]]
	inline bool all_bool(const vdmask32x3& a) {
		return (a.v[0] & a.v[1] & a.v[2]) >= UINT32_C(0x80000000);
	}

	[[nodiscard]]
	inline bool all_bool(const vdmask32x4& a) {
		return (a.v[0] & a.v[1] & a.v[2] & a.v[3]) >= UINT32_C(0x80000000);
	}

	[[nodiscard]]
	inline vdfloat32x4 loadu(const vdfloat4& v) {
		return vdfloat32x4 { v.x, v.y, v.z, v.w };
	}

	inline void storeu(void *dst, vdint32x4 v) {
		memcpy(dst, &v, 16);
	}

	[[nodiscard]]
	inline vdfloat32x3 select(vdmask32x3 mask, vdfloat32x3 x, vdfloat32x3 y) {
		return vdfloat32x3 {
			mask.v[0] ? x.v[0] : y.v[0],
			mask.v[1] ? x.v[1] : y.v[1],
			mask.v[2] ? x.v[2] : y.v[2]
		};
	}

	[[nodiscard]]
	inline vdfloat32x4 select(vdmask32x4 mask, vdfloat32x4 x, vdfloat32x4 y) {
		return vdfloat32x4 {
			mask.v[0] ? x.v[0] : y.v[0],
			mask.v[1] ? x.v[1] : y.v[1],
			mask.v[2] ? x.v[2] : y.v[2],
			mask.v[3] ? x.v[3] : y.v[3]
		};
	}

	[[nodiscard]]
	inline vdfloat32x4 nonzeromask(vdfloat32x4 x, vdmask32x4 mask) {
		return vdfloat32x4 {
			mask.v[0] ? x.v[0] : 0,
			mask.v[1] ? x.v[1] : 0,
			mask.v[2] ? x.v[2] : 0,
			mask.v[3] ? x.v[3] : 0,
		};
	}

	[[nodiscard]]
	inline vdfloat32x3 abs(vdfloat32x3 x) {
		return vdfloat32x3 { fabsf(x.v[0]), fabsf(x.v[1]), fabsf(x.v[2]) };
	}

	[[nodiscard]]
	inline vdfloat32x4 abs(vdfloat32x4 x) {
		return vdfloat32x4 { fabsf(x.v[0]), fabsf(x.v[1]), fabsf(x.v[2]), fabsf(x.v[3]) };
	}

	[[nodiscard]] inline float maxcomponent(vdfloat32x3 x) {
		return std::max<float>(x.v[0], std::max<float>(x.v[1], x.v[2]));
	}

	[[nodiscard]] inline float maxcomponent(vdfloat32x4 x) {
		return std::max<float>(x.v[0], std::max<float>(x.v[1], std::max<float>(x.v[2], x.v[3])));
	}

	[[nodiscard]]
	inline float dot(vdfloat32x3 x, vdfloat32x3 y) {
		return x.v[0]*y.v[0] + x.v[1]*y.v[1] + x.v[2]*y.v[2];
	}

	[[nodiscard]]
	inline float dot(vdfloat32x4 x, vdfloat32x4 y) {
		return x.v[0]*y.v[0] + x.v[1]*y.v[1] + x.v[2]*y.v[2] + x.v[3]*y.v[3];
	}

	[[nodiscard]]
	inline vdfloat32x3 sqrt(vdfloat32x3 x) {
		return vdfloat32x3 { sqrtf(x.v[0]), sqrtf(x.v[1]), sqrtf(x.v[2]) };
	}

	[[nodiscard]]
	inline vdfloat32x4 sqrt(vdfloat32x4 x) {
		return vdfloat32x4 { sqrtf(x.v[0]), sqrtf(x.v[1]), sqrtf(x.v[2]), sqrtf(x.v[3]) };
	}

	[[nodiscard]]
	inline vdfloat32x3 lerp(vdfloat32x3 x, vdfloat32x3 y, float f) {
		return x*(1.0f-f) + y*f;
	}

	[[nodiscard]]
	inline vdfloat32x3 lerp(vdfloat32x3 x, vdfloat32x3 y, vdfloat32x3 f) {
		return x*(vdfloat32x3::set1(1.0f) - f) + y*f;
	}

	[[nodiscard]]
	inline vdfloat32x4 rcp(vdfloat32x4 x) {
		return vdfloat32x4 { 1.0f/x.v[0], 1.0f/x.v[1], 1.0f/x.v[2], 1.0f/x.v[3] };
	}

	[[nodiscard]]
	inline vdfloat32x4 rcpest(vdfloat32x4 x) {
		return rcp(x);
	}

	[[nodiscard]]
	inline vdfloat32x4 rsqrt(vdfloat32x4 x) {
		return vdfloat32x4 { 1.0f/sqrtf(x.v[0]), 1.0f/sqrtf(x.v[1]), 1.0f/sqrtf(x.v[2]), 1.0f/sqrtf(x.v[3]) };
	}

	[[nodiscard]]
	inline vdfloat32x4 rsqrtest(vdfloat32x4 x) {
		return rsqrt(x);
	}

	template<unsigned xsel, unsigned ysel, unsigned zsel>
	[[nodiscard]]
	inline vdfloat32x3 permute(vdfloat32x3 x) {
		static_assert(xsel < 3 && ysel < 3 && zsel < 3);

		return vdfloat32x3 { x.v[xsel], x.v[ysel], x.v[zsel] };
	}

	template<unsigned xsel, unsigned ysel, unsigned zsel, unsigned wsel>
	[[nodiscard]]
	inline vdfloat32x4 permute(vdfloat32x4 x) {
		static_assert((xsel | ysel | zsel | wsel) < 4);

		return vdfloat32x4 { x.v[xsel], x.v[ysel], x.v[zsel], x.v[wsel] };
	}

	[[nodiscard]]
	inline vdfloat32x3 min(vdfloat32x3 x, vdfloat32x3 y) {
		return vdfloat32x3 {
			x.v[0] < y.v[0] ? x.v[0] : y.v[0],
			x.v[1] < y.v[1] ? x.v[1] : y.v[1],
			x.v[2] < y.v[2] ? x.v[2] : y.v[2],
			0,
		};
	}

	[[nodiscard]]
	inline vdfloat32x4 min(vdfloat32x4 x, vdfloat32x4 y) {
		return vdfloat32x4 {
			x.v[0] < y.v[0] ? x.v[0] : y.v[0],
			x.v[1] < y.v[1] ? x.v[1] : y.v[1],
			x.v[2] < y.v[2] ? x.v[2] : y.v[2],
			x.v[3] < y.v[3] ? x.v[3] : y.v[3],
		};
	}

	[[nodiscard]]
	inline vdfloat32x3 max0(vdfloat32x3 x) {
		return vdfloat32x3 {
			x.v[0] > 0.0f ? x.v[0] : 0.0f,
			x.v[1] > 0.0f ? x.v[1] : 0.0f,
			x.v[2] > 0.0f ? x.v[2] : 0.0f,
			0,
		};
	}

	[[nodiscard]]
	inline vdfloat32x3 max(vdfloat32x3 x, vdfloat32x3 y) {
		return vdfloat32x3 {
			x.v[0] > y.v[0] ? x.v[0] : y.v[0],
			x.v[1] > y.v[1] ? x.v[1] : y.v[1],
			x.v[2] > y.v[2] ? x.v[2] : y.v[2],
			0,
		};
	}

	[[nodiscard]]
	inline vdfloat32x4 max(vdfloat32x4 x, vdfloat32x4 y) {
		return vdfloat32x4 {
			x.v[0] > y.v[0] ? x.v[0] : y.v[0],
			x.v[1] > y.v[1] ? x.v[1] : y.v[1],
			x.v[2] > y.v[2] ? x.v[2] : y.v[2],
			x.v[3] > y.v[3] ? x.v[3] : y.v[3],
		};
	}

	[[nodiscard]]
	inline vdfloat32x3 clamp(vdfloat32x3 x, vdfloat32x3 mn, vdfloat32x3 mx) {
		return min(max(x, mn), mx);
	}

	[[nodiscard]]
	inline vdfloat32x4 clamp(vdfloat32x4 x, vdfloat32x4 mn, vdfloat32x4 mx) {
		return min(max(x, mn), mx);
	}

	[[nodiscard]]
	inline vdfloat32x3 saturate(vdfloat32x3 x) {
		return clamp(x, vdfloat32x3::set1(0), vdfloat32x3::set1(1));
	}

	[[nodiscard]]
	inline vdfloat32x4 saturate(vdfloat32x4 x) {
		return clamp(x, vdfloat32x4::set1(0), vdfloat32x4::set1(1));
	}

	[[nodiscard]]
	inline vdfloat32x3 pow(vdfloat32x3 x, float y) {
		return vdfloat32x3::set(
			powf(x.x(), y),
			powf(x.y(), y),
			powf(x.z(), y)
		);
	}

	[[nodiscard]]
	inline vdfloat32x4 pow(vdfloat32x4 x, float y) {
		return vdfloat32x4::set(
			powf(x.x(), y),
			powf(x.y(), y),
			powf(x.z(), y),
			powf(x.w(), y)
		);
	}

	[[nodiscard]]
	inline uint32 packus8(vdfloat32x3 x) {
		vdfloat32x3 y = clamp(x, vdfloat32x3::set1(0), vdfloat32x3::set1(255));

		return (uint32)(y.v[0] + 0.5f)
			+ ((uint32)(y.v[1] + 0.5f) <<  8)
			+ ((uint32)(y.v[2] + 0.5f) << 16);
	}

	[[nodiscard]]
	inline uint32 packus8(vdfloat32x4 x) {
		vdfloat32x4 y = clamp(x, vdfloat32x4::set1(0), vdfloat32x4::set1(255));

		return (uint32)(y.v[0] + 0.5f)
			+ ((uint32)(y.v[1] + 0.5f) <<  8)
			+ ((uint32)(y.v[2] + 0.5f) << 16)
			+ ((uint32)(y.v[3] + 0.5f) << 24);
	}

	[[nodiscard]]
	inline vdint32x4 ceilint(vdfloat32x4 v) {
		vdint32x4 iv;

		iv.v[0] = (sint32)v.v[0];
		iv.v[0] += (float)iv.v[0] < v.v[0] ? 1 : 0;
		iv.v[1] = (sint32)v.v[1];
		iv.v[1] += (float)iv.v[1] < v.v[1] ? 1 : 0;
		iv.v[2] = (sint32)v.v[2];
		iv.v[2] += (float)iv.v[2] < v.v[2] ? 1 : 0;
		iv.v[3] = (sint32)v.v[3];
		iv.v[3] += (float)iv.v[3] < v.v[3] ? 1 : 0;

		return iv;
	}
}

using nsVDVecMath::vdmask32x3;
using nsVDVecMath::vdmask32x4;
using nsVDVecMath::vdfloat32x3;
using nsVDVecMath::vdfloat32x4;
using nsVDVecMath::vdint32x4;

#endif
