#ifndef OPINE_TESTS_ORACLE_MPFR_EXACT_HPP
#define OPINE_TESTS_ORACLE_MPFR_EXACT_HPP

// Oracle Part 1: MPFR integration for exact mathematical results.
//
// Provides:
//   MpfrFloat        — RAII wrapper around mpfr_t
//   decode_to_mpfr   — Convert any OPINE bit pattern to its exact MPFR value
//   mpfr_exact_*     — Exact arithmetic at 256-bit precision
//   mpfrRoundToFormat — Round MPFR value to any IEEE 754-style format

#include <cstdint>
#include <cstring>
#include <utility>

#include <mpfr.h>

#include "opine/opine.hpp"

namespace opine::oracle {

// Working precision for exact computation. 256 bits is far more than
// enough for any format up to binary128. add/sub/mul of binary64
// inputs produce exact results at this precision; division is
// correctly rounded to 256 bits (more than enough headroom to
// determine the correctly-rounded result in any target format).
inline constexpr mpfr_prec_t ExactPrecision = 256;

// ===================================================================
// MpfrFloat — RAII wrapper around mpfr_t
// ===================================================================

class MpfrFloat {
public:
  explicit MpfrFloat(mpfr_prec_t prec = ExactPrecision) {
    mpfr_init2(Val, prec);
  }

  ~MpfrFloat() { mpfr_clear(Val); }

  // Non-copyable (mpfr_t holds heap-allocated limb data)
  MpfrFloat(const MpfrFloat &) = delete;
  MpfrFloat &operator=(const MpfrFloat &) = delete;

  // Move: steal contents, leave source in a valid NaN state
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

  // Access the underlying mpfr_t for MPFR C API calls
  mpfr_ptr get() { return Val; }
  mpfr_srcptr get() const { return Val; }
  operator mpfr_ptr() { return Val; }
  operator mpfr_srcptr() const { return Val; }

