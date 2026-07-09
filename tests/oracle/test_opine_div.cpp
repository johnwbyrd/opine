// Generic OPINE vs MPFR sweep for opine::div across every Type
// currently in the codebase — from FP8 (exhaustive) through
// float128 (structural + stratified + random).
//
// extFloat80's 131-bit and float128's 229-bit numerators run in
// the multi-limb digit geometry (digits.hpp) — there is no width
// ceiling. Division is the restoring bit-serial tier: slow and
// obviously correct.

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include "harness/generic_binary_test.hpp"
#include "harness/wide_formats.hpp"

using namespace opine;
using namespace opine::testing;

TEST_CASE_TEMPLATE("div: OPINE vs MPFR", T,
                   // FP8 (exhaustive)
                   fp8_e5m2, fp8_e4m3, fp8_e4m3fnuz, RbjType<5, 2>,
                   RbjType<4, 3>, FastType<5, 2>, FastType<4, 3>,
                   // FP16 and up (structural + stratified + random)
                   bfloat16, float16, float32, float64, extFloat80,
                   float128) {
  GenericBinaryFpTest<T>::run(Op::Div);
}

#if OPINE_TEST_HAS_WIDE_STORAGE
TEST_CASE_TEMPLATE("div: OPINE vs MPFR (binary256/1024)", T, float256,
                   float1024) {
  GenericBinaryFpTest<T>::run(Op::Div);
}
#endif

// Encoding × rounding sweep (exhaustive FP8). The default-rounding
// (ToNearestTiesToEven) combinations are covered above; FastType
// covers Relaxed × TowardZero.
TEST_CASE_TEMPLATE(
    "div: OPINE vs MPFR, rounding sweep", T,
    IeeeR<5, 2, rounding::TowardZero>, IeeeR<5, 2, rounding::TowardPositive>,
    IeeeR<5, 2, rounding::TowardNegative>, IeeeR<4, 3, rounding::TowardZero>,
    IeeeR<4, 3, rounding::TowardPositive>,
    IeeeR<4, 3, rounding::TowardNegative>, FnuzR<rounding::TowardZero>,
    FnuzR<rounding::TowardPositive>, FnuzR<rounding::TowardNegative>,
    RbjR<5, 2, rounding::TowardZero>, RbjR<5, 2, rounding::TowardPositive>,
    RbjR<5, 2, rounding::TowardNegative>) {
  GenericBinaryFpTest<T>::run(Op::Div);
}
