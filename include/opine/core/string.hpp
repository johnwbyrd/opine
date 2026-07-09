#ifndef OPINE_CORE_STRING_HPP
#define OPINE_CORE_STRING_HPP

// Decimal and hex string conversion for FloatingPoint Types.
//
//   toString<T>(bits, digits)   — correctly rounded decimal,
//                                 %g-style (positional or scientific,
//                                 trailing zeros trimmed).
//   fromString<T>(text)         — correctly rounded parse, honoring
//                                 T's Rounding axis and delivering
//                                 IEEE 754 flags through T's
//                                 Exceptions axis.
//   toHexString<T>(bits)        — exact C hex-float (%a-style), any
//                                 width, no rounding at all.
//
// Method: the exact big-integer school. A finite value is
// m · 2^e with integral m, so its D-digit decimal significand is
// round(m · 2^e · 10^(D-1-k)) for the right decimal exponent k —
// and every factor splits into powers of 2 (shifts) and powers of 5
// (exact DigitVector multiplies, or one division with the remainder
// deciding the round). No floating-point intermediates, no
// precomputed tables, no correctness-by-benchmark: each conversion
// is one exact integer computation. fromString runs the same recipe
// backwards and hands (sign, exponent, magnitude-with-G/R/S) to
// roundAndPack — decimal parsing is a conversion kernel, so it
// inherits subnormals, §7.4 overflow, denormal flushing, and
// exception flags from the shared epilogue.
//
// The working integers grow with the value's binary exponent
// (~1.7·|e| bits), dispatched over fixed DigitVector tiers up to
// 2^17 bits. That covers every value of every format through
// float128/extFloat80 (|e| ≤ 16494), and all wide-format values
// with decimal exponent within about ±19,700. Beyond the window —
// possible only for binary256/512/1024 — toString falls back to the
// exact hex form and fromString reports Invalid: correctly rounded
// decimal at 10^±20,000,000 needs multi-megabit multiplication,
// which is specialized-backend territory, not a place to guess.
//
// toString rounds its last digit to nearest, ties to even (the
// printf convention), independent of T's Rounding axis; parse
// honors the axis. Special values: "inf", "-inf", "nan", and zeros
// render as "0" / "-0".

#include <cstdint>
#include <string>
#include <string_view>

#include "opine/core/digits.hpp"
#include "opine/core/pack_unpack.hpp"
#include "opine/core/round_pack.hpp"

