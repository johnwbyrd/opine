#ifndef OPINE_CORE_MUL_HPP
#define OPINE_CORE_MUL_HPP

// Multiplication for FloatingPoint composites.
//
// The pipeline mirrors add.hpp:
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
//         its MSB sits at the SigBits + GBits - 1 working position
//         (right shifts accumulate sticky; left shifts, which only
//         happen for denormal operands, are exact).
//      d. Denormal path: if the result exponent falls below the
//         format's minimum normal, shift right (sticky) into the
//         subnormal range.
//      e. Round guard/round/sticky per the Type's Rounding.
//      f. Handle round-up overflow into the next binade.
//      g. Overflow → Inf if the format has it, saturate otherwise.
//      h. Denormal-output flush per denormal_mode.
//      i. IntegerExtremes overflow-collision: if the assembled
//         finite bit pattern lands on the +Inf pattern, emit Inf.
//
// Guard bits are fixed at 3 (G/R/S), matching add.hpp: the full
// product is computed exactly, and normalization folds everything
// below the round bit into sticky, so 3 bits decide every currently
// supported Rounding policy.

#include "opine/core/arith_detail.hpp"
#include "opine/core/bits.hpp"
#include "opine/core/pack_unpack.hpp"

namespace opine {

// -----------------------------------------------------------------
// mul
// -----------------------------------------------------------------
template <typename T>
constexpr typename T::storage_type
mul(typename T::storage_type a, typename T::storage_type b) {
  using Fmt = typename T::layout;
  using Num = typename T::number;
  using Rnd = typename T::rounding;
  using Storage = typename T::storage_type;

  constexpr int SigBits = Num::significand::digit_count;
  constexpr int Bias = Num::exponent_bias;
  constexpr int ExpMax = (1 << Fmt::exp_bits) - 1;
  constexpr int MaxBiasedExp =
      (Num::nan_encoding == NanEncoding::ReservedExponent ||
       Num::inf_encoding == InfEncoding::ReservedExponent)
          ? (ExpMax - 1)
          : ExpMax;
  constexpr int GBits = 3;
  constexpr int TotalBits = Fmt::total_bits;
  constexpr Storage SigStoredMask = (Storage{1} << Fmt::sig_bits) - 1;

  // The working type must hold the exact 2×SigBits-bit product and
  // the (SigBits + GBits)-bit normalized form. Use a power-of-two
  // width so shiftRightSticky's sizeof-based full-shift guard is
  // exact.
  using Wide = bits_t<(2 * SigBits + GBits > 64) ? 128 : 64>;

  UnpackedFloat<Storage> ua = unpack<T>(a);
  UnpackedFloat<Storage> ub = unpack<T>(b);

  // ---------- Input denormal flush ----------
  if constexpr (Num::denormal_mode == DenormalMode::FlushInputs ||
                Num::denormal_mode == DenormalMode::FlushBoth) {
    auto flush = [](UnpackedFloat<Storage> &u) {
      if (u.category == ValueCategory::Finite && u.biased_exp == 0) {
        u.category = ValueCategory::Zero;
        // Match the oracle: denormal patterns decode to +0 when
        // negative_zero=DoesNotExist (the only currently exercised
        // FlushInputs combination).
        if constexpr (Num::negative_zero == NegativeZero::DoesNotExist)
          u.sign = false;
      }
    };
    flush(ua);
    flush(ub);
  }

  // Small helper: pack a special-value category.
  auto packSpecial = [](ValueCategory c, bool sign) -> Storage {
    UnpackedFloat<Storage> u{};
    u.category = c;
    u.sign = sign;
    return pack<T>(u);
  };

  const bool result_sign = ua.sign != ub.sign;

  // ---------- Special value dispatch ----------

  if (ua.category == ValueCategory::NaN || ub.category == ValueCategory::NaN)
    return packSpecial(ValueCategory::NaN, false);

  if (ua.category == ValueCategory::Infinity ||
      ub.category == ValueCategory::Infinity) {
    if (ua.category == ValueCategory::Zero ||
        ub.category == ValueCategory::Zero)
      return packSpecial(ValueCategory::NaN, false); // Inf × 0 = NaN
    return packSpecial(ValueCategory::Infinity, result_sign);
  }

  if (ua.category == ValueCategory::Zero || ub.category == ValueCategory::Zero)
    return packSpecial(ValueCategory::Zero, result_sign);

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

  // Subnormal range: shift the significand right to align with
  // biased_exp = 0.
  if (result_exp < 1) {
    int extra = 1 - result_exp;
    magnitude = detail::shiftRightSticky(magnitude, extra);
    result_exp = 0;
  }

  // ---------- Round ----------
  Wide sig_top = magnitude >> GBits;
  bool lsb = (sig_top & Wide{1}) != 0;
  bool guard_bit = ((magnitude >> (GBits - 1)) & Wide{1}) != 0;
  bool round_bit =
      (GBits >= 2) ? ((magnitude >> (GBits - 2)) & Wide{1}) != 0 : false;
  Wide sticky_mask = (GBits >= 2) ? ((Wide{1} << (GBits - 2)) - 1) : Wide{0};
  bool sticky = (magnitude & sticky_mask) != 0;

  bool round_up = detail::shouldRoundUp<Rnd>(lsb, guard_bit, round_bit, sticky,
                                             result_sign);

  Wide stored_sig = sig_top;
  if (round_up)
    stored_sig += 1;

  // Round-up carried into a new binade (1.111… → 10.000…). The
  // semantic significand for normals lives in [1<<(SigBits-1),
  // 1<<SigBits); reaching 1<<SigBits means the leading bit just
  // moved up one position and we halve + bump exponent.
  if (stored_sig >= (Wide{1} << SigBits)) {
    stored_sig >>= 1;
    result_exp += 1;
  }

  // Subnormal-to-normal promotion: rounding may push a
  // subnormal significand up to include the implicit-bit
  // position, at which point biased_exp should be 1.
  if (result_exp == 0 && Fmt::implicit_digit &&
      stored_sig >= (Wide{1} << Fmt::sig_bits)) {
    result_exp = 1;
  }

  // ---------- Overflow ----------
  // IEEE 754 §7.4: overflow becomes Inf only when the rounding mode
  // carries the magnitude upward; otherwise saturate to max finite.
  if (result_exp > MaxBiasedExp) {
    if constexpr (Num::inf_encoding != InfEncoding::None) {
      if (detail::overflowRoundsToInf<Rnd>(result_sign))
        return packSpecial(ValueCategory::Infinity, result_sign);
    }
    result_exp = MaxBiasedExp;
    stored_sig = (Wide{1} << SigBits) - 1;
  }

  // ---------- Output denormal flush ----------
  if constexpr (Num::denormal_mode == DenormalMode::FlushToZero ||
                Num::denormal_mode == DenormalMode::FlushBoth) {
    if (result_exp == 0 && stored_sig != 0) {
      bool zero_sign =
          (Num::negative_zero == NegativeZero::Exists) ? result_sign : false;
      return packSpecial(ValueCategory::Zero, zero_sign);
    }
  }

  // ---------- IntegerExtremes overflow collision ----------
  // If the assembled positive-magnitude field pattern reaches the
  // +Inf bit pattern (0x7F…F), the "finite" result actually
  // overflows: Inf when the rounding mode carries upward, else the
  // largest finite pattern (one below +Inf).
  if constexpr (Num::inf_encoding == InfEncoding::IntegerExtremes) {
    Storage tentative =
        (Storage(result_exp) << Fmt::exp_offset) |
        ((Storage(stored_sig) & SigStoredMask) << Fmt::sig_offset);
    constexpr Storage PosInf = (Storage{1} << (TotalBits - 1)) - 1;
    if (tentative >= PosInf) {
      if (detail::overflowRoundsToInf<Rnd>(result_sign))
        return packSpecial(ValueCategory::Infinity, result_sign);
      result_exp = ExpMax;
      stored_sig = (Wide{1} << SigBits) - 2;
    }
  }

  // ---------- Assemble and pack ----------
  UnpackedFloat<Storage> result{};
  result.category = (stored_sig == 0 && result_exp == 0)
                        ? ValueCategory::Zero
                        : ValueCategory::Finite;
  result.sign = result_sign;
  result.biased_exp = result_exp;
  result.significand = Storage(stored_sig);
  return pack<T>(result);
}

} // namespace opine

#endif // OPINE_CORE_MUL_HPP
