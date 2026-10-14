#pragma once
#include <at/attest/test.h>
#ifdef _MSC_VER
#include <intrin.h>
#else
#define __debugbreak() __builtin_trap()
#endif
#define DEFINE_TEST AT_DEFINE_TEST
#define DEFINE_TEST_NONAUTO AT_DEFINE_TEST_NONAUTO
