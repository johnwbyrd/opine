// Oracle validation: verify that MPFR and SoftFloat agree by testing
// in both directions — MPFR vs SoftFloat, then SoftFloat vs MPFR.
//
// This is an instance of the "this against that" harness (tdd.md).
// Both implementations are opaque callables with the same signature;
// the harness doesn't know what backs them, so the order is arbitrary.

#include "harness/native_ops.hpp"
#include "harness/softfloat_ops.hpp"
#include "harness/test_harness.hpp"
#include "oracle/mpfr_exact.hpp"

#include <cstdio>

using namespace opine;
using namespace opine::oracle;
using namespace opine::testing;

// ===================================================================
// MPFR oracle callable: decode -> exact op -> round to format
// ===================================================================

template <typename FloatType>
auto makeOracleOp(Op Operation) {
  using BitsType = typename FloatType::storage_type;
  return [Operation](BitsType A, BitsType B) -> TestOutput<BitsType> {
    MpfrFloat Ma = decodeToMpfr<FloatType>(A);
    MpfrFloat Mb = decodeToMpfr<FloatType>(B);
    MpfrFloat Exact = mpfrExactOp(Operation, Ma, Mb);
    return {mpfrRoundToFormat<FloatType>(Exact), 0};
  };
}

// ===================================================================
// Per-format test runner
// ===================================================================

template <typename FloatType>
int runFormatTests() {
  using Sf = SoftFloatOps<FloatType>;
  using SfType = typename Sf::SfType;
  using BitsType = typename FloatType::storage_type;
  using SfBinOp = SfType (*)(SfType, SfType);
  constexpr int TotalBits = FloatType::format::total_bits;
  constexpr int HexWidth = (TotalBits + 3) / 4;

  // Iteration: targeted edge cases + 10,000 random pairs
  constexpr auto Interesting = interestingValues<FloatType>();
  auto Iter =
      combined(TargetedPairs<BitsType>{Interesting.data(),
                             static_cast<int>(Interesting.size())},
               RandomPairs<BitsType, TotalBits>{42, 1000000});

  NanAwareBitExact<FloatType> Cmp;

  struct OpDesc {
    const char *Name;
    Op OracleOp;
    SfBinOp SfFn;
  };

  OpDesc Tests[] = {
      {"add", Op::Add, &Sf::add},
      {"sub", Op::Sub, &Sf::sub},
      {"mul", Op::Mul, &Sf::mul},
      {"div", Op::Div, &Sf::div},
  };

  int TotalFailures = 0;

  std::printf("  MPFR vs SoftFloat:\n");
  for (auto &T : Tests) {
    char Label[64];
    std::snprintf(Label, sizeof(Label), "    %s", T.Name);
    auto Oracle = makeOracleOp<FloatType>(T.OracleOp);
    auto SfImpl = makeSoftFloatOp<FloatType>(T.SfFn);
    auto R = testAgainst<BitsType>(Label, HexWidth, Iter, Oracle, SfImpl, Cmp);
    TotalFailures += R.Failed;
  }

  std::printf("  SoftFloat vs MPFR:\n");
  for (auto &T : Tests) {
    char Label[64];
    std::snprintf(Label, sizeof(Label), "    %s", T.Name);
    auto Oracle = makeOracleOp<FloatType>(T.OracleOp);
    auto SfImpl = makeSoftFloatOp<FloatType>(T.SfFn);
    auto R = testAgainst<BitsType>(Label, HexWidth, Iter, SfImpl, Oracle, Cmp);
    TotalFailures += R.Failed;
  }

  return TotalFailures;
}

// ===================================================================
// Three-way test: native hardware vs MPFR and SoftFloat
// ===================================================================
// Only for formats with native CPU operations (float32, float64).
// This validates the oracle's decode/encode logic against a third
// independent implementation — the hardware FPU.

