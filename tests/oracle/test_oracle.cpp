// Cross-validation tests: verify that independent implementations agree.
//
// Every test is an instance of the same pattern: take two adapters that
// should agree, run them on the same inputs, compare outputs. No adapter
// is privileged. Adding a new adapter (e.g., OPINE itself) requires zero
// changes to existing tests — just add new TEST_CASE_TEMPLATE lines.

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include "harness/impl_mpfr.hpp"
#include "harness/impl_native.hpp"
#include "harness/impl_softfloat.hpp"
#include "harness/test_harness.hpp"

using namespace opine;
using namespace opine::testing;

// ===================================================================
// SoftFloat global state (must be set before any SoftFloat call)
// ===================================================================

struct SoftFloatInit {
  SoftFloatInit() {
    softfloat_roundingMode = softfloat_round_near_even;
    softfloat_detectTininess = softfloat_tininess_afterRounding;
  }
};

static SoftFloatInit GlobalSoftFloatInit;

// ===================================================================
// verifyAgreement — generic pairwise comparison
// ===================================================================
// Compares two adapters on all four arithmetic operations for a given
// FloatType. Uses the existing test_against() harness with targeted
// edge cases + random pairs.

template <typename FloatType, typename AdapterA, typename AdapterB>
void verifyAgreement(const AdapterA &A, const AdapterB &B) {
  using BitsType = typename FloatType::storage_type;
  constexpr int TotalBits = FloatType::format::total_bits;
  constexpr int HexWidth = (TotalBits + 3) / 4;

  constexpr auto Interesting = interestingValues<FloatType>();
  auto Iter = combined(
      TargetedPairs<BitsType>{Interesting.data(),
                              static_cast<int>(Interesting.size())},
      RandomPairs<BitsType, TotalBits>{42, 1000000});

  NanAwareBitExact<FloatType> Cmp;

  for (auto O : {Op::Add, Op::Sub, Op::Mul, Op::Div}) {
    SUBCASE(opName(O)) {
      auto ImplA = [&](BitsType X, BitsType Y) {
        return A.dispatch(O, X, Y);
      };
      auto ImplB = [&](BitsType X, BitsType Y) {
        return B.dispatch(O, X, Y);
      };
      auto R =
          testAgainst<BitsType>(opName(O), HexWidth, Iter, ImplA, ImplB, Cmp);
      CHECK(R.Failed == 0);
    }
  }
}

// ===================================================================
// Arithmetic agreement tests
// ===================================================================

TEST_CASE_TEMPLATE("MPFR vs SoftFloat", T, float16, float32, float64,
                   extFloat80, float128) {
  verifyAgreement<T>(MpfrAdapter<T>{}, SoftFloatAdapter<T>{});
}

TEST_CASE_TEMPLATE("Native vs MPFR", T, float32, float64) {
  verifyAgreement<T>(NativeAdapter<T>{}, MpfrAdapter<T>{});
}

TEST_CASE_TEMPLATE("Native vs SoftFloat", T, float32, float64) {
  verifyAgreement<T>(NativeAdapter<T>{}, SoftFloatAdapter<T>{});
}

// ===================================================================
// branchlessDecode — alternative decode for cross-checking
// ===================================================================
// A trivially simple decode that computes the mathematical definition:
//   value = (-1)^sign x significand x 2^(effective_exp - bias - sig_width)
// No infinity/NaN detection, no special cases beyond the exp==0 rule.
// This is a test utility, not part of any adapter.

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

  bool IsNegative =
      (extractField(Bits, Fmt::sign_offset, Fmt::sign_bits) != 0);
  BitsType RawExp = extractField(Bits, Fmt::exp_offset, Fmt::exp_bits);
  BitsType RawMant = extractField(Bits, Fmt::mant_offset, Fmt::mant_bits);
  int Exp = static_cast<int>(RawExp);

  int EffExp = (Exp == 0) ? 1 : Exp;

  BitsType Sig;
  int SigWidth;
  if constexpr (Enc::has_implicit_bit) {
    SigWidth = Fmt::mant_bits;
    Sig = (Exp == 0) ? RawMant : (RawMant | (BitsType{1} << Fmt::mant_bits));
  } else {
    SigWidth = Fmt::mant_bits - 1;
    Sig = RawMant;
  }

  MpfrFloat Result;
  mpz_t Z;
  mpz_init(Z);
  opine::testing::detail::bitsToMpz(Z, Sig);
  mpfr_set_z_2exp(Result, Z,
                  static_cast<mpfr_exp_t>(EffExp) - Bias - SigWidth,
                  MPFR_RNDN);
  mpz_clear(Z);

  if (IsNegative)
    mpfr_neg(Result, Result, MPFR_RNDN);

  return Result;
}

