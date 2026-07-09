#ifndef OPINE_CORE_DIV_HPP
#define OPINE_CORE_DIV_HPP

// Division for FloatingPoint composites.
//
// The kernel of the shared pipeline (see round_pack.hpp):
//
//   1. Unpack both operands; apply input-denormal flush per Number.
//   2. Dispatch on the (category_a × category_b) grid: NaN → NaN,
//      Inf ÷ Inf → NaN, Inf ÷ x → Inf, x ÷ Inf → Zero, 0 ÷ 0 → NaN,
//      0 ÷ x → Zero, x ÷ 0 → Inf (saturating to max finite when the
//      format has no Inf encoding, matching the oracle). The result
//      sign is always sign_a XOR sign_b.
//   3. Finite ÷ Finite:
//      a. Compute effective biased exponents (denormals → 1) and
//         pre-normalize denormal significands so both land in the
//         canonical range [2^(SigBits-1), 2^SigBits) — the quotient
//         of canonical significands is then in (1/2, 2), so the
//         normalization step is at most one right shift.
//      b. Divide (sig_a << K) by sig_b as digit vectors —
//         divModDigits yields quotient and remainder in one pass;
//         the remainder folds into the sticky bit, so rounding sees
//         the exact quotient.
//      c. Derive the result exponent from the operand exponents and
//         the quotient's MSB position.
//   4. roundAndPack does the rest: subnormal shift, G/R/S
//      rounding, overflow, denormal flush, IntegerExtremes
//      collision, pack.
//
// There is no width ceiling: the working geometry is however many
// limbs the shifted numerator needs (float128's 229-bit numerator
// is eight 32-bit limbs on the default platform). divModDigits is
// the restoring bit-serial tier — slow and obviously correct;
// faster division is a Platform specialization that must produce
// identical digits.

#include "opine/core/arith_detail.hpp"
#include "opine/core/digits.hpp"
#include "opine/core/round_pack.hpp"

namespace opine {

// -----------------------------------------------------------------
// div
// -----------------------------------------------------------------
template <typename T>
constexpr typename T::storage_type
div(typename T::storage_type a, typename T::storage_type b) {
  using Num = typename T::number;
  using Storage = typename T::storage_type;

  constexpr int SigBits = Num::significand::digit_count;
  constexpr int Bias = Num::exponent_bias;
  constexpr int GBits = detail::GuardBits;
  constexpr int K = SigBits + GBits;

  // The numerator is the dividend significand shifted up by K bits.
  using DV = detail::WorkingDigits<T, 2 * SigBits + GBits>;

  UnpackedFloat<Storage> ua = detail::unpackOperand<T>(a);
  UnpackedFloat<Storage> ub = detail::unpackOperand<T>(b);

  const bool result_sign = ua.sign != ub.sign;

  // ---------- Special value dispatch ----------

  if (ua.category == ValueCategory::NaN || ub.category == ValueCategory::NaN)
    return detail::packSpecial<T>(ValueCategory::NaN, false);

  if (ua.category == ValueCategory::Infinity) {
    if (ub.category == ValueCategory::Infinity)
      return detail::packSpecial<T>(ValueCategory::NaN, false); // Inf ÷ Inf
    return detail::packSpecial<T>(ValueCategory::Infinity, result_sign);
  }
  if (ub.category == ValueCategory::Infinity)
    return detail::packSpecial<T>(ValueCategory::Zero, result_sign);

  if (ua.category == ValueCategory::Zero) {
    if (ub.category == ValueCategory::Zero)
      return detail::packSpecial<T>(ValueCategory::NaN, false); // 0 ÷ 0
    return detail::packSpecial<T>(ValueCategory::Zero, result_sign);
  }
  if (ub.category == ValueCategory::Zero)
    return detail::packInfOrSaturate<T>(result_sign); // x ÷ 0

  // ---------- Finite ÷ Finite ----------

  int ea = (ua.biased_exp == 0) ? 1 : ua.biased_exp;
  int eb = (ub.biased_exp == 0) ? 1 : ub.biased_exp;

  // Pre-normalize denormal significands into the canonical range so
  // the quotient of significands lies in (1/2, 2).
  DV sig_a = detail::digitsFromStorage<typename DV::limb_type,
                                       DV::limb_count>(ua.significand);
  DV sig_b = detail::digitsFromStorage<typename DV::limb_type,
                                       DV::limb_count>(ub.significand);
  {
    int ma = detail::topBitPos(sig_a);
    if (ma < SigBits - 1) {
      sig_a = detail::shiftLeftDigits(sig_a, SigBits - 1 - ma);
      ea -= (SigBits - 1 - ma);
    }
    int mb = detail::topBitPos(sig_b);
    if (mb < SigBits - 1) {
      sig_b = detail::shiftLeftDigits(sig_b, SigBits - 1 - mb);
      eb -= (SigBits - 1 - mb);
    }
  }

  // Exact quotient: numerator shifted so the quotient carries
  // SigBits + GBits significant bits; the remainder is the sticky.
  // A quotient whose MSB lands at bit K corresponds to a significand
  // ratio in [1, 2) and result exponent ea − eb + Bias.
  auto dm = detail::divModDigits(detail::shiftLeftDigits(sig_a, K), sig_b);

  int cur_msb = detail::topBitPos(dm.quot);
  int result_exp = ea - eb + Bias + (cur_msb - K);

  // Bit 0 is the sticky position; fold the remainder in before the
  // (at most one) normalization right shift.
  DV magnitude = dm.quot;
  if (!detail::isZero(dm.rem))
    magnitude = detail::withBit(magnitude, 0);

  const int target_msb = SigBits + GBits - 1;
  if (cur_msb > target_msb)
    magnitude = detail::shiftRightStickyDigits(magnitude, cur_msb - target_msb);

  return detail::roundAndPack<T>(result_sign, result_exp, magnitude);
}

} // namespace opine

#endif // OPINE_CORE_DIV_HPP
