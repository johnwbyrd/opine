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

template <typename BitsType> void bitsToMpz(mpz_t Z, BitsType Val) {
  constexpr int NumBytes = sizeof(BitsType);
  unsigned char Bytes[NumBytes];
  for (int I = 0; I < NumBytes; ++I) {
    Bytes[I] = static_cast<unsigned char>(Val & BitsType{0xFF});
    Val >>= 8;
  }
  mpz_import(Z, NumBytes, -1, 1, 0, 0, Bytes);
}

template <typename BitsType> BitsType mpzToBits(const mpz_t Z) {
  constexpr int NumBytes = sizeof(BitsType);
  unsigned char Bytes[NumBytes] = {};
  mpz_export(Bytes, nullptr, -1, 1, 0, 0, Z);
  BitsType Val = 0;
  for (int I = NumBytes - 1; I >= 0; --I)
    Val = (Val << 8) | BitsType(Bytes[I]);
  return Val;
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
        extractField(Bits, Fmt::mant_offset, Fmt::mant_bits);
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

  if constexpr (Enc::sign_encoding == SignEncoding::SignMagnitude) {
    IsNegative = (RawSign != 0);
    MagExp = extractField(Bits, Fmt::exp_offset, Fmt::exp_bits);
    MagMant = extractField(Bits, Fmt::mant_offset, Fmt::mant_bits);
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
      MagExp = extractField(Positive, Fmt::exp_offset, Fmt::exp_bits);
      MagMant =
          extractField(Positive, Fmt::mant_offset, Fmt::mant_bits);
    } else {
      MagExp = extractField(Bits, Fmt::exp_offset, Fmt::exp_bits);
      MagMant = extractField(Bits, Fmt::mant_offset, Fmt::mant_bits);
    }
  } else if constexpr (Enc::sign_encoding == SignEncoding::OnesComplement) {
    IsNegative = (RawSign != 0);
    if (IsNegative) {
      constexpr BitsType ExpMask = (BitsType{1} << Fmt::exp_bits) - 1;
      constexpr BitsType MantMask = (BitsType{1} << Fmt::mant_bits) - 1;
      MagExp =
          extractField(Bits, Fmt::exp_offset, Fmt::exp_bits) ^ ExpMask;
      MagMant =
          extractField(Bits, Fmt::mant_offset, Fmt::mant_bits) ^
          MantMask;
    } else {
      MagExp = extractField(Bits, Fmt::exp_offset, Fmt::exp_bits);
      MagMant = extractField(Bits, Fmt::mant_offset, Fmt::mant_bits);
    }
  }

  // Phase 3: Check for special values identified by field values

  constexpr BitsType ExpMax = (BitsType{1} << Fmt::exp_bits) - 1;

  if constexpr (Enc::inf_encoding == InfEncoding::ReservedExponent) {
    if constexpr (Enc::has_implicit_bit) {
      if (MagExp == ExpMax && MagMant == 0) {
        mpfr_set_inf(Result, IsNegative ? -1 : +1);
        return Result;
      }
    } else {
      constexpr BitsType JBit = BitsType{1} << (Fmt::mant_bits - 1);
      constexpr BitsType FracMask = JBit - 1;
      if (MagExp == ExpMax && (MagMant & FracMask) == 0) {
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

  constexpr int Bias = FloatType::exponent_bias;
  mpfr_exp_t Exponent;
  BitsType Mantissa;

  if constexpr (Enc::has_implicit_bit) {
    if (MagExp == 0) {
      Exponent = 1 - Bias - Fmt::mant_bits;
      Mantissa = MagMant;
    } else {
      Exponent = static_cast<mpfr_exp_t>(static_cast<int>(MagExp)) - Bias -
                 Fmt::mant_bits;
      Mantissa = (BitsType{1} << Fmt::mant_bits) | MagMant;
    }
  } else {
    if (MagExp == 0) {
      Exponent = 1 - Bias - (Fmt::mant_bits - 1);
    } else {
      Exponent = static_cast<mpfr_exp_t>(static_cast<int>(MagExp)) - Bias -
                 (Fmt::mant_bits - 1);
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

inline MpfrFloat mpfrExactOp(Op Operation, const MpfrFloat &A,
                             const MpfrFloat &B) {
  MpfrFloat Result;
  switch (Operation) {
  case Op::Add: mpfr_add(Result, A, B, MPFR_RNDN); break;
  case Op::Sub: mpfr_sub(Result, A, B, MPFR_RNDN); break;
  case Op::Mul: mpfr_mul(Result, A, B, MPFR_RNDN); break;
  case Op::Div: mpfr_div(Result, A, B, MPFR_RNDN); break;
  case Op::Rem: mpfr_remainder(Result, A, B, MPFR_RNDN); break;
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

    if (IntSig >= (BitsType{1} << (RoundingMantBits + 1))) {
      IeeeExp++;
      IntSig >>= 1;
    }

    int BiasedExp = IeeeExp + Bias;
    if (BiasedExp > MaxBiasedExp) {
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
    StoredMant = IntSig & MantMask;

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
      if (Mant >= (BitsType{1} << (MantBits - 1))) {
        StoredExp = 1;
        StoredMant = BitsType{1} << (MantBits - 1);
      } else if (Mant == 0) {
        BitsType ZeroBits = 0;
        if (Negative && Enc::negative_zero == NegativeZero::Exists)
          ZeroBits |= BitsType{1} << Fmt::sign_offset;
        return ZeroBits;
      } else {
        StoredExp = 0;
        StoredMant = Mant;
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
    MpfrFloat Exact = mpfrExactOp(O, Ma, Mb);
    return {mpfrRoundToFormat<FloatType>(Exact), 0};
  }

  TestOutput<BitsType> dispatchUnary(Op O, BitsType A) const {
    using Fmt = typename FloatType::format;
    // Neg and Abs are non-computational sign-bit operations per IEEE 754.
    // They must not decode/reencode (which would normalize unnormals).
    constexpr BitsType SignBit = BitsType{1} << Fmt::sign_offset;
    switch (O) {
    case Op::Neg: return {BitsType(A ^ SignBit), 0};
    case Op::Abs: return {BitsType(A & ~SignBit), 0};
    default: break;
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
