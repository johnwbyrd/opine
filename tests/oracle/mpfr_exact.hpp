#ifndef OPINE_TESTS_ORACLE_MPFR_EXACT_HPP
#define OPINE_TESTS_ORACLE_MPFR_EXACT_HPP

// Oracle Part 1: MPFR integration for exact mathematical results.
//
// Provides:
//   MpfrFloat        — RAII wrapper around mpfr_t
//   decodeToMpfr     — Convert any OPINE bit pattern to its exact MPFR value
//   mpfrExactOp      — Exact arithmetic at 256-bit precision
//   mpfrRoundToFormat — Round MPFR value to any IEEE 754-style format
//
// All bit-pattern logic operates on BitsType (from FloatType::storage_type).
// The ONLY fixed-width C types appear at MPFR API boundaries, bridged by
// bitsToMpz / mpzToBits helpers that work for any width.

#include <cstdint>
#include <cstring>
#include <utility>

#include <gmp.h>
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
// Width-agnostic BitsType <-> mpz_t conversion
// ===================================================================

namespace detail {

template <typename BitsType>
void bitsToMpz(mpz_t Z, BitsType Val) {
  constexpr int NumBytes = sizeof(BitsType);
  unsigned char Bytes[NumBytes];
  for (int I = 0; I < NumBytes; ++I) {
    Bytes[I] = static_cast<unsigned char>(Val & BitsType{0xFF});
    Val >>= 8;
  }
  mpz_import(Z, NumBytes, -1, 1, 0, 0, Bytes);
}

template <typename BitsType>
BitsType mpzToBits(const mpz_t Z) {
  constexpr int NumBytes = sizeof(BitsType);
  unsigned char Bytes[NumBytes] = {};
  mpz_export(Bytes, nullptr, -1, 1, 0, 0, Z);
  BitsType Val = 0;
  for (int I = NumBytes - 1; I >= 0; --I)
    Val = (Val << 8) | BitsType(Bytes[I]);
  return Val;
}

// Extract a field of `Width` bits starting at bit `Offset` from `Bits`.
template <typename BitsType>
inline constexpr BitsType extractField(BitsType Bits, int Offset, int Width) {
  if (Width == 0)
    return BitsType{0};
  return (Bits >> Offset) & ((BitsType{1} << Width) - 1);
}

} // namespace detail

// ===================================================================
// decodeToMpfr — Convert a bit pattern to its exact MPFR value
// ===================================================================

