#ifndef OPINE_CORE_COMPARE_HPP
#define OPINE_CORE_COMPARE_HPP

// IEEE 754 quiet comparison predicates: eq, lt, le.
//
// Encoding-aware. The specialization keys off the Number's
// value_sign so that each format uses the cheapest correct
// comparison:
//
//   RadixComplement (rbj / PDP-10): the whole point of the encoding
//     is that signed-integer comparison equals float comparison.
//     After a NaN short-circuit, compare as signed integers of
//     total_bits width.
//
//   Explicit (IEEE, E4M3FNUZ, Relaxed, GPU-style): sign-aware
//     magnitude comparison. NaN short-circuits; ±0 are equal
//     (regardless of whether the format nominally has -0; when it
//     doesn't, the "-0 bit pattern" decodes semantically to +0 so
//     they must still compare equal). FlushInputs / FlushBoth
//     denormal handling collapses subnormal bit patterns to zero
//     before comparison, matching what the MPFR oracle does.
//
//   DiminishedRadixComplement (CDC 6600): not implemented in this
//     slice. Both +0 and -0 exist and comparison is more subtle.
//
// All three predicates are quiet — they never signal exceptions.
// A NaN input makes eq, lt, and le return false. IEEE 754 also
// specifies signaling variants (compareSignaling*); those wait
// until the Exceptions axis is wired into arithmetic.

#include "opine/core/layout.hpp"
#include "opine/core/number.hpp"