  // Special value queries
  bool isNan() const { return mpfr_nan_p(Val) != 0; }
  bool isInf() const { return mpfr_inf_p(Val) != 0; }
  bool isZero() const { return mpfr_zero_p(Val) != 0; }
  int sign() const { return mpfr_sgn(Val); }
  bool isNegative() const { return mpfr_signbit(Val) != 0; }

private:
  mpfr_t Val;
};

// ===================================================================
// Bit extraction helper
// ===================================================================

namespace detail {

// Extract a field of `Width` bits starting at bit `Offset` from `Bits`.
inline constexpr uint64_t extractField(uint64_t Bits, int Offset, int Width) {
  if (Width == 0)
    return 0;
  return (Bits >> Offset) & ((uint64_t{1} << Width) - 1);
}

} // namespace detail

// ===================================================================
// decode_to_mpfr — Convert a bit pattern to its exact MPFR value
// ===================================================================

// Decodes a raw bit pattern into an MpfrFloat representing the exact
// mathematical value. Handles all OPINE encoding types: sign-magnitude,
// two's complement, one's complement, and all special value schemes.
//
// Template parameter FloatType must be an opine::Float instantiation
// (provides format, encoding, and exponent_bias).
template <typename FloatType>
MpfrFloat decodeToMpfr(uint64_t Bits) {
  using Fmt = typename FloatType::format;
  using Enc = typename FloatType::encoding;

  constexpr int TotalBits = Fmt::total_bits;
  constexpr uint64_t WordMask =
      TotalBits >= 64 ? ~uint64_t{0} : (uint64_t{1} << TotalBits) - 1;

  Bits &= WordMask;

  MpfrFloat Result;

  // ------------------------------------------------------------------
  // Phase 1: Check for special values that are identified by their
  // complete bit pattern (before field extraction).
  // ------------------------------------------------------------------

  // Two's complement trap value NaN: the most-negative integer (0x80...0)
  if constexpr (Enc::nan_encoding == NanEncoding::TrapValue) {
    constexpr uint64_t TrapValue = uint64_t{1} << (TotalBits - 1);
    if (Bits == TrapValue) {
      mpfr_set_nan(Result);
      return Result;
    }
  }

  // Two's complement integer-extreme infinities
  if constexpr (Enc::inf_encoding == InfEncoding::IntegerExtremes) {
    // +Inf: max positive signed integer (0x7F...F)
    constexpr uint64_t PosInf = (uint64_t{1} << (TotalBits - 1)) - 1;
    // -Inf: two's complement of +Inf (0x80...1)
    constexpr uint64_t NegInf = WordMask - PosInf + 1;

    if (Bits == PosInf) {
      mpfr_set_inf(Result, +1);
      return Result;
    }
    if (Bits == NegInf) {
      mpfr_set_inf(Result, -1);
      return Result;
    }
  }

  // NegativeZeroBitPattern NaN (E4M3FNUZ): sign=1, exp=0, mant=0
  if constexpr (Enc::nan_encoding == NanEncoding::NegativeZeroBitPattern) {
    uint64_t RawSign =
        detail::extractField(Bits, Fmt::sign_offset, Fmt::sign_bits);
    uint64_t RawExp =
        detail::extractField(Bits, Fmt::exp_offset, Fmt::exp_bits);
    uint64_t RawMant =
        detail::extractField(Bits, Fmt::mant_offset, Fmt::mant_bits);
    if (RawSign == 1 && RawExp == 0 && RawMant == 0) {
      mpfr_set_nan(Result);
      return Result;
    }
  }

  // ------------------------------------------------------------------
  // Phase 2: Determine sign and extract magnitude fields.
  // ------------------------------------------------------------------

  uint64_t RawSign =
      detail::extractField(Bits, Fmt::sign_offset, Fmt::sign_bits);
  bool IsNegative = false;

  // The magnitude exponent and mantissa, after accounting for sign encoding
  uint64_t MagExp;
  uint64_t MagMant;

  if constexpr (Enc::sign_encoding == SignEncoding::SignMagnitude) {
    IsNegative = (RawSign != 0);
    MagExp = detail::extractField(Bits, Fmt::exp_offset, Fmt::exp_bits);
    MagMant = detail::extractField(Bits, Fmt::mant_offset, Fmt::mant_bits);
  } else if constexpr (Enc::sign_encoding == SignEncoding::TwosComplement) {
    IsNegative = (RawSign != 0);
    if (IsNegative) {
      // Two's complement negate the entire word to get positive form
      uint64_t Positive = (WordMask - Bits + 1) & WordMask;
      MagExp = detail::extractField(Positive, Fmt::exp_offset, Fmt::exp_bits);
      MagMant =
          detail::extractField(Positive, Fmt::mant_offset, Fmt::mant_bits);
    } else {
      MagExp = detail::extractField(Bits, Fmt::exp_offset, Fmt::exp_bits);
      MagMant = detail::extractField(Bits, Fmt::mant_offset, Fmt::mant_bits);
    }
  } else if constexpr (Enc::sign_encoding == SignEncoding::OnesComplement) {
    IsNegative = (RawSign != 0);
    if (IsNegative) {
      // One's complement: bitwise invert the exp and mant fields
      constexpr uint64_t ExpMask = (uint64_t{1} << Fmt::exp_bits) - 1;
      constexpr uint64_t MantMask = (uint64_t{1} << Fmt::mant_bits) - 1;
      MagExp =
          detail::extractField(Bits, Fmt::exp_offset, Fmt::exp_bits) ^ ExpMask;
      MagMant =
          detail::extractField(Bits, Fmt::mant_offset, Fmt::mant_bits) ^
          MantMask;
    } else {
      MagExp = detail::extractField(Bits, Fmt::exp_offset, Fmt::exp_bits);
      MagMant = detail::extractField(Bits, Fmt::mant_offset, Fmt::mant_bits);
    }
  }

  // ------------------------------------------------------------------
  // Phase 3: Check for special values identified by field values
  // (IEEE 754-style reserved exponent).
  // ------------------------------------------------------------------

  constexpr uint64_t ExpMax = (uint64_t{1} << Fmt::exp_bits) - 1;

  // Reserved exponent NaN: all-ones exponent, non-zero mantissa
  if constexpr (Enc::nan_encoding == NanEncoding::ReservedExponent) {
    if (MagExp == ExpMax && MagMant != 0) {
      mpfr_set_nan(Result);
      return Result;
    }
  }

  // Reserved exponent Inf: all-ones exponent, zero mantissa
  if constexpr (Enc::inf_encoding == InfEncoding::ReservedExponent) {
    if (MagExp == ExpMax && MagMant == 0) {
      mpfr_set_inf(Result, IsNegative ? -1 : +1);
      return Result;
    }
  }

  // ------------------------------------------------------------------
  // Phase 4: Zero detection
  // ------------------------------------------------------------------

  if (MagExp == 0 && MagMant == 0) {
    if constexpr (Enc::negative_zero == NegativeZero::Exists) {
      mpfr_set_zero(Result, IsNegative ? -1 : +1);
    } else {
      mpfr_set_zero(Result, +1); // always +0
    }
    return Result;
  }

  // ------------------------------------------------------------------
  // Phase 5: Decode finite value (normal or denormal)
  // ------------------------------------------------------------------

  constexpr int Bias = FloatType::exponent_bias;
  intmax_t Exponent;
  uintmax_t Mantissa;

  if constexpr (Enc::has_implicit_bit) {
    if (MagExp == 0) {
      // Denormal: no implicit bit, minimum exponent
      // value = (-1)^s * mant * 2^(1 - bias - mant_bits)
      Exponent = 1 - Bias - Fmt::mant_bits;
      Mantissa = MagMant;
    } else {
      // Normal: implicit leading 1
      // value = (-1)^s * (2^mant_bits + mant) * 2^(exp - bias - mant_bits)
      Exponent =
          static_cast<intmax_t>(MagExp) - Bias - Fmt::mant_bits;
      Mantissa = (uintmax_t{1} << Fmt::mant_bits) | MagMant;
    }
  } else {
    // No implicit bit (e.g., PDP10, CDC6600): mantissa is stored explicitly.
    // The leading bit of the mantissa IS the integer bit.
    // value = (-1)^s * mant * 2^(exp - bias - (mant_bits - 1))
    Exponent =
        static_cast<intmax_t>(MagExp) - Bias - (Fmt::mant_bits - 1);
    Mantissa = MagMant;
  }

  // Set result = Mantissa * 2^Exponent (exact at 256-bit precision)
  mpfr_set_uj_2exp(Result, Mantissa, Exponent, MPFR_RNDN);

  if (IsNegative) {
    mpfr_neg(Result, Result, MPFR_RNDN);
  }

  return Result;
}

// ===================================================================
// Exact arithmetic operations at 256-bit precision
// ===================================================================

enum class Op { Add, Sub, Mul, Div };

inline MpfrFloat mpfrExactOp(Op Operation, const MpfrFloat &A,
                             const MpfrFloat &B) {
  MpfrFloat Result;
  switch (Operation) {
  case Op::Add:
    mpfr_add(Result, A, B, MPFR_RNDN);
    break;
  case Op::Sub:
    mpfr_sub(Result, A, B, MPFR_RNDN);
    break;
  case Op::Mul:
    mpfr_mul(Result, A, B, MPFR_RNDN);
    break;
  case Op::Div:
    mpfr_div(Result, A, B, MPFR_RNDN);
    break;
  }
  return Result;
}

inline MpfrFloat mpfrExactAdd(const MpfrFloat &A, const MpfrFloat &B) {
  return mpfrExactOp(Op::Add, A, B);
}

inline MpfrFloat mpfrExactSub(const MpfrFloat &A, const MpfrFloat &B) {
  return mpfrExactOp(Op::Sub, A, B);
}

inline MpfrFloat mpfrExactMul(const MpfrFloat &A, const MpfrFloat &B) {
  return mpfrExactOp(Op::Mul, A, B);
}

inline MpfrFloat mpfrExactDiv(const MpfrFloat &A, const MpfrFloat &B) {
  return mpfrExactOp(Op::Div, A, B);
}

// ===================================================================
// Validation helper: round MPFR value to any IEEE 754-style format
// ===================================================================

// Round an MpfrFloat to the target FloatType with ties-to-even and return
// the bit pattern. Works for any sign-magnitude format with implicit bit
// (float16, float32, bfloat16, fp8_e5m2, fp8_e4m3, etc.).
//
// Strategy: scale |val| to the integer-mantissa domain for the appropriate
// exponent level, use mpfr_rint for correctly-rounded nearest-integer,
// then pack the bit pattern. Handles normals, subnormals, overflow to Inf,
// and underflow to zero, including all rounding boundary cases.
template <typename FloatType>
uint64_t mpfrRoundToFormat(const MpfrFloat &Val) {
  using Fmt = typename FloatType::format;
  using Enc = typename FloatType::encoding;
  constexpr int MantBits = Fmt::mant_bits;
  constexpr int ExpBits = Fmt::exp_bits;
  constexpr int Bias = FloatType::exponent_bias;
  constexpr uint64_t ExpAllOnes = (uint64_t{1} << ExpBits) - 1;
  constexpr uint64_t MantMask = (uint64_t{1} << MantBits) - 1;

  // Max biased exponent for finite values: reserved if NaN/Inf use it.
  constexpr int MaxBiasedExp = [] {
    if constexpr (Enc::nan_encoding == NanEncoding::ReservedExponent ||
                  Enc::inf_encoding == InfEncoding::ReservedExponent)
      return (1 << ExpBits) - 2;
    else
      return (1 << ExpBits) - 1;
  }();

  constexpr int EminIeee = 1 - Bias; // minimum normal IEEE exponent

  // --- Special values ---

  if (Val.isNan()) {
    if constexpr (Enc::nan_encoding == NanEncoding::ReservedExponent) {
      return (ExpAllOnes << Fmt::exp_offset) |
             (uint64_t{1} << (MantBits - 1));
    }
    return 0;
  }

  if (Val.isInf()) {
    if constexpr (Enc::inf_encoding == InfEncoding::ReservedExponent) {
      uint64_t Bits = ExpAllOnes << Fmt::exp_offset;
      if (Val.isNegative())
        Bits |= uint64_t{1} << Fmt::sign_offset;
      return Bits;
    }
    return 0;
  }

  if (Val.isZero()) {
    uint64_t Bits = 0;
    if (Val.isNegative() && Enc::negative_zero == NegativeZero::Exists)
      Bits |= uint64_t{1} << Fmt::sign_offset;
    return Bits;
  }

  // --- Finite: round by scaling to integer mantissa + mpfr_rint ---

  bool Negative = mpfr_signbit(Val) != 0;
  mpfr_exp_t MpfrExp = mpfr_get_exp(Val);
  int IeeeExp = static_cast<int>(MpfrExp) - 1;

  MpfrFloat Scaled;
  mpfr_abs(Scaled, Val, MPFR_RNDN);

  uint64_t StoredExp;
  uint64_t StoredMant;

  if (IeeeExp >= EminIeee) {
    // Normal range (may overflow to Inf via rounding cascade).
    //
    // Scale |val| * 2^(MantBits - IeeeExp) maps the significand
    // [1, 2) * 2^IeeeExp into the integer range [2^MantBits, 2^(MantBits+1)).
    // mpfr_rint gives the correctly-rounded integer significand.
    mpfr_mul_2si(Scaled, Scaled, MantBits - IeeeExp, MPFR_RNDN);
    mpfr_rint(Scaled, Scaled, MPFR_RNDN);
    uintmax_t IntSig = mpfr_get_uj(Scaled, MPFR_RNDN);

    // Rounding may carry into the next exponent (e.g., 1.111...1 → 10.0).
    if (IntSig >= (uintmax_t{1} << (MantBits + 1))) {
      IeeeExp++;
      IntSig >>= 1;
    }

    int BiasedExp = IeeeExp + Bias;
    if (BiasedExp > MaxBiasedExp) {
      // Overflow to Inf.
      if constexpr (Enc::inf_encoding == InfEncoding::ReservedExponent) {
        uint64_t Bits = ExpAllOnes << Fmt::exp_offset;
        if (Negative)
          Bits |= uint64_t{1} << Fmt::sign_offset;
        return Bits;
      }
      return 0;
    }

    StoredExp = static_cast<uint64_t>(BiasedExp);
    StoredMant = IntSig & MantMask;

  } else {
    // Subnormal range (may round up to smallest normal or down to zero).
    //
    // Scale |val| * 2^(Bias - 1 + MantBits) maps the subnormal mantissa
    // integers [1, 2^MantBits - 1] to themselves as reals.
    // mpfr_rint gives the correctly-rounded mantissa integer.
    mpfr_mul_2si(Scaled, Scaled, Bias - 1 + MantBits, MPFR_RNDN);
    mpfr_rint(Scaled, Scaled, MPFR_RNDN);
    uintmax_t Mant = mpfr_get_uj(Scaled, MPFR_RNDN);

    if (Mant >= (uintmax_t{1} << MantBits)) {
      // Rounded up to smallest normal.
      StoredExp = 1;
      StoredMant = 0;
    } else if (Mant == 0) {
      // Underflow to zero.
      uint64_t Bits = 0;
      if (Negative && Enc::negative_zero == NegativeZero::Exists)
        Bits |= uint64_t{1} << Fmt::sign_offset;
      return Bits;
    } else {
      StoredExp = 0;
      StoredMant = Mant;
    }
  }

  uint64_t Bits = 0;
  if (Negative)
    Bits |= uint64_t{1} << Fmt::sign_offset;
  Bits |= StoredExp << Fmt::exp_offset;
  Bits |= StoredMant << Fmt::mant_offset;

  return Bits;
}

} // namespace opine::oracle

#endif // OPINE_TESTS_ORACLE_MPFR_EXACT_HPP
