#ifndef OPINE_TESTS_HARNESS_IMPL_MPFR_HPP
#define OPINE_TESTS_HARNESS_IMPL_MPFR_HPP

// MPFR adapter: one implementation among equals.
//
// Provides MpfrAdapter<FloatType> satisfying the adapter interface:
//   dispatch(Op, BitsType, BitsType) -> TestOutput<BitsType>
//
// Internally contains:
//   MpfrFloat         — RAII wrapper around mpfr_t
//   decodeToMpfr      — bit pattern -> exact MPFR value (any format/encoding)
//   mpfrExactOp       — exact arithmetic at 256-bit precision
//   mpfrRoundToFormat — round MPFR value back to format bits
//   branchlessDecode  — alternative decode for cross-checking
//
// These are internals of the MPFR adapter, not a privileged oracle API.

#include <cstdint>
#include <cstring>
#include <type_traits>
#include <utility>

#include <gmp.h>
#include <mpfr.h>

#include "harness/ops.hpp"
#include "opine/opine.hpp"

namespace opine::testing {

// Working precision for exact computation. 256 bits is far more than
// enough for any format up to binary128.
inline constexpr mpfr_prec_t ExactPrecision = 256;

// ===================================================================
// MpfrFloat — RAII wrapper around mpfr_t
// ===================================================================

class MpfrFloat {
public:
  explicit MpfrFloat(mpfr_prec_t Prec = ExactPrecision) {
    mpfr_init2(Val, Prec);
  }

  ~MpfrFloat() { mpfr_clear(Val); }

  MpfrFloat(const MpfrFloat &) = delete;
  MpfrFloat &operator=(const MpfrFloat &) = delete;

  MpfrFloat(MpfrFloat &&Other) noexcept {
    Val[0] = Other.Val[0];
    mpfr_init2(Other.Val, 2);
    mpfr_set_nan(Other.Val);
  }

  MpfrFloat &operator=(MpfrFloat &&Other) noexcept {
    if (this != &Other) {
      mpfr_clear(Val);
      Val[0] = Other.Val[0];
      mpfr_init2(Other.Val, 2);
      mpfr_set_nan(Other.Val);
    }
    return *this;
  }

  mpfr_ptr get() { return Val; }
  mpfr_srcptr get() const { return Val; }
  operator mpfr_ptr() { return Val; }
  operator mpfr_srcptr() const { return Val; }