template <typename FloatType>
int runNativeTests() {
  using Nat = NativeOps<FloatType>;
  using Sf = SoftFloatOps<FloatType>;
  using SfType = typename Sf::SfType;
  using BitsType = typename FloatType::storage_type;
  using SfBinOp = SfType (*)(SfType, SfType);
  using NatBinOp = TestOutput<BitsType> (*)(BitsType, BitsType);
  constexpr int TotalBits = FloatType::format::total_bits;
  constexpr int HexWidth = (TotalBits + 3) / 4;

  constexpr auto Interesting = interestingValues<FloatType>();
  auto Iter =
      combined(TargetedPairs<BitsType>{Interesting.data(),
                             static_cast<int>(Interesting.size())},
               RandomPairs<BitsType, TotalBits>{42, 1000000});

  NanAwareBitExact<FloatType> Cmp;

  struct OpDesc {
    const char *Name;
    Op OracleOp;
    SfBinOp SfFn;
    NatBinOp NatFn;
  };

  OpDesc Tests[] = {
      {"add", Op::Add, &Sf::add, &Nat::add},
      {"sub", Op::Sub, &Sf::sub, &Nat::sub},
      {"mul", Op::Mul, &Sf::mul, &Nat::mul},
      {"div", Op::Div, &Sf::div, &Nat::div},
  };

  int TotalFailures = 0;

  std::printf("  Native vs MPFR:\n");
  for (auto &T : Tests) {
    char Label[64];
    std::snprintf(Label, sizeof(Label), "    %s", T.Name);
    auto Oracle = makeOracleOp<FloatType>(T.OracleOp);
    auto R = testAgainst<BitsType>(Label, HexWidth, Iter, T.NatFn, Oracle, Cmp);
    TotalFailures += R.Failed;
  }

  std::printf("  Native vs SoftFloat:\n");
  for (auto &T : Tests) {
    char Label[64];
    std::snprintf(Label, sizeof(Label), "    %s", T.Name);
    auto SfImpl = makeSoftFloatOp<FloatType>(T.SfFn);
    auto R = testAgainst<BitsType>(Label, HexWidth, Iter, T.NatFn, SfImpl, Cmp);
    TotalFailures += R.Failed;
  }

  return TotalFailures;
}

// ===================================================================
// Oracle decode validation: branchless formula cross-check
// ===================================================================
// Verify decodeToMpfr by comparing it against a trivially simple
// decode that just computes the mathematical definition of the format:
//   value = (-1)^sign × significand × 2^(effective_exp - bias - sig_width)
// No infinity/NaN detection, no special cases beyond the exp==0 rule.
// Applies to all formats; skips infinity and NaN (which have no
// finite mathematical value).

template <typename FloatType>
MpfrFloat branchlessDecode(typename FloatType::storage_type Bits) {
  using Fmt = typename FloatType::format;
  using Enc = typename FloatType::encoding;
  using BitsType = typename FloatType::storage_type;
  constexpr int Bias = FloatType::exponent_bias;

  constexpr int TotalBits = Fmt::total_bits;
  if constexpr (TotalBits < int(sizeof(BitsType) * 8)) {
    constexpr BitsType WordMask = (BitsType{1} << TotalBits) - 1;
    Bits &= WordMask;
  }

  bool IsNegative = (oracle::detail::extractField(Bits, Fmt::sign_offset,
                                          Fmt::sign_bits) != 0);
  BitsType RawExp = oracle::detail::extractField(Bits, Fmt::exp_offset, Fmt::exp_bits);
  BitsType RawMant = oracle::detail::extractField(Bits, Fmt::mant_offset,
                                          Fmt::mant_bits);
  int Exp = static_cast<int>(RawExp);

  // Effective exponent: exp==0 uses emin (same as exp==1)
  int EffExp = (Exp == 0) ? 1 : Exp;

  // Build full significand
  BitsType Sig;
  int SigWidth;
  if constexpr (Enc::has_implicit_bit) {
    SigWidth = Fmt::mant_bits;
    Sig = (Exp == 0) ? RawMant : (RawMant | (BitsType{1} << Fmt::mant_bits));
  } else {
    SigWidth = Fmt::mant_bits - 1; // integer bit is explicit
    Sig = RawMant;
  }

  // value = sig × 2^(eff_exp - bias - sig_width)
  MpfrFloat Result;
  mpz_t Z;
  mpz_init(Z);
  oracle::detail::bitsToMpz(Z, Sig);
  mpfr_set_z_2exp(Result, Z, static_cast<mpfr_exp_t>(EffExp) - Bias - SigWidth,
                  MPFR_RNDN);
  mpz_clear(Z);

  if (IsNegative)
    mpfr_neg(Result, Result, MPFR_RNDN);

  return Result;
}

