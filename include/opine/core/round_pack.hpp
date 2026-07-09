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
#include "opine/core/digits.hpp"
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

// The working digit geometry for NeedBits of significand
// arithmetic: enough limbs of the Type's Platform machine word
// (digits.hpp). This is where Platform::machine_word_bits becomes
// load-bearing — the default Generic32 platform computes in 32-bit
// limbs.
template <typename T, int NeedBits>
using WorkingDigits = DigitVector<
    typename LimbFor<T::platform::machine_word_bits>::type,
    (NeedBits + T::platform::machine_word_bits - 1) /
        T::platform::machine_word_bits>;

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
//
// The magnitude arrives as a DigitVector — the compute-side digit
// geometry (digits.hpp) — so the epilogue is width-agnostic: the
// same code rounds FP8 in one limb and binary1024 in many. In
// radix terms, every test below is a digit-boundary statement
// (guard digit, any-digit-below sticky, top-digit position), which
// is what lets a future decimal instantiation reuse the shape.
template <typename T, typename Limb, int Count>
constexpr typename T::storage_type
roundAndPack(bool result_sign, int result_exp,
             DigitVector<Limb, Count> magnitude) {
  using Fmt = typename T::layout;
  using Num = typename T::number;
  using Rnd = typename T::rounding;
  using Storage = typename T::storage_type;
  using DV = DigitVector<Limb, Count>;

  constexpr int SigBits = Num::significand::digit_count;
  constexpr int ExpMax = (1 << Fmt::exp_bits) - 1;
  constexpr int MaxBiasedExp = max_biased_exp<T>;
  constexpr int GBits = GuardBits;
  constexpr int TotalBits = Fmt::total_bits;
  constexpr Storage SigStoredMask = (Storage{1} << Fmt::sig_bits) - 1;
  static_assert(DV::total_bits > SigBits + GBits,
                "working digit geometry too narrow for this format");

  const DV One = digitsFrom<Limb, Count>(1);

  // Subnormal range: shift the significand right to align with
  // biased_exp = 0.
  if (result_exp < 1) {
    magnitude = shiftRightStickyDigits(magnitude, 1 - result_exp);
    result_exp = 0;
  }

  // ---------- Round ----------
  DV stored_sig = shiftRightDigits(magnitude, GBits);
  bool lsb = bitAt(magnitude, GBits);
  bool guard_bit = bitAt(magnitude, GBits - 1);
  bool round_bit = (GBits >= 2) ? bitAt(magnitude, GBits - 2) : false;
  bool sticky = (GBits >= 2) ? anyBitsBelow(magnitude, GBits - 2) : false;

  bool round_up = detail::shouldRoundUp<Rnd>(lsb, guard_bit, round_bit, sticky,
                                             result_sign);
  if (round_up)
    stored_sig = addDigits(stored_sig, One);

  // Round-up carried into a new binade (1.111… → 10.000…). The
  // semantic significand for normals lives in [1<<(SigBits-1),
  // 1<<SigBits); reaching 1<<SigBits means the leading bit just
  // moved up one position and we halve + bump exponent. (The
  // vacated low bit is 0 after a carry, so the plain shift is
  // exact.)
  if (topBitPos(stored_sig) >= SigBits) {
    stored_sig = shiftRightDigits(stored_sig, 1);
    result_exp += 1;
  }

  // Subnormal-to-normal promotion: rounding may push a subnormal
  // significand up into the leading-digit position (the implicit
  // bit, or the stored J-bit), at which point biased_exp should
  // be 1 — the canonical form of that value.
  if (result_exp == 0 && topBitPos(stored_sig) >= SigBits - 1) {
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
    stored_sig = maskLowDigits<Limb, Count>(SigBits);
  }

  // ---------- Output denormal flush ----------
  if constexpr (Num::denormal_mode == DenormalMode::FlushToZero ||
                Num::denormal_mode == DenormalMode::FlushBoth) {
    if (result_exp == 0 && !isZero(stored_sig)) {
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
    Storage sig_word = storageFromDigits<Storage>(stored_sig);
    Storage tentative =
        (Storage(result_exp) << Fmt::exp_offset) |
        ((sig_word & SigStoredMask) << Fmt::sig_offset);
    constexpr Storage PosInf = (Storage{1} << (TotalBits - 1)) - 1;
    if (tentative >= PosInf) {
      if (detail::overflowRoundsToInf<Rnd>(result_sign))
        return packSpecial<T>(ValueCategory::Infinity, result_sign);
      result_exp = ExpMax;
      stored_sig = subDigits(maskLowDigits<Limb, Count>(SigBits), One);
    }
  }

  // ---------- Assemble and pack ----------
  UnpackedFloat<Storage> result{};
  result.category = (isZero(stored_sig) && result_exp == 0)
                        ? ValueCategory::Zero
                        : ValueCategory::Finite;
  result.sign = result_sign;
  result.biased_exp = result_exp;
  result.significand = storageFromDigits<Storage>(stored_sig);
  return pack<T>(result);
}

// Transitional scalar entry (multi-limb step B): kernels that still
// hand over a scalar working word get it chunked into the
// Platform's digit geometry. Disappears when the kernels move onto
// DigitVector themselves.
template <typename T, typename Wide>
constexpr typename T::storage_type
roundAndPack(bool result_sign, int result_exp, Wide magnitude) {
  using DV = WorkingDigits<T, int(sizeof(Wide)) * 8>;
  return roundAndPack<T>(
      result_sign, result_exp,
      digitsFromStorage<typename DV::limb_type, DV::limb_count>(magnitude));
}

} // namespace detail
} // namespace opine

#endif // OPINE_CORE_ROUND_PACK_HPP
