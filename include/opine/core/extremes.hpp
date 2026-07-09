#ifndef OPINE_CORE_EXTREMES_HPP
#define OPINE_CORE_EXTREMES_HPP

// IEEE 754-2019 §9.6 minimum/maximum operations and §5.3.1
// nextUp/nextDown.
//
// minimum / maximum       — NaN-propagating; -0 orders below +0.
// minimumNumber / maximumNumber — a quiet NaN loses to a number;
//                           NaN only when both operands are NaN.
// nextUp / nextDown       — the adjacent representable value, walked
//                           on the canonical unpacked form so every
//                           encoding (rbj included) gets the same
//                           semantics. Formats without an Inf
//                           encoding saturate: nextUp(maxFinite)
//                           stays maxFinite. Formats that flush
//                           denormals step over the subnormal range
//                           entirely (those patterns have no value
//                           of their own).
//
// All results are canonical (repacked), like every computational op.

#include "opine/core/compare.hpp"
#include "opine/core/digits.hpp"
#include "opine/core/round_pack.hpp"

namespace opine {

namespace detail {

// Shared min/max core. `want_min` picks the direction;
// `nan_propagates` picks §9.6 minimum/maximum vs minimumNumber/
// maximumNumber.
template <typename T>
constexpr typename T::storage_type
minmaxCore(typename T::storage_type a, typename T::storage_type b,
           bool want_min, bool nan_propagates) {
  const auto ua = unpackOperand<T>(a);
  const auto ub = unpackOperand<T>(b);
  const bool an = ua.category == ValueCategory::NaN;
  const bool bn = ub.category == ValueCategory::NaN;
  if (an || bn) {
    if (nan_propagates || (an && bn))
      return packSpecial<T>(ValueCategory::NaN, false);
    return pack<T>(an ? ub : ua); // the number wins
  }
  // Zeros: -0 orders below +0 for these operations (§9.6).
  if (ua.category == ValueCategory::Zero &&
      ub.category == ValueCategory::Zero) {
    const bool pick_a = want_min ? ua.sign : !ua.sign;
    return pack<T>(pick_a ? ua : ub);
  }
  const int c = compareUnpacked(ua, ub);
  const bool a_wins = want_min ? (c <= 0) : (c >= 0);
  return pack<T>(a_wins ? ua : ub);
}

} // namespace detail

template <typename T>
constexpr typename T::storage_type minimum(typename T::storage_type a,
                                           typename T::storage_type b) {
  return detail::minmaxCore<T>(a, b, /*min*/ true, /*nan_prop*/ true);
}
template <typename T>
constexpr typename T::storage_type maximum(typename T::storage_type a,
                                           typename T::storage_type b) {
  return detail::minmaxCore<T>(a, b, false, true);
}
template <typename T>
constexpr typename T::storage_type minimumNumber(typename T::storage_type a,
                                                 typename T::storage_type b) {
  return detail::minmaxCore<T>(a, b, true, false);
}
template <typename T>
constexpr typename T::storage_type maximumNumber(typename T::storage_type a,
                                                 typename T::storage_type b) {
  return detail::minmaxCore<T>(a, b, false, false);
}

namespace detail {

// Step one representable value AWAY from zero (|x| grows).
// Precondition: finite, nonzero unpacked form. Returns Infinity
// category on leaving the finite range (caller packs Inf or
// saturates per the encoding).
template <typename T>
constexpr UnpackedFloat<typename T::storage_type>
stepAwayFromZero(UnpackedFloat<typename T::storage_type> u) {
  using Num = typename T::number;
  using Storage = typename T::storage_type;
  constexpr int P = Num::significand::digit_count;

  u.significand = wordAddSmall(u.significand, 1);
  if (!wordLess(u.significand, wordBit<Storage>(P))) {
    // 1.111… + ulp → 10.000…: next binade. (Only reachable from a
    // normal: the max subnormal + ulp lands on 2^(P-1), below.)
    u.significand = wordBit<Storage>(P - 1);
    u.biased_exp += 1;
    if (u.biased_exp > max_biased_exp<T>) {
      u.category = ValueCategory::Infinity;
      return u;
    }
  } else if (u.biased_exp == 0 && testWordBit(u.significand, P - 1)) {
    // Max subnormal + ulp = min normal; canonicalize the exponent.
    u.biased_exp = 1;
  }
  return u;
}

// Step one representable value TOWARD zero. Precondition: finite,
// nonzero. Returns Zero category when it walks off the bottom.
template <typename T>
constexpr UnpackedFloat<typename T::storage_type>
stepTowardZero(UnpackedFloat<typename T::storage_type> u) {
  using Num = typename T::number;
  using Storage = typename T::storage_type;
  constexpr int P = Num::significand::digit_count;
  const bool flush = Num::denormal_mode != DenormalMode::Full;

  const bool at_binade_floor =
      u.significand == wordBit<Storage>(P - 1) && u.biased_exp != 0;
  if (at_binade_floor) {
    if (u.biased_exp == 1) {
      // Leaving the normals: into the subnormal range, or straight
      // to zero for flushing formats.
      if (flush) {
        u.category = ValueCategory::Zero;
        return u;
      }
      u.biased_exp = 0;
      u.significand = wordSubSmall(u.significand, 1); // 0.111…
      return u;
    }
    u.biased_exp -= 1;
    u.significand = wordOnes<Storage>(P); // 1.111… of the binade below
    return u;
  }
  u.significand = wordSubSmall(u.significand, 1);
  if (u.biased_exp == 0 && isZeroWord(u.significand)) {
    u.category = ValueCategory::Zero;
    return u;
  }
  return u;
}

// nextUp on the unpacked form; nextDown is its mirror through sign.
template <typename T>
constexpr typename T::storage_type nextUpCore(typename T::storage_type bits,
                                              bool mirror) {
  using Num = typename T::number;
  using Storage = typename T::storage_type;
  constexpr int P = Num::significand::digit_count;

  auto u = unpackOperand<T>(bits);
  if (mirror && u.category != ValueCategory::NaN)
    u.sign = !u.sign;

  auto finish = [&](UnpackedFloat<Storage> r) -> Storage {
    if (mirror && r.category != ValueCategory::NaN)
      r.sign = !r.sign;
    if (r.category == ValueCategory::Infinity &&
        Num::inf_encoding == InfEncoding::None)
      return packMaxFinite<T>(r.sign); // saturate: no Inf to step onto
    return pack<T>(r);
  };

  switch (u.category) {
  case ValueCategory::NaN:
    return packSpecial<T>(ValueCategory::NaN, false);
  case ValueCategory::Infinity:
    if (!u.sign) {
      UnpackedFloat<Storage> r{};
      r.category = ValueCategory::Infinity;
      return finish(r); // nextUp(+Inf) = +Inf
    }
    // nextUp(-Inf) = -maxFinite (mirrored: nextDown(+Inf) = +maxFinite).
    return packMaxFinite<T>(!mirror);
  case ValueCategory::Zero: {
    // Smallest positive value: min subnormal, or min normal for
    // flushing formats.
    UnpackedFloat<Storage> r{};
    r.category = ValueCategory::Finite;
    r.sign = false;
    if (Num::denormal_mode == DenormalMode::Full) {
      r.biased_exp = 0;
      r.significand = wordBit<Storage>(0);
    } else {
      r.biased_exp = 1;
      r.significand = wordBit<Storage>(P - 1);
    }
    return finish(r);
  }
  default:
    break;
  }

  UnpackedFloat<Storage> r =
      u.sign ? stepTowardZero<T>(u) : stepAwayFromZero<T>(u);
  if (r.category == ValueCategory::Zero) {
    // Walked off the bottom from the negative side: -0 (§5.3.1),
    // or +0 where the format has no -0.
    UnpackedFloat<Storage> z{};
    z.category = ValueCategory::Zero;
    z.sign = true;
    return finish(z);
  }
  return finish(r);
}

} // namespace detail

// The least value that compares greater than x (NaN → NaN,
// +Inf → +Inf, formats without Inf saturate at max finite).
template <typename T>
constexpr typename T::storage_type nextUp(typename T::storage_type bits) {
  return detail::nextUpCore<T>(bits, /*mirror=*/false);
}

// The greatest value that compares less than x.
template <typename T>
constexpr typename T::storage_type nextDown(typename T::storage_type bits) {
  return detail::nextUpCore<T>(bits, /*mirror=*/true);
}

} // namespace opine

#endif // OPINE_CORE_EXTREMES_HPP
