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
//      b. Multiply the semantic significands in a working type wide
//         enough for the full 2×SigBits product — no information is
//         lost before rounding.
//      c. Derive the result exponent from the operand exponents and
//         the product's MSB position, then normalize the product so
//         its MSB sits at the SigBits + GuardBits - 1 working
//         position (right shifts accumulate sticky; left shifts,
//         which only happen for denormal operands, are exact).
//   4. roundAndPack does the rest: subnormal shift, G/R/S
//      rounding, overflow, denormal flush, IntegerExtremes
//      collision, pack.

#include "opine/core/arith_detail.hpp"
#include "opine/core/bits.hpp"
#include "opine/core/round_pack.hpp"

namespace opine {

// mul's working type tops out at 128 bits, so the exact significand
// product must fit: float128 (2×113 = 226 bits) needs a multi-word
// scheme — follow-up work. Callers that dispatch generically (e.g.
// the test adapter) can check this instead of tripping the
// static_assert.
template <typename T>
inline constexpr bool mul_supported =
    2 * T::number::significand::digit_count <= 128;

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

  // The working type must hold the exact 2×SigBits-bit product and
  // the (SigBits + GBits)-bit normalized form. Use a power-of-two
  // width so shiftRightSticky's sizeof-based full-shift guard is
  // exact.
  static_assert(mul_supported<T>,
                "mul's working type tops out at 128 bits");
  using Wide = bits_t<(2 * SigBits + GBits > 64) ? 128 : 64>;

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
  Wide magnitude = Wide(ua.significand) * Wide(ub.significand);
  int cur_msb = detail::msbPos(magnitude);
  int result_exp = ea + eb - Bias + (cur_msb - 2 * (SigBits - 1));

  // Normalize the product so its MSB sits at the working position.
  const int target_msb = SigBits + GBits - 1;
  if (cur_msb > target_msb) {
    magnitude = detail::shiftRightSticky(magnitude, cur_msb - target_msb);
  } else if (cur_msb < target_msb) {
    // Denormal operand(s): the product is short. Exact left shift.
    magnitude <<= (target_msb - cur_msb);
  }

  return detail::roundAndPack<T>(result_sign, result_exp, magnitude);
}

} // namespace opine

#endif // OPINE_CORE_MUL_HPP
