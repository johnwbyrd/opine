// Generic OPINE vs MPFR sweep for opine::add across every Type
// currently in the codebase — from FP8 (exhaustive) through FP64
// (structural + stratified + random).
//
// This file replaces the previous per-FP8 exhaustive add test.
// The strategy per Type is chosen by GenericBinaryFpTest based on
// total_bits; adding a Type is one line in ALL_TYPES below.
//
// Not tested yet: extFloat80 (explicit-J-bit path in opine::add
// hasn't been validated) and float128 (Wide=uint64 is too narrow
// for the 113-bit significand). Both are follow-up work.

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include "harness/generic_binary_test.hpp"

using namespace opine;
using namespace opine::testing;

TEST_CASE_TEMPLATE("add: OPINE vs MPFR", T,
                   // FP8 (exhaustive)
                   fp8_e5m2, fp8_e4m3, fp8_e4m3fnuz, RbjType<5, 2>,
                   RbjType<4, 3>, FastType<5, 2>, FastType<4, 3>,
                   // FP16 and up (structural + stratified + random)
                   bfloat16, float16, float32, float64) {
  GenericBinaryFpTest<T>::run(Op::Add);
}
