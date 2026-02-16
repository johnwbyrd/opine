#ifndef OPINE_TESTS_HARNESS_TEST_HARNESS_HPP
#define OPINE_TESTS_HARNESS_TEST_HARNESS_HPP

// Generic "this against that" test harness (from tdd.md).
//
// testAgainst(Name, HexWidth, Iter, ImplA, ImplB, Cmp)
//   runs ImplA and ImplB on every input pair yielded by Iter,
//   compares outputs using Cmp, and prints results.
//
// Both ImplA and ImplB are opaque callables:
//   (uint64_t, uint64_t) -> TestOutput
// The harness knows nothing about what library backs them.

#include <array>
#include <cstdint>
#include <cstdio>
#include <random>
#include <tuple>

#include "opine/opine.hpp"

namespace opine::testing {

// ===================================================================
// Core types
// ===================================================================

struct TestOutput {
  uint64_t Bits;
  uint8_t Flags; // 0 if implementation doesn't report flags
};

struct Failure {
  uint64_t InputA;
  uint64_t InputB;
  TestOutput OutputA;
  TestOutput OutputB;
};

struct TestResult {
  int Total = 0;
  int Passed = 0;
  int Failed = 0;
};

// ===================================================================
// testAgainst — the harness
// ===================================================================

static constexpr int MaxReportedFailures = 10;

template <typename IterFn, typename ImplA, typename ImplB,
          typename Comparator>
TestResult testAgainst(const char *Name, int HexWidth, IterFn Iter, ImplA A,
                       ImplB B, Comparator Cmp) {
  TestResult R;
  Failure Failures[MaxReportedFailures];
  int NumReported = 0;

  Iter([&](uint64_t ABits, uint64_t BBits) {
    R.Total++;
    TestOutput OA = A(ABits, BBits);
    TestOutput OB = B(ABits, BBits);
    if (Cmp(OA, OB)) {
      R.Passed++;
    } else {
      R.Failed++;
      if (NumReported < MaxReportedFailures) {
        Failures[NumReported++] = {ABits, BBits, OA, OB};
      }
    }
  });

  std::printf("%s: %d/%d passed", Name, R.Passed, R.Total);
  if (R.Failed > 0) {
    std::printf(" (%d FAILED)", R.Failed);
  }
  std::printf("\n");

  for (int I = 0; I < NumReported; ++I) {
    auto &F = Failures[I];
    std::fprintf(
        stderr,
        "  FAIL %s: a=0x%0*llX b=0x%0*llX  implA=0x%0*llX implB=0x%0*llX\n",
        Name, HexWidth, (unsigned long long)F.InputA, HexWidth,
        (unsigned long long)F.InputB, HexWidth,
        (unsigned long long)F.OutputA.Bits, HexWidth,
        (unsigned long long)F.OutputB.Bits);
  }

  return R;
}

// ===================================================================
// Iteration strategies
// ===================================================================

// All pairs from a list of interesting values.
struct TargetedPairs {
  const uint64_t *Values;
  int Count;

