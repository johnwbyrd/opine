#ifndef OPINE_TESTS_HARNESS_TEST_HARNESS_HPP
#define OPINE_TESTS_HARNESS_TEST_HARNESS_HPP

// Generic "this against that" test harness (from tdd.md).
//
// test_against(Name, HexWidth, Iter, ImplA, ImplB, Cmp)
//   runs ImplA and ImplB on every input pair yielded by Iter,
//   compares outputs using Cmp, and prints results.
//
// Both ImplA and ImplB are opaque callables:
//   (BitsType, BitsType) -> TestOutput<BitsType>
// The harness knows nothing about what library backs them.
//
// This header includes only ops.hpp. It has no knowledge of MPFR,
// SoftFloat, native FPU, or any other implementation.

#include <array>
#include <cstdio>
#include <random>
#include <tuple>

#include "harness/ops.hpp"
#include "opine/opine.hpp"

namespace opine::testing {

// ===================================================================
// Hex printing for arbitrary-width bit types
// ===================================================================

template <typename BitsType>
void printHex(FILE *Out, BitsType Val, int Width) {
  for (int I = Width - 1; I >= 0; --I) {
    int Nibble = static_cast<int>((Val >> (I * 4)) & BitsType{0xF});
    std::fputc("0123456789ABCDEF"[Nibble], Out);
  }
}

// ===================================================================
// Failure record
// ===================================================================

template <typename BitsType> struct Failure {
  BitsType InputA;
  BitsType InputB;
  TestOutput<BitsType> OutputA;
  TestOutput<BitsType> OutputB;
};

struct TestResult {
  int Total = 0;
  int Passed = 0;
  int Failed = 0;
};

// ===================================================================
// test_against — the harness
// ===================================================================

static constexpr int MaxReportedFailures = 10;

template <typename BitsType, typename IterFn, typename ImplA, typename ImplB,
          typename Comparator>
TestResult testAgainst(const char *Name, int HexWidth, IterFn Iter, ImplA A,
                       ImplB B, Comparator Cmp) {
  TestResult R;
  Failure<BitsType> Failures[MaxReportedFailures];
  int NumReported = 0;

  Iter([&](BitsType ABits, BitsType BBits) {
    R.Total++;
    TestOutput<BitsType> OA = A(ABits, BBits);
    TestOutput<BitsType> OB = B(ABits, BBits);
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
    std::fprintf(stderr, "  FAIL %s: a=0x", Name);
    printHex(stderr, F.InputA, HexWidth);
    std::fprintf(stderr, " b=0x");
    printHex(stderr, F.InputB, HexWidth);
    std::fprintf(stderr, "  implA=0x");
    printHex(stderr, F.OutputA.Bits, HexWidth);
    std::fprintf(stderr, " implB=0x");
    printHex(stderr, F.OutputB.Bits, HexWidth);
    std::fprintf(stderr, "\n");
  }

  return R;
}

// ===================================================================
// Iteration strategies
// ===================================================================

// All pairs from a list of interesting values.
template <typename BitsType> struct TargetedPairs {
  const BitsType *Values;
  int Count;

  template <typename Fn> void operator()(Fn &&Callback) const {
    for (int I = 0; I < Count; ++I)
      for (int J = 0; J < Count; ++J)
        Callback(Values[I], Values[J]);
  }
};

// Uniform random pairs over the format's bit range.
template <typename BitsType, int TotalBits> struct RandomPairs {
  uint64_t Seed;
  int Count;

  template <typename Fn> void operator()(Fn &&Callback) const {
    std::mt19937_64 Rng(Seed);
    constexpr int ChunkBits = 64;

    auto Gen = [&]() -> BitsType {
      BitsType Val = 0;
      for (int I = 0; I < (TotalBits + ChunkBits - 1) / ChunkBits; ++I)
        Val |= BitsType(Rng()) << (I * ChunkBits);
      if constexpr (TotalBits < int(sizeof(BitsType) * 8)) {
        constexpr BitsType Mask = (BitsType{1} << TotalBits) - 1;
        Val &= Mask;
      }
      return Val;
    };

    for (int I = 0; I < Count; ++I)
      Callback(Gen(), Gen());
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

template <typename BitsType> struct BitExact {
  bool operator()(TestOutput<BitsType> A, TestOutput<BitsType> B) const {
    return A.Bits == B.Bits && A.Flags == B.Flags;
  }
};

template <typename BitsType> struct BitExactIgnoreFlags {
  bool operator()(TestOutput<BitsType> A, TestOutput<BitsType> B) const {
    return A.Bits == B.Bits;
  }
};

// NaN-aware comparison: if both outputs are NaN (regardless of payload),
// they match. Otherwise bit-exact. Parameterized on FloatType so it
// knows how to detect NaN for any format/encoding.
template <typename FloatType> struct NanAwareBitExact {
  using BitsType = typename FloatType::storage_type;

  static bool isNan(BitsType Bits) {
    using Fmt = typename FloatType::format;
    using Enc = typename FloatType::encoding;
    if constexpr (Enc::nan_encoding == NanEncoding::ReservedExponent) {
      constexpr BitsType ExpMax = (BitsType{1} << Fmt::exp_bits) - 1;
      BitsType Exp = (Bits >> Fmt::exp_offset) & ExpMax;
      BitsType Mant = Bits & ((BitsType{1} << Fmt::mant_bits) - 1);
      return Exp == ExpMax && Mant != 0;
    } else if constexpr (Enc::nan_encoding == NanEncoding::TrapValue) {
      constexpr BitsType TrapVal = BitsType{1} << (Fmt::total_bits - 1);
      return Bits == TrapVal;
    } else if constexpr (Enc::nan_encoding ==
                         NanEncoding::NegativeZeroBitPattern) {
      constexpr BitsType NanBits = BitsType{1} << (Fmt::total_bits - 1);
      return Bits == NanBits;
    } else {
      return false;
    }
  }

  bool operator()(TestOutput<BitsType> A, TestOutput<BitsType> B) const {
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
template <typename FloatType> constexpr auto interestingValues() {
  using Fmt = typename FloatType::format;
  using Enc = typename FloatType::encoding;
  using BitsType = typename FloatType::storage_type;
  constexpr int E = Fmt::exp_bits;
  constexpr int M = Fmt::mant_bits;
  constexpr int Bias = FloatType::exponent_bias;
  constexpr BitsType SignBit = BitsType{1} << Fmt::sign_offset;
  constexpr BitsType ExpMax = (BitsType{1} << E) - 1;
  constexpr BitsType MantMask = (BitsType{1} << M) - 1;

  if constexpr (Enc::has_implicit_bit) {
    return std::array<BitsType, 22>{{
        0,                                                         // +0
        SignBit,                                                   // -0
        ExpMax << Fmt::exp_offset,                                 // +Inf
        SignBit | (ExpMax << Fmt::exp_offset),                     // -Inf
        (ExpMax << Fmt::exp_offset) | (BitsType{1} << (M - 1)),   // QNaN
        (ExpMax << Fmt::exp_offset) | 1,                           // SNaN min
        (ExpMax << Fmt::exp_offset) | ((BitsType{1} << (M - 1)) - 1), // SNaN max
        SignBit | (ExpMax << Fmt::exp_offset) |
            (BitsType{1} << (M - 1)),                              // -QNaN
        BitsType{1},                                               // min +subnormal
        SignBit | BitsType{1},                                     // min -subnormal
        MantMask,                                                  // max subnormal
        BitsType{1} << M,                                          // min +normal
        ((ExpMax - 1) << Fmt::exp_offset) | MantMask,              // max +finite
        SignBit | ((ExpMax - 1) << Fmt::exp_offset) | MantMask,    // max -finite
        BitsType(Bias) << Fmt::exp_offset,                         // 1.0
        SignBit | (BitsType(Bias) << Fmt::exp_offset),             // -1.0
        BitsType(Bias + 1) << Fmt::exp_offset,                    // 2.0
        BitsType(Bias - 1) << Fmt::exp_offset,                    // 0.5
        (BitsType{1} << M) + 1,                  // min normal + 1 ULP
        (BitsType(Bias) << Fmt::exp_offset) + 1, // 1.0 + 1 ULP
        (BitsType(Bias) << Fmt::exp_offset) - 1, // 1.0 - 1 ULP
        BitsType(Bias - M) << Fmt::exp_offset,   // machine epsilon
    }};
  } else {
    // Explicit integer bit (e.g. extFloat80): J-bit is bit M-1 of mantissa.
    constexpr BitsType JBit = BitsType{1} << (M - 1);
    constexpr BitsType MantMaskNoJ = MantMask & ~JBit;
    return std::array<BitsType, 38>{{
        // === Canonical encodings ===
        0,                                                   // +0
        SignBit,                                             // -0
        (ExpMax << Fmt::exp_offset) | JBit,                  // +Inf (J=1, frac=0)
        SignBit | (ExpMax << Fmt::exp_offset) | JBit,        // -Inf
        (ExpMax << Fmt::exp_offset) | JBit |
            (BitsType{1} << (M - 2)),                        // QNaN
        (ExpMax << Fmt::exp_offset) | JBit | 1,             // SNaN min
        (ExpMax << Fmt::exp_offset) | MantMask,              // SNaN max (all bits)
        SignBit | (ExpMax << Fmt::exp_offset) | JBit |
            (BitsType{1} << (M - 2)),                        // -QNaN
        BitsType{1},                                         // min +subnormal (J=0)
        SignBit | BitsType{1},                               // min -subnormal
        MantMaskNoJ,                             // max subnormal (J=0, frac=all 1s)
        (BitsType{1} << Fmt::exp_offset) | JBit, // min +normal (exp=1, J=1)
        ((ExpMax - 1) << Fmt::exp_offset) | MantMask,       // max +finite
        SignBit | ((ExpMax - 1) << Fmt::exp_offset) | MantMask, // max -finite
        (BitsType(Bias) << Fmt::exp_offset) | JBit,         // 1.0
        SignBit | (BitsType(Bias) << Fmt::exp_offset) | JBit, // -1.0
        (BitsType(Bias + 1) << Fmt::exp_offset) | JBit,     // 2.0
        (BitsType(Bias - 1) << Fmt::exp_offset) | JBit,     // 0.5
        (BitsType{1} << Fmt::exp_offset) | JBit | 1,        // min normal + 1 ULP
        (BitsType(Bias) << Fmt::exp_offset) | JBit | 1,     // 1.0 + 1 ULP
        (BitsType(Bias) << Fmt::exp_offset) |
            (JBit - 1), // unnormal: exp=Bias, J=0, frac=all 1s
        (BitsType(Bias - (M - 1)) << Fmt::exp_offset) | JBit, // machine epsilon
        // === Unnormals: non-zero exponent, J=0 ===
        (BitsType{1} << Fmt::exp_offset),   // unnormal-zero: exp=1, sig=0
        (BitsType(Bias) << Fmt::exp_offset), // unnormal-zero: exp=Bias, sig=0
        SignBit | (BitsType(Bias) << Fmt::exp_offset), // negative unnormal-zero
        (BitsType{1} << Fmt::exp_offset) |
            MantMaskNoJ, // unnormal: exp=1, J=0, frac=all 1s
        (BitsType(Bias) << Fmt::exp_offset) |
            (JBit >> 1), // unnormal 0.5: exp=Bias, sig=0x4000...
        (BitsType{2} << Fmt::exp_offset) |
            MantMaskNoJ, // unnormal: exp=2, J=0, frac=all 1s
        ((ExpMax - 1) << Fmt::exp_offset) |
            MantMaskNoJ, // unnormal near max: exp=max-1, J=0, frac=all 1s
        // === Pseudo-denormals: exp=0, J=1 ===
        JBit,                   // pseudo-denormal: exp=0, J=1, frac=0
        SignBit | JBit,         // negative pseudo-denormal
        JBit | 1,               // pseudo-denormal: exp=0, J=1, frac=1
        JBit | MantMaskNoJ,     // pseudo-denormal: exp=0, J=1, frac=all 1s
        // === Pseudo-infinities: exp=max, J=0, frac=0 ===
        (ExpMax << Fmt::exp_offset),           // pseudo-infinity
        SignBit | (ExpMax << Fmt::exp_offset),  // negative pseudo-infinity
        // === Pseudo-NaNs: exp=max, J=0, frac!=0 ===
        (ExpMax << Fmt::exp_offset) | (BitsType{1} << (M - 2)), // pseudo-QNaN
        (ExpMax << Fmt::exp_offset) | 1,        // pseudo-SNaN min
        (ExpMax << Fmt::exp_offset) | MantMaskNoJ, // pseudo-SNaN max
    }};
  }
}

} // namespace opine::testing

#endif // OPINE_TESTS_HARNESS_TEST_HARNESS_HPP
