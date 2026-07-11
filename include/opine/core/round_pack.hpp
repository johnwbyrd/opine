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

#include <cstdint>
#include <type_traits>

#include "opine/core/arith_detail.hpp"
#include "opine/core/bits.hpp"
#include "opine/core/digits.hpp"
#include "opine/core/exceptions.hpp"
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
// Flag delivery
// -----------------------------------------------------------------
// Every kernel computes its IEEE 754 §7 flags as a pure value and
// hands the packed result through here; the Type's Exceptions axis
// decides the disposition. Silent discards them (the computation is
// dead code the optimizer removes), StatusFlags accumulates into
// the per-thread sticky set (runtime only — constant evaluation
// cannot touch thread_local state), and ReturnStatus changes the
// operation's return type to WithStatus<T>.
template <typename T>
constexpr auto deliver(typename T::storage_type bits, flags_t flags) {
  using E = typename T::exceptions;
  static_assert(!E::has_traps,
                "exceptions::Trap is declared but not yet implemented");
  if constexpr (std::is_same_v<E, exceptions::ReturnStatus>) {
    return WithStatus<T>{bits, flags};
  } else {
    if constexpr (E::has_status_flags) {
      if (!std::is_constant_evaluated())
        statusFlags() |= flags;
    }
    return bits;
  }
}

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

// unpackOperand + working-precision truncation: how the ARITHMETIC
// kernels (add/sub/mul/div/fma/sqrt) receive an operand. When the
// Type's compute_format carries fewer significand bits than the
// Number (see WithComputePrecision in type.hpp), the low bits of
// the significand FIELD are cleared — positional truncation, so
// normals keep their top K significant bits and subnormals truncate
// on the same fixed absolute grid, exactly what masking a register
// costs on a small target. This happens BEFORE the kernel's
// special-value grid: a subnormal truncated to nothing takes part
// in the operation as a signed zero (so at K=8, denormal × Inf is
// 0 × Inf — invalid — by definition; the oracle applies the same
// order). Truncation itself raises no flags; inexact means inexact
// relative to the truncated operands. The comparison,
// classification, conversion, and string operations are NOT
// truncated — they see the stored value.
//
// At full compute precision (every predefined Type) this compiles
// to unpackOperand exactly.
template <typename T>
constexpr UnpackedFloat<typename T::storage_type>
computeOperand(typename T::storage_type bits) {
  UnpackedFloat<typename T::storage_type> u = unpackOperand<T>(bits);
  constexpr int P = T::number::significand::digit_count;
  constexpr int K = T::compute_format::mant_bits;
  if constexpr (K < P) {
    if (u.category == ValueCategory::Finite) {
      u.significand = shiftWordLeft(shiftWordRight(u.significand, P - K),
                                    P - K);
      if (isZeroWord(u.significand))
        u.category = ValueCategory::Zero; // truncated off the grid
    }
  }
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
  u.significand = wordOnes<Storage>(SigBits);
  if constexpr (Num::inf_encoding == InfEncoding::IntegerExtremes)
    // The all-ones pattern IS +Inf; max finite sits one below it.
    u.significand = wordSubSmall(u.significand, 1);
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
//
// Flags (IEEE 754 §7, ORed into `flags`):
//   inexact   — any G/R/S bit set (the kernels fold every discarded
//               bit into sticky, so this is exact-vs-rounded).
//   overflow  — the biased exponent exceeds the format's max, or
//               the assembled pattern collides with an
//               IntegerExtremes Inf. Implies inexact (§7.4).
//   underflow — tiny AND inexact (§7.5), with tininess detected
//               AFTER rounding as though the exponent range were
//               unbounded. Note this is not the same as testing the
//               final exponent: a subnormal-range value that the
//               format grid rounds up to the smallest normal is
//               still tiny.
template <typename T, typename Limb, int Count>
constexpr typename T::storage_type
roundAndPack(bool result_sign, int result_exp,
             DigitVector<Limb, Count> magnitude, flags_t &flags) {
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
  static_assert(DV::total_bits > SigBits + GBits,
                "working digit geometry too narrow for this format");

  const DV One = digitsFrom<Limb, Count>(1);

  // After-rounding tininess (§7.5), judged BEFORE the value is
  // coarsened into the subnormal grid: round the incoming magnitude
  // at full precision as though the exponent range were unbounded,
  // and ask whether it still lies below the smallest normal. This
  // differs from testing the final exponent exactly when rounding
  // promotes a subnormal-range value up to the smallest normal —
  // the delivered result is normal, but IEEE (and x86) still call
  // it tiny, because the unbounded-range rounding kept p significant
  // bits and stayed below 2^emin.
  bool tiny = false;
  if (result_exp < 1) {
    const bool ug = bitAt(magnitude, GBits - 1);
    const bool ur = (GBits >= 2) ? bitAt(magnitude, GBits - 2) : false;
    const bool us = (GBits >= 2) ? anyBitsBelow(magnitude, GBits - 2) : false;
    const bool ul = bitAt(magnitude, GBits);
    int e_unbounded = result_exp;
    if (detail::shouldRoundUp<Rnd>(ul, ug, ur, us, result_sign)) {
      DV t = addDigits(shiftRightDigits(magnitude, GBits), One);
      if (topBitPos(t) >= SigBits)
        e_unbounded += 1; // carried into the next binade
    }
    tiny = e_unbounded < 1;
  }

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

  if (guard_bit || round_bit || sticky)
    flags |= FlagInexact;

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
    flags |= FlagOverflow | FlagInexact;
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
      flags |= FlagUnderflow | FlagInexact;
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
    Storage tentative = orWords(
        shiftWordLeft(wordFromUint<Storage>(std::uint64_t(result_exp)),
                      Fmt::exp_offset),
        shiftWordLeft(andWords(sig_word, wordOnes<Storage>(Fmt::sig_bits)),
                      Fmt::sig_offset));
    const Storage PosInf = wordOnes<Storage>(TotalBits - 1);
    if (!wordLess(tentative, PosInf)) {
      flags |= FlagOverflow | FlagInexact;
      if (detail::overflowRoundsToInf<Rnd>(result_sign))
        return packSpecial<T>(ValueCategory::Infinity, result_sign);
      result_exp = ExpMax;
      stored_sig = subDigits(maskLowDigits<Limb, Count>(SigBits), One);
    }
  }

  // ---------- Underflow ----------
  // §7.5: tininess (after rounding, computed above) AND loss of
  // accuracy (the format-grid rounding was inexact).
  if (tiny && (flags & FlagInexact))
    flags |= FlagUnderflow;

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

} // namespace detail
} // namespace opine

#endif // OPINE_CORE_ROUND_PACK_HPP