// ===================================================================
// Decode validation: branchless formula cross-check
// ===================================================================
// Verify the MPFR adapter's decode by comparing it against the
// branchless formula above.

template <typename FloatType> void verifyDecode() {
  using Fmt = typename FloatType::format;
  using Enc = typename FloatType::encoding;
  using BitsType = typename FloatType::storage_type;
  constexpr int HexWidth = (Fmt::total_bits + 3) / 4;
  constexpr BitsType ExpMax = (BitsType{1} << Fmt::exp_bits) - 1;

  constexpr auto Values = interestingValues<FloatType>();
  int Failures = 0;

  for (auto Bits : Values) {
    // Skip infinity and NaN — the branchless formula can't represent them
    BitsType Exp = extractField(Bits, Fmt::exp_offset, Fmt::exp_bits);
    BitsType Mant = extractField(Bits, Fmt::mant_offset, Fmt::mant_bits);
    if constexpr (Enc::has_implicit_bit) {
      if (Exp == ExpMax)
        continue;
    } else {
      constexpr BitsType JBit = BitsType{1} << (Fmt::mant_bits - 1);
      constexpr BitsType FracMask = JBit - 1;
      if (Exp == ExpMax && (Mant & FracMask) == 0)
        continue;
      if (Exp == ExpMax && Mant != 0)
        continue;
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

  CHECK(Failures == 0);
}

TEST_CASE_TEMPLATE("decode: branchless vs full", T, float16, float32, float64,
                   extFloat80, float128) {
  verifyDecode<T>();
}

// ===================================================================
// Value equivalence: explicit-bit format encoding pairs
// ===================================================================
// Verify that bit patterns known to represent the same mathematical
// value produce equal MPFR values from the decode.

template <typename FloatType> void verifyValueEquivalence() {
  using Fmt = typename FloatType::format;
  using Enc = typename FloatType::encoding;
  using BitsType = typename FloatType::storage_type;
  constexpr int HexWidth = (Fmt::total_bits + 3) / 4;

  if constexpr (!Enc::has_implicit_bit) {
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
        {"unnormal-zero{exp=1,sig=0} == +0",
         BitsType{1} << Fmt::exp_offset, BitsType{0}},
        {"unnormal-zero{exp=bias,sig=0} == +0",
         BitsType(Bias) << Fmt::exp_offset, BitsType{0}},
        {"neg unnormal-zero{exp=bias,sig=0} == -0",
         SignBit | (BitsType(Bias) << Fmt::exp_offset), SignBit},
        {"pseudo-denormal{exp=0,J=1} == normal{exp=1,J=1}",
         JBit, (BitsType{1} << Fmt::exp_offset) | JBit},
        {"pseudo-inf{exp=max,J=0} == canonical inf{exp=max,J=1}",
         ExpMax << Fmt::exp_offset,
         (ExpMax << Fmt::exp_offset) | JBit},
        {"neg pseudo-inf == neg canonical inf",
         SignBit | (ExpMax << Fmt::exp_offset),
         SignBit | (ExpMax << Fmt::exp_offset) | JBit},
    };

    int Failures = 0;

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

    CHECK(Failures == 0);
  }
}

TEST_CASE("extFloat80 value equivalence") {
  verifyValueEquivalence<extFloat80>();
}
