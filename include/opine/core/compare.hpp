#ifndef OPINE_CORE_COMPARE_HPP
#define OPINE_CORE_COMPARE_HPP

// IEEE 754 quiet comparison predicates: eq, lt, le.
//
// Generic over every encoding: both operands go through the same
// prologue as arithmetic (unpack + input-denormal flush, see
// round_pack.hpp) and the predicates are decided on the canonical
// unpacked form. All format knowledge — NaN encodings, trap
// values, two's-complement negation, redundant zero encodings, x87
// unnormal canonicalization — lives in unpack; nothing here
// inspects bits.
//
// The rbj two's-complement selling point (float comparison equals
// signed-integer comparison) is therefore a THEOREM about this
// implementation rather than a special-cased code path; the
// exhaustive FP8 compare tests assert it directly. Bit-level fast
// paths (signed compare for rbj, sign-magnitude compare for IEEE
// shapes) can return later as Platform specializations, which per
// the design must produce results identical to this generic form.
//
// All three predicates are quiet — they never signal exceptions.
// A NaN input makes eq, lt, and le return false. IEEE 754 also
// specifies signaling variants (compareSignaling*); those wait
// until the Exceptions axis is wired into arithmetic.

#include "opine/core/pack_unpack.hpp"
#include "opine/core/round_pack.hpp"

namespace opine {

namespace detail {

// Three-way value order of two non-NaN unpacked operands:
// negative if a < b, zero if a == b, positive if a > b.
template <typename Storage>
constexpr int compareUnpacked(const UnpackedFloat<Storage> &a,
                              const UnpackedFloat<Storage> &b) {
  // Zeros are equal regardless of sign: IEEE -0 == +0 by §5.11,
  // and formats without -0 decode the sign-set zero pattern to +0.
  const bool a_zero = a.category == ValueCategory::Zero;
  const bool b_zero = b.category == ValueCategory::Zero;
  if (a_zero && b_zero)
    return 0;
  if (a_zero)
    return b.sign ? 1 : -1;
  if (b_zero)
    return a.sign ? -1 : 1;

  // Both nonzero: differing signs decide outright.
  if (a.sign != b.sign)
    return a.sign ? -1 : 1;

  // Same sign: order by magnitude, flipped when both are negative.
  const int flip = a.sign ? -1 : 1;

  const bool a_inf = a.category == ValueCategory::Infinity;
  const bool b_inf = b.category == ValueCategory::Infinity;
  if (a_inf && b_inf)
    return 0;
  if (a_inf)
    return flip; // |a| = Inf > |b|
  if (b_inf)
    return -flip;

  // Both finite: (biased_exp, significand) lexicographic order is
  // magnitude order. Biased exponents 0 and 1 share the same
  // weight, but a denormal significand (leading digit 0) is always
  // below a normal one (leading digit 1), so the boundary orders
  // correctly too.
  if (a.biased_exp != b.biased_exp)
    return (a.biased_exp < b.biased_exp) ? -flip : flip;
  if (a.significand != b.significand)
    return (a.significand < b.significand) ? -flip : flip;
  return 0;
}

} // namespace detail

// -----------------------------------------------------------------
// eq
// -----------------------------------------------------------------
template <typename T>
constexpr bool eq(typename T::storage_type a, typename T::storage_type b) {
  const auto ua = detail::unpackOperand<T>(a);
  const auto ub = detail::unpackOperand<T>(b);
  if (ua.category == ValueCategory::NaN || ub.category == ValueCategory::NaN)
    return false;
  return detail::compareUnpacked(ua, ub) == 0;
}

// -----------------------------------------------------------------
// lt
// -----------------------------------------------------------------
template <typename T>
constexpr bool lt(typename T::storage_type a, typename T::storage_type b) {
  const auto ua = detail::unpackOperand<T>(a);
  const auto ub = detail::unpackOperand<T>(b);
  if (ua.category == ValueCategory::NaN || ub.category == ValueCategory::NaN)
    return false;
  return detail::compareUnpacked(ua, ub) < 0;
}

// -----------------------------------------------------------------
// le
// -----------------------------------------------------------------
template <typename T>
constexpr bool le(typename T::storage_type a, typename T::storage_type b) {
  const auto ua = detail::unpackOperand<T>(a);
  const auto ub = detail::unpackOperand<T>(b);
  if (ua.category == ValueCategory::NaN || ub.category == ValueCategory::NaN)
    return false;
  return detail::compareUnpacked(ua, ub) <= 0;
}

} // namespace opine

#endif // OPINE_CORE_COMPARE_HPP
