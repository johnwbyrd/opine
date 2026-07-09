#ifndef OPINE_CORE_ADD_HPP
#define OPINE_CORE_ADD_HPP

// Addition for FloatingPoint composites.
//
// The kernel of the shared pipeline (see round_pack.hpp):
//
//   1. Unpack both operands; apply input-denormal flush per Number.
//   2. Dispatch on the (category_a × category_b) grid: NaN, Inf,
//      Zero → shortcut with the appropriate encoded value.
//   3. Finite + Finite:
//      a. Compute effective biased exponents (denormals → 1).
//      b. Order operands so |ua| ≥ |ub|.
//      c. Widen both significands by GuardBits guard bits, align
//         ub with a sticky-accumulating right shift.
//      d. Same-sign → sum, different-sign → subtract. Exact
//         cancellation returns signed zero per rounding policy.
//      e. Normalize: right-shift on carry, left-shift on
//         cancellation.
//   4. roundAndPack does the rest: subnormal shift, G/R/S
//      rounding, overflow, denormal flush, IntegerExtremes
//      collision, pack.

#include "opine/core/arith_detail.hpp"
#include "opine/core/bits.hpp"
#include "opine/core/round_pack.hpp"

namespace opine {

namespace detail {

// -----------------------------------------------------------------
// addWithSign — shared kernel for add and sub
// -----------------------------------------------------------------
// sub is add with b's sign flipped, but the flip must happen on the
// UNPACKED value: a bit-level negate has no -0 encoding to land on
// in fnuz formats (the sign-set zero pattern is their NaN), while
// the unpacked form carries the sign out-of-band.
template <typename T>
constexpr auto addWithSign(typename T::storage_type a,
                           typename T::storage_type b, bool negate_b) {
  using Num = typename T::number;
  using Rnd = typename T::rounding;
  using Storage = typename T::storage_type;

  constexpr int SigBits = Num::significand::digit_count;
  constexpr int GBits = GuardBits;

  // The working geometry holds the widened significand plus guard
  // bits and one carry bit. No width ceiling: wider formats just
  // take more limbs.
  using DV = WorkingDigits<T, SigBits + GBits + 1>;
  using Limb = typename DV::limb_type;

  UnpackedFloat<Storage> ua = unpackOperand<T>(a);
  UnpackedFloat<Storage> ub = unpackOperand<T>(b);

  // Subtraction: negate the unpacked b. NaN carries no sign.
  if (negate_b && ub.category != ValueCategory::NaN)
    ub.sign = !ub.sign;

  // ---------- Special value dispatch ----------

  if (ua.category == ValueCategory::NaN || ub.category == ValueCategory::NaN)
    return deliver<T>(packSpecial<T>(ValueCategory::NaN, false), FlagNone);

  if (ua.category == ValueCategory::Infinity &&
      ub.category == ValueCategory::Infinity) {
    if (ua.sign == ub.sign)
      return deliver<T>(packSpecial<T>(ValueCategory::Infinity, ua.sign),
                        FlagNone);
    // Inf − Inf = NaN: invalid operation (§7.2).
    return deliver<T>(packSpecial<T>(ValueCategory::NaN, false), FlagInvalid);
  }
  if (ua.category == ValueCategory::Infinity)
    return deliver<T>(packSpecial<T>(ValueCategory::Infinity, ua.sign),
                      FlagNone);
  if (ub.category == ValueCategory::Infinity)
    return deliver<T>(packSpecial<T>(ValueCategory::Infinity, ub.sign),
                      FlagNone);

  if (ua.category == ValueCategory::Zero &&
      ub.category == ValueCategory::Zero) {
    bool sum_sign =
        (ua.sign == ub.sign) ? ua.sign : detail::exactZeroSumSign<Rnd>();
    return deliver<T>(packSpecial<T>(ValueCategory::Zero, sum_sign), FlagNone);
  }
  // Repack rather than return the raw input: unpack canonicalizes
  // non-canonical explicit-J encodings (x87 unnormals and
  // pseudo-denormals), and zero + x must return x's canonical form.
  // For implicit-digit formats pack∘unpack is the identity.
  if (ua.category == ValueCategory::Zero)
    return deliver<T>(pack<T>(ub), FlagNone);
  if (ub.category == ValueCategory::Zero)
    return deliver<T>(pack<T>(ua), FlagNone);

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

  DV sa = shiftLeftDigits(
      digitsFromStorage<Limb, DV::limb_count>(ua.significand), GBits);
  DV sb = shiftLeftDigits(
      digitsFromStorage<Limb, DV::limb_count>(ub.significand), GBits);
  sb = shiftRightStickyDigits(sb, ea - eb);

  bool result_sign = ua.sign;
  DV magnitude;
  if (ua.sign == ub.sign) {
    magnitude = addDigits(sa, sb);
  } else {
    magnitude = subDigits(sa, sb);
    if (isZero(magnitude))
      return deliver<T>(packSpecial<T>(ValueCategory::Zero,
                                       detail::exactZeroSumSign<Rnd>()),
                        FlagNone);
  }

  int result_exp = ea;
  const int target_msb = SigBits + GBits - 1;
  int cur_msb = topBitPos(magnitude);

  if (cur_msb > target_msb) {
    // Carry from add: right-shift with sticky.
    int rs = cur_msb - target_msb;
    magnitude = shiftRightStickyDigits(magnitude, rs);
    result_exp += rs;
  } else if (cur_msb < target_msb) {
    // Cancellation from subtract: left-shift, exp goes down.
    int ls = target_msb - cur_msb;
    magnitude = shiftLeftDigits(magnitude, ls);
    result_exp -= ls;
  }

  flags_t flags = FlagNone;
  auto bits = roundAndPack<T>(result_sign, result_exp, magnitude, flags);
  return deliver<T>(bits, flags);
}

} // namespace detail

// -----------------------------------------------------------------
// add
// -----------------------------------------------------------------
template <typename T>
constexpr auto add(typename T::storage_type a, typename T::storage_type b) {
  return detail::addWithSign<T>(a, b, false);
}

} // namespace opine

#endif // OPINE_CORE_ADD_HPP