template <typename FloatType>
int verifyDecode() {
  using Fmt = typename FloatType::format;
  using Enc = typename FloatType::encoding;
  using BitsType = typename FloatType::storage_type;
  constexpr int HexWidth = (Fmt::total_bits + 3) / 4;
  constexpr BitsType ExpMax = (BitsType{1} << Fmt::exp_bits) - 1;

  constexpr auto Values = interestingValues<FloatType>();
  int Failures = 0;

  for (auto Bits : Values) {
    // Skip infinity and NaN — the branchless formula can't represent them
    BitsType Exp = oracle::detail::extractField(Bits, Fmt::exp_offset, Fmt::exp_bits);
    BitsType Mant = oracle::detail::extractField(Bits, Fmt::mant_offset,
                                         Fmt::mant_bits);
    if constexpr (Enc::has_implicit_bit) {
      if (Exp == ExpMax)
        continue; // infinity or NaN
    } else {
      constexpr BitsType JBit = BitsType{1} << (Fmt::mant_bits - 1);
      constexpr BitsType FracMask = JBit - 1;
      if (Exp == ExpMax && (Mant & FracMask) == 0)
        continue; // infinity (canonical or pseudo)
      if (Exp == ExpMax && Mant != 0)
        continue; // NaN (canonical or pseudo)
    }

    MpfrFloat Oracle = decodeToMpfr<FloatType>(Bits);
    MpfrFloat Formula = branchlessDecode<FloatType>(Bits);

    bool Match;
    if (Oracle.isZero() && Formula.isZero()) {
      Match = (Oracle.isNegative() == Formula.isNegative());
    } else {
      Match = (mpfr_cmp(Oracle, Formula) == 0);
    }

    if (!Match) {
      Failures++;
      if (Failures <= 10) {
        std::fprintf(stderr, "  DECODE MISMATCH: bits=0x");
        printHex(stderr, Bits, HexWidth);
        char OBuf[80], FBuf[80];
        mpfr_snprintf(OBuf, sizeof(OBuf), "%.30Rg", Oracle.get());
        mpfr_snprintf(FBuf, sizeof(FBuf), "%.30Rg", Formula.get());
        std::fprintf(stderr, "  oracle=%s  formula=%s\n", OBuf, FBuf);
      }
    }
  }

  std::printf("    decode: %d/%d passed\n",
              static_cast<int>(Values.size()) - Failures,
              static_cast<int>(Values.size()));
  return Failures;
}

// ===================================================================
// Oracle decode validation: value-equivalence
// ===================================================================
// Verify that bit patterns known to represent the same mathematical
// value produce equal MPFR values from decodeToMpfr.

