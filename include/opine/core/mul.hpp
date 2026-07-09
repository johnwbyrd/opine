#ifndef OPINE_CORE_MUL_HPP
#define OPINE_CORE_MUL_HPP

// Multiplication for FloatingPoint composites.
//
// The kernel of the shared pipeline (see round_pack.hpp):
//
//   1. Unpack both operands; apply input-denormal flush per Number.
//   2. Dispatch on the (category_a × category_b) grid: NaN → NaN,
//      Inf × Zero → NaN (invalid), Inf × finite → Inf, Zero ×
//      finite → Zero. The result sign is always sign_a XOR sign_b.
//   3. Finite × Finite:
//      a. Compute effective biased exponents (denormals → 1).
//      b. Multiply the semantic significands as digit vectors —
//         mulDigits produces the exact double-length product, so no
//         information is lost before rounding at any width.
//      c. Derive the result exponent from the operand exponents and
//         the product's MSB position, then normalize the product so
//         its MSB sits at the SigBits + GuardBits - 1 working
//         position (right shifts accumulate sticky; left shifts,
//         which only happen for denormal operands, are exact).
//   4. roundAndPack does the rest: subnormal shift, G/R/S
//      rounding, overflow, denormal flush, IntegerExtremes
//      collision, pack.
//
// There is no width ceiling: the working geometry is however many
// limbs the exact product needs (float128's 226-bit product is
// eight 32-bit limbs on the default platform).

#include "opine/core/arith_detail.hpp"
#include "opine/core/digits.hpp"
#include "opine/core/round_pack.hpp"

namespace opine {

// -----------------------------------------------------------------
// mul
// -----------------------------------------------------------------
template <typename T>
constexpr typename T::storage_type
mul(typename T::storage_type a, typename T::storage_type b) {
  using Num = typename T::number;
  using Storage = typename T::storage_type;

  constexpr int SigBits = Num::significand::digit_count;
  constexpr int Bias = Num::exponent_bias;
  constexpr int GBits = detail::GuardBits;

  // Operands as digit vectors sized for one significand; the exact
  // product is their double-length mulDigits result. The working
  // width also covers the SigBits + GBits normalized form (the
  // product of two significands is at least 2·(SigBits−1) bits
  // wide, and SigBits ≥ GBits for every real format).
  using SigDV = detail::WorkingDigits<T, SigBits>;
  using DV = detail::DigitVector<typename SigDV::limb_type,
                                 2 * SigDV::limb_count>;
  static_assert(DV::total_bits >= SigBits + GBits + 1,
                "product geometry cannot hold the normalized form");

  UnpackedFloat<Storage> ua = detail::unpackOperand<T>(a);
  UnpackedFloat<Storage> ub = detail::unpackOperand<T>(b);

  const bool result_sign = ua.sign != ub.sign;

  // ---------- Special value dispatch ----------

  if (ua.category == ValueCategory::NaN || ub.category == ValueCategory::NaN)
    return detail::packSpecial<T>(ValueCategory::NaN, false);

  if (ua.category == ValueCategory::Infinity ||
      ub.category == ValueCategory::Infinity) {
    if (ua.category == ValueCategory::Zero ||
        ub.category == ValueCategory::Zero)
      return detail::packSpecial<T>(ValueCategory::NaN, false); // Inf × 0
    return detail::packSpecial<T>(ValueCategory::Infinity, result_sign);
  }

  if (ua.category == ValueCategory::Zero || ub.category == ValueCategory::Zero)
    return detail::packSpecial<T>(ValueCategory::Zero, result_sign);

  // ---------- Finite × Finite ----------

  // Effective biased exponents. Denormals live at exponent 1 in
  // math terms even though the field reads 0.
  const int ea = (ua.biased_exp == 0) ? 1 : ua.biased_exp;
  const int eb = (ub.biased_exp == 0) ? 1 : ub.biased_exp;

  // Exact product of the semantic significands. Each significand
  // scales as sig / 2^(SigBits-1), so a product whose MSB lands at
  // bit 2·(SigBits-1) corresponds to result exponent ea + eb − Bias;
  // every MSB position off that reference shifts the exponent by one.
  const auto sig_a = detail::digitsFromStorage<typename SigDV::limb_type,
                                               SigDV::limb_count>(
      ua.significand);
  const auto sig_b = detail::digitsFromStorage<typename SigDV::limb_type,
                                               SigDV::limb_count>(
      ub.significand);
  DV magnitude = detail::mulDigits(sig_a, sig_b);
  int cur_msb = detail::topBitPos(magnitude);
  int result_exp = ea + eb - Bias + (cur_msb - 2 * (SigBits - 1));

  // Normalize the product so its MSB sits at the working position.
  const int target_msb = SigBits + GBits - 1;
  if (cur_msb > target_msb) {
    magnitude = detail::shiftRightStickyDigits(magnitude, cur_msb - target_msb);
  } else if (cur_msb < target_msb) {
    // Denormal operand(s): the product is short. Exact left shift.
    magnitude = detail::shiftLeftDigits(magnitude, target_msb - cur_msb);
  }

  return detail::roundAndPack<T>(result_sign, result_exp, magnitude);
}

} // namespace opine

#endif // OPINE_CORE_MUL_HPP
