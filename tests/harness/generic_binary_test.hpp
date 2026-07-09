#ifndef OPINE_TESTS_HARNESS_GENERIC_BINARY_TEST_HPP
#define OPINE_TESTS_HARNESS_GENERIC_BINARY_TEST_HPP

// GenericBinaryFpTest<T> — one runner, every width.
//
// Given a Type T, picks an input strategy from T::layout::total_bits
// and runs OpineAdapter vs MpfrAdapter for the requested op:
//
//   total_bits ≤ 8   → exhaustive pair sweep (65,536)
//   total_bits ≤ 16  → structural² + structural × stratified + random
//   total_bits ≥ 32  → structural² + structural × stratified + random
//
// The three tiers scale independently:
//
//   - Structural set (from structuralValues<T>): every ±0, ±1, min /
//     max normal, min / max subnormal, ±Inf per encoding, NaN per
//     encoding, ULP neighbors, every biased exponent, machine epsilon.
//   - ExponentStratifiedSingles<T, K>: K pseudo-random mantissas per
//     biased exponent value. Every binade gets touched at every width.
//   - RandomPairs: uniform sampling for residual coverage.
//
// Adding a new op: one case in OpineAdapter::dispatch + one call to
// GenericBinaryFpTest<T>::run(op, name). Adding a new Type: append
// to whatever list the test file iterates over.

#include <cstdio>
#include <vector>

#include <doctest/doctest.h>

#include "harness/impl_mpfr.hpp"
#include "harness/impl_opine.hpp"
#include "harness/test_harness.hpp"

namespace opine::testing {

// Rounding-parameterized variants of the predefined bundles, for
// encoding × rounding sweeps. The predefined aliases in type.hpp fix
// the Rounding axis to its default; these leave it open.
template <int E, int M, typename Rnd>
using IeeeR = Type<numbers::IEEE754<E, M>, layouts::IEEE<E, M, true>, Rnd>;
template <int E, int M, typename Rnd>
using RbjR =
    Type<numbers::RbjTwosComplement<E, M>, layouts::IEEE<E, M, true>, Rnd>;
template <typename Rnd>
using FnuzR = Type<numbers::E4M3FNUZ, layouts::IEEE<4, 3, true>, Rnd>;

template <typename T> struct GenericBinaryFpTest {
  using Storage = typename T::storage_type;
  static constexpr int TotalBits = T::layout::total_bits;
  static constexpr int HexWidth = (TotalBits + 3) / 4;

  // Sampling parameters scaled to the format width.
  //   K = stratified samples per biased exponent.
  //   MaxStratExp = cap on how many binades are enumerated.
  //   R = random pairs.
  // The ≥ 19 and ≥ 24 tiers keep binary256/1024 in fast-suite
  // territory: their binade counts (2^19, 2^27) can't be
  // enumerated, and binary1024's bit-serial division makes each
  // opine op itself expensive.
  static constexpr int StratK() {
    if constexpr (TotalBits <= 8) return 0; // exhaustive covers it
    if constexpr (TotalBits <= 16) return 8;
    // 15-bit exponents mean ~32k binades; keep struct × strat pair
    // counts in fast-suite territory.
    if constexpr (T::layout::exp_bits >= 15) return 1;
    return 4;
  }
  static constexpr int MaxStratExp(Op op) {
    if constexpr (T::layout::exp_bits >= 24) return 64;
    if constexpr (T::layout::exp_bits >= 19) return 512;
    // Division past 64-bit significands runs the bit-serial tier;
    // sample every 8th binade instead of all 32k so the
    // struct × strat crosses stay in fast-suite territory.
    if constexpr (T::layout::exp_bits >= 15)
      return op == Op::Div ? 4096 : (1 << 15);
    return 1 << 15;
  }
  static constexpr int RandomCount(Op op) {
    if constexpr (TotalBits <= 8) return 0;
    if constexpr (TotalBits <= 16) return 200000;
    if constexpr (T::layout::exp_bits >= 24) return 4000;
    if constexpr (T::layout::exp_bits >= 19) return 50000;
    // Division past 64-bit significands is the restoring bit-serial
    // tier — orders of magnitude slower per op than add — so its
    // random-pair budget shrinks to keep the fast suite fast; the
    // structural and stratified tiers are untouched.
    if constexpr (T::layout::exp_bits >= 15)
      return op == Op::Div ? 150000 : 1000000;
    return 1000000;
  }

  static void run(Op op) {
    const char *name = opName(op);

    OpineAdapter<T> opine;
    MpfrAdapter<T> mpfr;
    NanAwareBitExact<T> cmp;

    auto opineImpl = [&](Storage a, Storage b) {
      return opine.dispatch(op, a, b);
    };
    auto mpfrImpl = [&](Storage a, Storage b) {
      return mpfr.dispatch(op, a, b);
    };

    TestResult r;

    if constexpr (TotalBits <= 8) {
      ExhaustivePairs<Storage, TotalBits> iter;
      r = testAgainst<Storage>(name, HexWidth, iter, opineImpl, mpfrImpl, cmp);
    } else {
      // Materialize the structural set and the stratified list into
      // stable arrays for the pair iterators.
      auto structural = structuralValues<T>();

      std::vector<Storage> stratified;
      ExponentStratifiedSingles<T, StratK()> strat_gen{/*Seed=*/uint64_t(42),
                                                       MaxStratExp(op)};
      strat_gen([&](Storage x) { stratified.push_back(x); });

      TargetedPairs<Storage> struct_x_struct{structural.data(),
                                             int(structural.size())};
      TargetedSingles<Storage> struct_singles{structural.data(),
                                              int(structural.size())};
      TargetedSingles<Storage> strat_singles{stratified.data(),
                                             int(stratified.size())};
      auto struct_x_strat = crossPairs<T>(struct_singles, strat_singles);
      auto strat_x_struct = crossPairs<T>(strat_singles, struct_singles);
      RandomPairs<Storage, TotalBits> random_pairs{
          /*Seed=*/uint64_t(1234567), RandomCount(op)};

      auto iter = combined(struct_x_struct, struct_x_strat, strat_x_struct,
                           random_pairs);
      r = testAgainst<Storage>(name, HexWidth, iter, opineImpl, mpfrImpl, cmp);
    }

    CHECK(r.Failed == 0);
  }
};

} // namespace opine::testing

#endif // OPINE_TESTS_HARNESS_GENERIC_BINARY_TEST_HPP
