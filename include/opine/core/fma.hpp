#ifndef OPINE_CORE_FMA_HPP
#define OPINE_CORE_FMA_HPP

// Fused multiply-add for FloatingPoint composites: a×b + c with a
// SINGLE rounding — the exact product participates in the addition
// at full width, so (1+ε)² − (1+2ε) comes out as exactly ε² instead
// of the zero a rounded multiply would leave behind.
//
// The kernel of the shared pipeline (see round_pack.hpp):
//
//   1. Unpack all three operands; apply input-denormal flush.
//   2. Specials (§7.2): any NaN → NaN (quietly — for Inf×0 + qNaN
//      the standard leaves signaling implementation-defined, and
//      OPINE chooses not to). Inf×0 or 0×Inf → NaN with invalid.
//      An infinite product meeting an opposite-signed infinite c →
//      NaN with invalid; otherwise infinities propagate. An exact
//      zero product returns c (canonicalized), with the §6.3
//      zero-sum sign rule when c is zero too.
//   3. Finite: the exact 2P-bit product and the P-bit addend are
//      placed in a window wide enough (4P+3 bits) that whenever
//      cancellation is possible — magnitudes within 2P+2 binades —
//      BOTH values sit in the window exactly. Beyond that gap the
//      smaller operand right-shifts in with the usual sticky jam,
//      where subtraction can strip at most one leading bit. Add or
//      subtract, then normalize to the standard working form.
//   4. roundAndPack does the rest: subnormal shift, G/R/S
//      rounding, overflow, denormal flush, IntegerExtremes
//      collision, pack. One rounding, total.
//
// No width ceiling: binary1024's window is 63 limbs on 64-bit
// platforms.

#include "opine/core/arith_detail.hpp"
#include "opine/core/digits.hpp"
#include "opine/core/round_pack.hpp"

