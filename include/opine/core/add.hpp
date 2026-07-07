#ifndef OPINE_CORE_ADD_HPP
#define OPINE_CORE_ADD_HPP

// Addition for FloatingPoint composites.
//
// The design's operation pipeline in one function:
//
//   1. Unpack both operands; apply input-denormal flush per Number.
//   2. Dispatch on the (category_a × category_b) grid: NaN, Inf,
//      Zero → shortcut with the appropriate encoded value.
//   3. Finite + Finite:
//      a. Compute effective biased exponents (denormals → 1).
//      b. Order operands so |ua| ≥ |ub|.
//      c. Widen both significands by GBits guard bits, align ub
//         with a sticky-accumulating right shift.
//      d. Same-sign → sum, different-sign → subtract. Exact
//         cancellation returns signed zero per rounding policy.
//      e. Normalize: right-shift on carry, left-shift on
//         cancellation.
//      f. Denormal path: if the result exponent falls below the
//         format's minimum normal, shift right (sticky) into the
//         subnormal range.
//      g. Round guard/round/sticky per the Type's Rounding.
//      h. Handle round-up overflow into the next binade.
//      i. Overflow → Inf if the format has it, saturate otherwise.
//      j. Denormal-output flush per denormal_mode.
//      k. IntegerExtremes overflow-collision: if the assembled
//         finite bit pattern lands on the +Inf pattern, emit Inf.
//
// Guard bits are fixed at 3 (G/R/S) — that is the max any
// currently supported Rounding policy needs, and using a wider
// working significand than Rounding::guard_bits does not change
// the result.

#include "opine/core/arith_detail.hpp"
#include "opine/core/bits.hpp"
#include "opine/core/pack_unpack.hpp"

namespace opine {

namespace detail {

// -----------------------------------------------------------------
// addWithSign — shared pipeline for add and sub
// -----------------------------------------------------------------
// sub is add with b's sign flipped, but the flip must happen on the
// UNPACKED value: a bit-level negate has no -0 encoding to land on
// in fnuz formats (the sign-set zero pattern is their NaN), while
// the unpacked form carries the sign out-of-band.
template <typename T>
constexpr typename T::storage_type
addWithSign(typename T::storage_type a, typename T::storage_type b,
            bool negate_b) {
  using Fmt = typename T::layout;
  using Num = typename T::number;
  using Rnd = typename T::rounding;
  using Storage = typename T::storage_type;

  constexpr int SigBits = Num::significand::digit_count;

  // The working type holds the wider significand plus guard bits and
  // one carry bit. Power-of-two widths keep shiftRightSticky's
  // sizeof-based full-shift guard exact.
  using Wide = bits_t<(SigBits + 3 + 1 > 64) ? 128 : 64>;
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

  // Subtraction: negate the unpacked b. NaN carries no sign.
  if (negate_b && ub.category != ValueCategory::NaN)
    ub.sign = !ub.sign;

  // Small helper: pack a special-value category.
  auto packSpecial = [](ValueCategory c, bool sign) -> Storage {
    UnpackedFloat<Storage> u{};
    u.category = c;
    u.sign = sign;
    return pack<T>(u);
  };

  // ---------- Special value dispatch ----------

  if (ua.category == ValueCategory::NaN || ub.category == ValueCategory::NaN)
    return packSpecial(ValueCategory::NaN, false);

  if (ua.category == ValueCategory::Infinity &&
      ub.category == ValueCategory::Infinity) {
    if (ua.sign == ub.sign)
      return packSpecial(ValueCategory::Infinity, ua.sign);
    return packSpecial(ValueCategory::NaN, false); // Inf − Inf = NaN
  }
  if (ua.category == ValueCategory::Infinity)
    return packSpecial(ValueCategory::Infinity, ua.sign);
  if (ub.category == ValueCategory::Infinity)
    return packSpecial(ValueCategory::Infinity, ub.sign);

  if (ua.category == ValueCategory::Zero &&
      ub.category == ValueCategory::Zero) {
    bool sum_sign =
        (ua.sign == ub.sign) ? ua.sign : detail::exactZeroSumSign<Rnd>();
    return packSpecial(ValueCategory::Zero, sum_sign);
  }
  // Repack rather than return the raw input: unpack canonicalizes
  // non-canonical explicit-J encodings (x87 unnormals and
  // pseudo-denormals), and zero + x must return x's canonical form.
  // For implicit-digit formats pack∘unpack is the identity.
  if (ua.category == ValueCategory::Zero)
    return pack<T>(ub);
  if (ub.category == ValueCategory::Zero)
    return pack<T>(ua);

  // ---------- Finite + Finite ----------

  // Effective biased exponents. Denormals live at exponent 1 in
  // math terms even though the field reads 0.
  int ea = (ua.biased_exp == 0) ? 1 : ua.biased_exp;
  int eb = (ub.biased_exp == 0) ? 1 : ub.biased_exp;

  // Order so ua carries the larger magnitude.
  if (ea < eb || (ea == eb && ua.significand < ub.significand)) {
    UnpackedFloat<Storage> tmp = ua;
    ua = ub;
    ub = tmp;
    int et = ea;
    ea = eb;
    eb = et;
  }

  Wide sa = Wide(ua.significand) << GBits;
  Wide sb = Wide(ub.significand) << GBits;
  sb = detail::shiftRightSticky(sb, ea - eb);

  bool result_sign = ua.sign;
  Wide magnitude;
  if (ua.sign == ub.sign) {
    magnitude = sa + sb;
  } else {
    magnitude = sa - sb;
    if (magnitude == 0)
      return packSpecial(ValueCategory::Zero,
                         detail::exactZeroSumSign<Rnd>());
  }

  int result_exp = ea;
  const int target_msb = SigBits + GBits - 1;
  int cur_msb = detail::msbPos(magnitude);

  if (cur_msb > target_msb) {
    // Carry from add: right-shift with sticky.
    int rs = cur_msb - target_msb;
    magnitude = detail::shiftRightSticky(magnitude, rs);
    result_exp += rs;
  } else if (cur_msb < target_msb) {
    // Cancellation from subtract: left-shift, exp goes down.
    int ls = target_msb - cur_msb;
    magnitude <<= ls;
    result_exp -= ls;
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

  // Subnormal-to-normal promotion: rounding may push a subnormal
  // significand up into the leading-digit position (the implicit
  // bit, or the stored J-bit), at which point biased_exp should
  // be 1 — the canonical form of that value.
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

} // namespace detail

// -----------------------------------------------------------------
// add
// -----------------------------------------------------------------
template <typename T>
constexpr typename T::storage_type
add(typename T::storage_type a, typename T::storage_type b) {
  return detail::addWithSign<T>(a, b, false);
}

} // namespace opine

#endif // OPINE_CORE_ADD_HPP
