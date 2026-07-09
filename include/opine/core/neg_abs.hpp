#ifndef OPINE_CORE_NEG_ABS_HPP
#define OPINE_CORE_NEG_ABS_HPP

// IEEE 754 non-computational sign operations: neg and abs.
//
// "Non-computational" means these never round, never signal
// exceptions, and never look at the operand's magnitude. They
// only rewrite the encoded sign in place. For most formats that
// is a bit toggle or clear; for rbj / PDP-10 it is a whole-word
// complement.
//
// value_sign dispatch:
//
//   Explicit           — sign is a dedicated bit. Neg toggles it,
//                        Abs clears it. Cheapest case.
//   RadixComplement    — sign is baked into the whole-word two's
//                        complement. Neg is the whole-word negate.
//                        Abs is a conditional negate (if MSB set).
//   DiminishedRadixComplement — same idea with one's complement.
//
// nan_encoding short-circuit:
//
//   TrapValue (rbj) — the trap value (0x80…0) is its own two's
//     complement, so Neg would leave it unchanged anyway, but we
//     short-circuit explicitly to keep the intent obvious.
//   NegativeZeroBitPattern (E4M3FNUZ) — the NaN pattern IS a bit
//     with only the sign set. Naive sign-toggle would smear the
//     NaN into +0. Short-circuit.
//   ReservedExponent — the NaN's exp and sig fields survive a
//     sign-bit transform, so no special handling is needed.
//   None — no NaN inputs to worry about.

#include "opine/core/digits.hpp"
#include "opine/core/layout.hpp"
#include "opine/core/number.hpp"

namespace opine {

namespace detail {

// True when the bit pattern encodes NaN AND that NaN is a fixed
// bit pattern that a sign transform would corrupt. For
// ReservedExponent and None, this returns false — either NaN
// survives the transform, or there is no NaN.
template <typename T>
constexpr bool isFixedNanPattern(typename T::storage_type bits) {
  using Fmt = typename T::layout;
  using Num = typename T::number;
  using Storage = typename T::storage_type;
  constexpr int TotalBits = Fmt::total_bits;

  if constexpr (Num::nan_encoding == NanEncoding::TrapValue) {
    return bits == wordBit<Storage>(TotalBits - 1);
  } else if constexpr (Num::nan_encoding ==
                       NanEncoding::NegativeZeroBitPattern) {
    return bits == wordBit<Storage>(Fmt::sign_offset);
  }
  return false;
}

} // namespace detail

// -----------------------------------------------------------------
// neg
// -----------------------------------------------------------------
template <typename T>
constexpr typename T::storage_type neg(typename T::storage_type bits) {
  using Fmt = typename T::layout;
  using Num = typename T::number;
  using Storage = typename T::storage_type;
  constexpr int TotalBits = Fmt::total_bits;

  if (detail::isFixedNanPattern<T>(bits))
    return bits;

  if constexpr (Num::value_sign == SignMethod::Explicit)
    return detail::xorWords(bits, detail::wordBit<Storage>(Fmt::sign_offset));
  else if constexpr (Num::value_sign == SignMethod::RadixComplement)
    return detail::negateWordBits(bits, TotalBits);
  else if constexpr (Num::value_sign == SignMethod::DiminishedRadixComplement)
    return detail::wordNot(bits, TotalBits);
  return bits;
}

// -----------------------------------------------------------------
// abs
// -----------------------------------------------------------------
template <typename T>
constexpr typename T::storage_type abs(typename T::storage_type bits) {
  using Fmt = typename T::layout;
  using Num = typename T::number;
  using Storage = typename T::storage_type;
  constexpr int TotalBits = Fmt::total_bits;

  if (detail::isFixedNanPattern<T>(bits))
    return bits;

  if constexpr (Num::value_sign == SignMethod::Explicit) {
    return detail::andWords(
        bits, detail::wordNot(detail::wordBit<Storage>(Fmt::sign_offset),
                              TotalBits));
  } else if constexpr (Num::value_sign == SignMethod::RadixComplement) {
    if (detail::testWordBit(bits, TotalBits - 1))
      return detail::negateWordBits(bits, TotalBits);
    return bits;
  } else if constexpr (Num::value_sign ==
                       SignMethod::DiminishedRadixComplement) {
    if (detail::testWordBit(bits, TotalBits - 1))
      return detail::wordNot(bits, TotalBits);
    return bits;
  }
  return bits;
}

} // namespace opine

#endif // OPINE_CORE_NEG_ABS_HPP
