// Generic OPINE vs MPFR sweep for opine::mul across every Type
// currently in the codebase — from FP8 (exhaustive) through FP64
// (structural + stratified + random).
//
// Same type list as test_opine_add.cpp; the strategy per Type is
// chosen by GenericBinaryFpTest based on total_bits.
//
// extFloat80 exercises the explicit-J-bit path (its 64×64-bit
// product exactly fills the 128-bit working type). Not tested yet:
// float128 — the 226-bit exact product needs a multi-word scheme
// (static_assert'd in mul.hpp). Follow-up work.

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
                   bfloat16, float16, float32, float64, extFloat80) {
  GenericBinaryFpTest<T>::run(Op::Mul);
}

// Encoding × rounding sweep (exhaustive FP8). The default-rounding
// (ToNearestTiesToEven) combinations are covered above; FastType
// covers Relaxed × TowardZero.
TEST_CASE_TEMPLATE(
    "mul: OPINE vs MPFR, rounding sweep", T,
    IeeeR<5, 2, rounding::TowardZero>, IeeeR<5, 2, rounding::TowardPositive>,
    IeeeR<5, 2, rounding::TowardNegative>, IeeeR<4, 3, rounding::TowardZero>,
    IeeeR<4, 3, rounding::TowardPositive>,
    IeeeR<4, 3, rounding::TowardNegative>, FnuzR<rounding::TowardZero>,
    FnuzR<rounding::TowardPositive>, FnuzR<rounding::TowardNegative>,
    RbjR<5, 2, rounding::TowardZero>, RbjR<5, 2, rounding::TowardPositive>,
    RbjR<5, 2, rounding::TowardNegative>) {
  GenericBinaryFpTest<T>::run(Op::Mul);
}
