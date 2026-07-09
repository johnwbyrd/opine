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

// Per-format working precision. Decoding must hold the full
// significand exactly, and the compute-then-round-to-format chain
// is double-rounding-safe when the intermediate carries at least
// 2p + 2 bits; the margin on top is free. binary1024 (p = 997)
// works at 2026 bits while FP8 stays at 256.
template <typename FloatType>
inline constexpr mpfr_prec_t oraclePrecision =
    (2 * FloatType::number::significand::digit_count + 32 >
     int(ExactPrecision))
        ? mpfr_prec_t(2 * FloatType::number::significand::digit_count + 32)
        : ExactPrecision;

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
  if constexpr (opine::detail::is_digit_vector<BitsType>) {
    // Little-endian limb order, host endianness within each limb.
    mpz_import(Z, BitsType::limb_count, -1,
               sizeof(typename BitsType::limb_type), 0, 0, Val.d);
  } else {
    constexpr int NumBytes = sizeof(BitsType);
    unsigned char Bytes[NumBytes];
    for (int I = 0; I < NumBytes; ++I) {
      Bytes[I] = static_cast<unsigned char>(Val);
      if (I + 1 < NumBytes)
        Val >>= 8;
    }
    mpz_import(Z, NumBytes, -1, 1, 0, 0, Bytes);
  }
}

