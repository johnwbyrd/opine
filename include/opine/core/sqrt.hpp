#ifndef OPINE_CORE_SQRT_HPP
#define OPINE_CORE_SQRT_HPP

// Square root for FloatingPoint composites.
//
// The kernel of the shared pipeline (see round_pack.hpp):
//
//   1. Unpack; apply input-denormal flush per Number.
//   2. Specials: NaN → NaN, +Inf → +Inf, ±0 → ±0 (§5.4.1: the sign
//      of the operand zero is preserved — sqrt(-0) is -0), and any
//      other negative operand → NaN with invalid (§7.2).
//   3. Finite positive:
//      a. Pre-normalize a denormal significand into the canonical
//         range [2^(SigBits-1), 2^SigBits) and split the unbiased
//         exponent as e = 2q + r with r in {0, 1} (floor division,
//         so negative exponents work).
//      b. Shift the significand up so its square-root carries
//         SigBits + GuardBits significant bits with the parity bit
//         r folded in: sqrtRemDigits of the shifted significand
//         yields the truncated root with its MSB exactly at the
//         working target, and the remainder is the sticky. A
//         nonzero remainder also rules out ties: sqrt of a
//         non-square is irrational, so the true result can never
//         sit exactly halfway between representable values.
//      c. The result exponent is q + Bias; the root of a value in
//         [1, 4) is in [1, 2), so no normalization shift is needed.
//   4. roundAndPack does the rest. Overflow and underflow are
//      impossible (sqrt contracts the exponent range), so the only
//      flag a finite positive can raise is inexact.
//
// No width ceiling: the working geometry is 2*(SigBits+GuardBits)
// bits of whatever limb the Platform picks. sqrtRemDigits is the
// restoring bit-serial tier — slow and obviously correct; faster
// root extraction is a Platform specialization that must produce
// identical digits.

#include "opine/core/arith_detail.hpp"
#include "opine/core/digits.hpp"
#include "opine/core/round_pack.hpp"

namespace opine {

// -----------------------------------------------------------------
// sqrt
// -----------------------------------------------------------------
template <typename T>
constexpr auto sqrt(typename T::storage_type a) {
  using Num = typename T::number;
  using Storage = typename T::storage_type;

  constexpr int SigBits = Num::significand::digit_count;
  constexpr int Bias = Num::exponent_bias;
  constexpr int GBits = detail::GuardBits;
  using DV = detail::WorkingDigits<T, 2 * (SigBits + GBits)>;

  UnpackedFloat<Storage> ua = detail::computeOperand<T>(a);

  // ---------- Special value dispatch ----------

  if (ua.category == ValueCategory::NaN)
    return detail::deliver<T>(
        detail::packSpecial<T>(ValueCategory::NaN, false), FlagNone);

  if (ua.category == ValueCategory::Zero)
    // §5.4.1: sqrt(±0) = ±0, operand sign preserved.
    return detail::deliver<T>(
        detail::packSpecial<T>(ValueCategory::Zero, ua.sign), FlagNone);

  if (ua.category == ValueCategory::Infinity) {
    if (!ua.sign)
      return detail::deliver<T>(
          detail::packSpecial<T>(ValueCategory::Infinity, false), FlagNone);
    return detail::deliver<T>(
        detail::packSpecial<T>(ValueCategory::NaN, false), FlagInvalid);
  }

  if (ua.sign)
    // Negative finite: invalid operation (§7.2).
    return detail::deliver<T>(
        detail::packSpecial<T>(ValueCategory::NaN, false), FlagInvalid);

  // ---------- Finite positive ----------

  int ea = (ua.biased_exp == 0) ? 1 : ua.biased_exp;

  DV sig = detail::digitsFromStorage<typename DV::limb_type, DV::limb_count>(
      ua.significand);
  {
    const int m = detail::topBitPos(sig);
    if (m < SigBits - 1) {
      sig = detail::shiftLeftDigits(sig, SigBits - 1 - m);
      ea -= (SigBits - 1 - m);
    }
  }

  // e = 2q + r, r in {0, 1}. C++20 defines >> on negative ints as
  // arithmetic shift, i.e. floor division by two.
  const int e_unb = ea - Bias;
  const int q = e_unb >> 1;
  const int r = e_unb - 2 * q;

  // A = 1.f × 2^r scaled to 2*(SigBits+GBits) - 1 + r bits, so its
  // root lands with MSB exactly at SigBits + GBits - 1 — the
  // magnitude form roundAndPack expects, guard bits included.
  const DV area = detail::shiftLeftDigits(sig, SigBits + 2 * GBits - 1 + r);
  const auto sr = detail::sqrtRemDigits(area);

  DV magnitude = sr.root;
  if (!detail::isZero(sr.rem))
    magnitude = detail::withBit(magnitude, 0); // sticky

  flags_t flags = FlagNone;
  auto bits = detail::roundAndPack<T>(false, q + Bias, magnitude, flags);
  return detail::deliver<T>(bits, flags);
}

} // namespace opine

#endif // OPINE_CORE_SQRT_HPP