  template <typename Fn> void operator()(Fn &&Callback) const {
    for (int I = 0; I < Count; ++I)
      for (int J = 0; J < Count; ++J)
        Callback(Values[I], Values[J]);
  }
};

// Uniform random pairs over the format's bit range.
template <int TotalBits> struct RandomPairs {
  uint64_t Seed;
  int Count;

  template <typename Fn> void operator()(Fn &&Callback) const {
    std::mt19937_64 Rng(Seed);
    constexpr uint64_t Mask =
        TotalBits >= 64 ? ~uint64_t{0} : (uint64_t{1} << TotalBits) - 1;
    std::uniform_int_distribution<uint64_t> Dist(0, Mask);
    for (int I = 0; I < Count; ++I)
      Callback(Dist(Rng), Dist(Rng));
  }
};

// Run multiple strategies in sequence.
template <typename... Strategies> struct Combined {
  std::tuple<Strategies...> Strats;

  template <typename Fn> void operator()(Fn &&Callback) const {
    std::apply([&](const auto &...S) { (S(Callback), ...); }, Strats);
  }
};

template <typename... Strategies>
Combined<Strategies...> combined(Strategies... S) {
  return {std::tuple{std::move(S)...}};
}

// ===================================================================
// Comparators
// ===================================================================

struct BitExact {
  bool operator()(TestOutput A, TestOutput B) const {
    return A.Bits == B.Bits && A.Flags == B.Flags;
  }
};

struct BitExactIgnoreFlags {
  bool operator()(TestOutput A, TestOutput B) const {
    return A.Bits == B.Bits;
  }
};

// NaN-aware comparison: if both outputs are NaN (regardless of payload),
// they match. Otherwise bit-exact. Parameterized on FloatType so it
// knows how to detect NaN for any format/encoding.
template <typename FloatType> struct NanAwareBitExact {
  static bool isNan(uint64_t Bits) {
    using Fmt = typename FloatType::format;
    using Enc = typename FloatType::encoding;
    if constexpr (Enc::nan_encoding == NanEncoding::ReservedExponent) {
      constexpr uint64_t ExpMax = (uint64_t{1} << Fmt::exp_bits) - 1;
      uint64_t Exp = (Bits >> Fmt::exp_offset) & ExpMax;
      uint64_t Mant = Bits & ((uint64_t{1} << Fmt::mant_bits) - 1);
      return Exp == ExpMax && Mant != 0;
    } else if constexpr (Enc::nan_encoding == NanEncoding::TrapValue) {
      constexpr uint64_t TrapVal = uint64_t{1} << (Fmt::total_bits - 1);
      return Bits == TrapVal;
    } else if constexpr (Enc::nan_encoding ==
                         NanEncoding::NegativeZeroBitPattern) {
      constexpr uint64_t NanBits = uint64_t{1} << (Fmt::total_bits - 1);
      return Bits == NanBits;
    } else {
      return false;
    }
  }

  bool operator()(TestOutput A, TestOutput B) const {
    if (isNan(A.Bits) && isNan(B.Bits))
      return true;
    return A.Bits == B.Bits;
  }
};

// ===================================================================
// Interesting values generator
// ===================================================================

// Generates edge-case bit patterns from format/encoding parameters.
// Works for any IEEE 754-style format.
template <typename FloatType>
constexpr std::array<uint64_t, 22> interestingValues() {
  using Fmt = typename FloatType::format;
  constexpr int E = Fmt::exp_bits;
  constexpr int M = Fmt::mant_bits;
  constexpr int Bias = FloatType::exponent_bias;
  constexpr uint64_t SignBit = uint64_t{1} << Fmt::sign_offset;
  constexpr uint64_t ExpMax = (uint64_t{1} << E) - 1;
  constexpr uint64_t MantMask = (uint64_t{1} << M) - 1;

  return {{
      0,                                                              // +0
      SignBit,                                                        // -0
      ExpMax << Fmt::exp_offset,                                      // +Inf
      SignBit | (ExpMax << Fmt::exp_offset),                          // -Inf
      (ExpMax << Fmt::exp_offset) | (uint64_t{1} << (M - 1)),        // QNaN
      (ExpMax << Fmt::exp_offset) | 1,                                // SNaN min
      (ExpMax << Fmt::exp_offset) | ((uint64_t{1} << (M - 1)) - 1),  // SNaN max
      SignBit | (ExpMax << Fmt::exp_offset) | (uint64_t{1} << (M - 1)), // -QNaN
      1,                                                              // min +subnormal
      SignBit | 1,                                                    // min -subnormal
      MantMask,                                                       // max subnormal
      uint64_t{1} << M,                                              // min +normal
      ((ExpMax - 1) << Fmt::exp_offset) | MantMask,                  // max +finite
      SignBit | ((ExpMax - 1) << Fmt::exp_offset) | MantMask,        // max -finite
      uint64_t(Bias) << Fmt::exp_offset,                             // 1.0
      SignBit | (uint64_t(Bias) << Fmt::exp_offset),                 // -1.0
      uint64_t(Bias + 1) << Fmt::exp_offset,                        // 2.0
      uint64_t(Bias - 1) << Fmt::exp_offset,                        // 0.5
      (uint64_t{1} << M) + 1,                                       // min normal + 1 ULP
      (uint64_t(Bias) << Fmt::exp_offset) + 1,                      // 1.0 + 1 ULP
      (uint64_t(Bias) << Fmt::exp_offset) - 1,                      // 1.0 - 1 ULP
      uint64_t(Bias - M) << Fmt::exp_offset,                        // machine epsilon
  }};
}

} // namespace opine::testing

#endif // OPINE_TESTS_HARNESS_TEST_HARNESS_HPP
