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
// Storage may be a scalar bits_t word (formats up to 128 bits) or
// a DigitVector of limbs (wider); every bit-level step here goes
// through the storage-word operations in digits.hpp, so one codec
// serves both.
//
// This slice covers Explicit and RadixComplement value_sign only.
// DiminishedRadixComplement (CDC 6600) is deferred.

#include "opine/core/arith_detail.hpp"
#include "opine/core/bits.hpp"
#include "opine/core/digits.hpp"
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
  static_assert(Number::exponent_base == 2,
                "non-binary exponent bases (IBM hex, decimal) are declared "
                "in the type system but not yet implemented");

  constexpr int TotalBits = Layout::total_bits;
  constexpr std::uint64_t ExpMax = (std::uint64_t{1} << Layout::exp_bits) - 1;

  bits = detail::andWords(bits, detail::wordOnes<Storage>(TotalBits));

  UnpackedFloat<Storage> u{};

  // ---------- Phase 1: whole-word special values ----------
  // These are identified by exact bit patterns and short-circuit
  // before field extraction (necessary because negating the trap
  // value is undefined).

  if constexpr (Number::nan_encoding == NanEncoding::TrapValue) {
    if (bits == detail::wordBit<Storage>(TotalBits - 1)) {
      u.category = ValueCategory::NaN;
      return u;
    }
  }

  if constexpr (Number::inf_encoding == InfEncoding::IntegerExtremes) {
    const Storage PosInf = detail::wordOnes<Storage>(TotalBits - 1);
    const Storage NegInf = detail::negateWordBits(PosInf, TotalBits);
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
    sign = detail::extractIntField(bits, Layout::sign_offset,
                                   Layout::sign_bits) != 0;
    // Fields are already positive-magnitude for Explicit.
  } else if constexpr (Number::value_sign == SignMethod::RadixComplement) {
    // MSB of whole word is sign. If set, negate to positive form.
    sign = detail::testWordBit(bits, TotalBits - 1);
    if (sign)
      mag_bits = detail::negateWordBits(bits, TotalBits);
  } else {
    static_assert(Number::value_sign == SignMethod::Explicit ||
                      Number::value_sign == SignMethod::RadixComplement,
                  "unpack does not yet support this value_sign");
  }

  const std::uint64_t raw_exp =
      detail::extractIntField(mag_bits, Layout::exp_offset, Layout::exp_bits);
  Storage raw_sig =
      detail::extractWordField(mag_bits, Layout::sig_offset, Layout::sig_bits);

  // ---------- Phase 3: field-based special values ----------

  if constexpr (Number::nan_encoding == NanEncoding::NegativeZeroBitPattern) {
    // Only the exact sign=1, exp=0, sig=0 pattern is NaN. Detect
    // against the *raw* sign bit — not the unpacked sign flag —
    // since Explicit value_sign leaves negative zero's fields
    // as (sign=1, exp=0, sig=0).
    if (sign && raw_exp == 0 && detail::isZeroWord(raw_sig)) {
      u.category = ValueCategory::NaN;
      return u;
    }
  }

  if constexpr (Number::inf_encoding == InfEncoding::ReservedExponent) {
    if constexpr (Layout::implicit_digit) {
      if (raw_exp == ExpMax && detail::isZeroWord(raw_sig)) {
        u.category = ValueCategory::Infinity;
        u.sign = sign;
        return u;
      }
    } else {
      // Explicit leading digit: max exponent with zero fraction is
      // infinity whether or not J is set — a clear J-bit here is the
      // x87 pseudo-infinity, which decodes to the same value.
      const Storage Frac =
          detail::andWords(raw_sig, detail::wordOnes<Storage>(Layout::sig_bits - 1));
      if (raw_exp == ExpMax && detail::isZeroWord(Frac)) {
        u.category = ValueCategory::Infinity;
        u.sign = sign;
        return u;
      }
    }
  }

  if constexpr (Number::nan_encoding == NanEncoding::ReservedExponent) {
    if constexpr (Layout::implicit_digit) {
      if (raw_exp == ExpMax && !detail::isZeroWord(raw_sig)) {
        u.category = ValueCategory::NaN;
        return u;
      }
    } else {
      // For explicit-J-bit formats, any exp=max with fraction!=0
      // (regardless of J) is a NaN or pseudo-NaN.
      const Storage Frac =
          detail::andWords(raw_sig, detail::wordOnes<Storage>(Layout::sig_bits - 1));
      if (raw_exp == ExpMax && !detail::isZeroWord(Frac)) {
        u.category = ValueCategory::NaN;
        return u;
      }
    }
  }

  // ---------- Phase 4: zero ----------
  // For explicit-leading-digit formats a zero significand encodes
  // zero at ANY exponent (the x87 "unnormal-zero"); for implicit
  // formats a zero stored significand with nonzero exponent is a
  // power of two.

  if (detail::isZeroWord(raw_sig) && (raw_exp == 0 || !Layout::implicit_digit)) {
    u.category = ValueCategory::Zero;
    // When the Number has no negative zero, a sign-bit-set zero
    // pattern is a redundant encoding of +0 (the fnuz NaN case was
    // already dispatched above) — decode it unsigned, matching the
    // oracle.
    u.sign = sign && Number::negative_zero == NegativeZero::Exists;
    return u;
  }

  // ---------- Phase 5: finite (normal or denormal) ----------

  u.category = ValueCategory::Finite;
  u.sign = sign;

  if constexpr (Layout::implicit_digit) {
    // Semantic significand = stored bits, with implicit leading 1
    // for normals and leading 0 for denormals.
    u.biased_exp = static_cast<int>(raw_exp);
    if (raw_exp == 0)
      u.significand = raw_sig;
    else
      u.significand =
          detail::orWords(raw_sig, detail::wordBit<Storage>(Layout::sig_bits));
  } else {
    // Leading digit is stored explicitly. Canonicalize the
    // non-canonical encodings so downstream arithmetic can rely on
    // the normal-range invariant. Unnormals (nonzero exponent, J
    // clear) renormalize upward, or shift into true subnormal form
    // when the exponent bottoms out; pseudo-denormals (exponent 0,
    // J set) become the exp=1 normal. Both rewrites preserve value —
    // biased exponents 0 and 1 share the same weight.
    constexpr int JPos = Layout::sig_bits - 1;
    Storage sig = raw_sig;
    int exp = static_cast<int>(raw_exp);
    const bool jbit = detail::testWordBit(sig, JPos);
    if (exp > 0 && !jbit) {
      int shift = JPos - detail::wordTopBit(sig);
      if (exp > shift) {
        exp -= shift;
        sig = detail::shiftWordLeft(sig, shift);
      } else {
        sig = detail::shiftWordLeft(sig, exp - 1);
        exp = 0;
      }
    } else if (exp == 0 && jbit) {
      exp = 1;
    }
    u.biased_exp = exp;
    u.significand = sig;
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
  static_assert(Number::exponent_base == 2,
                "non-binary exponent bases (IBM hex, decimal) are declared "
                "in the type system but not yet implemented");

  constexpr int TotalBits = Layout::total_bits;
  constexpr std::uint64_t ExpMax = (std::uint64_t{1} << Layout::exp_bits) - 1;

  // ---------- Special values ----------

  if (u.category == ValueCategory::NaN) {
    if constexpr (Number::nan_encoding == NanEncoding::TrapValue) {
      return detail::wordBit<Storage>(TotalBits - 1);
    } else if constexpr (Number::nan_encoding ==
                         NanEncoding::NegativeZeroBitPattern) {
      return detail::wordBit<Storage>(Layout::sign_offset);
    } else if constexpr (Number::nan_encoding ==
                         NanEncoding::ReservedExponent) {
      // Canonical qNaN: exp=all-ones, MSB of stored sig set.
      Storage nan_bits = detail::shiftWordLeft(
          detail::wordFromUint<Storage>(ExpMax), Layout::exp_offset);
      if constexpr (Layout::implicit_digit) {
        nan_bits = detail::orWords(
            nan_bits, detail::wordBit<Storage>(Layout::exp_offset - 1));
      } else {
        nan_bits = detail::orWords(
            nan_bits,
            detail::orWords(
                detail::wordBit<Storage>(Layout::sig_offset + Layout::sig_bits - 1),
                detail::wordBit<Storage>(Layout::sig_offset + Layout::sig_bits - 2)));
      }
      return nan_bits;
    } else {
      // NanEncoding::None: a NaN category is a caller precondition
      // violation — the format cannot represent it. Callers route
      // around this (e.g. Relaxed formats never produce NaN).
      // Return +0 deterministically rather than UB.
      return Storage{};
    }
  }

  if (u.category == ValueCategory::Infinity) {
    if constexpr (Number::inf_encoding == InfEncoding::IntegerExtremes) {
      const Storage PosInf = detail::wordOnes<Storage>(TotalBits - 1);
      if (u.sign)
        return detail::negateWordBits(PosInf, TotalBits);
      return PosInf;
    } else if constexpr (Number::inf_encoding == InfEncoding::ReservedExponent) {
      Storage inf_bits = detail::shiftWordLeft(
          detail::wordFromUint<Storage>(ExpMax), Layout::exp_offset);
      if constexpr (!Layout::implicit_digit) {
        inf_bits = detail::orWords(
            inf_bits,
            detail::wordBit<Storage>(Layout::sig_offset + Layout::sig_bits - 1));
      }
      if (u.sign)
        inf_bits =
            detail::orWords(inf_bits, detail::wordBit<Storage>(Layout::sign_offset));
      return inf_bits;
    } else {
      // InfEncoding::None: an Infinity category is a caller
      // precondition violation — the format cannot represent it.
      // Callers that may face an Inf-into-no-Inf format must route
      // through packInfOrSaturate (round_pack.hpp) instead. Return
      // +0 deterministically rather than UB.
      return Storage{};
    }
  }

  if (u.category == ValueCategory::Zero) {
    if (u.sign && Number::negative_zero == NegativeZero::Exists) {
      if constexpr (Number::value_sign == SignMethod::Explicit)
        return detail::wordBit<Storage>(Layout::sign_offset);
      // RadixComplement has no negative zero (enforced by static_assert
      // on FloatingPoint); DiminishedRadixComplement not covered here.
    }
    return Storage{};
  }

  // ---------- Finite ----------

  // Strip the implicit leading digit (if the value is normal;
  // denormals have leading 0 and don't need stripping). Explicit-J
  // formats store the whole significand.
  Storage stored_sig =
      detail::andWords(u.significand, detail::wordOnes<Storage>(Layout::sig_bits));

  Storage bits = detail::orWords(
      detail::shiftWordLeft(
          detail::wordFromUint<Storage>(std::uint64_t(u.biased_exp)),
          Layout::exp_offset),
      detail::shiftWordLeft(stored_sig, Layout::sig_offset));

  if constexpr (Number::value_sign == SignMethod::Explicit) {
    if (u.sign)
      bits = detail::orWords(bits, detail::wordBit<Storage>(Layout::sign_offset));
    return bits;
  } else if constexpr (Number::value_sign == SignMethod::RadixComplement) {
    if (u.sign)
      bits = detail::negateWordBits(bits, TotalBits);
    return detail::andWords(bits, detail::wordOnes<Storage>(TotalBits));
  } else {
    return bits;
  }
}

} // namespace opine

#endif // OPINE_CORE_PACK_UNPACK_HPP
