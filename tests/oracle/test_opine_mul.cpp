// Generic OPINE vs MPFR sweep for opine::mul across every Type
// currently in the codebase — from FP8 (exhaustive) through FP64
// (structural + stratified + random).
//
// Same type list as test_opine_add.cpp; the strategy per Type is
// chosen by GenericBinaryFpTest based on total_bits.
//
// Not tested yet: extFloat80 (explicit-J-bit path in opine::mul
// hasn't been validated) and float128 (the working type tops out
// at 128 bits, short of the 226-bit exact product). Both are
// follow-up work, tracked alongside the same gaps in opine::add.

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include "harness/generic_binary_test.hpp"

using namespace opine;
using namespace opine::testing;

TEST_CASE_TEMPLATE("mul: OPINE vs MPFR", T,
                   // FP8 (exhaustive)
                   fp8_e5m2, fp8_e4m3, fp8_e4m3fnuz, RbjType<5, 2>,
                   RbjType<4, 3>, FastType<5, 2>, FastType<4, 3>,
                   // FP16 and up (structural + stratified + random)
                   bfloat16, float16, float32, float64) {
  GenericBinaryFpTest<T>::run(Op::Mul);
}
