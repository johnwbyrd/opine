// ComputeFormat verification: arithmetic at reduced working
// precision, OPINE vs MPFR.
//
// WithComputePrecision<T, K> truncates every arithmetic operand to
// its top K significand bits before computing (positional
// truncation — subnormals truncate on the fixed absolute grid, and
// a value truncated to nothing participates as a signed zero). The
// oracle applies the same truncation to the decoded values through
// completely different code (mpfr_prec_round toward zero in the
// normal range, grid rounding below it), so agreement here pins
// the semantics from both sides.
//
// Coverage:
//   - exhaustive FP8 pairs for add/sub/mul/div at every K below
//     the format's precision, across IEEE / fnuz / rbj / Relaxed
//     encodings and the interesting rounding modes;
//   - exhaustive FP8 sqrt singles and sampled fma triples;
//   - the identity theorem: K >= P is bit-identical to the base
//     Type (compile-time no-op, proven exhaustively);
//   - flags under ReturnStatus (inexact means inexact relative to
//     the TRUNCATED operands — the oracle derives its flags from
//     the same truncated values independently);
//   - binary32 at K=8 — the "sloppy soft-float" configuration —
//     sampled structurally, per-binade, and randomly.

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include "harness/generic_binary_test.hpp"
#include "harness/generic_unary_test.hpp"

using namespace opine;
using namespace opine::testing;

// Reduced-precision variants. FP8 e4m3 has P=4, e5m2 has P=3.
template <int K> using IeeeK43 = WithComputePrecision<fp8_e4m3, K>;
template <int K> using IeeeK52 = WithComputePrecision<fp8_e5m2, K>;
template <int K> using FnuzK = WithComputePrecision<fp8_e4m3fnuz, K>;
template <int K> using RbjK = WithComputePrecision<RbjType<4, 3>, K>;
template <int K> using FastK = WithComputePrecision<FastType<4, 3>, K>;
template <int K>
using IeeeK43Z =
    WithComputePrecision<Type<numbers::IEEE754<4, 3>,
                              layouts::IEEE<4, 3, true>,
                              rounding::TowardZero>,
                         K>;

TEST_CASE_TEMPLATE("compute: reduced-K FP8 vs MPFR (exhaustive)", T,
                   IeeeK43<1>, IeeeK43<2>, IeeeK43<3>, IeeeK52<1>, IeeeK52<2>,
                   FnuzK<2>, FnuzK<3>, RbjK<2>, RbjK<3>, FastK<2>,
                   IeeeK43Z<2>, IeeeK43Z<3>) {
  GenericBinaryFpTest<T>::run(Op::Add);
  GenericBinaryFpTest<T>::run(Op::Sub);
  GenericBinaryFpTest<T>::run(Op::Mul);
  GenericBinaryFpTest<T>::run(Op::Div);
}

TEST_CASE_TEMPLATE("compute: reduced-K FP8 sqrt vs MPFR (exhaustive)", T,
                   IeeeK43<2>, IeeeK52<2>, FnuzK<2>, RbjK<2>) {
  GenericUnaryFpTest<T>::run(Op::Sqrt);
}

TEST_CASE_TEMPLATE("compute: reduced-K FP8 fma vs MPFR (sampled)", T,
                   IeeeK43<2>, IeeeK52<2>, FnuzK<2>) {
  using Storage = typename T::storage_type;
  OpineAdapter<T> Opine;
  MpfrAdapter<T> Mpfr;
  NanAwareBitExact<T> Cmp;

  auto structural = structuralValues<T>();
  TargetedTriples<Storage> Struct3{structural.data(), int(structural.size())};
  RandomTriples<Storage, 8> Rnd{/*Seed=*/uint64_t(24601), 100000};
  auto Iter = combined(Struct3, Rnd);

  auto ImplA = [&](Storage X, Storage Y, Storage Z) {
    return Opine.dispatchTernary(Op::MulAdd, X, Y, Z);
  };
  auto ImplB = [&](Storage X, Storage Y, Storage Z) {
    return Mpfr.dispatchTernary(Op::MulAdd, X, Y, Z);
  };
  auto R = testAgainstTernary<Storage>("fma", 2, Iter, ImplA, ImplB, Cmp);
  CHECK(R.Failed == 0);
}

// -----------------------------------------------------------------
// Identity theorem: K >= P leaves every operation bit-identical
// -----------------------------------------------------------------
TEST_CASE("compute: K = P is the base Type, exhaustively") {
  using T = fp8_e4m3;                       // P = 4
  using TK = WithComputePrecision<T, 4>;
  int Failed = 0;
  for (unsigned I = 0; I < 256; ++I) {
    for (unsigned J = 0; J < 256; ++J) {
      auto A = typename T::storage_type(I);
      auto B = typename T::storage_type(J);
      if (add<T>(A, B) != add<TK>(A, B) || mul<T>(A, B) != mul<TK>(A, B) ||
          div<T>(A, B) != div<TK>(A, B))
        ++Failed;
    }
    if (sqrt<T>(typename T::storage_type(I)) !=
        sqrt<TK>(typename T::storage_type(I)))
      ++Failed;
  }
  CHECK(Failed == 0);
}

// -----------------------------------------------------------------
// Flags at reduced K (ReturnStatus): both sides derive their flags
// from the truncated operands, independently
// -----------------------------------------------------------------
TEST_CASE("compute: reduced-K flags vs MPFR (exhaustive FP8)") {
  using T = WithComputePrecision<
      Type<numbers::IEEE754<4, 3>, layouts::IEEE<4, 3, true>,
           rounding::Default, exceptions::ReturnStatus>,
      2>;
  using BitsType = typename T::storage_type;

  OpineAdapter<T> Opine;
  MpfrAdapter<T> Mpfr;
  ExhaustivePairs<BitsType, 8> Iter;
  BitExactWithFlags<BitsType> Cmp;

  for (auto O : {Op::Add, Op::Mul, Op::Div}) {
    SUBCASE(opName(O)) {
      auto ImplA = [&](BitsType X, BitsType Y) {
        return Opine.dispatch(O, X, Y);
      };
      auto ImplB = [&](BitsType X, BitsType Y) {
        return Mpfr.dispatch(O, X, Y);
      };
      auto R = testAgainst<BitsType>(opName(O), 2, Iter, ImplA, ImplB, Cmp);
      CHECK(R.Failed == 0);
    }
  }
}

// -----------------------------------------------------------------
// The sloppy soft-float configuration: binary32 storage, 8-bit
// compute — sampled structurally, per-binade, and randomly
// -----------------------------------------------------------------
TEST_CASE_TEMPLATE("compute: binary32 at K=8 vs MPFR (sampled)", T,
                   WithComputePrecision<float32, 8>,
                   WithComputePrecision<
                       Type<numbers::IEEE754<8, 23>, layouts::IEEE<8, 23, true>,
                            rounding::TowardZero>,
                       8>) {
  GenericBinaryFpTest<T>::run(Op::Add);
  GenericBinaryFpTest<T>::run(Op::Mul);
  GenericBinaryFpTest<T>::run(Op::Div);
  GenericUnaryFpTest<T>::run(Op::Sqrt);
}
