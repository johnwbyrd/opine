#ifndef OPINE_CORE_CONVERT_HPP
#define OPINE_CORE_CONVERT_HPP

// Format conversion between Types (TDD step 11): convert<Dst, Src>.
//
// Conversion is the identity kernel of the shared pipeline
// (round_pack.hpp): unpack under Src, map special-value categories,
// rebase the exponent, place the significand at Dst's working
// position, and hand off to roundAndPack under Dst. Rounding,
// overflow, underflow-into-subnormals, denormal flushing, and
// encoding collisions therefore behave exactly as they do for
// arithmetic — the only conversion-specific code is the exponent
// rebase and one shift.
//
// Semantics:
//
//   - The rounding mode is DST's Rounding axis. "Convert FP32→FP16
//     truncating" is convert<Type<..., TowardZero>, float32>.
//     Converting between Types that differ only in Rounding (or
//     Exceptions/Platform) is value-exact by construction.
//
//   - Conversion is COMPUTATIONAL (IEEE 754 §5.4.2 convertFormat),
//     unlike neg/abs: the result is canonical. convert<T, T> of an
//     x87 unnormal or pseudo-denormal yields the canonical
//     encoding, matching the oracle. There is deliberately no
//     bit-copy fast path.
//
//   - NaN → Dst's canonical quiet NaN. Payloads are not
//     propagated, matching arithmetic.
//
//   - Inf into a format with no Inf encoding saturates to Dst's
//     max finite (the oracle's EmitInfOrSaturate; same policy as
//     x ÷ 0).
//
//   - −0 into a format without −0 packs as +0 (never the fnuz NaN
//     pattern — pack guards this).
//
//   - Src-side FlushInputs applies at unpack; Dst-side FlushToZero
//     applies in roundAndPack, exactly as in arithmetic.
//
// Call-site shape: BOTH template parameters must be spelled —
// destination first, reading like a cast. Src is not deducible
// from the argument because distinct Types share a storage type
// (fp8_e5m2 and fp8_e4m3 are both bits_t<8>):
//
//   auto h = convert<float16, fp8_e4m3>(x);   // FP8 → FP16, exact
//   auto q = convert<fp8_e4m3, float32>(y);   // rounds per fp8_e4m3
//
// Chained conversions are NOT equivalent to direct ones
// (convert<FP8>(convert<FP16>(x)) may double-round versus
// convert<FP8>(x)); a correctly-rounded via-format chain is future
// work for rounding::ToOdd.
//
// This is the generic implementation. Platforms may later provide
// specializations (hardware cvt instructions); per design.md they
// must produce results identical to this form.

#include <bit>
#include <cstdint>
#include <limits>

#include "opine/core/arith_detail.hpp"
#include "opine/core/bits.hpp"
#include "opine/core/pack_unpack.hpp"
#include "opine/core/round_pack.hpp"
#include "opine/core/type.hpp"

