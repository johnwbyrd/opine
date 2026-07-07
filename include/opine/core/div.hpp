#ifndef OPINE_CORE_DIV_HPP
#define OPINE_CORE_DIV_HPP

// Division for FloatingPoint composites.
//
// The pipeline mirrors mul.hpp:
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
//      b. Divide (sig_a << K) by sig_b in a working type wide
//         enough for the full numerator; the remainder folds into
//         the sticky bit, so rounding sees the exact quotient.
//      c. Derive the result exponent from the operand exponents and
//         the quotient's MSB position.
//      d–i. Subnormal shift, G/R/S rounding, round-up carry,
//         overflow per §7.4, denormal flush, IntegerExtremes
//         collision — identical to mul.
//
// Guard bits are fixed at 3 (G/R/S), matching add/mul.

#include "opine/core/arith_detail.hpp"
#include "opine/core/bits.hpp"
#include "opine/core/pack_unpack.hpp"

namespace opine {

// div's numerator is the dividend significand shifted up by
// SigBits + 3 bits, and the working type tops out at 128 bits:
// extFloat80 (64-bit significands → 131-bit numerator) and float128
// need a multi-word scheme — follow-up work. Callers that dispatch
// generically can check this instead of tripping the static_assert.
template <typename T>
inline constexpr bool div_supported =
    2 * T::number::significand::digit_count + 3 <= 128;

// -----------------------------------------------------------------
// div
// -----------------------------------------------------------------
template <typename T>
constexpr typename T::storage_type
div(typename T::storage_type a, typename T::storage_type b) {
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

  static_assert(div_supported<T>,
                "div's working type tops out at 128 bits");
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

  // x ÷ 0 and true infinities: Inf when the format encodes it,
  // max finite otherwise (the oracle's EmitInfOrSaturate).
  auto packInfOrSaturate = [&](bool sign) -> Storage {
    if constexpr (Num::inf_encoding != InfEncoding::None) {
      return packSpecial(ValueCategory::Infinity, sign);
    } else {
      UnpackedFloat<Storage> u{};
      u.category = ValueCategory::Finite;
      u.sign = sign;
      u.biased_exp = MaxBiasedExp;
      u.significand = Storage((Storage{1} << SigBits) - 1);
      return pack<T>(u);
    }
  };

  const bool result_sign = ua.sign != ub.sign;

  // ---------- Special value dispatch ----------

  if (ua.category == ValueCategory::NaN || ub.category == ValueCategory::NaN)
    return packSpecial(ValueCategory::NaN, false);

  if (ua.category == ValueCategory::Infinity) {
    if (ub.category == ValueCategory::Infinity)
      return packSpecial(ValueCategory::NaN, false); // Inf ÷ Inf = NaN
    return packSpecial(ValueCategory::Infinity, result_sign);
  }
  if (ub.category == ValueCategory::Infinity)
    return packSpecial(ValueCategory::Zero, result_sign);

  if (ua.category == ValueCategory::Zero) {
    if (ub.category == ValueCategory::Zero)
      return packSpecial(ValueCategory::NaN, false); // 0 ÷ 0 = NaN
    return packSpecial(ValueCategory::Zero, result_sign);
  }
  if (ub.category == ValueCategory::Zero)
    return packInfOrSaturate(result_sign); // x ÷ 0

  // ---------- Finite ÷ Finite ----------

  int ea = (ua.biased_exp == 0) ? 1 : ua.biased_exp;
  int eb = (ub.biased_exp == 0) ? 1 : ub.biased_exp;

  // Pre-normalize denormal significands into the canonical range so
  // the quotient of significands lies in (1/2, 2).
  Wide sig_a = Wide(ua.significand);
  Wide sig_b = Wide(ub.significand);
  {
    int ma = detail::msbPos(sig_a);
    if (ma < SigBits - 1) {
      sig_a <<= (SigBits - 1 - ma);
      ea -= (SigBits - 1 - ma);
    }
    int mb = detail::msbPos(sig_b);
    if (mb < SigBits - 1) {
      sig_b <<= (SigBits - 1 - mb);
      eb -= (SigBits - 1 - mb);
    }
  }

  // Exact quotient: numerator shifted so the quotient carries
  // SigBits + GBits significant bits; the remainder is the sticky.
  // A quotient whose MSB lands at bit K corresponds to a significand
  // ratio in [1, 2) and result exponent ea − eb + Bias.
  constexpr int K = SigBits + GBits;
  Wide quotient = (sig_a << K) / sig_b;
  Wide remainder = (sig_a << K) % sig_b;

  int cur_msb = detail::msbPos(quotient);
  int result_exp = ea - eb + Bias + (cur_msb - K);

  // Bit 0 is the sticky position; fold the remainder in before the
  // (at most one) normalization right shift.
  Wide magnitude = quotient;
  if (remainder != 0)
    magnitude |= Wide{1};

  const int target_msb = SigBits + GBits - 1;
  if (cur_msb > target_msb)
    magnitude = detail::shiftRightSticky(magnitude, cur_msb - target_msb);

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

  // Round-up carried into a new binade (1.111… → 10.000…).
  if (stored_sig >= (Wide{1} << SigBits)) {
    stored_sig >>= 1;
    result_exp += 1;
  }

  // Subnormal-to-normal promotion: rounding may push a subnormal
  // significand up into the leading-digit position.
  if (result_exp == 0 && stored_sig >= (Wide{1} << (SigBits - 1))) {
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

#endif // OPINE_CORE_DIV_HPP