template <typename FloatType>
int verifyValueEquivalence() {
  using Fmt = typename FloatType::format;
  using Enc = typename FloatType::encoding;
  using BitsType = typename FloatType::storage_type;
  constexpr int HexWidth = (Fmt::total_bits + 3) / 4;

  if constexpr (!Enc::has_implicit_bit) {
    // Explicit-bit format: build pairs of equivalent encodings
    constexpr int Bias = FloatType::exponent_bias;
    constexpr BitsType SignBit = BitsType{1} << Fmt::sign_offset;
    constexpr BitsType JBit = BitsType{1} << (Fmt::mant_bits - 1);
    constexpr BitsType ExpMax = (BitsType{1} << Fmt::exp_bits) - 1;

    struct EquivPair {
      const char *Desc;
      BitsType A;
      BitsType B;
    };

    EquivPair Pairs[] = {
        // Unnormal-zero == canonical zero
        {"unnormal-zero{exp=1,sig=0} == +0",
         BitsType{1} << Fmt::exp_offset, BitsType{0}},
        {"unnormal-zero{exp=bias,sig=0} == +0",
         BitsType(Bias) << Fmt::exp_offset, BitsType{0}},
        {"neg unnormal-zero{exp=bias,sig=0} == -0",
         SignBit | (BitsType(Bias) << Fmt::exp_offset), SignBit},
        // Pseudo-denormal == normal with same value
        // {exp=0, J=1, frac=0} == {exp=1, J=1, frac=0} == 2^(1-bias)
        {"pseudo-denormal{exp=0,J=1} == normal{exp=1,J=1}",
         JBit, (BitsType{1} << Fmt::exp_offset) | JBit},
        // Pseudo-infinity == canonical infinity
        {"pseudo-inf{exp=max,J=0} == canonical inf{exp=max,J=1}",
         ExpMax << Fmt::exp_offset,
         (ExpMax << Fmt::exp_offset) | JBit},
        {"neg pseudo-inf == neg canonical inf",
         SignBit | (ExpMax << Fmt::exp_offset),
         SignBit | (ExpMax << Fmt::exp_offset) | JBit},
    };

    int Failures = 0;
    int Total = sizeof(Pairs) / sizeof(Pairs[0]);

    for (auto &P : Pairs) {
      MpfrFloat ValA = decodeToMpfr<FloatType>(P.A);
      MpfrFloat ValB = decodeToMpfr<FloatType>(P.B);

      bool Match;
      if (ValA.isNan() && ValB.isNan()) {
        Match = true;
      } else if (ValA.isInf() && ValB.isInf()) {
        Match = (ValA.isNegative() == ValB.isNegative());
      } else if (ValA.isZero() && ValB.isZero()) {
        Match = (ValA.isNegative() == ValB.isNegative());
      } else {
        Match = (mpfr_cmp(ValA, ValB) == 0);
      }

      if (!Match) {
        Failures++;
        char ABuf[80], BBuf[80];
        mpfr_snprintf(ABuf, sizeof(ABuf), "%.30Rg", ValA.get());
        mpfr_snprintf(BBuf, sizeof(BBuf), "%.30Rg", ValB.get());
        std::fprintf(stderr, "  EQUIV MISMATCH: %s\n", P.Desc);
        std::fprintf(stderr, "    A=0x");
        printHex(stderr, P.A, HexWidth);
        std::fprintf(stderr, " -> %s\n    B=0x", ABuf);
        printHex(stderr, P.B, HexWidth);
        std::fprintf(stderr, " -> %s\n", BBuf);
      }
    }

    std::printf("    equiv:  %d/%d passed\n", Total - Failures, Total);
    return Failures;
  } else {
    // Implicit-bit formats have no equivalent-encoding pairs to test
    std::printf("    equiv:  (not applicable)\n");
    return 0;
  }
}

template <typename FloatType>
int verifyOracle() {
  int Failures = 0;
  std::printf("  Oracle decode validation:\n");
  Failures += verifyDecode<FloatType>();
  Failures += verifyValueEquivalence<FloatType>();
  return Failures;
}

// ===================================================================
// Main
// ===================================================================

int main() {
  softfloat_roundingMode = softfloat_round_near_even;
  softfloat_detectTininess = softfloat_tininess_afterRounding;

  int Failures = 0;

  std::printf("=== float16 (IEEE 754 binary16) ===\n");
  Failures += verifyOracle<float16>();
  Failures += runFormatTests<float16>();

  std::printf("\n=== float32 (IEEE 754 binary32) ===\n");
  Failures += verifyOracle<float32>();
  Failures += runFormatTests<float32>();
  Failures += runNativeTests<float32>();

  std::printf("\n=== float64 (IEEE 754 binary64) ===\n");
  Failures += verifyOracle<float64>();
  Failures += runFormatTests<float64>();
  Failures += runNativeTests<float64>();

  std::printf("\n=== extFloat80 (x87 80-bit extended) ===\n");
  Failures += verifyOracle<extFloat80>();
  Failures += runFormatTests<extFloat80>();

  std::printf("\n=== float128 (IEEE 754 binary128) ===\n");
  Failures += verifyOracle<float128>();
  Failures += runFormatTests<float128>();

  if (Failures > 0) {
    std::fprintf(stderr, "\nFAILED: %d total failures\n", Failures);
    return 1;
  }

  std::printf("\nPASS: all implementations agree\n");
  return 0;
}
