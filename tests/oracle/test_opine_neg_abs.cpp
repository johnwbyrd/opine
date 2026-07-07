// Exhaustive OPINE-vs-MPFR tests for the non-computational sign
// operations neg and abs.
//
// For every FP8 Type (IEEE + non-IEEE), verify that opine::neg /
// opine::abs agree bit-exactly with the MPFR adapter's Neg / Abs,
// which the harness treats as raw sign transforms (short-circuiting
// TrapValue and NegativeZeroBitPattern NaN patterns and applying
// the encoding-appropriate whole-word transform for rbj).

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include "harness/impl_mpfr.hpp"
#include "harness/impl_opine.hpp"
#include "harness/test_harness.hpp"

using namespace opine;
using namespace opine::testing;

template <typename T> void verifyNegAbs() {
  using BitsType = typename T::storage_type;
  constexpr int TotalBits = T::layout::total_bits;
  static_assert(TotalBits == 8, "exhaustive sweep only feasible for FP8");
  constexpr int HexWidth = (TotalBits + 3) / 4;

  OpineAdapter<T> Opine;
  MpfrAdapter<T> Mpfr;
  ExhaustiveSingles<BitsType, TotalBits> Iter;
  BitExactIgnoreFlags<BitsType> Cmp;

  for (auto O : {Op::Neg, Op::Abs}) {
    SUBCASE(opName(O)) {
      auto ImplA = [&](BitsType X) { return Opine.dispatchUnary(O, X); };
      auto ImplB = [&](BitsType X) { return Mpfr.dispatchUnary(O, X); };
      auto R = testAgainstUnary<BitsType>(opName(O), HexWidth, Iter, ImplA,
                                          ImplB, Cmp);
      CHECK(R.Failed == 0);
    }
  }
}

TEST_CASE_TEMPLATE("neg/abs: OPINE vs MPFR (exhaustive FP8)", T, fp8_e5m2,
                   fp8_e4m3, fp8_e4m3fnuz, RbjType<5, 2>, RbjType<4, 3>,
                   FastType<5, 2>, FastType<4, 3>) {
  verifyNegAbs<T>();
}