template <typename FloatType>
MpfrFloat decodeToMpfr(typename FloatType::storage_type Bits) {
  using Fmt = typename FloatType::format;
  using Enc = typename FloatType::encoding;
  using BitsType = typename FloatType::storage_type;

  constexpr int TotalBits = Fmt::total_bits;
  if constexpr (TotalBits < int(sizeof(BitsType) * 8)) {
    constexpr BitsType WordMask = (BitsType{1} << TotalBits) - 1;
    Bits &= WordMask;
  }

  MpfrFloat Result;

  // ------------------------------------------------------------------
  // Phase 1: Check for special values identified by complete bit pattern
  // ------------------------------------------------------------------

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
        detail::extractField(Bits, Fmt::sign_offset, Fmt::sign_bits);
    BitsType RawExp =
        detail::extractField(Bits, Fmt::exp_offset, Fmt::exp_bits);
    BitsType RawMant =
        detail::extractField(Bits, Fmt::mant_offset, Fmt::mant_bits);
    if (RawSign != 0 && RawExp == 0 && RawMant == 0) {
      mpfr_set_nan(Result);
      return Result;
    }
  }

  // ------------------------------------------------------------------
  // Phase 2: Determine sign and extract magnitude fields
  // ------------------------------------------------------------------

  BitsType RawSign =
      detail::extractField(Bits, Fmt::sign_offset, Fmt::sign_bits);
  bool IsNegative = false;

  BitsType MagExp;
  BitsType MagMant;

  if constexpr (Enc::sign_encoding == SignEncoding::SignMagnitude) {
    IsNegative = (RawSign != 0);
    MagExp = detail::extractField(Bits, Fmt::exp_offset, Fmt::exp_bits);
    MagMant = detail::extractField(Bits, Fmt::mant_offset, Fmt::mant_bits);
  } else if constexpr (Enc::sign_encoding == SignEncoding::TwosComplement) {
    IsNegative = (RawSign != 0);
    if (IsNegative) {
      BitsType Positive;
      if constexpr (TotalBits < int(sizeof(BitsType) * 8)) {
        constexpr BitsType WordMask = (BitsType{1} << TotalBits) - 1;
        Positive = (WordMask - Bits + 1) & WordMask;
      } else {
        Positive = ~Bits + 1;
      }
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
      constexpr BitsType ExpMask = (BitsType{1} << Fmt::exp_bits) - 1;
      constexpr BitsType MantMask = (BitsType{1} << Fmt::mant_bits) - 1;
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
  // ------------------------------------------------------------------

  constexpr BitsType ExpMax = (BitsType{1} << Fmt::exp_bits) - 1;

  // Inf must be checked before NaN for explicit-bit formats, because
  // Inf has a non-zero mantissa field (J=1, frac=0) which would
  // otherwise be caught by the NaN check (MagMant != 0).
  if constexpr (Enc::inf_encoding == InfEncoding::ReservedExponent) {
    if constexpr (Enc::has_implicit_bit) {
      if (MagExp == ExpMax && MagMant == 0) {
        mpfr_set_inf(Result, IsNegative ? -1 : +1);
        return Result;
      }
    } else {
      constexpr BitsType JBit = BitsType{1} << (Fmt::mant_bits - 1);
      constexpr BitsType FracMask = JBit - 1;
      if (MagExp == ExpMax && (MagMant & JBit) != 0 &&
          (MagMant & FracMask) == 0) {
        mpfr_set_inf(Result, IsNegative ? -1 : +1);
        return Result;
      }
    }
  }

  if constexpr (Enc::nan_encoding == NanEncoding::ReservedExponent) {
    if constexpr (Enc::has_implicit_bit) {
      if (MagExp == ExpMax && MagMant != 0) {
        mpfr_set_nan(Result);
        return Result;
      }
    } else {
      // Explicit-bit: NaN = max exp with J=1 and frac!=0, or J=0 (pseudo-NaN)
      if (MagExp == ExpMax) {
        constexpr BitsType JBit = BitsType{1} << (Fmt::mant_bits - 1);
        constexpr BitsType FracMask = JBit - 1;
        bool IsInf = (MagMant & JBit) != 0 && (MagMant & FracMask) == 0;
        if (!IsInf && MagMant != 0) {
          mpfr_set_nan(Result);
          return Result;
        }
      }
    }
  }

  // ------------------------------------------------------------------
  // Phase 4: Zero detection
  // ------------------------------------------------------------------

  if (MagExp == 0 && MagMant == 0) {
    if constexpr (Enc::negative_zero == NegativeZero::Exists) {
      mpfr_set_zero(Result, IsNegative ? -1 : +1);
    } else {
      mpfr_set_zero(Result, +1);
    }
    return Result;
  }

  // ------------------------------------------------------------------
  // Phase 5: Decode finite value (normal or denormal)
  // ------------------------------------------------------------------

  constexpr int Bias = FloatType::exponent_bias;
  mpfr_exp_t Exponent;
  BitsType Mantissa;

  if constexpr (Enc::has_implicit_bit) {
    if (MagExp == 0) {
      // Denormal: no implicit bit, minimum exponent
      Exponent = 1 - Bias - Fmt::mant_bits;
      Mantissa = MagMant;
    } else {
      // Normal: implicit leading 1
      Exponent = static_cast<mpfr_exp_t>(static_cast<int>(MagExp)) - Bias -
                 Fmt::mant_bits;
      Mantissa = (BitsType{1} << Fmt::mant_bits) | MagMant;
    }
  } else {
    // No implicit bit (e.g., extFloat80): mantissa stored explicitly
    if (MagExp == 0) {
      // Denormal: minimum exponent (same rule as implicit-bit)
      Exponent = 1 - Bias - (Fmt::mant_bits - 1);
    } else {
      Exponent = static_cast<mpfr_exp_t>(static_cast<int>(MagExp)) - Bias -
                 (Fmt::mant_bits - 1);
    }
    Mantissa = MagMant;
  }

  // Convert mantissa to mpz, then to MPFR with exponent — one path, any width
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
// mpfrRoundToFormat — Round MPFR value to any IEEE 754-style format
// ===================================================================

template <typename FloatType>
typename FloatType::storage_type mpfrRoundToFormat(const MpfrFloat &Val) {
  using Fmt = typename FloatType::format;
  using Enc = typename FloatType::encoding;
  using BitsType = typename FloatType::storage_type;
  constexpr int MantBits = Fmt::mant_bits;
  constexpr int ExpBits = Fmt::exp_bits;
  constexpr int Bias = FloatType::exponent_bias;
  constexpr BitsType ExpAllOnes = (BitsType{1} << ExpBits) - 1;
  constexpr BitsType MantMask = (BitsType{1} << MantBits) - 1;

  constexpr int MaxBiasedExp = [] {
    if constexpr (Enc::nan_encoding == NanEncoding::ReservedExponent ||
                  Enc::inf_encoding == InfEncoding::ReservedExponent)
      return (1 << ExpBits) - 2;
    else
      return (1 << ExpBits) - 1;
  }();

  // For explicit-bit formats, the effective mantissa precision for rounding
  // is one less (the J-bit is explicit, not implicit).
  constexpr int RoundingMantBits =
      Enc::has_implicit_bit ? MantBits : (MantBits - 1);

  constexpr int EminIeee = 1 - Bias;

  // --- Special values ---

  if (Val.isNan()) {
    if constexpr (Enc::nan_encoding == NanEncoding::ReservedExponent) {
      if constexpr (Enc::has_implicit_bit) {
        return (ExpAllOnes << Fmt::exp_offset) |
               (BitsType{1} << (MantBits - 1));
      } else {
        // Explicit bit: J=1, quiet bit set
        constexpr BitsType JBit = BitsType{1} << (MantBits - 1);
        constexpr BitsType QuietBit = BitsType{1} << (MantBits - 2);
        return (ExpAllOnes << Fmt::exp_offset) | JBit | QuietBit;
      }
    }
    return BitsType{0};
  }

  if (Val.isInf()) {
    if constexpr (Enc::inf_encoding == InfEncoding::ReservedExponent) {
      BitsType InfBits;
      if constexpr (Enc::has_implicit_bit) {
        InfBits = ExpAllOnes << Fmt::exp_offset;
      } else {
        constexpr BitsType JBit = BitsType{1} << (MantBits - 1);
        InfBits = (ExpAllOnes << Fmt::exp_offset) | JBit;
      }
      if (Val.isNegative())
        InfBits |= BitsType{1} << Fmt::sign_offset;
      return InfBits;
    }
    return BitsType{0};
  }

  if (Val.isZero()) {
    BitsType ZeroBits = 0;
    if (Val.isNegative() && Enc::negative_zero == NegativeZero::Exists)
      ZeroBits |= BitsType{1} << Fmt::sign_offset;
    return ZeroBits;
  }

  // --- Finite: round by scaling to integer mantissa + mpfr_rint ---

  bool Negative = mpfr_signbit(Val) != 0;
  mpfr_exp_t MpfrExp = mpfr_get_exp(Val);
  int IeeeExp = static_cast<int>(MpfrExp) - 1;

  MpfrFloat Scaled;
  mpfr_abs(Scaled, Val, MPFR_RNDN);

  BitsType StoredExp;
  BitsType StoredMant;

  if (IeeeExp >= EminIeee) {
    // Normal range
    mpfr_mul_2si(Scaled, Scaled, RoundingMantBits - IeeeExp, MPFR_RNDN);
    mpfr_rint(Scaled, Scaled, MPFR_RNDN);

    mpz_t Z;
    mpz_init(Z);
    mpfr_get_z(Z, Scaled, MPFR_RNDN);
    BitsType IntSig = detail::mpzToBits<BitsType>(Z);
    mpz_clear(Z);

    // Rounding may carry into the next exponent
    if (IntSig >= (BitsType{1} << (RoundingMantBits + 1))) {
      IeeeExp++;
      IntSig >>= 1;
    }

    int BiasedExp = IeeeExp + Bias;
    if (BiasedExp > MaxBiasedExp) {
      // Overflow to Inf
      if constexpr (Enc::inf_encoding == InfEncoding::ReservedExponent) {
        BitsType InfBits;
        if constexpr (Enc::has_implicit_bit) {
          InfBits = ExpAllOnes << Fmt::exp_offset;
        } else {
          constexpr BitsType JBit = BitsType{1} << (MantBits - 1);
          InfBits = (ExpAllOnes << Fmt::exp_offset) | JBit;
        }
        if (Negative)
          InfBits |= BitsType{1} << Fmt::sign_offset;
        return InfBits;
      }
      return BitsType{0};
    }

    StoredExp = static_cast<BitsType>(BiasedExp);
    if constexpr (Enc::has_implicit_bit) {
      StoredMant = IntSig & MantMask;
    } else {
      // Explicit bit: store the full significand including J-bit
      StoredMant = IntSig & MantMask;
    }

  } else {
    // Subnormal range
    mpfr_mul_2si(Scaled, Scaled, Bias - 1 + RoundingMantBits, MPFR_RNDN);
    mpfr_rint(Scaled, Scaled, MPFR_RNDN);

    mpz_t Z;
    mpz_init(Z);
    mpfr_get_z(Z, Scaled, MPFR_RNDN);
    BitsType Mant = detail::mpzToBits<BitsType>(Z);
    mpz_clear(Z);

    if constexpr (Enc::has_implicit_bit) {
      if (Mant >= (BitsType{1} << MantBits)) {
        StoredExp = 1;
        StoredMant = 0;
      } else if (Mant == 0) {
        BitsType ZeroBits = 0;
        if (Negative && Enc::negative_zero == NegativeZero::Exists)
          ZeroBits |= BitsType{1} << Fmt::sign_offset;
        return ZeroBits;
      } else {
        StoredExp = 0;
        StoredMant = Mant;
      }
    } else {
      // Explicit bit: subnormals have J=0
      if (Mant >= (BitsType{1} << (MantBits - 1))) {
        // Rounded up to smallest normal (J=1)
        StoredExp = 1;
        StoredMant = BitsType{1} << (MantBits - 1); // J=1, frac=0
      } else if (Mant == 0) {
        BitsType ZeroBits = 0;
        if (Negative && Enc::negative_zero == NegativeZero::Exists)
          ZeroBits |= BitsType{1} << Fmt::sign_offset;
        return ZeroBits;
      } else {
        StoredExp = 0;
        StoredMant = Mant; // J=0 for subnormals
      }
    }
  }

  BitsType Result = 0;
  if (Negative)
    Result |= BitsType{1} << Fmt::sign_offset;
  Result |= StoredExp << Fmt::exp_offset;
  Result |= StoredMant << Fmt::mant_offset;

  return Result;
}

} // namespace opine::oracle

#endif // OPINE_TESTS_ORACLE_MPFR_EXACT_HPP