namespace opine {

namespace detail {

// True if bits encodes NaN under T's Number and Layout.
template <typename T>
constexpr bool isNanBits(typename T::storage_type bits) {
  using Fmt = typename T::layout;
  using Num = typename T::number;
  using Storage = typename T::storage_type;
  constexpr int TotalBits = Fmt::total_bits;

  if constexpr (Num::nan_encoding == NanEncoding::None) {
    return false;
  } else if constexpr (Num::nan_encoding == NanEncoding::TrapValue) {
    constexpr Storage Trap = Storage{1} << (TotalBits - 1);
    return bits == Trap;
  } else if constexpr (Num::nan_encoding ==
                       NanEncoding::NegativeZeroBitPattern) {
    constexpr Storage NanBits = Storage{1} << Fmt::sign_offset;
    return bits == NanBits;
  } else if constexpr (Num::nan_encoding == NanEncoding::ReservedExponent) {
    constexpr Storage ExpMax = (Storage{1} << Fmt::exp_bits) - 1;
    constexpr Storage SigMask = (Storage{1} << Fmt::sig_bits) - 1;
    Storage exp_field = (bits >> Fmt::exp_offset) & ExpMax;
    Storage sig_field = (bits >> Fmt::sig_offset) & SigMask;
    if constexpr (Fmt::implicit_digit) {
      return exp_field == ExpMax && sig_field != 0;
    } else {
      // Explicit J-bit: exp=max, fraction!=0 → NaN (regardless of J).
      constexpr Storage FracMask =
          (Storage{1} << (Fmt::sig_bits - 1)) - 1;
      return exp_field == ExpMax && (sig_field & FracMask) != 0;
    }
  }
  return false;
}

// True if bits encodes an implicit-digit subnormal (exp=0, sig≠0).
// Meaningful only for Explicit value_sign in this slice.
template <typename T>
constexpr bool isSubnormalBits(typename T::storage_type bits) {
  using Fmt = typename T::layout;
  using Storage = typename T::storage_type;

  if constexpr (!Fmt::implicit_digit) {
    return false;
  } else {
    constexpr Storage ExpMask = (Storage{1} << Fmt::exp_bits) - 1;
    constexpr Storage SigMask = (Storage{1} << Fmt::sig_bits) - 1;
    Storage exp_field = (bits >> Fmt::exp_offset) & ExpMask;
    Storage sig_field = (bits >> Fmt::sig_offset) & SigMask;
    return exp_field == 0 && sig_field != 0;
  }
}

// Apply input-denormal flushing per the Number's denormal_mode.
// For FlushInputs and FlushBoth, subnormal patterns become +0
// (matching what decodeToMpfr does in the oracle).
template <typename T>
constexpr typename T::storage_type
maybeFlushInput(typename T::storage_type bits) {
  using Num = typename T::number;
  if constexpr (Num::denormal_mode == DenormalMode::FlushInputs ||
                Num::denormal_mode == DenormalMode::FlushBoth) {
    if (isSubnormalBits<T>(bits))
      return typename T::storage_type{0};
  }
  return bits;
}

} // namespace detail

// -----------------------------------------------------------------
// eq
// -----------------------------------------------------------------
template <typename T>
constexpr bool eq(typename T::storage_type a, typename T::storage_type b) {
  using Fmt = typename T::layout;
  using Num = typename T::number;
  using Storage = typename T::storage_type;

  if (detail::isNanBits<T>(a) || detail::isNanBits<T>(b))
    return false;

  if constexpr (Num::value_sign == SignMethod::RadixComplement) {
    // rbj: exactly one bit pattern per non-NaN value, so bit-equal
    // implies value-equal.
    return a == b;
  } else if constexpr (Num::value_sign == SignMethod::Explicit) {
    a = detail::maybeFlushInput<T>(a);
    b = detail::maybeFlushInput<T>(b);
    if (a == b)
      return true;
    // ±0 compare equal even when the sign bits disagree. This
    // covers both IEEE (-0 == +0 by spec) and formats that
    // declare no -0 but still admit the "-0 pattern" as an input
    // (which decodes semantically to +0).
    constexpr Storage SignBit = Storage{1} << Fmt::sign_offset;
    Storage a_abs = a & ~SignBit;
    Storage b_abs = b & ~SignBit;
    return a_abs == 0 && b_abs == 0;
  }
  return a == b;
}

// -----------------------------------------------------------------
// lt
// -----------------------------------------------------------------
template <typename T>
constexpr bool lt(typename T::storage_type a, typename T::storage_type b) {
  using Fmt = typename T::layout;
  using Num = typename T::number;
  using Storage = typename T::storage_type;
  constexpr int TotalBits = Fmt::total_bits;

  if (detail::isNanBits<T>(a) || detail::isNanBits<T>(b))
    return false;

  if constexpr (Num::value_sign == SignMethod::RadixComplement) {
    // Signed-integer comparison, expressed width-independently:
    // negatives (MSB=1) are smaller than positives (MSB=0); within
    // one sign, unsigned comparison gives correct ordering.
    constexpr Storage MsbMask = Storage{1} << (TotalBits - 1);
    bool a_neg = (a & MsbMask) != 0;
    bool b_neg = (b & MsbMask) != 0;
    if (a_neg != b_neg)
      return a_neg;
    return a < b;
  } else if constexpr (Num::value_sign == SignMethod::Explicit) {
    a = detail::maybeFlushInput<T>(a);
    b = detail::maybeFlushInput<T>(b);

    constexpr Storage SignBit = Storage{1} << Fmt::sign_offset;
    Storage a_abs = a & ~SignBit;
    Storage b_abs = b & ~SignBit;

    // ±0 are neither less than nor greater than each other.
    if (a_abs == 0 && b_abs == 0)
      return false;

    bool a_neg = (a & SignBit) != 0;
    bool b_neg = (b & SignBit) != 0;

    if (a_neg != b_neg)
      return a_neg; // negative < positive
    if (a_neg)
      return a_abs > b_abs; // both negative: larger magnitude → smaller value
    return a_abs < b_abs;   // both positive: larger magnitude → larger value
  }
  return false;
}

// -----------------------------------------------------------------
// le
// -----------------------------------------------------------------
template <typename T>
constexpr bool le(typename T::storage_type a, typename T::storage_type b) {
  return eq<T>(a, b) || lt<T>(a, b);
}

} // namespace opine

#endif // OPINE_CORE_COMPARE_HPP
