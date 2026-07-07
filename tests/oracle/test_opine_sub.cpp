// Generic OPINE vs MPFR sweep for opine::sub across every Type
// currently in the codebase — from FP8 (exhaustive) through FP64
// (structural + stratified + random).
//
// sub is add(a, neg(b)); this sweep proves the composition against
// mpfr_sub (which the oracle computes directly, not by composition),
// including the §6.3 exact-zero-difference signs and the neg
// short-circuits for TrapValue and NegativeZeroBitPattern NaNs.

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include "harness/generic_binary_test.hpp"

using namespace opine;
using namespace opine::testing;

TEST_CASE_TEMPLATE("sub: OPINE vs MPFR", T,
                   // FP8 (exhaustive)
                   fp8_e5m2, fp8_e4m3, fp8_e4m3fnuz, RbjType<5, 2>,
                   RbjType<4, 3>, FastType<5, 2>, FastType<4, 3>,
                   // FP16 and up (structural + stratified + random)
                   bfloat16, float16, float32, float64, extFloat80,
                   float128) {
  GenericBinaryFpTest<T>::run(Op::Sub);
}

// Encoding × rounding sweep (exhaustive FP8). The default-rounding
// (ToNearestTiesToEven) combinations are covered above; FastType
// covers Relaxed × TowardZero.
TEST_CASE_TEMPLATE(
    "sub: OPINE vs MPFR, rounding sweep", T,
    IeeeR<5, 2, rounding::TowardZero>, IeeeR<5, 2, rounding::TowardPositive>,
    IeeeR<5, 2, rounding::TowardNegative>, IeeeR<4, 3, rounding::TowardZero>,
    IeeeR<4, 3, rounding::TowardPositive>,
    IeeeR<4, 3, rounding::TowardNegative>, FnuzR<rounding::TowardZero>,
    FnuzR<rounding::TowardPositive>, FnuzR<rounding::TowardNegative>,
    RbjR<5, 2, rounding::TowardZero>, RbjR<5, 2, rounding::TowardPositive>,
    RbjR<5, 2, rounding::TowardNegative>) {
  GenericBinaryFpTest<T>::run(Op::Sub);
}
