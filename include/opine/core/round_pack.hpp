#ifndef OPINE_CORE_ROUND_PACK_HPP
#define OPINE_CORE_ROUND_PACK_HPP

// The shared stages of the rounding-operation pipeline.
//
// Every rounding operation (add, sub, mul, div, and later sqrt,
// fma, convert) is the same three-stage pipeline:
//
//   prologue : bits → UnpackedFloat     unpack + input-denormal flush
//   kernel   : UnpackedFloat… → (sign, exp, magnitude)
//   epilogue : (sign, exp, magnitude) → bits
//
// Only the kernel is operation-specific. This header owns the
// prologue (unpackOperand), the special-value packing helpers the
// kernels' dispatch grids share, and the epilogue (roundAndPack).
//
// roundAndPack fuses rounding and packing into one stage on
// purpose. The two cannot be split: whether an overflowed result
// becomes Inf or saturates depends on the Rounding policy, and
// whether the assembled bit pattern collides with an
// IntegerExtremes Inf encoding is only known after the round-up
// carry. (For dynamic-boundary layouts like posits the coupling is
// even tighter — the packing structure determines the rounding
// target — so this fusion is the boundary that generalizes.)
//
// Guard bits are fixed at 3 (G/R/S): the max any currently
// supported Rounding policy needs, and using a wider working
// significand than Rounding::guard_bits does not change the result.

#include "opine/core/arith_detail.hpp"
#include "opine/core/bits.hpp"
#include "opine/core/pack_unpack.hpp"

namespace opine {
namespace detail {

// Working guard bits below the significand in a kernel's magnitude.
inline constexpr int GuardBits = 3;

// The largest biased exponent finite values may occupy. Formats
// whose NaN or Inf encoding reserves the top exponent lose that
// binade to specials.
template <typename T>
inline constexpr int max_biased_exp =
    (T::number::nan_encoding == NanEncoding::ReservedExponent ||
     T::number::inf_encoding == InfEncoding::ReservedExponent)
        ? ((1 << T::layout::exp_bits) - 1) - 1
        : ((1 << T::layout::exp_bits) - 1);

// -----------------------------------------------------------------
// Prologue
// -----------------------------------------------------------------

// Apply input-denormal flushing per the Number's denormal_mode.
// Matches the oracle: denormal patterns decode to +0 when
// negative_zero=DoesNotExist (the only currently exercised
// FlushInputs combination).
template <typename T>
constexpr void
flushInputDenormal(UnpackedFloat<typename T::storage_type> &u) {
  using Num = typename T::number;
  if constexpr (Num::denormal_mode == DenormalMode::FlushInputs ||
                Num::denormal_mode == DenormalMode::FlushBoth) {
    if (u.category == ValueCategory::Finite && u.biased_exp == 0) {
      u.category = ValueCategory::Zero;
      if constexpr (Num::negative_zero == NegativeZero::DoesNotExist)
        u.sign = false;
    }
  }
}

// unpack + input flush: how every kernel receives an operand.
template <typename T>
constexpr UnpackedFloat<typename T::storage_type>
unpackOperand(typename T::storage_type bits) {
  UnpackedFloat<typename T::storage_type> u = unpack<T>(bits);
  flushInputDenormal<T>(u);
  return u;
}

// -----------------------------------------------------------------
// Special-value packing
// -----------------------------------------------------------------

// Pack a special-value category.
template <typename T>
constexpr typename T::storage_type packSpecial(ValueCategory c, bool sign) {
  UnpackedFloat<typename T::storage_type> u{};
  u.category = c;
  u.sign = sign;
  return pack<T>(u);
}

// The format's largest finite magnitude with the given sign.
template <typename T>
constexpr typename T::storage_type packMaxFinite(bool sign) {
  using Num = typename T::number;
  using Storage = typename T::storage_type;
  constexpr int SigBits = Num::significand::digit_count;
  constexpr int MaxBiasedExp = max_biased_exp<T>;
  UnpackedFloat<Storage> u{};
  u.category = ValueCategory::Finite;
  u.sign = sign;
  u.biased_exp = MaxBiasedExp;
  u.significand = Storage((Storage{1} << SigBits) - 1);
  return pack<T>(u);
}

// Inf when the format encodes it, max finite otherwise (the
// oracle's EmitInfOrSaturate).
template <typename T>
constexpr typename T::storage_type packInfOrSaturate(bool sign) {
  if constexpr (T::number::inf_encoding != InfEncoding::None)
    return packSpecial<T>(ValueCategory::Infinity, sign);
  else
    return packMaxFinite<T>(sign);
}

// -----------------------------------------------------------------
// Epilogue: roundAndPack
// -----------------------------------------------------------------
// Kernel postcondition = roundAndPack precondition:
//
//   - magnitude is nonzero and holds the significand in working
//     form with GuardBits guard bits below it: when result_exp ≥ 1
//     its MSB sits at SigBits + GuardBits − 1, and everything the
//     kernel discarded is folded into sticky (bit 0).
//   - result_exp is the tentative biased exponent, possibly < 1
//     (subnormal range) or > the format's max (overflow) — both
//     are handled here.
//
// From that point the result is a pure function of (sign, exp,
// magnitude, Type): subnormal shift, G/R/S rounding, round-up
// carry, subnormal-to-normal promotion, §7.4 overflow, output
// denormal flush, IntegerExtremes collision, assemble, pack.
template <typename T, typename Wide>
constexpr typename T::storage_type
roundAndPack(bool result_sign, int result_exp, Wide magnitude) {
  using Fmt = typename T::layout;
  using Num = typename T::number;
  using Rnd = typename T::rounding;
  using Storage = typename T::storage_type;

  constexpr int SigBits = Num::significand::digit_count;
  constexpr int ExpMax = (1 << Fmt::exp_bits) - 1;
  constexpr int MaxBiasedExp = max_biased_exp<T>;
  constexpr int GBits = GuardBits;
  constexpr int TotalBits = Fmt::total_bits;
  constexpr Storage SigStoredMask = (Storage{1} << Fmt::sig_bits) - 1;

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
        return packSpecial<T>(ValueCategory::Infinity, result_sign);
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
      return packSpecial<T>(ValueCategory::Zero, zero_sign);
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
        return packSpecial<T>(ValueCategory::Infinity, result_sign);
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
} // namespace opine

#endif // OPINE_CORE_ROUND_PACK_HPP
