#ifndef OPINE_CORE_CLASSIFY_HPP
#define OPINE_CORE_CLASSIFY_HPP

// IEEE 754 §5.7.2 non-computational classification predicates.
//
// All quiet, all generic over every encoding via the same prologue
// arithmetic uses (unpack + input-denormal flush) — so a FlushInputs
// format's subnormal patterns classify as the zeros they decode to,
// and x87 unnormals classify by their canonical value.
//
// isSignMinus inspects the RAW sign (it is defined on bit patterns,
// including NaNs and unflushed encodings): the dedicated sign bit
// for Explicit formats, the word MSB for the complement encodings.

#include "opine/core/digits.hpp"
#include "opine/core/round_pack.hpp"

namespace opine {

template <typename T> constexpr bool isNan(typename T::storage_type bits) {
  return detail::unpackOperand<T>(bits).category == ValueCategory::NaN;
}

template <typename T>
constexpr bool isInfinite(typename T::storage_type bits) {
  return detail::unpackOperand<T>(bits).category == ValueCategory::Infinity;
}

template <typename T> constexpr bool isZero(typename T::storage_type bits) {
  return detail::unpackOperand<T>(bits).category == ValueCategory::Zero;
}

template <typename T> constexpr bool isFinite(typename T::storage_type bits) {
  const auto c = detail::unpackOperand<T>(bits).category;
  return c == ValueCategory::Finite || c == ValueCategory::Zero;
}

template <typename T>
constexpr bool isSubnormal(typename T::storage_type bits) {
  const auto u = detail::unpackOperand<T>(bits);
  return u.category == ValueCategory::Finite && u.biased_exp == 0;
}

template <typename T> constexpr bool isNormal(typename T::storage_type bits) {
  const auto u = detail::unpackOperand<T>(bits);
  return u.category == ValueCategory::Finite && u.biased_exp != 0;
}

template <typename T>
constexpr bool isSignMinus(typename T::storage_type bits) {
  using Num = typename T::number;
  using Fmt = typename T::layout;
  if constexpr (Num::value_sign == SignMethod::Explicit)
    return detail::testWordBit(bits, Fmt::sign_offset);
  else
    return detail::testWordBit(bits, Fmt::total_bits - 1);
}

} // namespace opine

#endif // OPINE_CORE_CLASSIFY_HPP