namespace opine {

// -----------------------------------------------------------------
// fma
// -----------------------------------------------------------------
template <typename T>
constexpr auto fma(typename T::storage_type a, typename T::storage_type b,
                   typename T::storage_type c) {
  using Num = typename T::number;
  using Rnd = typename T::rounding;
  using Storage = typename T::storage_type;

  constexpr int SigBits = Num::significand::digit_count;
  constexpr int Bias = Num::exponent_bias;
  constexpr int GBits = detail::GuardBits;

  // Exact-window geometry. Gaps up to ExactGap keep the smaller
  // operand fully inside the window (worst case: gap ExactGap plus
  // a 2P-bit product below the anchor); past that, only the sticky
  // jam matters and cancellation cannot reach the result's top.
  constexpr int ExactGap = 2 * SigBits + 2;
  constexpr int Anchor = ExactGap + 2 * SigBits - 1;
  using DV = detail::WorkingDigits<T, Anchor + 2>;

  UnpackedFloat<Storage> ua = detail::computeOperand<T>(a);
  UnpackedFloat<Storage> ub = detail::computeOperand<T>(b);
  UnpackedFloat<Storage> uc = detail::computeOperand<T>(c);

  // ---------- Special value dispatch ----------

  if (ua.category == ValueCategory::NaN ||
      ub.category == ValueCategory::NaN || uc.category == ValueCategory::NaN)
    return detail::deliver<T>(
        detail::packSpecial<T>(ValueCategory::NaN, false), FlagNone);

  const bool sign_p = ua.sign != ub.sign;

  const bool a_inf = ua.category == ValueCategory::Infinity;
  const bool b_inf = ub.category == ValueCategory::Infinity;
  const bool a_zero = ua.category == ValueCategory::Zero;
  const bool b_zero = ub.category == ValueCategory::Zero;

  if ((a_inf && b_zero) || (a_zero && b_inf))
    // Inf × 0 = NaN: invalid operation (§7.2).
    return detail::deliver<T>(
        detail::packSpecial<T>(ValueCategory::NaN, false), FlagInvalid);

  if (a_inf || b_inf) {
    // Infinite product (the other factor is nonzero here).
    if (uc.category == ValueCategory::Infinity && uc.sign != sign_p)
      // Inf − Inf = NaN: invalid operation (§7.2).
      return detail::deliver<T>(
          detail::packSpecial<T>(ValueCategory::NaN, false), FlagInvalid);
    return detail::deliver<T>(
        detail::packSpecial<T>(ValueCategory::Infinity, sign_p), FlagNone);
  }

  if (uc.category == ValueCategory::Infinity)
    return detail::deliver<T>(
        detail::packSpecial<T>(ValueCategory::Infinity, uc.sign), FlagNone);

  if (a_zero || b_zero) {
    // Exact zero product: the result is c, exactly. Zero + zero
    // follows the §6.3 exact-zero-sum sign rule.
    if (uc.category == ValueCategory::Zero) {
      const bool sum_sign =
          (sign_p == uc.sign) ? sign_p : detail::exactZeroSumSign<Rnd>();
      return detail::deliver<T>(
          detail::packSpecial<T>(ValueCategory::Zero, sum_sign), FlagNone);
    }
    return detail::deliver<T>(pack<T>(uc), FlagNone);
  }

  // ---------- Finite × finite (+ finite or zero) ----------

  int ea = (ua.biased_exp == 0) ? 1 : ua.biased_exp;
  int eb = (ub.biased_exp == 0) ? 1 : ub.biased_exp;
  int ec = (uc.biased_exp == 0) ? 1 : uc.biased_exp;

  // Pre-normalize denormal significands into the canonical range,
  // then form the exact 2P-bit product.
  using FDV = detail::WorkingDigits<T, SigBits>;
  FDV sig_a = detail::digitsFromStorage<typename FDV::limb_type,
                                        FDV::limb_count>(ua.significand);
  FDV sig_b = detail::digitsFromStorage<typename FDV::limb_type,
                                        FDV::limb_count>(ub.significand);
  FDV sig_c = detail::digitsFromStorage<typename FDV::limb_type,
                                        FDV::limb_count>(uc.significand);
  {
    const int ma = detail::topBitPos(sig_a);
    if (ma < SigBits - 1) {
      sig_a = detail::shiftLeftDigits(sig_a, SigBits - 1 - ma);
      ea -= (SigBits - 1 - ma);
    }
    const int mb = detail::topBitPos(sig_b);
    if (mb < SigBits - 1) {
      sig_b = detail::shiftLeftDigits(sig_b, SigBits - 1 - mb);
      eb -= (SigBits - 1 - mb);
    }
  }
  const bool c_zero = uc.category == ValueCategory::Zero;
  if (!c_zero) {
    const int mc = detail::topBitPos(sig_c);
    if (mc < SigBits - 1) {
      sig_c = detail::shiftLeftDigits(sig_c, SigBits - 1 - mc);
      ec -= (SigBits - 1 - mc);
    }
  }

  const auto prod = detail::mulDigits(sig_a, sig_b); // 2·FDV limbs, exact

  // Each value is an integer times 2^unit; tops are MSB weights.
  const int unit_p = (ea - Bias) + (eb - Bias) - 2 * (SigBits - 1);
  const int msb_p = detail::topBitPos(prod); // 2P-1 or 2P-2
  const int top_p = unit_p + msb_p;
  const int unit_c = (ec - Bias) - (SigBits - 1);
  const int top_c = ec - Bias;

  // Window bit k weighs 2^(E0 + k); the larger operand's MSB sits
  // at Anchor with one headroom bit above for the add carry.
  const int E0 =
      (c_zero ? top_p : (top_p > top_c ? top_p : top_c)) - Anchor;

  auto place = [&](const auto &mag, int unit) -> DV {
    const DV m = detail::resizeDigits<DV::limb_count>(mag);
    const int sh = unit - E0;
    return sh >= 0 ? detail::shiftLeftDigits(m, sh)
                   : detail::shiftRightStickyDigits(m, -sh);
  };

  const DV pw = place(prod, unit_p);
  const DV cw = c_zero ? DV{} : place(sig_c, unit_c);

  bool result_sign;
  DV magnitude;
  if (c_zero || uc.sign == sign_p) {
    magnitude = detail::addDigits(pw, cw);
    result_sign = sign_p;
  } else {
    const int cmp = detail::compareDigits(pw, cw);
    if (cmp == 0)
      // Exact cancellation: signed zero per the §6.3 rule.
      return detail::deliver<T>(
          detail::packSpecial<T>(ValueCategory::Zero,
                                 detail::exactZeroSumSign<Rnd>()),
          FlagNone);
    magnitude = cmp > 0 ? detail::subDigits(pw, cw)
                        : detail::subDigits(cw, pw);
    result_sign = cmp > 0 ? sign_p : uc.sign;
  }

  int result_exp = E0 + Bias + (SigBits + GBits - 1);
  const int target_msb = SigBits + GBits - 1;
  const int cur_msb = detail::topBitPos(magnitude);
  if (cur_msb > target_msb) {
    const int rs = cur_msb - target_msb;
    magnitude = detail::shiftRightStickyDigits(magnitude, rs);
    result_exp += rs;
  } else if (cur_msb < target_msb) {
    // Cancellation only happens in the exact-window regime, so the
    // left shift brings up genuine zeros.
    const int ls = target_msb - cur_msb;
    magnitude = detail::shiftLeftDigits(magnitude, ls);
    result_exp -= ls;
  }

  flags_t flags = FlagNone;
  auto bits = detail::roundAndPack<T>(result_sign, result_exp, magnitude,
                                      flags);
  return detail::deliver<T>(bits, flags);
}

} // namespace opine

#endif // OPINE_CORE_FMA_HPP
