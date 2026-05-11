//	Altirra - Atari 800/800XL/5200 emulator
//	Copyright (C) 2026 Avery Lee
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

#include <stdafx.h>
#include <at/attest/test.h>
#include <vd2/system/vdstl_any.h>

AT_DEFINE_TEST(System_Any) {
	VDAny any;

	AT_TEST_ASSERT(!any.HasValue());
	AT_TEST_ASSERT(!any.HasType<int>());
	AT_TEST_ASSERT(any.ValueOrDefault<int>() == 0);

	any = 1;
	AT_TEST_ASSERT(any.HasValue());
	AT_TEST_ASSERT(any.HasType<int>());
	AT_TEST_ASSERT(!any.HasType<double>());
	AT_TEST_ASSERT(any.Value<int>() == 1);
	AT_TEST_ASSERT(*any.TryGetValue<int>() == 1);
	AT_TEST_ASSERT(any.TryGetValue<double>() == nullptr);
	AT_TEST_ASSERT(*const_cast<const VDAny&>(any).TryGetValue<int>() == 1);
	AT_TEST_ASSERT(const_cast<const VDAny&>(any).TryGetValue<double>() == nullptr);
	AT_TEST_ASSERT(any.ValueOrDefault<int>() == 1);

	struct Foo {
		int v[4] = { 0, 1, 2, 3 };

		bool operator==(const Foo&) const = default;
	};

	VDAny any2 { Foo() };
	AT_TEST_ASSERT(any2.HasValue());
	AT_TEST_ASSERT(any2.HasType<Foo>());
	AT_TEST_ASSERT(!any2.HasType<double>());
	AT_TEST_ASSERT(any2.Value<Foo>() == Foo());
	AT_TEST_ASSERT(*any2.TryGetValue<Foo>() == Foo());

	any = any2;
	any2.Value<Foo>().v[0] = 4;

	AT_TEST_ASSERT(any.HasType<Foo>());
	AT_TEST_ASSERT(any2.HasType<Foo>());
	AT_TEST_ASSERT(any.Value<Foo>().v[0] == 0);
	AT_TEST_ASSERT(any2.Value<Foo>().v[0] == 4);

	any = std::move(any2);

	AT_TEST_ASSERT(any.HasType<Foo>());
	AT_TEST_ASSERT(any.Value<Foo>().v[0] == 4);
	AT_TEST_ASSERT(!any2.HasValue());

	VDAny any3(std::move(any));
	AT_TEST_ASSERT(any3.HasType<Foo>());
	AT_TEST_ASSERT(any3.Value<Foo>().v[0] == 4);
	AT_TEST_ASSERT(!any.HasValue());

	struct ThrowingMoveData {
		int v = 0;

		ThrowingMoveData() = default;
		ThrowingMoveData(const ThrowingMoveData& src) noexcept(false)
			: v(src.v)
		{}

		ThrowingMoveData& operator=(const ThrowingMoveData& src) noexcept(false) {
			v = src.v;
			return *this;
		}

		bool operator==(const ThrowingMoveData&) const = default;
	};

	any = ThrowingMoveData();
	AT_TEST_ASSERT(any.HasValue());
	AT_TEST_ASSERT(any.HasType<ThrowingMoveData>());
	AT_TEST_ASSERT(any.Value<ThrowingMoveData>() == ThrowingMoveData());

	any2 = any;
	AT_TEST_ASSERT(any2.HasValue());
	AT_TEST_ASSERT(any2.HasType<ThrowingMoveData>());
	AT_TEST_ASSERT(any2.Value<ThrowingMoveData>() == ThrowingMoveData());

	using PairIntInt = std::pair<int, int>;

	any3.Emplace<PairIntInt>(10, 20);
	AT_TEST_ASSERT(any3.HasType<PairIntInt>());
	AT_TEST_ASSERT(any3.Value<PairIntInt>().first == 10);
	AT_TEST_ASSERT(any3.Value<PairIntInt>().second == 20);

	return 0;
}