  bool isNan() const { return mpfr_nan_p(Val) != 0; }
  bool isInf() const { return mpfr_inf_p(Val) != 0; }
  bool isZero() const { return mpfr_zero_p(Val) != 0; }
  int sign() const { return mpfr_sgn(Val); }
  bool isNegative() const { return mpfr_signbit(Val) != 0; }

private:
  mpfr_t Val;
};

// ===================================================================
// Width-agnostic BitsType <-> mpz_t conversion
// ===================================================================

namespace detail {

// Never shift on the final loop iteration: for _BitInt storage the
// type's value width can equal 8 (or not be a multiple of 8), and a
// shift by >= the width is undefined — Clang miscompiles it. sizeof
// rounds up, so the guarded shifts are always strictly in-width.
template <typename BitsType> void bitsToMpz(mpz_t Z, BitsType Val) {
  constexpr int NumBytes = sizeof(BitsType);
  unsigned char Bytes[NumBytes];
  for (int I = 0; I < NumBytes; ++I) {
    Bytes[I] = static_cast<unsigned char>(Val);
    if (I + 1 < NumBytes)
      Val >>= 8;
  }
  mpz_import(Z, NumBytes, -1, 1, 0, 0, Bytes);
}

template <typename BitsType> BitsType mpzToBits(const mpz_t Z) {
  constexpr int NumBytes = sizeof(BitsType);
  unsigned char Bytes[NumBytes] = {};
  mpz_export(Bytes, nullptr, -1, 1, 0, 0, Z);
  BitsType Val = 0;
  for (int I = NumBytes - 1; I >= 0; --I) {
    if (I != NumBytes - 1)
      Val <<= 8;
    Val |= BitsType(Bytes[I]);
  }
  return Val;
}

} // namespace detail

// ===================================================================
// decodeToMpfr — Convert a bit pattern to its exact MPFR value
// ===================================================================

template <typename FloatType>
MpfrFloat decodeToMpfr(typename FloatType::storage_type Bits) {
  using Fmt = typename FloatType::layout;
  using Enc = typename FloatType::number;
  using BitsType = typename FloatType::storage_type;

  constexpr int TotalBits = Fmt::total_bits;
  Bits &= opine::maskLow<BitsType>(TotalBits);

  MpfrFloat Result;

  // Phase 1: Check for special values identified by complete bit pattern

  if constexpr (Enc::nan_encoding == NanEncoding::TrapValue) {
    constexpr BitsType TrapValue = BitsType{1} << (TotalBits - 1);
    if (Bits == TrapValue) {
      mpfr_set_nan(Result);
      return Result;
    }
  }

  if constexpr (Enc::inf_encoding == InfEncoding::IntegerExtremes) {
    constexpr BitsType PosInf = (BitsType{1} << (TotalBits - 1)) - 1;
    if constexpr (TotalBits < int(sizeof(BitsType) * 8)) {
      constexpr BitsType WordMask = (BitsType{1} << TotalBits) - 1;
      constexpr BitsType NegInf = (WordMask - PosInf + 1) & WordMask;
      if (Bits == PosInf) {
        mpfr_set_inf(Result, +1);
        return Result;
      }
      if (Bits == NegInf) {
        mpfr_set_inf(Result, -1);
        return Result;
      }
    } else {
      constexpr BitsType NegInf = ~PosInf + 1;
      if (Bits == PosInf) {
        mpfr_set_inf(Result, +1);
        return Result;
      }
      if (Bits == NegInf) {
        mpfr_set_inf(Result, -1);
        return Result;
      }
    }
  }

  if constexpr (Enc::nan_encoding == NanEncoding::NegativeZeroBitPattern) {
    BitsType RawSign =
        extractField(Bits, Fmt::sign_offset, Fmt::sign_bits);
    BitsType RawExp =
        extractField(Bits, Fmt::exp_offset, Fmt::exp_bits);
    BitsType RawMant =
        extractField(Bits, Fmt::sig_offset, Fmt::sig_bits);
    if (RawSign != 0 && RawExp == 0 && RawMant == 0) {
      mpfr_set_nan(Result);
      return Result;
    }
  }

  // Phase 2: Determine sign and extract magnitude fields

  BitsType RawSign =
      extractField(Bits, Fmt::sign_offset, Fmt::sign_bits);
  bool IsNegative = false;

  BitsType MagExp;
  BitsType MagMant;

  if constexpr (Enc::value_sign == SignMethod::Explicit) {
    IsNegative = (RawSign != 0);
    MagExp = extractField(Bits, Fmt::exp_offset, Fmt::exp_bits);
    MagMant = extractField(Bits, Fmt::sig_offset, Fmt::sig_bits);
  } else if constexpr (Enc::value_sign == SignMethod::RadixComplement) {
    IsNegative = (RawSign != 0);
    if (IsNegative) {
      BitsType Positive;
      if constexpr (TotalBits < int(sizeof(BitsType) * 8)) {
        constexpr BitsType WordMask = (BitsType{1} << TotalBits) - 1;
        Positive = (WordMask - Bits + 1) & WordMask;
      } else {
        Positive = ~Bits + 1;
      }
      MagExp = extractField(Positive, Fmt::exp_offset, Fmt::exp_bits);
      MagMant =
          extractField(Positive, Fmt::sig_offset, Fmt::sig_bits);
    } else {
      MagExp = extractField(Bits, Fmt::exp_offset, Fmt::exp_bits);
      MagMant = extractField(Bits, Fmt::sig_offset, Fmt::sig_bits);
    }
  } else if constexpr (Enc::value_sign == SignMethod::DiminishedRadixComplement) {
    IsNegative = (RawSign != 0);
    if (IsNegative) {
      constexpr BitsType ExpMask = (BitsType{1} << Fmt::exp_bits) - 1;
      constexpr BitsType MantMask = (BitsType{1} << Fmt::sig_bits) - 1;
      MagExp =
          extractField(Bits, Fmt::exp_offset, Fmt::exp_bits) ^ ExpMask;
      MagMant =
          extractField(Bits, Fmt::sig_offset, Fmt::sig_bits) ^
          MantMask;
    } else {
      MagExp = extractField(Bits, Fmt::exp_offset, Fmt::exp_bits);
      MagMant = extractField(Bits, Fmt::sig_offset, Fmt::sig_bits);
    }
  }

  // Phase 3: Check for special values identified by field values

  constexpr BitsType ExpMax = (BitsType{1} << Fmt::exp_bits) - 1;

  if constexpr (Enc::inf_encoding == InfEncoding::ReservedExponent) {
    if constexpr (Fmt::implicit_digit) {
      if (MagExp == ExpMax && MagMant == 0) {
        mpfr_set_inf(Result, IsNegative ? -1 : +1);
        return Result;
      }
    } else {
      constexpr BitsType JBit = BitsType{1} << (Fmt::sig_bits - 1);
      constexpr BitsType FracMask = JBit - 1;
      if (MagExp == ExpMax && (MagMant & FracMask) == 0) {
        mpfr_set_inf(Result, IsNegative ? -1 : +1);
        return Result;
      }
    }
  }

  if constexpr (Enc::nan_encoding == NanEncoding::ReservedExponent) {
    if constexpr (Fmt::implicit_digit) {
      if (MagExp == ExpMax && MagMant != 0) {
        mpfr_set_nan(Result);
        return Result;
      }
    } else {
      if (MagExp == ExpMax && MagMant != 0) {
        mpfr_set_nan(Result);
        return Result;
      }
    }
  }

  // Phase 4: Zero detection

  if (MagExp == 0 && MagMant == 0) {
    if constexpr (Enc::negative_zero == NegativeZero::Exists) {
      mpfr_set_zero(Result, IsNegative ? -1 : +1);
    } else {
      mpfr_set_zero(Result, +1);
    }
    return Result;
  }

  // Phase 5: Decode finite value (normal or denormal)

  constexpr int Bias = FloatType::number::exponent_bias;
  mpfr_exp_t Exponent;
  BitsType Mantissa;

  if constexpr (Fmt::implicit_digit) {
    if (MagExp == 0) {
      // Denormal (implicit-bit format). If the Number flushes input
      // denormals, replace with signed zero.
      if constexpr (Enc::denormal_mode == DenormalMode::FlushInputs ||
                    Enc::denormal_mode == DenormalMode::FlushBoth) {
        mpfr_set_zero(Result, (IsNegative && Enc::negative_zero ==
                                                  NegativeZero::Exists)
                                  ? -1
                                  : +1);
        return Result;
      }
      Exponent = 1 - Bias - Fmt::sig_bits;
      Mantissa = MagMant;
    } else {
      Exponent = static_cast<mpfr_exp_t>(static_cast<int>(MagExp)) - Bias -
                 Fmt::sig_bits;
      Mantissa = (BitsType{1} << Fmt::sig_bits) | MagMant;
    }
  } else {
    if (MagExp == 0) {
      Exponent = 1 - Bias - (Fmt::sig_bits - 1);
    } else {
      Exponent = static_cast<mpfr_exp_t>(static_cast<int>(MagExp)) - Bias -
                 (Fmt::sig_bits - 1);
    }
    Mantissa = MagMant;
  }

  mpz_t Z;
  mpz_init(Z);
  detail::bitsToMpz(Z, Mantissa);
  mpfr_set_z_2exp(Result, Z, Exponent, MPFR_RNDN);
  mpz_clear(Z);

  if (IsNegative) {
    mpfr_neg(Result, Result, MPFR_RNDN);
  }

  return Result;
}

// ===================================================================
// Exact arithmetic operations at 256-bit precision
// ===================================================================

// The Rounding policy is threaded through as an MPFR mode because of
// IEEE 754 §6.3: an exact zero sum is -0 under roundTowardNegative
// and +0 under every other mode, and MPFR applies that rule at the
// operation, not at the later format-rounding step. Only RNDD ever
// differs from RNDN here — the numeric rounding at 256-bit working
// precision is floor in both steps for TowardNegative, and floor is
// idempotent across precisions, so no double-rounding hazard.
template <typename Rnd> constexpr mpfr_rnd_t mpfrExactOpMode() {
  return std::is_same_v<Rnd, rounding::TowardNegative> ? MPFR_RNDD
                                                       : MPFR_RNDN;
}

inline MpfrFloat mpfrExactOp(Op Operation, const MpfrFloat &A,
                             const MpfrFloat &B,
                             mpfr_rnd_t Mode = MPFR_RNDN) {
  MpfrFloat Result;
  switch (Operation) {
  case Op::Add: mpfr_add(Result, A, B, Mode); break;
  case Op::Sub: mpfr_sub(Result, A, B, Mode); break;
  case Op::Mul: mpfr_mul(Result, A, B, Mode); break;
  case Op::Div: mpfr_div(Result, A, B, Mode); break;
  case Op::Rem: mpfr_remainder(Result, A, B, Mode); break;
  default: break;
  }
  return Result;
}

// ===================================================================
// Exact unary operations at 256-bit precision
// ===================================================================

inline MpfrFloat mpfrExactUnaryOp(Op Operation, const MpfrFloat &A) {
  MpfrFloat Result;
  switch (Operation) {
  case Op::Sqrt: mpfr_sqrt(Result, A, MPFR_RNDN); break;
  case Op::Neg: mpfr_neg(Result, A, MPFR_RNDN); break;
  case Op::Abs: mpfr_abs(Result, A, MPFR_RNDN); break;
  default: break;
  }
  return Result;
}

// ===================================================================
// Exact ternary operations at 256-bit precision
// ===================================================================

inline MpfrFloat mpfrExactTernaryOp(Op Operation, const MpfrFloat &A,
                                    const MpfrFloat &B, const MpfrFloat &C) {
  MpfrFloat Result;
  switch (Operation) {
  case Op::MulAdd: mpfr_fma(Result, A, B, C, MPFR_RNDN); break;
  default: break;
  }
  return Result;
}

// ===================================================================
// mpfrRoundToFormat — Round an MPFR value to a Number+Layout bit pattern
// ===================================================================
//
// This is the "round" and "pack" steps of the design's operation
// pipeline (unpack → compute → round → pack). The MPFR value is
// assumed to carry the exact result of some Compute step at
// arbitrary precision; this function reduces it to the target
// format's storage bits.
//
// Structure:
//
//   1. Compile-time constants come out of Number and Layout.
//   2. Two bit-level helpers (MaskWidth, ApplySign) hide the
//      value_sign encoding.
//   3. Three category-emitters (EmitNan, EmitInfOrSaturate,
//      EmitZero) produce the bit pattern for a given category.
//      They're called from both the dispatch head and the
//      overflow / underflow paths of the finite branch.
//   4. RoundToInteger is the shared rounding kernel: |Val| scaled
//      by 2^shift, round-to-nearest, read out as an integer.
//   5. The main pipeline dispatches on category, then rounds the
//      finite case in either the normal or subnormal range,
//      applies output-denormal flush, checks the IntegerExtremes
//      overflow-into-Inf collision, and applies value_sign.

template <typename FloatType>
typename FloatType::storage_type mpfrRoundToFormat(const MpfrFloat &Val) {
  using Fmt = typename FloatType::layout;
  using Num = typename FloatType::number;
  using Rnd = typename FloatType::rounding;
  using BitsType = typename FloatType::storage_type;

  // The Rounding policies supported by the oracle so far. ToOdd
  // and ToNearestTiesAway don't have direct MPFR analogs and are
  // not yet needed; extending is a matter of custom post-rint
  // adjustment.
  static_assert(std::is_same_v<Rnd, rounding::TowardZero> ||
                    std::is_same_v<Rnd, rounding::ToNearestTiesToEven> ||
                    std::is_same_v<Rnd, rounding::TowardPositive> ||
                    std::is_same_v<Rnd, rounding::TowardNegative>,
                "mpfrRoundToFormat only supports TowardZero, "
                "ToNearestTiesToEven, TowardPositive, TowardNegative");

  // -- Compile-time constants derived from Number and Layout -----
  constexpr int TotalBits = Fmt::total_bits;
  constexpr int MantBits = Fmt::sig_bits;
  constexpr int ExpBits = Fmt::exp_bits;
  constexpr int Bias = Num::exponent_bias;
  constexpr BitsType ExpAllOnes = (BitsType{1} << ExpBits) - 1;
  constexpr BitsType MantMask = (BitsType{1} << MantBits) - 1;
  constexpr BitsType SignBit = BitsType{1} << Fmt::sign_offset;

  // The largest biased exponent finite values may use. When either
  // NaN or Inf sits at ReservedExponent, one exponent value is
  // reserved and finites cap at ExpAllOnes - 1.
  constexpr int MaxBiasedExp = [] {
    if constexpr (Num::nan_encoding == NanEncoding::ReservedExponent ||
                  Num::inf_encoding == InfEncoding::ReservedExponent)
      return (1 << ExpBits) - 2;
    else
      return (1 << ExpBits) - 1;
  }();

  // Precision of the rounded integer significand. For implicit-digit
  // formats the semantic significand is MantBits + 1, and the
  // rounded integer ranges over [2^RoundingMantBits, 2^(RoundingMantBits+1))
  // for normals — so RoundingMantBits == MantBits. For explicit-J-bit
  // formats the leading digit is stored, so RoundingMantBits == MantBits - 1.
  constexpr int RoundingMantBits =
      Fmt::implicit_digit ? MantBits : (MantBits - 1);

  constexpr int EminIeee = 1 - Bias;

  // -- Bit-level helpers -----------------------------------------

  // Mask a computed value to the storage word's actual width.
  // Necessary when storage_type is wider than the format (e.g., a
  // 12-bit padded format stored in uint16_t).
  auto MaskWidth = [](BitsType b) -> BitsType {
    return b & opine::maskLow<BitsType>(TotalBits);
  };

  // Apply the Number's value_sign to a positive-magnitude bit
  // pattern. Explicit sets the sign bit; RadixComplement negates
  // the whole word (two's complement); DiminishedRadixComplement
  // one's-complements it (CDC 6600).
  auto ApplySign = [&](BitsType positive, bool negative) -> BitsType {
    if (!negative)
      return positive;
    if constexpr (Num::value_sign == SignMethod::Explicit)
      return positive | SignBit;
    else if constexpr (Num::value_sign == SignMethod::RadixComplement)
      return MaskWidth((~positive) + BitsType{1});
    else if constexpr (Num::value_sign ==
                       SignMethod::DiminishedRadixComplement)
      return MaskWidth(~positive);
    return positive;
  };

  // -- Category-specific bit-pattern producers -------------------

  // Canonical NaN bit pattern for the Number's nan_encoding.
  // ReservedExponent → all-ones exponent + MSB of stored sig (qNaN).
  // TrapValue → 0x80…0. NegativeZeroBitPattern → SignBit.
  // None → +0 as a best effort (upstream must not produce NaN in
  // formats that can't represent it).
  auto EmitNan = [&]() -> BitsType {
    if constexpr (Num::nan_encoding == NanEncoding::ReservedExponent) {
      if constexpr (Fmt::implicit_digit)
        return (BitsType(ExpAllOnes) << Fmt::exp_offset) |
               (BitsType{1} << (MantBits - 1));
      constexpr BitsType JBit = BitsType{1} << (MantBits - 1);
      constexpr BitsType QBit = BitsType{1} << (MantBits - 2);
      return (BitsType(ExpAllOnes) << Fmt::exp_offset) | JBit | QBit;
    } else if constexpr (Num::nan_encoding == NanEncoding::TrapValue) {
      return BitsType{1} << (TotalBits - 1);
    } else if constexpr (Num::nan_encoding ==
                         NanEncoding::NegativeZeroBitPattern) {
      return SignBit;
    }
    return BitsType{0};
  };

  // Positive-magnitude +Inf. For None, this returns the max-finite
  // bit pattern instead — inf_encoding=None means overflow
  // saturates rather than emitting Inf.
  auto PosInfBits = [&]() -> BitsType {
    if constexpr (Num::inf_encoding == InfEncoding::ReservedExponent) {
      if constexpr (Fmt::implicit_digit)
        return BitsType(ExpAllOnes) << Fmt::exp_offset;
      constexpr BitsType JBit = BitsType{1} << (MantBits - 1);
      return (BitsType(ExpAllOnes) << Fmt::exp_offset) | JBit;
    } else if constexpr (Num::inf_encoding == InfEncoding::IntegerExtremes) {
      return (BitsType{1} << (TotalBits - 1)) - 1;
    } else {
      // None: saturate. Under this branch MaxBiasedExp is
      // ExpAllOnes (no NaN or Inf uses ReservedExponent).
      return (BitsType(MaxBiasedExp) << Fmt::exp_offset) |
             (MantMask << Fmt::sig_offset);
    }
  };

  auto EmitInfOrSaturate = [&](bool negative) -> BitsType {
    return ApplySign(PosInfBits(), negative);
  };

  // Positive-magnitude largest-finite pattern.
  auto MaxFiniteBits = [&]() -> BitsType {
    if constexpr (Num::inf_encoding == InfEncoding::IntegerExtremes)
      return ((BitsType{1} << (TotalBits - 1)) - 1) - 1; // one below +Inf
    // ReservedExponent (and None, where PosInfBits already
    // saturates): max biased exponent, all-ones significand. For
    // explicit-J-bit formats that is J=1 plus an all-ones fraction —
    // the same all-ones stored field.
    return (BitsType(MaxBiasedExp) << Fmt::exp_offset) |
           (MantMask << Fmt::sig_offset);
  };

  // Finite result that overflowed the format. IEEE 754 §7.4: the
  // rounding mode decides between Inf and the largest finite value.
  // (True infinities from the exact computation bypass this and
  // always emit Inf via EmitInfOrSaturate.)
  auto EmitOverflow = [&](bool negative) -> BitsType {
    if constexpr (Num::inf_encoding != InfEncoding::None) {
      if (!opine::detail::overflowRoundsToInf<Rnd>(negative))
        return ApplySign(MaxFiniteBits(), negative);
    }
    return EmitInfOrSaturate(negative);
  };

  // Signed zero. -0 exists only when the Number opts in; for those
  // formats the encoding is Explicit → SignBit, DRC → all-ones word
  // (CDC 6600). RadixComplement + Exists is excluded structurally.
  auto EmitZero = [&](bool negative) -> BitsType {
    if (negative && Num::negative_zero == NegativeZero::Exists) {
      if constexpr (Num::value_sign == SignMethod::Explicit)
        return SignBit;
      if constexpr (Num::value_sign == SignMethod::DiminishedRadixComplement)
        return MaskWidth(~BitsType{0});
    }
    return BitsType{0};
  };

  // -- Rounding kernel -------------------------------------------

  // Translate the Type's Rounding policy to an MPFR rounding-mode
  // constant, applied to the abs value. Directed modes flip when
  // the signed value is negative — rounding a negative value
  // "toward +Inf" means rounding |value| toward zero, and vice
  // versa. Round-to-nearest modes are sign-symmetric.
  auto AbsRoundingMode = [](bool negative) -> mpfr_rnd_t {
    if constexpr (std::is_same_v<Rnd, rounding::TowardZero>)
      return MPFR_RNDZ;
    else if constexpr (std::is_same_v<Rnd, rounding::ToNearestTiesToEven>)
      return MPFR_RNDN;
    else if constexpr (std::is_same_v<Rnd, rounding::TowardPositive>)
      return negative ? MPFR_RNDZ : MPFR_RNDU;
    else if constexpr (std::is_same_v<Rnd, rounding::TowardNegative>)
      return negative ? MPFR_RNDU : MPFR_RNDZ;
    return MPFR_RNDN;
  };

  // Multiply |Val| by 2^shift, round to nearest integer with the
  // given mode, return as an unsigned bitfield. Callers pick shift
  // to place the rounded integer at the desired precision, and
  // mode via AbsRoundingMode(sign_of_Val).
  auto RoundToInteger = [&](int shift, mpfr_rnd_t mode) -> BitsType {
    MpfrFloat Scaled;
    mpfr_abs(Scaled, Val, MPFR_RNDN);
    mpfr_mul_2si(Scaled, Scaled, shift, MPFR_RNDN);
    mpfr_rint(Scaled, Scaled, mode);
    mpz_t Z;
    mpz_init(Z);
    mpfr_get_z(Z, Scaled, MPFR_RNDN);
    BitsType r = detail::mpzToBits<BitsType>(Z);
    mpz_clear(Z);
    return r;
  };

  // -- Category dispatch -----------------------------------------

  if (Val.isNan())
    return EmitNan();
  if (Val.isInf())
    return EmitInfOrSaturate(Val.isNegative());
  if (Val.isZero())
    return EmitZero(Val.isNegative());

  // -- Finite: round abs(Val), then reassemble the fields --------

  const bool Negative = mpfr_signbit(Val) != 0;
  const int IeeeExp = int(mpfr_get_exp(Val)) - 1;
  const mpfr_rnd_t AbsRnd = AbsRoundingMode(Negative);

  BitsType StoredExp{};
  BitsType StoredMant{};
  bool UnderflowedToZero = false;

  if (IeeeExp >= EminIeee) {
    // Normal range. Scale so the rounded integer significand lands
    // in [2^RoundingMantBits, 2^(RoundingMantBits+1)).
    BitsType IntSig = RoundToInteger(RoundingMantBits - IeeeExp, AbsRnd);

    // Round-up-into-next-binade (e.g. 1.111… → 10.0).
    int ExpAdj = IeeeExp;
    if (IntSig >= (BitsType{1} << (RoundingMantBits + 1))) {
      ExpAdj++;
      IntSig >>= 1;
    }

    const int BiasedExp = ExpAdj + Bias;
    if (BiasedExp > MaxBiasedExp)
      return EmitOverflow(Negative);

    StoredExp = BitsType(BiasedExp);
    StoredMant = IntSig & MantMask;
  } else {
    // Subnormal range. Scale so the rounded integer is the
    // subnormal mantissa (implicit-digit) or the full stored
    // significand (explicit-J-bit).
    BitsType Mant = RoundToInteger(Bias - 1 + RoundingMantBits, AbsRnd);

    if constexpr (Fmt::implicit_digit) {
      if (Mant >= (BitsType{1} << MantBits)) {
        // Rounded up into the smallest normal.
        StoredExp = 1;
        StoredMant = 0;
      } else if (Mant == 0) {
        UnderflowedToZero = true;
      } else {
        StoredExp = 0;
        StoredMant = Mant;
      }
    } else {
      // Explicit-J-bit: smallest normal has J-bit set, fraction 0.
      if (Mant >= (BitsType{1} << (MantBits - 1))) {
        StoredExp = 1;
        StoredMant = BitsType{1} << (MantBits - 1);
      } else if (Mant == 0) {
        UnderflowedToZero = true;
      } else {
        StoredExp = 0;
        StoredMant = Mant;
      }
    }
  }

  // -- Output-denormal flush -------------------------------------
  // FlushToZero and FlushBoth collapse any subnormal *result* to
  // zero, even if it wasn't the initial underflow-to-zero case.
  if constexpr (Num::denormal_mode == DenormalMode::FlushToZero ||
                Num::denormal_mode == DenormalMode::FlushBoth) {
    if (!UnderflowedToZero && Fmt::implicit_digit && StoredExp == 0 &&
        StoredMant != 0) {
      UnderflowedToZero = true;
    }
  }

  if (UnderflowedToZero)
    return EmitZero(Negative);

  // -- Assemble positive-magnitude form and apply value_sign -----

  BitsType Positive =
      (StoredExp << Fmt::exp_offset) | (StoredMant << Fmt::sig_offset);

  // IntegerExtremes overflow collision: if the rounded (exp, sig)
  // fields, laid out in positive-magnitude form, reach the +Inf
  // bit pattern (0x7F…F), the "finite" result actually overflows.
  if constexpr (Num::inf_encoding == InfEncoding::IntegerExtremes) {
    constexpr BitsType PosInf = (BitsType{1} << (TotalBits - 1)) - 1;
    if (Positive >= PosInf)
      return EmitOverflow(Negative);
  }

  return ApplySign(Positive, Negative);
}

// ===================================================================
// MpfrAdapter — the adapter struct
// ===================================================================

template <typename FloatType> struct MpfrAdapter {
  using BitsType = typename FloatType::storage_type;

  static constexpr const char *name() { return "MPFR"; }

  TestOutput<BitsType> dispatch(Op O, BitsType A, BitsType B) const {
    MpfrFloat Ma = decodeToMpfr<FloatType>(A);
    MpfrFloat Mb = decodeToMpfr<FloatType>(B);

    // Comparison ops: direct comparison, no rounding
    switch (O) {
    case Op::Eq: return {BitsType(mpfr_equal_p(Ma, Mb) ? 1 : 0), 0};
    case Op::Lt: return {BitsType(mpfr_less_p(Ma, Mb) ? 1 : 0), 0};
    case Op::Le: return {BitsType(mpfr_lessequal_p(Ma, Mb) ? 1 : 0), 0};
    default: break;
    }

    // Arithmetic ops: compute exact, round to format
    MpfrFloat Exact = mpfrExactOp(
        O, Ma, Mb, mpfrExactOpMode<typename FloatType::rounding>());
    return {mpfrRoundToFormat<FloatType>(Exact), 0};
  }

  TestOutput<BitsType> dispatchUnary(Op O, BitsType A) const {
    using Fmt = typename FloatType::layout;
    using Num = typename FloatType::number;
    // Neg and Abs are non-computational per IEEE 754: they must not
    // decode/reencode (which would normalize unnormals) and must
    // preserve NaN. The mechanics depend on value_sign:
    //   Explicit  → toggle / clear the sign bit
    //   RadixComplement → two's complement of the whole word
    //   DiminishedRadixComplement → one's complement of the whole word
    // NaN encoded at a specific bit pattern (TrapValue for rbj,
    // NegativeZeroBitPattern for E4M3FNUZ) must be short-circuited
    // before the sign transform, or the transform would smear the
    // NaN into a finite value.
    constexpr int TotalBits = Fmt::total_bits;
    constexpr BitsType SignBit = BitsType{1} << Fmt::sign_offset;

    auto MaskWidth = [](BitsType b) -> BitsType {
      return b & opine::maskLow<BitsType>(TotalBits);
    };

    auto IsNanBitPattern = [&](BitsType b) -> bool {
      if constexpr (Num::nan_encoding == NanEncoding::TrapValue) {
        constexpr BitsType Trap = BitsType{1} << (TotalBits - 1);
        return b == Trap;
      } else if constexpr (Num::nan_encoding ==
                           NanEncoding::NegativeZeroBitPattern) {
        return b == SignBit;
      }
      // ReservedExponent NaN survives sign transformations.
      return false;
    };

    if (O == Op::Neg) {
      if (IsNanBitPattern(A))
        return {A, 0};
      if constexpr (Num::value_sign == SignMethod::Explicit)
        return {BitsType(A ^ SignBit), 0};
      else if constexpr (Num::value_sign == SignMethod::RadixComplement)
        return {MaskWidth((~A) + BitsType{1}), 0};
      else if constexpr (Num::value_sign ==
                         SignMethod::DiminishedRadixComplement)
        return {MaskWidth(~A), 0};
      return {A, 0};
    }
    if (O == Op::Abs) {
      if (IsNanBitPattern(A))
        return {A, 0};
      if constexpr (Num::value_sign == SignMethod::Explicit)
        return {BitsType(A & ~SignBit), 0};
      else if constexpr (Num::value_sign == SignMethod::RadixComplement) {
        if (A & SignBit)
          return {MaskWidth((~A) + BitsType{1}), 0};
        return {A, 0};
      } else if constexpr (Num::value_sign ==
                           SignMethod::DiminishedRadixComplement) {
        if (A & SignBit)
          return {MaskWidth(~A), 0};
        return {A, 0};
      }
      return {A, 0};
    }
    MpfrFloat Ma = decodeToMpfr<FloatType>(A);
    MpfrFloat Exact = mpfrExactUnaryOp(O, Ma);
    return {mpfrRoundToFormat<FloatType>(Exact), 0};
  }

  TestOutput<BitsType> dispatchTernary(Op O, BitsType A, BitsType B,
                                       BitsType C) const {
    MpfrFloat Ma = decodeToMpfr<FloatType>(A);
    MpfrFloat Mb = decodeToMpfr<FloatType>(B);
    MpfrFloat Mc = decodeToMpfr<FloatType>(C);
    MpfrFloat Exact = mpfrExactTernaryOp(O, Ma, Mb, Mc);
    return {mpfrRoundToFormat<FloatType>(Exact), 0};
  }
};

} // namespace opine::testing

#endif // OPINE_TESTS_HARNESS_IMPL_MPFR_HPP
