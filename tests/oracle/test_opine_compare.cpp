// Exhaustive OPINE-vs-MPFR comparison tests.
//
// For every FP8 Type (IEEE + non-IEEE), verify that opine::eq /
// opine::lt / opine::le agree with the MPFR oracle for all 65,536
// input pairs. The MPFR side calls decodeToMpfr on both operands
// and uses mpfr_equal_p / mpfr_less_p / mpfr_lessequal_p, all of
// which return false for NaN inputs — matching IEEE 754 quiet
// comparison semantics.
//
// This is the first cross-check between an OPINE library
// implementation and the MPFR oracle. Adding new arithmetic ops
// later is a matter of extending OpineAdapter::dispatch and
// adding one more TEST_CASE_TEMPLATE line.

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include "harness/impl_mpfr.hpp"
#include "harness/impl_opine.hpp"
#include "harness/test_harness.hpp"

using namespace opine;
using namespace opine::testing;

// -----------------------------------------------------------------
// verifyCompare — exhaustive FP8 pair sweep, three predicates
// -----------------------------------------------------------------
template <typename T> void verifyCompare() {
  using BitsType = typename T::storage_type;
  constexpr int TotalBits = T::layout::total_bits;
  static_assert(TotalBits == 8, "exhaustive pair sweep only feasible for FP8");
  constexpr int HexWidth = (TotalBits + 3) / 4;

  OpineAdapter<T> Opine;
  MpfrAdapter<T> Mpfr;
  ExhaustivePairs<BitsType, TotalBits> Iter;
  BitExactIgnoreFlags<BitsType> Cmp;

  for (auto O : {Op::Eq, Op::Lt, Op::Le}) {
    SUBCASE(opName(O)) {
      auto ImplA = [&](BitsType X, BitsType Y) {
        return Opine.dispatch(O, X, Y);
      };
      auto ImplB = [&](BitsType X, BitsType Y) {
        return Mpfr.dispatch(O, X, Y);
      };
      auto R =
          testAgainst<BitsType>(opName(O), HexWidth, Iter, ImplA, ImplB, Cmp);
      CHECK(R.Failed == 0);
    }
  }
}

TEST_CASE_TEMPLATE("compare: OPINE vs MPFR (exhaustive FP8)", T, fp8_e5m2,
                   fp8_e4m3, fp8_e4m3fnuz, RbjType<5, 2>, RbjType<4, 3>,
                   FastType<5, 2>, FastType<4, 3>) {
  verifyCompare<T>();
}
