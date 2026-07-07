#ifndef OPINE_CORE_ARITH_DETAIL_HPP
#define OPINE_CORE_ARITH_DETAIL_HPP

// Shared helpers for the arithmetic pipelines (add, mul, ...):
// sticky shifting, bit scanning, and guard/round/sticky rounding
// decisions. Everything here works on an unsigned "Wide" working
// type whose bit width equals sizeof(Wide) * 8 (standard unsigned
// types, __int128, or power-of-two _BitInt widths).

#include <type_traits>

#include "opine/core/rounding.hpp"

namespace opine {
namespace detail {

// Shift a wide value right by 'shift' bits, folding any bits that
// fall off into an OR'd sticky at bit 0.
template <typename Wide>
constexpr Wide shiftRightSticky(Wide value, int shift) {
  if (shift <= 0)
    return value;
  if (shift >= int(sizeof(Wide) * 8))
    return (value != 0) ? Wide{1} : Wide{0};
  Wide mask = (Wide{1} << shift) - 1;
  Wide lost = value & mask;
  Wide shifted = value >> shift;
  if (lost != 0)
    shifted |= Wide{1};
  return shifted;
}

// Position of the highest set bit, or -1 if zero. Constexpr and
// portable.
template <typename Wide> constexpr int msbPos(Wide value) {
  int pos = -1;
  while (value) {
    pos++;
    value >>= 1;
  }
  return pos;
}

// Decide whether to round the pre-rounding integer significand up.
template <typename Rnd>
constexpr bool shouldRoundUp(bool lsb, bool guard, bool round_bit, bool sticky,
                             bool negative) {
  const bool any_low = guard || round_bit || sticky;
  if constexpr (std::is_same_v<Rnd, rounding::TowardZero>) {
    return false;
  } else if constexpr (std::is_same_v<Rnd, rounding::ToNearestTiesToEven>) {
    if (!guard)
      return false;
    if (round_bit || sticky)
      return true;
    return lsb; // exact tie → to even
  } else if constexpr (std::is_same_v<Rnd, rounding::TowardPositive>) {
    return any_low && !negative;
  } else if constexpr (std::is_same_v<Rnd, rounding::TowardNegative>) {
    return any_low && negative;
  }
  return false;
}

// Sign of an exact zero result of add. IEEE 754 §6.3: for all
// rounding modes except TowardNegative, the sum of two operands
// with opposite signs is +0; TowardNegative gives -0.
template <typename Rnd> constexpr bool exactZeroSumSign() {
  return std::is_same_v<Rnd, rounding::TowardNegative>;
}

} // namespace detail
} // namespace opine

#endif // OPINE_CORE_ARITH_DETAIL_HPP
