// Direct test of mpfrRoundToFormat's rounding-mode dispatch.
//
// The MPFR adapter reads the Type's Rounding policy and asks
// mpfr_rint to round with the corresponding MPFR mode, flipping
// direction for the abs value when the signed value is negative.
// This file verifies each of the four supported modes against
// hand-worked-out expected bit patterns.
//
// Test values are chosen so different modes give distinct results:
//
//   FP32 significand precision = 24 bits, ulp of 1.0 = 2^-23.
//
//   +v_tie = 1.0 + 2^-24                   (exactly halfway 1.0 → 1.0+ulp)
//     RNDN (ties to even): 1.0 has mant=0 (even) → 1.0
//     RNDZ: floor → 1.0
//     RNDU: ceiling → 1.0+ulp
//     RNDD: floor → 1.0
//
//   -v_tie = -(1.0 + 2^-24)                (halfway on the negative side)
//     RNDN: -1.0 (ties to even, magnitude-wise same)
//     RNDZ: toward zero → -1.0
//     RNDU: toward +Inf → -1.0
//     RNDD: toward -Inf → -1.0-ulp
//
//   +v_up = 1.0 + 3 * 2^-25                (3/4 of the way to 1.0+ulp)
//     RNDN: nearest is 1.0+ulp
//     RNDZ: floor → 1.0
//     RNDU: ceiling → 1.0+ulp
//     RNDD: floor → 1.0
//
//   -v_up = -(1.0 + 3 * 2^-25)             (3/4 of the way on the negative side)
//     RNDN: nearest → -1.0-ulp
//     RNDZ: toward zero → -1.0
//     RNDU: toward +Inf → -1.0
//     RNDD: toward -Inf → -1.0-ulp

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <cstdint>
#include <doctest/doctest.h>

#include "harness/impl_mpfr.hpp"

using namespace opine;
using namespace opine::testing;

// Bit patterns for reference values.
static constexpr uint32_t Fp32_Pos_One = 0x3F800000;  // +1.0
static constexpr uint32_t Fp32_Pos_OnePlus = 0x3F800001;  // 1.0 + ulp
static constexpr uint32_t Fp32_Neg_One = 0xBF800000;  // -1.0
static constexpr uint32_t Fp32_Neg_OnePlus = 0xBF800001;  // -(1.0 + ulp)

// Type aliases with each supported Rounding policy.
using Fp32_RNDN = Type<numbers::IEEE754<8, 23>, layouts::IEEE<8, 23>,
                       rounding::ToNearestTiesToEven>;
using Fp32_RNDZ = Type<numbers::IEEE754<8, 23>, layouts::IEEE<8, 23>,
                       rounding::TowardZero>;
using Fp32_RNDU = Type<numbers::IEEE754<8, 23>, layouts::IEEE<8, 23>,
                       rounding::TowardPositive>;
using Fp32_RNDD = Type<numbers::IEEE754<8, 23>, layouts::IEEE<8, 23>,
                       rounding::TowardNegative>;

// Build an MPFR value from a rational: numerator * 2^exp.
static MpfrFloat mkVal(long numerator, mpfr_exp_t exp) {
  MpfrFloat v;
  mpfr_set_si_2exp(v.get(), numerator, exp, MPFR_RNDN);
  return v;
}

TEST_CASE("rounding: positive halfway 1.0 + 2^-24") {
  // 1 + 2^-24 = (2^24 + 1) * 2^-24
  MpfrFloat v = mkVal((1L << 24) + 1, -24);

  CHECK(uint32_t(mpfrRoundToFormat<Fp32_RNDN>(v)) == Fp32_Pos_One);
  CHECK(uint32_t(mpfrRoundToFormat<Fp32_RNDZ>(v)) == Fp32_Pos_One);
  CHECK(uint32_t(mpfrRoundToFormat<Fp32_RNDU>(v)) == Fp32_Pos_OnePlus);
  CHECK(uint32_t(mpfrRoundToFormat<Fp32_RNDD>(v)) == Fp32_Pos_One);
}

TEST_CASE("rounding: negative halfway -(1.0 + 2^-24)") {
  MpfrFloat v = mkVal(-((1L << 24) + 1), -24);

  CHECK(uint32_t(mpfrRoundToFormat<Fp32_RNDN>(v)) == Fp32_Neg_One);
  CHECK(uint32_t(mpfrRoundToFormat<Fp32_RNDZ>(v)) == Fp32_Neg_One);
  CHECK(uint32_t(mpfrRoundToFormat<Fp32_RNDU>(v)) == Fp32_Neg_One);
  CHECK(uint32_t(mpfrRoundToFormat<Fp32_RNDD>(v)) == Fp32_Neg_OnePlus);
}

TEST_CASE("rounding: positive 3/4 of the way 1.0 + 3 * 2^-25") {
  // 1 + 3 * 2^-25 = (2^25 + 3) * 2^-25
  MpfrFloat v = mkVal((1L << 25) + 3, -25);

  CHECK(uint32_t(mpfrRoundToFormat<Fp32_RNDN>(v)) == Fp32_Pos_OnePlus);
  CHECK(uint32_t(mpfrRoundToFormat<Fp32_RNDZ>(v)) == Fp32_Pos_One);
  CHECK(uint32_t(mpfrRoundToFormat<Fp32_RNDU>(v)) == Fp32_Pos_OnePlus);
  CHECK(uint32_t(mpfrRoundToFormat<Fp32_RNDD>(v)) == Fp32_Pos_One);
}

TEST_CASE("rounding: negative 3/4 of the way -(1.0 + 3 * 2^-25)") {
  MpfrFloat v = mkVal(-((1L << 25) + 3), -25);

  CHECK(uint32_t(mpfrRoundToFormat<Fp32_RNDN>(v)) == Fp32_Neg_OnePlus);
  CHECK(uint32_t(mpfrRoundToFormat<Fp32_RNDZ>(v)) == Fp32_Neg_One);
  CHECK(uint32_t(mpfrRoundToFormat<Fp32_RNDU>(v)) == Fp32_Neg_One);
  CHECK(uint32_t(mpfrRoundToFormat<Fp32_RNDD>(v)) == Fp32_Neg_OnePlus);
}