namespace opine {

namespace detail {

// Unbiased exponent of a format's largest finite binade.
template <typename T>
inline constexpr int max_unbiased_exp =
    max_biased_exp<T> - T::number::exponent_bias;

// Weight (unbiased power of two) of the smallest positive value —
// the bottom subnormal's ulp.
template <typename T>
inline constexpr int min_value_weight =
    (1 - T::number::exponent_bias) -
    (T::number::significand::digit_count - 1);

} // namespace detail

// -----------------------------------------------------------------
// exact_conversion
// -----------------------------------------------------------------
// True when every finite Src VALUE converts to Dst without
// rounding: Dst carries at least Src's relative precision, its top
// binade reaches at least as high, and its smallest positive
// weight reaches at least as low. When Dst flushes output
// denormals, Src's smallest values must additionally land in Dst's
// normal range.
//
// The trait speaks about values, not bits: NaN payloads are
// canonicalized regardless, and −0 converts to +0 when Dst has no
// −0. Round-trip identity on bit patterns additionally requires
// canonical inputs (x87) and holds for non-NaN patterns only.
template <typename Src, typename Dst>
inline constexpr bool exact_conversion =
    Dst::number::significand::digit_count >=
        Src::number::significand::digit_count &&
    detail::max_unbiased_exp<Dst> >= detail::max_unbiased_exp<Src> &&
    detail::min_value_weight<Dst> <= detail::min_value_weight<Src> &&
    (Dst::number::denormal_mode == DenormalMode::Full ||
     detail::min_value_weight<Src> >= 1 - Dst::number::exponent_bias);

// -----------------------------------------------------------------
// convert
// -----------------------------------------------------------------
template <typename Dst, typename Src>
constexpr auto convert(typename Src::storage_type bits) {
  using SrcNum = typename Src::number;
  using DstNum = typename Dst::number;
  using SrcStorage = typename Src::storage_type;

  constexpr int SrcSigBits = SrcNum::significand::digit_count;
  constexpr int DstSigBits = DstNum::significand::digit_count;
  constexpr int SrcBias = SrcNum::exponent_bias;
  constexpr int DstBias = DstNum::exponent_bias;
  constexpr int GBits = detail::GuardBits;

  // The working geometry holds the wider of the source significand
  // and Dst's significand-plus-guard form. No width ceiling: wider
  // pairs just take more limbs (of Dst's Platform word — the
  // conversion runs in the destination's pipeline).
  constexpr int NeedBits =
      (SrcSigBits > DstSigBits + GBits ? SrcSigBits : DstSigBits + GBits) + 1;
  using DV = detail::WorkingDigits<Dst, NeedBits>;

  UnpackedFloat<SrcStorage> u = detail::unpackOperand<Src>(bits);

  // ---------- Special value dispatch ----------

  if (u.category == ValueCategory::NaN)
    return detail::deliver<Dst>(
        detail::packSpecial<Dst>(ValueCategory::NaN, false), FlagNone);
  if (u.category == ValueCategory::Infinity) {
    // Inf into a format with no Inf encoding saturates: the value
    // exceeded every finite — overflow + inexact.
    constexpr flags_t InfFlags =
        DstNum::inf_encoding == InfEncoding::None
            ? flags_t(FlagOverflow | FlagInexact)
            : FlagNone;
    return detail::deliver<Dst>(detail::packInfOrSaturate<Dst>(u.sign),
                                InfFlags);
  }
  if (u.category == ValueCategory::Zero)
    return detail::deliver<Dst>(
        detail::packSpecial<Dst>(ValueCategory::Zero, u.sign), FlagNone);

  // ---------- Finite ----------

  // Effective biased exponent (denormals live at exponent 1) and
  // the value's unbiased leading-bit exponent: a significand whose
  // MSB sits at SrcSigBits−1 has exponent e − SrcBias; every MSB
  // position lower (denormals) is one binade lower.
  const int e = (u.biased_exp == 0) ? 1 : u.biased_exp;

  DV magnitude = detail::digitsFromStorage<typename DV::limb_type,
                                           DV::limb_count>(u.significand);
  const int cur_msb = detail::topBitPos(magnitude);
  const int unbiased = (e - SrcBias) + (cur_msb - (SrcSigBits - 1));

  // Rebase onto Dst's exponent scale and place the MSB at Dst's
  // working position: left shift is exact widening; right shift
  // folds discarded bits into sticky for roundAndPack.
  const int result_exp = unbiased + DstBias;
  const int target_msb = DstSigBits + GBits - 1;
  if (cur_msb > target_msb)
    magnitude = detail::shiftRightStickyDigits(magnitude, cur_msb - target_msb);
  else if (cur_msb < target_msb)
    magnitude = detail::shiftLeftDigits(magnitude, target_msb - cur_msb);

  flags_t flags = FlagNone;
  auto out = detail::roundAndPack<Dst>(u.sign, result_exp, magnitude, flags);
  return detail::deliver<Dst>(out, flags);
}

// -----------------------------------------------------------------
// Native bridges
// -----------------------------------------------------------------
// fromNative / toFloat / toDouble connect OPINE Types to the
// platform's IEEE 754 binary32/binary64 via bit_cast + convert.
// These are the intended way to construct OPINE values from
// numeric literals.

template <typename T>
constexpr auto fromNative(float v) {
  static_assert(std::numeric_limits<float>::is_iec559 &&
                    sizeof(float) == 4,
                "fromNative(float) requires IEEE 754 binary32");
  return convert<T, float32>(
      typename float32::storage_type(std::bit_cast<std::uint32_t>(v)));
}

template <typename T>
constexpr auto fromNative(double v) {
  static_assert(std::numeric_limits<double>::is_iec559 &&
                    sizeof(double) == 8,
                "fromNative(double) requires IEEE 754 binary64");
  return convert<T, float64>(
      typename float64::storage_type(std::bit_cast<std::uint64_t>(v)));
}

template <typename T>
constexpr float toFloat(typename T::storage_type bits) {
  static_assert(std::numeric_limits<float>::is_iec559 &&
                    sizeof(float) == 4,
                "toFloat requires IEEE 754 binary32");
  return std::bit_cast<float>(
      std::uint32_t(convert<float32, T>(bits)));
}

template <typename T>
constexpr double toDouble(typename T::storage_type bits) {
  static_assert(std::numeric_limits<double>::is_iec559 &&
                    sizeof(double) == 8,
                "toDouble requires IEEE 754 binary64");
  return std::bit_cast<double>(
      std::uint64_t(convert<float64, T>(bits)));
}

} // namespace opine

#endif // OPINE_CORE_CONVERT_HPP