template <typename BitsType> BitsType mpzToBits(const mpz_t Z) {
  if constexpr (opine::detail::is_digit_vector<BitsType>) {
    BitsType Val{};
    mpz_export(Val.d, nullptr, -1, sizeof(typename BitsType::limb_type), 0, 0,
               Z);
    return Val;
  } else {
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
  Bits = opine::detail::andWords(Bits,
                                 opine::detail::wordOnes<BitsType>(TotalBits));

  MpfrFloat Result{oraclePrecision<FloatType>};

  // Phase 1: Check for special values identified by complete bit pattern

  if constexpr (Enc::nan_encoding == NanEncoding::TrapValue) {
    if (Bits == opine::detail::wordBit<BitsType>(TotalBits - 1)) {
      mpfr_set_nan(Result);
      return Result;
    }
  }

  if constexpr (Enc::inf_encoding == InfEncoding::IntegerExtremes) {
    const BitsType PosInf = opine::detail::wordOnes<BitsType>(TotalBits - 1);
    const BitsType NegInf = opine::detail::negateWordBits(PosInf, TotalBits);
    if (Bits == PosInf) {
      mpfr_set_inf(Result, +1);
      return Result;
    }
    if (Bits == NegInf) {
      mpfr_set_inf(Result, -1);
      return Result;
    }
  }

  if constexpr (Enc::nan_encoding == NanEncoding::NegativeZeroBitPattern) {
    if (opine::detail::extractIntField(Bits, Fmt::sign_offset,
                                       Fmt::sign_bits) != 0 &&
        opine::detail::extractIntField(Bits, Fmt::exp_offset, Fmt::exp_bits) ==
            0 &&
        opine::detail::isZeroWord(opine::detail::extractWordField(
            Bits, Fmt::sig_offset, Fmt::sig_bits))) {
      mpfr_set_nan(Result);
      return Result;
    }
  }

  // Phase 2: Determine sign and extract magnitude fields

  bool IsNegative = false;
  std::uint64_t MagExp = 0;
  BitsType MagMant{};

  if constexpr (Enc::value_sign == SignMethod::Explicit) {
    IsNegative = opine::detail::extractIntField(Bits, Fmt::sign_offset,
                                                Fmt::sign_bits) != 0;
    MagExp = opine::detail::extractIntField(Bits, Fmt::exp_offset,
                                            Fmt::exp_bits);
    MagMant = opine::detail::extractWordField(Bits, Fmt::sig_offset,
                                              Fmt::sig_bits);
  } else if constexpr (Enc::value_sign == SignMethod::RadixComplement) {
    IsNegative = opine::detail::testWordBit(Bits, TotalBits - 1);
    BitsType Positive =
        IsNegative ? opine::detail::negateWordBits(Bits, TotalBits) : Bits;
    MagExp = opine::detail::extractIntField(Positive, Fmt::exp_offset,
                                            Fmt::exp_bits);
    MagMant = opine::detail::extractWordField(Positive, Fmt::sig_offset,
                                              Fmt::sig_bits);
  } else if constexpr (Enc::value_sign == SignMethod::DiminishedRadixComplement) {
    IsNegative = opine::detail::extractIntField(Bits, Fmt::sign_offset,
                                                Fmt::sign_bits) != 0;
    MagExp = opine::detail::extractIntField(Bits, Fmt::exp_offset,
                                            Fmt::exp_bits);
    MagMant = opine::detail::extractWordField(Bits, Fmt::sig_offset,
                                              Fmt::sig_bits);
    if (IsNegative) {
      MagExp ^= (std::uint64_t{1} << Fmt::exp_bits) - 1;
      MagMant = opine::detail::xorWords(
          MagMant, opine::detail::wordOnes<BitsType>(Fmt::sig_bits));
    }
  }

  // Phase 3: Check for special values identified by field values

  constexpr std::uint64_t ExpMax = (std::uint64_t{1} << Fmt::exp_bits) - 1;

  if constexpr (Enc::inf_encoding == InfEncoding::ReservedExponent) {
    if constexpr (Fmt::implicit_digit) {
      if (MagExp == ExpMax && opine::detail::isZeroWord(MagMant)) {
        mpfr_set_inf(Result, IsNegative ? -1 : +1);
        return Result;
      }
    } else {
      if (MagExp == ExpMax &&
          opine::detail::isZeroWord(opine::detail::andWords(
              MagMant,
              opine::detail::wordOnes<BitsType>(Fmt::sig_bits - 1)))) {
        mpfr_set_inf(Result, IsNegative ? -1 : +1);
        return Result;
      }
    }
  }

  if constexpr (Enc::nan_encoding == NanEncoding::ReservedExponent) {
    if (MagExp == ExpMax && !opine::detail::isZeroWord(MagMant)) {
      mpfr_set_nan(Result);
      return Result;
    }
  }

  // Phase 4: Zero detection

  if (MagExp == 0 && opine::detail::isZeroWord(MagMant)) {
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
  BitsType Mantissa{};

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
      Mantissa = opine::detail::orWords(
          opine::detail::wordBit<BitsType>(Fmt::sig_bits), MagMant);
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
                             const MpfrFloat &B, mpfr_rnd_t Mode = MPFR_RNDN,
                             mpfr_prec_t Prec = ExactPrecision) {
  MpfrFloat Result{Prec};
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

inline MpfrFloat mpfrExactUnaryOp(Op Operation, const MpfrFloat &A,
                                  mpfr_prec_t Prec = ExactPrecision) {
  MpfrFloat Result{Prec};
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
                                    const MpfrFloat &B, const MpfrFloat &C,
                                    mpfr_prec_t Prec = ExactPrecision) {
  MpfrFloat Result{Prec};
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

  // -- Compile-time constants derived from Number and Layout -----
  constexpr int TotalBits = Fmt::total_bits;
  constexpr int MantBits = Fmt::sig_bits;
  constexpr int ExpBits = Fmt::exp_bits;
  constexpr int Bias = Num::exponent_bias;
  constexpr std::uint64_t ExpAllOnes = (std::uint64_t{1} << ExpBits) - 1;
  const BitsType MantMask = opine::detail::wordOnes<BitsType>(MantBits);
  const BitsType SignBit = opine::detail::wordBit<BitsType>(Fmt::sign_offset);

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
    return opine::detail::andWords(b,
                                   opine::detail::wordOnes<BitsType>(TotalBits));
  };

  // Apply the Number's value_sign to a positive-magnitude bit
  // pattern. Explicit sets the sign bit; RadixComplement negates
  // the whole word (two's complement); DiminishedRadixComplement
  // one's-complements it (CDC 6600).
  auto ApplySign = [&](BitsType positive, bool negative) -> BitsType {
    if (!negative)
      return positive;
    if constexpr (Num::value_sign == SignMethod::Explicit)
      return opine::detail::orWords(positive, SignBit);
    else if constexpr (Num::value_sign == SignMethod::RadixComplement)
      return opine::detail::negateWordBits(positive, TotalBits);
    else if constexpr (Num::value_sign ==
                       SignMethod::DiminishedRadixComplement)
      return opine::detail::wordNot(positive, TotalBits);
    return positive;
  };

  // -- Category-specific bit-pattern producers -------------------

  // Canonical NaN bit pattern for the Number's nan_encoding.
  // ReservedExponent → all-ones exponent + MSB of stored sig (qNaN).
  // TrapValue → 0x80…0. NegativeZeroBitPattern → SignBit.
  // None → +0 as a best effort (upstream must not produce NaN in
  // formats that can't represent it).
  auto EmitNan = [&]() -> BitsType {
    using opine::detail::orWords;
    using opine::detail::shiftWordLeft;
    using opine::detail::wordBit;
    using opine::detail::wordFromUint;
    if constexpr (Num::nan_encoding == NanEncoding::ReservedExponent) {
      BitsType ExpField = shiftWordLeft(wordFromUint<BitsType>(ExpAllOnes),
                                        Fmt::exp_offset);
      if constexpr (Fmt::implicit_digit)
        return orWords(ExpField,
                       wordBit<BitsType>(Fmt::sig_offset + MantBits - 1));
      return orWords(
          ExpField,
          orWords(wordBit<BitsType>(Fmt::sig_offset + MantBits - 1),
                  wordBit<BitsType>(Fmt::sig_offset + MantBits - 2)));
    } else if constexpr (Num::nan_encoding == NanEncoding::TrapValue) {
      return opine::detail::wordBit<BitsType>(TotalBits - 1);
    } else if constexpr (Num::nan_encoding ==
                         NanEncoding::NegativeZeroBitPattern) {
      return SignBit;
    }
    return BitsType{};
  };

  // Positive-magnitude +Inf. For None, this returns the max-finite
  // bit pattern instead — inf_encoding=None means overflow
  // saturates rather than emitting Inf.
  auto PosInfBits = [&]() -> BitsType {
    using opine::detail::orWords;
    using opine::detail::shiftWordLeft;
    using opine::detail::wordBit;
    using opine::detail::wordFromUint;
    if constexpr (Num::inf_encoding == InfEncoding::ReservedExponent) {
      BitsType ExpField = shiftWordLeft(wordFromUint<BitsType>(ExpAllOnes),
                                        Fmt::exp_offset);
      if constexpr (Fmt::implicit_digit)
        return ExpField;
      return orWords(ExpField,
                     wordBit<BitsType>(Fmt::sig_offset + MantBits - 1));
    } else if constexpr (Num::inf_encoding == InfEncoding::IntegerExtremes) {
      return opine::detail::wordOnes<BitsType>(TotalBits - 1);
    } else {
      // None: saturate. Under this branch MaxBiasedExp is
      // ExpAllOnes (no NaN or Inf uses ReservedExponent).
      return orWords(
          shiftWordLeft(wordFromUint<BitsType>(std::uint64_t(MaxBiasedExp)),
                        Fmt::exp_offset),
          shiftWordLeft(MantMask, Fmt::sig_offset));
    }
  };

  auto EmitInfOrSaturate = [&](bool negative) -> BitsType {
    return ApplySign(PosInfBits(), negative);
  };

  // Positive-magnitude largest-finite pattern.
  auto MaxFiniteBits = [&]() -> BitsType {
    using opine::detail::shiftWordLeft;
    using opine::detail::wordFromUint;
    if constexpr (Num::inf_encoding == InfEncoding::IntegerExtremes)
      return opine::detail::wordSubSmall(
          opine::detail::wordOnes<BitsType>(TotalBits - 1), 1); // below +Inf
    // ReservedExponent (and None, where PosInfBits already
    // saturates): max biased exponent, all-ones significand. For
    // explicit-J-bit formats that is J=1 plus an all-ones fraction —
    // the same all-ones stored field.
    return opine::detail::orWords(
        shiftWordLeft(wordFromUint<BitsType>(std::uint64_t(MaxBiasedExp)),
                      Fmt::exp_offset),
        shiftWordLeft(MantMask, Fmt::sig_offset));
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
        return opine::detail::wordOnes<BitsType>(TotalBits);
    }
    return BitsType{};
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

  // Multiply |Val| by 2^shift, round to integer with the Type's
  // rounding, return as an unsigned bitfield. Callers pick shift to
  // place the rounded integer at the desired precision, and mode
  // via AbsRoundingMode(sign_of_Val). Two modes have no mpfr_rnd_t
  // spelling and are handled directly: ties-away is mpfr_round
  // (which rounds halfway cases away from zero — up, on |Val|),
  // and round-to-odd truncates then forces an inexact result's low
  // bit to 1 (both are sign-symmetric on the magnitude).
  auto RoundToInteger = [&](int shift, mpfr_rnd_t mode) -> BitsType {
    MpfrFloat Scaled{oraclePrecision<FloatType>};
    mpfr_abs(Scaled, Val, MPFR_RNDN);
    mpfr_mul_2si(Scaled, Scaled, shift, MPFR_RNDN);
    bool ForceOdd = false;
    if constexpr (std::is_same_v<Rnd, rounding::ToNearestTiesAway>) {
      mpfr_round(Scaled, Scaled);
    } else if constexpr (std::is_same_v<Rnd, rounding::ToOdd>) {
      MpfrFloat Trunc{oraclePrecision<FloatType>};
      mpfr_rint(Trunc, Scaled, MPFR_RNDZ);
      ForceOdd = mpfr_equal_p(Trunc, Scaled) == 0;
      mpfr_set(Scaled, Trunc, MPFR_RNDN);
    } else {
      mpfr_rint(Scaled, Scaled, mode);
    }
    mpz_t Z;
    mpz_init(Z);
    mpfr_get_z(Z, Scaled, MPFR_RNDN);
    if (ForceOdd && mpz_even_p(Z))
      mpz_add_ui(Z, Z, 1); // cannot carry: the low bit was 0
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

  std::uint64_t StoredExp = 0;
  BitsType StoredMant{};
  bool UnderflowedToZero = false;

  if (IeeeExp >= EminIeee) {
    // Normal range. Scale so the rounded integer significand lands
    // in [2^RoundingMantBits, 2^(RoundingMantBits+1)).
    BitsType IntSig = RoundToInteger(RoundingMantBits - IeeeExp, AbsRnd);

    // Round-up-into-next-binade (e.g. 1.111… → 10.0).
    int ExpAdj = IeeeExp;
    if (!opine::detail::wordLess(
            IntSig, opine::detail::wordBit<BitsType>(RoundingMantBits + 1))) {
      ExpAdj++;
      IntSig = opine::detail::shiftWordRight(IntSig, 1);
    }

    const int BiasedExp = ExpAdj + Bias;
    if (BiasedExp > MaxBiasedExp)
      return EmitOverflow(Negative);

    StoredExp = std::uint64_t(BiasedExp);
    StoredMant = opine::detail::andWords(IntSig, MantMask);
  } else {
    // Subnormal range. Scale so the rounded integer is the
    // subnormal mantissa (implicit-digit) or the full stored
    // significand (explicit-J-bit).
    BitsType Mant = RoundToInteger(Bias - 1 + RoundingMantBits, AbsRnd);

    if constexpr (Fmt::implicit_digit) {
      if (!opine::detail::wordLess(Mant,
                                   opine::detail::wordBit<BitsType>(MantBits))) {
        // Rounded up into the smallest normal.
        StoredExp = 1;
        StoredMant = BitsType{};
      } else if (opine::detail::isZeroWord(Mant)) {
        UnderflowedToZero = true;
      } else {
        StoredExp = 0;
        StoredMant = Mant;
      }
    } else {
      // Explicit-J-bit: smallest normal has J-bit set, fraction 0.
      if (!opine::detail::wordLess(
              Mant, opine::detail::wordBit<BitsType>(MantBits - 1))) {
        StoredExp = 1;
        StoredMant = opine::detail::wordBit<BitsType>(MantBits - 1);
      } else if (opine::detail::isZeroWord(Mant)) {
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
        !opine::detail::isZeroWord(StoredMant)) {
      UnderflowedToZero = true;
    }
  }

  if (UnderflowedToZero)
    return EmitZero(Negative);

  // -- Assemble positive-magnitude form and apply value_sign -----

  BitsType Positive = opine::detail::orWords(
      opine::detail::shiftWordLeft(
          opine::detail::wordFromUint<BitsType>(StoredExp), Fmt::exp_offset),
      opine::detail::shiftWordLeft(StoredMant, Fmt::sig_offset));

  // IntegerExtremes overflow collision: if the rounded (exp, sig)
  // fields, laid out in positive-magnitude form, reach the +Inf
  // bit pattern (0x7F…F), the "finite" result actually overflows.
  if constexpr (Num::inf_encoding == InfEncoding::IntegerExtremes) {
    const BitsType PosInf = opine::detail::wordOnes<BitsType>(TotalBits - 1);
    if (!opine::detail::wordLess(Positive, PosInf))
      return EmitOverflow(Negative);
  }

  return ApplySign(Positive, Negative);
}

// ===================================================================
// Oracle exception flags (IEEE 754 Â§7)
// ===================================================================
// Computed independently of the library's pipeline:
//   invalid    — from the operand-category grid (InfâInf, 0ÃInf,
//                0/0, Inf/Inf).
//   divByZero  — finite nonzero Ã· Â±0 (flagged whether the format
//                emits Inf or saturates).
//   inexact    — decode(result bits) â  exact value.
//   overflow   — |round_p(exact)| exceeds the format's largest
//                finite value, where round_p rounds to the format's
//                precision at unbounded exponent range (Â§7.4).
//   underflow  — tiny AND inexact, tininess after rounding:
//                0 < |round_p(exact)| < smallest normal (Â§7.5).

// The Rounding policy as a signed MPFR mode (directed modes keep
// their sign semantics; contrast AbsRoundingMode inside
// mpfrRoundToFormat, which works on |value|).
template <typename Rnd> constexpr mpfr_rnd_t mpfrSignedMode() {
  if constexpr (std::is_same_v<Rnd, rounding::TowardZero>)
    return MPFR_RNDZ;
  else if constexpr (std::is_same_v<Rnd, rounding::TowardPositive>)
    return MPFR_RNDU;
  else if constexpr (std::is_same_v<Rnd, rounding::TowardNegative>)
    return MPFR_RNDD;
  return MPFR_RNDN;
}

// round_p(exact): reduce Y (holding the exact value) to precision P
// with unbounded exponent range in the Type's rounding mode. For
// the four MPFR-native modes this is mpfr_prec_round; ties-away and
// round-to-odd (no mpfr_rnd_t spelling) go through the same
// scale-to-integer trick as mpfrRoundToFormat, on |value| (both are
// sign-symmetric on the magnitude).
template <typename FloatType> void mpfrRoundToPrecision(MpfrFloat &Y) {
  using Rnd = typename FloatType::rounding;
  constexpr int P = FloatType::number::significand::digit_count;
  if (mpfr_nan_p(Y))
    return; // scale-to-integer below would misbehave; NaN stays NaN
  if constexpr (std::is_same_v<Rnd, rounding::ToNearestTiesAway> ||
                std::is_same_v<Rnd, rounding::ToOdd>) {
    const bool Neg = mpfr_signbit(Y) != 0;
    const long Shift = long(P) - long(mpfr_get_exp(Y));
    mpfr_abs(Y, Y, MPFR_RNDN);
    mpfr_mul_2si(Y, Y, Shift, MPFR_RNDN);
    if constexpr (std::is_same_v<Rnd, rounding::ToNearestTiesAway>) {
      mpfr_round(Y, Y);
    } else {
      MpfrFloat Trunc{mpfr_get_prec(Y)};
      mpfr_rint(Trunc, Y, MPFR_RNDZ);
      if (!mpfr_equal_p(Trunc, Y)) {
        mpz_t Z;
        mpz_init(Z);
        mpfr_get_z(Z, Trunc, MPFR_RNDN);
        if (mpz_even_p(Z))
          mpz_add_ui(Z, Z, 1);
        mpfr_set_z(Trunc, Z, MPFR_RNDN);
        mpz_clear(Z);
      }
      mpfr_set(Y, Trunc, MPFR_RNDN);
    }
    mpfr_mul_2si(Y, Y, -Shift, MPFR_RNDN);
    if (Neg)
      mpfr_neg(Y, Y, MPFR_RNDN);
  } else {
    mpfr_prec_round(Y, mpfr_prec_t(P), mpfrSignedMode<Rnd>());
  }
}

template <typename FloatType>
uint8_t mpfrFlags(Op O, const MpfrFloat &Ma, const MpfrFloat &Mb,
                  const MpfrFloat &Exact,
                  typename FloatType::storage_type ResultBits) {
  using Num = typename FloatType::number;
  using Fmt = typename FloatType::layout;
  using BitsType = typename FloatType::storage_type;
  uint8_t Flags = 0;

  const bool ANan = Ma.isNan(), BNan = Mb.isNan();
  const bool AInf = Ma.isInf(), BInf = Mb.isInf();
  const bool AZero = Ma.isZero(), BZero = Mb.isZero();

  if (!ANan && !BNan) {
    switch (O) {
    case Op::Add:
      if (AInf && BInf && Ma.isNegative() != Mb.isNegative())
        Flags |= opine::FlagInvalid;
      break;
    case Op::Sub:
      if (AInf && BInf && Ma.isNegative() == Mb.isNegative())
        Flags |= opine::FlagInvalid;
      break;
    case Op::Mul:
      if ((AInf && BZero) || (AZero && BInf))
        Flags |= opine::FlagInvalid;
      break;
    case Op::Div:
      if ((AInf && BInf) || (AZero && BZero))
        Flags |= opine::FlagInvalid;
      else if (BZero && !AInf)
        // §7.3: divideByZero needs a FINITE dividend — Inf ÷ 0 is
        // an exact infinity, no exception.
        Flags |= opine::FlagDivByZero;
      break;
    default:
      break;
    }
  }

  if (Exact.isNan())
    return Flags;

  // inexact: decode the delivered bits back and compare values.
  // Same-sign infinities compare equal; Â±0 compare equal (the sign
  // of an exact zero is a Â§6.3 rule, not an inexactness).
  MpfrFloat Back = decodeToMpfr<FloatType>(ResultBits);
  if (!mpfr_equal_p(Back, Exact))
    Flags |= opine::FlagInexact;

  if (!Exact.isInf() && !Exact.isZero()) {
    // round_p(exact): the format's precision, unbounded exponent
    // range, the Type's signed rounding mode.
    MpfrFloat Y{oraclePrecision<FloatType>};
    mpfr_set(Y, Exact, MPFR_RNDN); // exact copy (same precision)
    mpfrRoundToPrecision<FloatType>(Y);

    // Largest finite value, decoded from its bit pattern.
    // packMaxFinite is IntegerExtremes-aware (the all-ones pattern
    // is +Inf there; max finite sits one below it).
    BitsType MaxBits = opine::detail::packMaxFinite<FloatType>(false);
    MpfrFloat MaxF = decodeToMpfr<FloatType>(MaxBits);

    if (mpfr_cmpabs(Y, MaxF) > 0)
      Flags |= opine::FlagOverflow;

    // Smallest normal: 2^(1 - bias).
    MpfrFloat MinNorm{oraclePrecision<FloatType>};
    mpfr_set_ui_2exp(MinNorm, 1,
                     mpfr_exp_t(1 - FloatType::number::exponent_bias),
                     MPFR_RNDN);
    const bool Tiny = !mpfr_zero_p(Y) && mpfr_cmpabs(Y, MinNorm) < 0;
    if (Tiny && (Flags & opine::FlagInexact))
      Flags |= opine::FlagUnderflow;
  }

  return Flags;
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
    case Op::Eq:
      return {opine::detail::wordFromUint<BitsType>(mpfr_equal_p(Ma, Mb) ? 1 : 0), 0};
    case Op::Lt:
      return {opine::detail::wordFromUint<BitsType>(mpfr_less_p(Ma, Mb) ? 1 : 0), 0};
    case Op::Le:
      return {opine::detail::wordFromUint<BitsType>(mpfr_lessequal_p(Ma, Mb) ? 1 : 0), 0};
    default: break;
    }

    // Arithmetic ops: compute exact, round to format. Flags are
    // computed only for ReturnStatus Types — mirroring the library,
    // whose flag output is observable through that policy.
    MpfrFloat Exact =
        mpfrExactOp(O, Ma, Mb, mpfrExactOpMode<typename FloatType::rounding>(),
                    oraclePrecision<FloatType>);
    BitsType R = mpfrRoundToFormat<FloatType>(Exact);
    uint8_t Flags = 0;
    if constexpr (std::is_same_v<typename FloatType::exceptions,
                                 opine::exceptions::ReturnStatus>)
      Flags = mpfrFlags<FloatType>(O, Ma, Mb, Exact, R);
    return {R, Flags};
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
    const BitsType SignBit = opine::detail::wordBit<BitsType>(Fmt::sign_offset);

    auto IsNanBitPattern = [&](BitsType b) -> bool {
      if constexpr (Num::nan_encoding == NanEncoding::TrapValue) {
        return b == opine::detail::wordBit<BitsType>(TotalBits - 1);
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
        return {opine::detail::xorWords(A, SignBit), 0};
      else if constexpr (Num::value_sign == SignMethod::RadixComplement)
        return {opine::detail::negateWordBits(A, TotalBits), 0};
      else if constexpr (Num::value_sign ==
                         SignMethod::DiminishedRadixComplement)
        return {opine::detail::wordNot(A, TotalBits), 0};
      return {A, 0};
    }
    if (O == Op::Abs) {
      if (IsNanBitPattern(A))
        return {A, 0};
      if constexpr (Num::value_sign == SignMethod::Explicit)
        return {opine::detail::andWords(
                    A, opine::detail::wordNot(SignBit, TotalBits)),
                0};
      else if constexpr (Num::value_sign == SignMethod::RadixComplement) {
        if (opine::detail::testWordBit(A, TotalBits - 1))
          return {opine::detail::negateWordBits(A, TotalBits), 0};
        return {A, 0};
      } else if constexpr (Num::value_sign ==
                           SignMethod::DiminishedRadixComplement) {
        if (opine::detail::testWordBit(A, TotalBits - 1))
          return {opine::detail::wordNot(A, TotalBits), 0};
        return {A, 0};
      }
      return {A, 0};
    }
    MpfrFloat Ma = decodeToMpfr<FloatType>(A);
    MpfrFloat Exact = mpfrExactUnaryOp(O, Ma, oraclePrecision<FloatType>);
    return {mpfrRoundToFormat<FloatType>(Exact), 0};
  }

  TestOutput<BitsType> dispatchTernary(Op O, BitsType A, BitsType B,
                                       BitsType C) const {
    MpfrFloat Ma = decodeToMpfr<FloatType>(A);
    MpfrFloat Mb = decodeToMpfr<FloatType>(B);
    MpfrFloat Mc = decodeToMpfr<FloatType>(C);
    MpfrFloat Exact =
        mpfrExactTernaryOp(O, Ma, Mb, Mc, oraclePrecision<FloatType>);
    return {mpfrRoundToFormat<FloatType>(Exact), 0};
  }
};

} // namespace opine::testing

#endif // OPINE_TESTS_HARNESS_IMPL_MPFR_HPP
