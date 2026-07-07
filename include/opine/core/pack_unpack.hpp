#ifndef OPINE_CORE_PACK_UNPACK_HPP
#define OPINE_CORE_PACK_UNPACK_HPP

// pack / unpack for FloatingPoint Numbers with fixed-width Layouts.
//
// unpack:  storage bits → canonical UnpackedFloat
// pack:    canonical UnpackedFloat → storage bits
//
// The unpacked form is a category tag plus (for finite values) a
// sign flag, the raw biased exponent, and the semantic significand
// (with any implicit leading digit restored). This shape is
// deliberately Number-agnostic: any FloatingPoint composite
// unpacks to the same layout, which is what lets subsequent
// arithmetic steps be format-blind.
//
// This slice covers Explicit and RadixComplement value_sign only.
// DiminishedRadixComplement (CDC 6600) is deferred.

#include "opine/core/bits.hpp"
#include "opine/core/layout.hpp"
#include "opine/core/number.hpp"

namespace opine {

// -----------------------------------------------------------------
// Category tag for an unpacked value
// -----------------------------------------------------------------
enum class ValueCategory {
  Finite,
  Zero,
  Infinity,
  NaN,
};

// -----------------------------------------------------------------
// UnpackedFloat — canonical form after decode
// -----------------------------------------------------------------
// For Finite:
//   sign         : true if negative
//   biased_exp   : the raw stored exponent (0 = denormal)
//   significand  : the semantic significand digits, leading digit
//                  restored if the Layout stored it implicitly.
//                  Denormals have leading digit 0.
//
// For Zero and Infinity: only sign is meaningful.
// For NaN: neither sign nor magnitude is meaningful.
template <typename Storage> struct UnpackedFloat {
  ValueCategory category;
  bool sign;
  int biased_exp;
  Storage significand;
};

namespace detail {

template <typename Storage>
constexpr Storage extractField(Storage bits, int offset, int width) {
  if (width == 0)
    return Storage{0};
  return (bits >> offset) & ((Storage{1} << width) - 1);
}

// Negate the low TotalBits of a storage word (two's complement).
template <typename Storage>
constexpr Storage negateWord(Storage bits, int total_bits) {
  return Storage((~bits) + Storage{1}) & maskLow<Storage>(total_bits);
}

// Mask the low TotalBits of a storage word.
template <typename Storage>
constexpr Storage maskToWidth(Storage bits, int total_bits) {
  return bits & maskLow<Storage>(total_bits);
}

} // namespace detail

// -----------------------------------------------------------------
// unpack
// -----------------------------------------------------------------
template <typename T>
constexpr UnpackedFloat<typename T::storage_type>
unpack(typename T::storage_type bits) {
  using Number = typename T::number;
  using Layout = typename T::layout;
  using Storage = typename T::storage_type;

  static_assert(Number::is_composite,
                "unpack currently supports FloatingPoint composites only");

  constexpr int TotalBits = Layout::total_bits;
  constexpr Storage ExpMax = (Storage{1} << Layout::exp_bits) - 1;

  bits = detail::maskToWidth(bits, TotalBits);

  UnpackedFloat<Storage> u{};

  // ---------- Phase 1: whole-word special values ----------
  // These are identified by exact bit patterns and short-circuit
  // before field extraction (necessary because negating the trap
  // value is undefined).

  if constexpr (Number::nan_encoding == NanEncoding::TrapValue) {
    constexpr Storage TrapVal = Storage{1} << (TotalBits - 1);
    if (bits == TrapVal) {
      u.category = ValueCategory::NaN;
      return u;
    }
  }

  if constexpr (Number::inf_encoding == InfEncoding::IntegerExtremes) {
    constexpr Storage PosInf = (Storage{1} << (TotalBits - 1)) - 1;
    Storage NegInf = detail::negateWord(PosInf, TotalBits);
    if (bits == PosInf) {
      u.category = ValueCategory::Infinity;
      u.sign = false;
      return u;
    }
    if (bits == NegInf) {
      u.category = ValueCategory::Infinity;
      u.sign = true;
      return u;
    }
  }

  // ---------- Phase 2: extract sign, get positive-magnitude fields ----------

  bool sign = false;
  Storage mag_bits = bits;

  if constexpr (Number::value_sign == SignMethod::Explicit) {
    sign = (detail::extractField(bits, Layout::sign_offset, Layout::sign_bits) != 0);
    // Fields are already positive-magnitude for Explicit.
  } else if constexpr (Number::value_sign == SignMethod::RadixComplement) {
    // MSB of whole word is sign. If set, negate to positive form.
    Storage MsbMask = Storage{1} << (TotalBits - 1);
    sign = (bits & MsbMask) != 0;
    if (sign)
      mag_bits = detail::negateWord(bits, TotalBits);
  } else {
    static_assert(Number::value_sign == SignMethod::Explicit ||
                      Number::value_sign == SignMethod::RadixComplement,
                  "unpack does not yet support this value_sign");
  }

  Storage raw_exp = detail::extractField(mag_bits, Layout::exp_offset,
                                         Layout::exp_bits);
  Storage raw_sig = detail::extractField(mag_bits, Layout::sig_offset,
                                         Layout::sig_bits);

  // ---------- Phase 3: field-based special values ----------

  if constexpr (Number::nan_encoding == NanEncoding::NegativeZeroBitPattern) {
    // Only the exact sign=1, exp=0, sig=0 pattern is NaN. Detect
    // against the *raw* sign bit — not the unpacked sign flag —
    // since Explicit value_sign leaves negative zero's fields
    // as (sign=1, exp=0, sig=0).
    if (sign && raw_exp == 0 && raw_sig == 0) {
      u.category = ValueCategory::NaN;
      return u;
    }
  }

  if constexpr (Number::inf_encoding == InfEncoding::ReservedExponent) {
    if constexpr (Layout::implicit_digit) {
      if (raw_exp == ExpMax && raw_sig == 0) {
        u.category = ValueCategory::Infinity;
        u.sign = sign;
        return u;
      }
    } else {
      // Explicit leading digit: Inf requires J-bit set, fraction zero.
      constexpr Storage JBit = Storage{1} << (Layout::sig_bits - 1);
      constexpr Storage FracMask = JBit - 1;
      if (raw_exp == ExpMax && (raw_sig & FracMask) == 0 &&
          (raw_sig & JBit) != 0) {
        u.category = ValueCategory::Infinity;
        u.sign = sign;
        return u;
      }
    }
  }

  if constexpr (Number::nan_encoding == NanEncoding::ReservedExponent) {
    if constexpr (Layout::implicit_digit) {
      if (raw_exp == ExpMax && raw_sig != 0) {
        u.category = ValueCategory::NaN;
        return u;
      }
    } else {
      // For explicit-J-bit formats, any exp=max with fraction!=0
      // (regardless of J) is a NaN or pseudo-NaN.
      constexpr Storage JBit = Storage{1} << (Layout::sig_bits - 1);
      constexpr Storage FracMask = JBit - 1;
      if (raw_exp == ExpMax && (raw_sig & FracMask) != 0) {
        u.category = ValueCategory::NaN;
        return u;
      }
    }
  }

  // ---------- Phase 4: zero ----------

  if (raw_exp == 0 && raw_sig == 0) {
    u.category = ValueCategory::Zero;
    u.sign = sign;
    return u;
  }

  // ---------- Phase 5: finite (normal or denormal) ----------

  u.category = ValueCategory::Finite;
  u.sign = sign;
  u.biased_exp = static_cast<int>(raw_exp);

  if constexpr (Layout::implicit_digit) {
    // Semantic significand = stored bits, with implicit leading 1
    // for normals and leading 0 for denormals.
    if (raw_exp == 0)
      u.significand = raw_sig;
    else
      u.significand = raw_sig | (Storage{1} << Layout::sig_bits);
  } else {
    // Leading digit is stored explicitly; use as-is.
    u.significand = raw_sig;
  }

  return u;
}

// -----------------------------------------------------------------
// pack
// -----------------------------------------------------------------
template <typename T>
constexpr typename T::storage_type
pack(const UnpackedFloat<typename T::storage_type> &u) {
  using Number = typename T::number;
  using Layout = typename T::layout;
  using Storage = typename T::storage_type;

  static_assert(Number::is_composite,
                "pack currently supports FloatingPoint composites only");

  constexpr int TotalBits = Layout::total_bits;
  constexpr Storage ExpMax = (Storage{1} << Layout::exp_bits) - 1;
  constexpr Storage SigMask = (Storage{1} << Layout::sig_bits) - 1;

  // ---------- Special values ----------

  if (u.category == ValueCategory::NaN) {
    if constexpr (Number::nan_encoding == NanEncoding::TrapValue) {
      return Storage{1} << (TotalBits - 1);
    } else if constexpr (Number::nan_encoding ==
                         NanEncoding::NegativeZeroBitPattern) {
      return Storage{1} << Layout::sign_offset;
    } else if constexpr (Number::nan_encoding ==
                         NanEncoding::ReservedExponent) {
      // Canonical qNaN: exp=all-ones, MSB of stored sig set.
      Storage nan_bits = ExpMax << Layout::exp_offset;
      if constexpr (Layout::implicit_digit) {
        nan_bits |= Storage{1} << (Layout::exp_offset - 1);
      } else {
        constexpr Storage JBit = Storage{1} << (Layout::sig_bits - 1);
        constexpr Storage QBit = Storage{1} << (Layout::sig_bits - 2);
        nan_bits |= JBit | QBit;
      }
      return nan_bits;
    } else {
      return Storage{0};
    }
  }

  if (u.category == ValueCategory::Infinity) {
    if constexpr (Number::inf_encoding == InfEncoding::IntegerExtremes) {
      constexpr Storage PosInf = (Storage{1} << (TotalBits - 1)) - 1;
      if (u.sign)
        return detail::negateWord(PosInf, TotalBits);
      return PosInf;
    } else if constexpr (Number::inf_encoding == InfEncoding::ReservedExponent) {
      Storage inf_bits = ExpMax << Layout::exp_offset;
      if constexpr (!Layout::implicit_digit) {
        constexpr Storage JBit = Storage{1} << (Layout::sig_bits - 1);
        inf_bits |= JBit;
      }
      if (u.sign)
        inf_bits |= Storage{1} << Layout::sign_offset;
      return inf_bits;
    } else {
      return Storage{0};
    }
  }

  if (u.category == ValueCategory::Zero) {
    Storage zero_bits = 0;
    if (u.sign && Number::negative_zero == NegativeZero::Exists) {
      if constexpr (Number::value_sign == SignMethod::Explicit)
        zero_bits |= Storage{1} << Layout::sign_offset;
      // RadixComplement has no negative zero (enforced by static_assert
      // on FloatingPoint); DiminishedRadixComplement not covered here.
    }
    return zero_bits;
  }

  // ---------- Finite ----------

  Storage stored_sig;
  if constexpr (Layout::implicit_digit) {
    // Strip the implicit leading digit (if the value is normal;
    // denormals have leading 0 and don't need stripping).
    stored_sig = u.significand & SigMask;
  } else {
    stored_sig = u.significand & SigMask;
  }

  Storage bits =
      (Storage(u.biased_exp) << Layout::exp_offset) |
      (stored_sig << Layout::sig_offset);

  if constexpr (Number::value_sign == SignMethod::Explicit) {
    if (u.sign)
      bits |= Storage{1} << Layout::sign_offset;
    return bits;
  } else if constexpr (Number::value_sign == SignMethod::RadixComplement) {
    if (u.sign)
      bits = detail::negateWord(bits, TotalBits);
    return detail::maskToWidth(bits, TotalBits);
  } else {
    return bits;
  }
}

} // namespace opine

#endif // OPINE_CORE_PACK_UNPACK_HPP
