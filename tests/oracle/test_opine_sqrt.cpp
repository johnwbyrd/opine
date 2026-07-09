// Generic OPINE vs MPFR sweep for opine::sqrt across every Type in
// the codebase — FP8 exhaustive, FP16 and up structural +
// stratified + random, wide formats sampled. The oracle side is
// mpfr_sqrt at working precision followed by mpfrRoundToFormat;
// like division, square root has an exclusion zone around
// representable values, so the compute-then-round chain at ≥ 2p+2
// bits is double-rounding-safe in every mode.
//
// A ReturnStatus case spot-checks the §7 flags sqrt can raise:
// invalid on negative operands, inexact on irrational roots, and
// nothing at all on exact ones (sqrt cannot overflow or underflow).

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include "harness/generic_unary_test.hpp"

using namespace opine;
using namespace opine::testing;

TEST_CASE_TEMPLATE("sqrt: OPINE vs MPFR", T,
                   // FP8 (exhaustive)
                   fp8_e5m2, fp8_e4m3, fp8_e4m3fnuz, RbjType<5, 2>,
                   RbjType<4, 3>, FastType<5, 2>, FastType<4, 3>,
                   // FP16 and up (structural + stratified + random)
                   bfloat16, float16, float32, float64, extFloat80,
                   float128) {
  GenericUnaryFpTest<T>::run(Op::Sqrt);
}

TEST_CASE_TEMPLATE("sqrt: OPINE vs MPFR (binary256/1024)", T, float256,
                   float1024) {
  GenericUnaryFpTest<T>::run(Op::Sqrt);
}

// Encoding × rounding sweep (exhaustive FP8), including the modes
// with no direct MPFR analog.
TEST_CASE_TEMPLATE(
    "sqrt: OPINE vs MPFR, rounding sweep", T,
    IeeeR<5, 2, rounding::TowardZero>, IeeeR<5, 2, rounding::TowardPositive>,
    IeeeR<5, 2, rounding::TowardNegative>, IeeeR<4, 3, rounding::TowardZero>,
    IeeeR<4, 3, rounding::TowardPositive>,
    IeeeR<4, 3, rounding::TowardNegative>, FnuzR<rounding::TowardZero>,
    FnuzR<rounding::TowardPositive>, FnuzR<rounding::TowardNegative>,
    RbjR<5, 2, rounding::TowardZero>, RbjR<5, 2, rounding::TowardPositive>,
    RbjR<5, 2, rounding::TowardNegative>,
    IeeeR<5, 2, rounding::ToNearestTiesAway>,
    IeeeR<4, 3, rounding::ToNearestTiesAway>,
    FnuzR<rounding::ToNearestTiesAway>,
    RbjR<5, 2, rounding::ToNearestTiesAway>, IeeeR<5, 2, rounding::ToOdd>,
    IeeeR<4, 3, rounding::ToOdd>, FnuzR<rounding::ToOdd>,
    RbjR<5, 2, rounding::ToOdd>) {
  GenericUnaryFpTest<T>::run(Op::Sqrt);
}

// -----------------------------------------------------------------
// §7 flags through the ReturnStatus policy
// -----------------------------------------------------------------
TEST_CASE("sqrt: flags (ReturnStatus, binary32)") {
  using T = Type<numbers::IEEE754<8, 23>, layouts::IEEE<8, 23, true>,
                 rounding::Default, exceptions::ReturnStatus>;

  auto one = fromNative<T>(1.0f);
  auto four = fromNative<T>(4.0f);
  auto two = fromNative<T>(2.0f);
  auto negone = fromNative<T>(-1.0f);

  // Exact roots: no flags.
  auto r4 = opine::sqrt<T>(four.bits);
  CHECK(r4.bits == two.bits);
  CHECK(r4.flags == FlagNone);
  CHECK(opine::sqrt<T>(one.bits).flags == FlagNone);

  // Irrational root: inexact only.
  auto r2 = opine::sqrt<T>(two.bits);
  CHECK(r2.flags == FlagInexact);
  CHECK(r2.bits == fromNative<T>(1.41421356237f).bits);

  // Negative operand: invalid, canonical NaN.
  auto rn = opine::sqrt<T>(negone.bits);
  CHECK(rn.flags == FlagInvalid);
  CHECK(isNan<T>(rn.bits));

  // sqrt(-Inf) is invalid; sqrt(+Inf) is +Inf, quiet.
  auto pinf = opine::detail::packSpecial<T>(ValueCategory::Infinity, false);
  auto ninf = opine::detail::packSpecial<T>(ValueCategory::Infinity, true);
  CHECK(opine::sqrt<T>(ninf).flags == FlagInvalid);
  CHECK(opine::sqrt<T>(pinf).flags == FlagNone);
  CHECK(opine::sqrt<T>(pinf).bits == pinf);

  // §5.4.1: sqrt(-0) = -0, and it is NOT invalid.
  auto nzero = opine::neg<T>(opine::detail::packSpecial<T>(ValueCategory::Zero,
                                                           false));
  auto rz = opine::sqrt<T>(nzero);
  CHECK(rz.bits == nzero);
  CHECK(rz.flags == FlagNone);

  // A quiet NaN in is a quiet NaN out, no invalid.
  auto nan = opine::detail::packSpecial<T>(ValueCategory::NaN, false);
  CHECK(opine::sqrt<T>(nan).flags == FlagNone);
}
