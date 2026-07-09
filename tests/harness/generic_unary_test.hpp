#ifndef OPINE_TESTS_HARNESS_GENERIC_UNARY_TEST_HPP
#define OPINE_TESTS_HARNESS_GENERIC_UNARY_TEST_HPP

// Width-scaled OPINE-vs-MPFR sweep for unary rounding operations
// (sqrt today). The singles analog of GenericBinaryFpTest, reusing
// its width tiers:
//
//   total_bits ≤ 8  → exhaustive singles
//   otherwise       → structural + stratified (every binade the
//                     tier allows) + uniform random singles
//
// Singles are far cheaper than pairs, so the random budget reuses
// the binary tier unchanged.

#include <vector>

#include <doctest/doctest.h>

#include "harness/generic_binary_test.hpp"

namespace opine::testing {

template <typename T> struct GenericUnaryFpTest {
  using Storage = typename T::storage_type;
  static constexpr int TotalBits = T::layout::total_bits;
  static constexpr int HexWidth = (TotalBits + 3) / 4;
  using Tiers = GenericBinaryFpTest<T>;

  static void run(Op op) {
    const char *name = opName(op);

    OpineAdapter<T> opine;
    MpfrAdapter<T> mpfr;
    NanAwareBitExact<T> cmp;

    auto opineImpl = [&](Storage a) { return opine.dispatchUnary(op, a); };
    auto mpfrImpl = [&](Storage a) { return mpfr.dispatchUnary(op, a); };

    TestResult r;

    if constexpr (TotalBits <= 8) {
      ExhaustiveSingles<Storage, TotalBits> iter;
      r = testAgainstUnary<Storage>(name, HexWidth, iter, opineImpl, mpfrImpl,
                                    cmp);
    } else {
      auto structural = structuralValues<T>();

      std::vector<Storage> stratified;
      ExponentStratifiedSingles<T, Tiers::StratK()> strat_gen{
          /*Seed=*/uint64_t(42), Tiers::MaxStratExp(op)};
      strat_gen([&](Storage x) { stratified.push_back(x); });

      TargetedSingles<Storage> struct_singles{structural.data(),
                                              int(structural.size())};
      TargetedSingles<Storage> strat_singles{stratified.data(),
                                             int(stratified.size())};
      RandomSingles<Storage, TotalBits> random_singles{
          /*Seed=*/uint64_t(1234567), Tiers::RandomCount(op)};

      auto iter = combined(struct_singles, strat_singles, random_singles);
      r = testAgainstUnary<Storage>(name, HexWidth, iter, opineImpl, mpfrImpl,
                                    cmp);
    }

    CHECK(r.Failed == 0);
  }
};

} // namespace opine::testing

#endif // OPINE_TESTS_HARNESS_GENERIC_UNARY_TEST_HPP