namespace opine {

// Decimal digits sufficient for bit-exact round-trip of a format:
// ceil(p·log10 2) + 1.
template <typename T>
inline constexpr int roundTripDigits =
    int((std::int64_t(T::number::significand::digit_count) * 30103 + 99999) /
        100000) +
    1;

namespace detail {

// -----------------------------------------------------------------
// Working-integer tiers
// -----------------------------------------------------------------
// Budget in bits for the exact computation: the operand, the power
// of five, and the shifted product all must fit. Dispatch over a
// few fixed geometries; return false if even the largest is too
// small (out-of-window value).
template <typename F> bool withDecimalBudget(long need_bits, F &&f) {
  if (need_bits <= 2048) {
    f(std::integral_constant<int, 32>{}); // 2048-bit: through float64
    return true;
  }
  if (need_bits <= 20480) {
    f(std::integral_constant<int, 320>{}); // 20k-bit: all of float128
    return true;
  }
  if (need_bits <= 65536) {
    f(std::integral_constant<int, 1024>{}); // 64k-bit: |e| to ~2^16
    return true;
  }
  return false;
}

// 5^n, exact, by repeated squaring. Fits by budget construction.
template <int Limbs>
DigitVector<std::uint64_t, Limbs> pow5Digits(long n) {
  using DV = DigitVector<std::uint64_t, Limbs>;
  DV result = digitsFrom<std::uint64_t, Limbs>(1);
  DV base = digitsFrom<std::uint64_t, Limbs>(5);
  while (n > 0) {
    if (n & 1)
      result = resizeDigits<Limbs>(mulDigits(result, base));
    n >>= 1;
    if (n)
      base = resizeDigits<Limbs>(mulDigits(base, base));
  }
  return result;
}

// floor(x · log10 2) for |x| up to ~2^40, exact enough to land the
// decimal exponent estimate within ±1 (corrected by the caller).
inline long floorLog10Pow2(long x) {
  const long long p = (long long)(x)*30103LL;
  return long(p >= 0 ? p / 100000 : (p - 99999) / 100000);
}

// Round an integer with explicit guard/sticky to nearest, ties to
// even. `q` is the truncated integer; the discarded part is (guard,
// sticky).
template <typename DV>
DV roundNearestEven(DV q, bool guard, bool sticky) {
  if (guard && (sticky || bitAt(q, 0)))
    return addDigits(q, digitsFrom<typename DV::limb_type, DV::limb_count>(1));
  return q;
}

// -----------------------------------------------------------------
// Exact decimal digits of a finite value (the toString core)
// -----------------------------------------------------------------
// Result: value ≈ (neg ? -1 : 1) · 0.d1d2…dD · 10^(k10+1) with the
// digit string correctly rounded to D digits (nearest, ties even).
struct DecimalDigits {
  bool neg = false;
  std::string digits; // exactly D characters '0'-'9'
  long k10 = 0;       // exponent of the leading digit: d1 · 10^k10
  bool ok = false;    // false: out of the supported window
};

// One attempt at a given decimal exponent k. Returns the rounded
// D-digit integer N with 10^(D-1) <= N < 10^D when k was right;
// the caller nudges k when the estimate was off by one.
template <int Limbs>
bool decimalAttempt(const DigitVector<std::uint64_t, Limbs> &m, long e, int D,
                    long k, DigitVector<std::uint64_t, Limbs> &n_out) {
  using DV = DigitVector<std::uint64_t, Limbs>;
  const long t = long(D) - 1 - k; // N = round(m · 2^(e+t) · 5^t)
  if (t >= 0) {
    DV scaled = resizeDigits<Limbs>(mulDigits(m, pow5Digits<Limbs>(t)));
    const long shift = e + t;
    if (shift >= 0) {
      n_out = shiftLeftDigits(scaled, int(shift)); // exact
    } else {
      const bool guard = bitAt(scaled, int(-shift) - 1);
      const bool sticky = anyBitsBelow(scaled, int(-shift) - 1);
      n_out = roundNearestEven(shiftRightDigits(scaled, int(-shift)), guard,
                               sticky);
    }
  } else {
    // N = m · 2^(e+t) / 5^(-t). e + t is usually positive (large
    // values), but a large value can still have a negative binary
    // exponent when its significand is wide — then the power of two
    // joins the divisor instead. Round via the remainder: 2·rem vs
    // divisor, ties to even.
    const long sh = e + t;
    DV num = sh >= 0 ? shiftLeftDigits(m, int(sh)) : m;
    DV den = pow5Digits<Limbs>(-t);
    if (sh < 0)
      den = shiftLeftDigits(den, int(-sh));
    auto dm = divModDigits(num, den);
    const DV twice = shiftLeftDigits(dm.rem, 1);
    int cmp = compareDigits(twice, den);
    bool up = cmp > 0 || (cmp == 0 && bitAt(dm.quot, 0));
    n_out = up ? addDigits(dm.quot, digitsFrom<std::uint64_t, Limbs>(1))
               : dm.quot;
  }
  return true;
}

template <typename T, int Limbs>
void decimalFromUnpacked(const UnpackedFloat<typename T::storage_type> &u,
                         int D, DecimalDigits &out) {
  using DV = DigitVector<std::uint64_t, Limbs>;
  using Num = typename T::number;
  constexpr int P = Num::significand::digit_count;

  DV m = digitsFromStorage<std::uint64_t, Limbs>(u.significand);
  const int eff = (u.biased_exp == 0) ? 1 : u.biased_exp;
  const long e = long(eff) - Num::exponent_bias - (P - 1); // value = m · 2^e

  // Decimal exponent estimate from the value's binary magnitude.
  long k = floorLog10Pow2(e + topBitPos(m));

  // 10^(D-1) and 10^D bounds for the correction loop.
  const DV pow10_dm1 = shiftLeftDigits(pow5Digits<Limbs>(D - 1), D - 1);
  const DV pow10_d = shiftLeftDigits(pow5Digits<Limbs>(D), D);

  DV n{};
  for (int tries = 0; tries < 4; ++tries) {
    decimalAttempt<Limbs>(m, e, D, k, n);
    if (compareDigits(n, pow10_d) >= 0) {
      ++k;
      continue;
    }
    if (compareDigits(n, pow10_dm1) < 0) {
      --k;
      continue;
    }
    break;
  }

  // Extract decimal digits from the (small) integer N.
  DigitVector<std::uint64_t, 64> small =
      resizeDigits<64>(n); // ≤ ~3.33·D bits
  const auto ten18 = digitsFrom<std::uint64_t, 64>(1000000000000000000ULL);
  std::string s;
  while (!isZero(small)) {
    auto dm = divModDigits(small, ten18);
    std::uint64_t chunk = lowUint64(dm.rem);
    small = dm.quot;
    const bool last = isZero(small);
    for (int i = 0; i < 18 && (chunk != 0 || !last); ++i) {
      s.push_back(char('0' + chunk % 10));
      chunk /= 10;
    }
    if (last && chunk == 0 && s.empty())
      s.push_back('0');
  }
  std::string digits(s.rbegin(), s.rend());
  // Defensive: the correction loop guarantees exactly D digits.
  if (int(digits.size()) > D)
    digits.resize(D);
  while (int(digits.size()) < D)
    digits.push_back('0');

  out.neg = u.sign;
  out.digits = std::move(digits);
  out.k10 = k;
  out.ok = true;
}

// Correctly rounded D-digit decimal decomposition, or ok=false when
// the value's exponent exceeds the supported window.
template <typename T>
DecimalDigits decimalDigits(typename T::storage_type bits, int D) {
  using Num = typename T::number;
  constexpr int P = Num::significand::digit_count;

  DecimalDigits out;
  const auto u = detail::unpackOperand<T>(bits);
  if (u.category != ValueCategory::Finite)
    return out; // caller handles specials/zero

  const int eff = (u.biased_exp == 0) ? 1 : u.biased_exp;
  const long e = long(eff) - Num::exponent_bias - (P - 1);
  // Budget: the scaled operand carries ≈ P + 0.71·|e| + 2.33·D
  // bits in either direction; ¾·|e| + 4·D leaves slack.
  const long ea = e < 0 ? -e : e;
  const long need = (3 * ea) / 4 + 4L * D + P + 96;
  bool fit = withDecimalBudget(need, [&](auto limbs) {
    decimalFromUnpacked<T, decltype(limbs)::value>(u, D, out);
  });
  out.ok = out.ok && fit;
  return out;
}

} // namespace detail

// -----------------------------------------------------------------
// toHexString — exact C hex-float, any width
// -----------------------------------------------------------------
template <typename T> std::string toHexString(typename T::storage_type bits) {
  using Num = typename T::number;
  constexpr int P = Num::significand::digit_count;

  const auto u = detail::unpackOperand<T>(bits);
  std::string s = u.sign ? "-" : "";
  switch (u.category) {
  case ValueCategory::NaN:
    return "nan";
  case ValueCategory::Infinity:
    return s + "inf";
  case ValueCategory::Zero:
    return s + "0x0p+0";
  default:
    break;
  }

  // Leading semantic digit, then the P-1 fraction bits in nibbles.
  const int eff = (u.biased_exp == 0) ? 1 : u.biased_exp;
  s += detail::testWordBit(u.significand, P - 1) ? "0x1" : "0x0";
  constexpr int FracBits = P - 1;
  std::string frac;
  for (int t = 0; t < (FracBits + 3) / 4; ++t) {
    const int hi = FracBits - 1 - 4 * t; // top fraction bit of this nibble
    int nib = 0;
    for (int b = 0; b < 4; ++b) {
      const int idx = hi - b; // significand bit index
      nib = (nib << 1) |
            ((idx >= 0 && detail::testWordBit(u.significand, idx)) ? 1 : 0);
    }
    frac.push_back("0123456789abcdef"[nib]);
  }
  while (!frac.empty() && frac.back() == '0')
    frac.pop_back();
  if (!frac.empty())
    s += "." + frac;

  const long e2 = long(eff) - Num::exponent_bias;
  s += 'p';
  if (e2 >= 0)
    s += '+';
  s += std::to_string(e2);
  return s;
}

// -----------------------------------------------------------------
// toString — correctly rounded decimal, %g-style
// -----------------------------------------------------------------
template <typename T>
std::string toString(typename T::storage_type bits,
                     int digits = roundTripDigits<T>) {
  if (digits < 1)
    digits = 1;
  if (digits > 1000)
    digits = 1000; // digit-extraction buffer bound
  const auto u = detail::unpackOperand<T>(bits);
  switch (u.category) {
  case ValueCategory::NaN:
    return "nan";
  case ValueCategory::Infinity:
    return u.sign ? "-inf" : "inf";
  case ValueCategory::Zero:
    return u.sign ? "-0" : "0";
  default:
    break;
  }

  detail::DecimalDigits d = detail::decimalDigits<T>(bits, digits);
  if (!d.ok)
    return toHexString<T>(bits); // outside the decimal window: exact hex

  std::string mant = d.digits;
  while (mant.size() > 1 && mant.back() == '0')
    mant.pop_back();

  std::string s = d.neg ? "-" : "";
  if (d.k10 >= -4 && d.k10 < long(digits)) {
    // Positional.
    if (d.k10 >= 0) {
      if (long(mant.size()) > d.k10 + 1) {
        s += mant.substr(0, d.k10 + 1) + "." + mant.substr(d.k10 + 1);
      } else {
        s += mant + std::string(d.k10 + 1 - mant.size(), '0');
      }
    } else {
      s += "0." + std::string(-d.k10 - 1, '0') + mant;
    }
  } else {
    // Scientific: d.ddd e±k
    s += mant.substr(0, 1);
    if (mant.size() > 1)
      s += "." + mant.substr(1);
    s += 'e';
    if (d.k10 >= 0)
      s += '+';
    s += std::to_string(d.k10);
  }
  return s;
}

// -----------------------------------------------------------------
// fromString — correctly rounded parse through the shared epilogue
// -----------------------------------------------------------------
namespace detail {

template <typename T, int Limbs>
typename T::storage_type
parseFinite(bool neg, const std::string &digits, long q, bool tail_sticky,
            flags_t &flags) {
  using DV = DigitVector<std::uint64_t, Limbs>;
  using Num = typename T::number;
  constexpr int P = Num::significand::digit_count;
  constexpr int GBits = GuardBits;
  constexpr int Target = P + GBits - 1;

  // The parsed significand as an integer.
  DV n{};
  const auto ten = digitsFrom<std::uint64_t, Limbs>(10);
  for (char c : digits) {
    n = resizeDigits<Limbs>(mulDigits(n, ten));
    n = addDigits(n, digitsFrom<std::uint64_t, Limbs>(std::uint64_t(c - '0')));
  }

  // value = n · 10^q · (1 + tail): fold everything into a magnitude
  // with G/R/S below the target position and hand it to the shared
  // epilogue.
  DV mag{};
  long e2 = 0; // value = mag · 2^(e2 - Target) exactly-with-sticky
  bool sticky = tail_sticky;

  if (q >= 0) {
    // value = (n · 5^q) · 2^q, exact. Normalize the MSB to Target;
    // bits shifted out fold into sticky at bit 0, which sits below
    // the guard/round positions the epilogue reads.
    DV v = resizeDigits<Limbs>(mulDigits(n, pow5Digits<Limbs>(q)));
    int msb = topBitPos(v);
    e2 = q + msb;
    if (msb > Target)
      mag = shiftRightStickyDigits(v, msb - Target);
    else
      mag = shiftLeftDigits(v, Target - msb);
    if (sticky)
      mag = withBit(mag, 0);
  } else {
    // value = n · 2^S / 5^(-q) · 2^(q - S): pick S so the quotient
    // carries at least Target + 2 significant bits.
    DV den = pow5Digits<Limbs>(-q);
    const int need = Target + 2 + topBitPos(den) - topBitPos(n) + 1;
    const int S = need > 0 ? need : 0;
    auto dm = divModDigits(shiftLeftDigits(n, S), den);
    sticky = sticky || !isZero(dm.rem);
    int msb = topBitPos(dm.quot);
    e2 = q - S + msb;
    if (msb > Target)
      mag = shiftRightStickyDigits(dm.quot, msb - Target);
    else
      mag = shiftLeftDigits(dm.quot, Target - msb);
    if (sticky)
      mag = withBit(mag, 0);
  }

  // Biased exponent for a magnitude whose MSB is the value's
  // leading bit at position Target.
  const int result_exp = int(e2) + Num::exponent_bias;

  // Re-chunk to the platform's working geometry for the epilogue.
  using WDV = WorkingDigits<T, P + GBits + 1>;
  WDV wmag = digitsFromStorage<typename WDV::limb_type, WDV::limb_count>(
      resizeDigits<(P + GBits + 64 + 63) / 64>(mag));
  return roundAndPack<T>(neg, result_exp, wmag, flags);
}

} // namespace detail

// Parses a decimal floating-point literal: [+-]? (ddd[.ddd] | .ddd)
// ([eE][+-]?ddd)?, or inf / infinity / nan (case-insensitive).
// Correctly rounded per T's Rounding axis; flags (inexact, overflow,
// underflow; Invalid for malformed or out-of-window input) delivered
// per T's Exceptions axis.
template <typename T> auto fromString(std::string_view text) {
  using detail::deliver;
  using detail::packSpecial;

  auto invalid = [&] {
    return deliver<T>(packSpecial<T>(ValueCategory::NaN, false), FlagInvalid);
  };

  size_t i = 0;
  bool neg = false;
  if (i < text.size() && (text[i] == '+' || text[i] == '-')) {
    neg = text[i] == '-';
    ++i;
  }

  auto ieq = [&](std::string_view word) {
    if (text.size() - i != word.size())
      return false;
    for (size_t j = 0; j < word.size(); ++j) {
      char c = text[i + j];
      if (c >= 'A' && c <= 'Z')
        c = char(c - 'A' + 'a');
      if (c != word[j])
        return false;
    }
    return true;
  };
  if (ieq("inf") || ieq("infinity"))
    return deliver<T>(packSpecial<T>(ValueCategory::Infinity, neg), FlagNone);
  if (ieq("nan"))
    return deliver<T>(packSpecial<T>(ValueCategory::NaN, false), FlagNone);

  // Digits, with at most one point.
  std::string digits;
  long point_shift = 0; // digits after the '.' seen so far
  bool seen_point = false, seen_digit = false;
  for (; i < text.size(); ++i) {
    char c = text[i];
    if (c >= '0' && c <= '9') {
      seen_digit = true;
      digits.push_back(c);
      if (seen_point)
        ++point_shift;
    } else if (c == '.' && !seen_point) {
      seen_point = true;
    } else {
      break;
    }
  }
  if (!seen_digit)
    return invalid();

  long exp10 = 0;
  if (i < text.size() && (text[i] == 'e' || text[i] == 'E')) {
    ++i;
    bool eneg = false;
    if (i < text.size() && (text[i] == '+' || text[i] == '-')) {
      eneg = text[i] == '-';
      ++i;
    }
    if (i >= text.size() || text[i] < '0' || text[i] > '9')
      return invalid();
    long v = 0;
    for (; i < text.size() && text[i] >= '0' && text[i] <= '9'; ++i) {
      if (v < 100000000)
        v = v * 10 + (text[i] - '0');
    }
    exp10 = eneg ? -v : v;
  }
  if (i != text.size())
    return invalid();

  // Normalize: strip leading zeros; cap kept digits, folding the
  // excess into sticky (they sit strictly below the rounding
  // position once the kept prefix carries P + margin bits).
  size_t lead = 0;
  while (lead + 1 < digits.size() && digits[lead] == '0')
    ++lead;
  digits.erase(0, lead);
  constexpr int P = T::number::significand::digit_count;
  const int keep = int((std::int64_t(P) * 30103) / 100000) + 8;
  bool tail_sticky = false;
  if (int(digits.size()) > keep) {
    for (size_t j = keep; j < digits.size(); ++j)
      tail_sticky = tail_sticky || digits[j] != '0';
    exp10 += long(digits.size()) - keep;
    digits.resize(keep);
  }
  const long q = exp10 - point_shift;

  if (digits == "0" || digits.find_first_not_of('0') == std::string::npos)
    return deliver<T>(packSpecial<T>(ValueCategory::Zero, neg), FlagNone);

  // Budget: n carries 3.33·digits bits; scaling by 10^q adds
  // ≈ 3.33·|q| more (or the same again in shift for the division
  // case).
  const long qa = q < 0 ? -q : q;
  const long need = (7 * (qa + long(digits.size()))) / 2 + P + 128;
  typename T::storage_type out{};
  flags_t flags = FlagNone;
  bool fit = detail::withDecimalBudget(need, [&](auto limbs) {
    out = detail::parseFinite<T, decltype(limbs)::value>(neg, digits, q,
                                                         tail_sticky, flags);
  });
  if (!fit)
    return invalid(); // outside the decimal window (see header)
  return deliver<T>(out, flags);
}

} // namespace opine

#endif // OPINE_CORE_STRING_HPP
