// Exception-flag verification (TDD step 12).
//
// ReturnStatus variants of the FP8 encodings run every ordered
// input pair through add/sub/mul/div, and the harness compares BOTH
// the result bits AND the IEEE 754 §7 flags against the MPFR
// oracle. The oracle computes its flags independently: invalid and
// divByZero from the operand-category grid, inexact by decoding the
// delivered bits and comparing values, overflow and underflow from
// precision-rounded thresholds (after-rounding tininess) — none of
// which shares code with the library's G/R/S-based flag path.
//
// A StatusFlags case checks the other delivery policy: sticky
// per-thread accumulation, cleared on demand.

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include "harness/impl_mpfr.hpp"
#include "harness/impl_opine.hpp"
#include "harness/test_harness.hpp"

using namespace opine;
using namespace opine::testing;

// ReturnStatus variants of the FP8 encodings (and directed-rounding
// IEEE variants, to exercise the §7.4 overflow Inf-vs-saturate
// interaction with flags).
template <int E, int M, typename Rnd = rounding::Default>
using IeeeS = Type<numbers::IEEE754<E, M>, layouts::IEEE<E, M, true>, Rnd,
                   exceptions::ReturnStatus>;
using FnuzS = Type<numbers::E4M3FNUZ, layouts::IEEE<4, 3, true>,
                   rounding::Default, exceptions::ReturnStatus>;
template <int E, int M>
using RbjS = Type<numbers::RbjTwosComplement<E, M>, layouts::IEEE<E, M, true>,
                  rounding::Default, exceptions::ReturnStatus>;
template <int E, int M>
using FastS = Type<numbers::Relaxed<E, M>, layouts::IEEE<E, M, true>,
                   rounding::TowardZero, exceptions::ReturnStatus>;

template <typename T> void verifyFlags() {
  using BitsType = typename T::storage_type;
  constexpr int TotalBits = T::layout::total_bits;
  static_assert(TotalBits == 8, "exhaustive pair sweep only feasible for FP8");
  constexpr int HexWidth = (TotalBits + 3) / 4;

  OpineAdapter<T> Opine;
  MpfrAdapter<T> Mpfr;
  ExhaustivePairs<BitsType, TotalBits> Iter;
  BitExactWithFlags<BitsType> Cmp;

  for (auto O : {Op::Add, Op::Sub, Op::Mul, Op::Div}) {
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

TEST_CASE_TEMPLATE("flags: OPINE vs MPFR (exhaustive FP8)", T, IeeeS<5, 2>,
                   IeeeS<4, 3>, FnuzS, RbjS<5, 2>, RbjS<4, 3>, FastS<5, 2>,
                   FastS<4, 3>, IeeeS<4, 3, rounding::TowardZero>,
                   IeeeS<4, 3, rounding::TowardPositive>,
                   IeeeS<4, 3, rounding::TowardNegative>) {
  verifyFlags<T>();
}

// -----------------------------------------------------------------
// StatusFlags policy: sticky thread-local accumulation
// -----------------------------------------------------------------
TEST_CASE("flags: StatusFlags policy accumulates and clears") {
  using T = Type<numbers::IEEE754<4, 3>, layouts::IEEE<4, 3, true>,
                 rounding::Default, exceptions::StatusFlags>;

  clearStatusFlags();
  auto one = fromNative<T>(1.0f);
  add<T>(one, one); // exact: 1 + 1 = 2
  CHECK(statusFlags() == FlagNone);

  auto maxf = opine::detail::packMaxFinite<T>(false);
  add<T>(maxf, maxf); // overflows
  CHECK((statusFlags() & FlagOverflow) != 0);
  CHECK((statusFlags() & FlagInexact) != 0);

  div<T>(one, fromNative<T>(0.0f)); // divides by zero
  CHECK((statusFlags() & FlagDivByZero) != 0);
  // Sticky: the earlier overflow is still recorded.
  CHECK((statusFlags() & FlagOverflow) != 0);

  clearStatusFlags();
  CHECK(statusFlags() == FlagNone);
}

// -----------------------------------------------------------------
// ReturnStatus policy: flags ride the return value
// -----------------------------------------------------------------
TEST_CASE("flags: ReturnStatus returns {bits, flags}") {
  using T = IeeeS<4, 3>;

  auto one = fromNative<T>(1.0f);
  CHECK(one.flags == FlagNone); // conversion of 1.0f is exact

  auto r = add<T>(one.bits, one.bits);
  CHECK(r.flags == FlagNone);
  CHECK(toFloat<Type<numbers::IEEE754<4, 3>, layouts::IEEE<4, 3, true>>>(
            r.bits) == 2.0f);

  auto third = div<T>(one.bits, fromNative<T>(3.0f).bits);
  CHECK((third.flags & FlagInexact) != 0);
  CHECK((third.flags & (FlagInvalid | FlagDivByZero | FlagOverflow)) == 0);

  auto boom = div<T>(one.bits, fromNative<T>(0.0f).bits);
  CHECK(boom.flags == FlagDivByZero);
}
