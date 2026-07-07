// Exhaustive OPINE vs MPFR for opine::add on every FP8 Type.
//
// Starts by failing everywhere (the initial add.hpp is a stub that
// returns 0). As the implementation lands, the failure count
// should shrink, ultimately to zero for all seven Types.

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include "harness/impl_mpfr.hpp"
#include "harness/impl_opine.hpp"
#include "harness/test_harness.hpp"

using namespace opine;
using namespace opine::testing;

template <typename T> void verifyAdd() {
  using BitsType = typename T::storage_type;
  constexpr int TotalBits = T::layout::total_bits;
  static_assert(TotalBits == 8, "exhaustive pair sweep only feasible for FP8");
  constexpr int HexWidth = (TotalBits + 3) / 4;

  OpineAdapter<T> Opine;
  MpfrAdapter<T> Mpfr;
  ExhaustivePairs<BitsType, TotalBits> Iter;
  NanAwareBitExact<T> Cmp;

  auto ImplA = [&](BitsType X, BitsType Y) {
    return Opine.dispatch(Op::Add, X, Y);
  };
  auto ImplB = [&](BitsType X, BitsType Y) {
    return Mpfr.dispatch(Op::Add, X, Y);
  };
  auto R = testAgainst<BitsType>("add", HexWidth, Iter, ImplA, ImplB, Cmp);
  CHECK(R.Failed == 0);
}

TEST_CASE_TEMPLATE("add: OPINE vs MPFR (exhaustive FP8)", T, fp8_e5m2,
                   fp8_e4m3, fp8_e4m3fnuz, RbjType<5, 2>, RbjType<4, 3>,
                   FastType<5, 2>, FastType<4, 3>) {
  verifyAdd<T>();
}
