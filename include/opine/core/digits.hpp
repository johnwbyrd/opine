#ifndef OPINE_CORE_DIGITS_HPP
#define OPINE_CORE_DIGITS_HPP

// DigitVector: the compute-side digit geometry.
//
// A Number is a digit sequence (radix, digit_width, digit_count).
// Layout says how those digits are chunked for STORAGE; this header
// says how they are chunked for COMPUTATION. A limb is a compute
// digit: sixteen digits of radix 2^64 carry the same value as 1024
// digits of radix 2. Re-chunking is sound whenever the compute
// radix is a power of the Number's radix — radix-2^64 arithmetic IS
// binary arithmetic, differently packed — so rounding positions and
// sticky remain addressable inside a limb.
//
// DigitVector is deliberately NOT an integer type. There are no
// operator overloads; each primitive is a named digit-sequence
// algorithm (TAOCP vol. 2, §4.3, stated there for arbitrary radix).
// The names describe algorithmic roles so that a future non-binary
// instantiation (e.g. radix-10^19 decimal limbs) swaps the
// per-digit carry/estimation arithmetic without changing the
// algorithm shapes — the seam a pretend-integer type would weld
// shut. This slice implements the full-width binary instantiation:
// limbs are standard unsigned integers, the limb radix is
// 2^limb_bits, and value semantics are mod 2^total_bits (identical
// to unsigned _BitInt of the same width, which is what the
// differential tests exploit).
//
// These generic implementations define correct behavior. Platforms
// may later specialize the hot primitives (add-with-carry chains,
// hardware clz — the capabilities platform.hpp already declares, or
// wholesale _BitInt lowering where the compiler provides it); per
// design.md every specialization must produce identical digits.
//
// Conventions:
//   - d[0] is the LEAST significant limb (little-endian limb order,
//     the mpn convention).
//   - Bit positions are absolute: bit p lives in limb p / limb_bits
//     at offset p % limb_bits.
//   - Shift counts <= 0 are no-ops; counts >= total_bits shift
//     everything out (sticky variant preserves the lost-bits OR),
//     matching arith_detail::shiftRightSticky's contract.
//   - divModDigits is restoring bit-serial long division: one
//     quotient bit per step, O(total_bits) iterations. Slow and
//     obviously correct — the tier that defines semantics. Knuth
//     Algorithm D (radix-parameterized quotient estimation) can
//     arrive later as an equal-results upgrade.

#include <bit>
#include <cstdint>
#include <type_traits>

#include "opine/core/bits.hpp"

namespace opine {
namespace detail {

template <typename L>
concept LimbType = std::is_unsigned_v<L> && !std::is_same_v<L, bool>;

template <typename Limb, int Count>
  requires LimbType<Limb> && (Count > 0)
struct DigitVector {
  using limb_type = Limb;
  static constexpr int limb_count = Count;
  static constexpr int limb_bits = int(sizeof(Limb)) * 8;
  static constexpr int total_bits = limb_bits * Count;

  Limb d[Count]; // d[0] = least significant
};

// -----------------------------------------------------------------
// Limb selection
// -----------------------------------------------------------------
// The compute digit for a machine word width. This is where
// Platform::machine_word_bits becomes load-bearing: a 32-bit target
// computes in 32-bit limbs, an 8-bit target in bytes.
template <int Bits> struct LimbFor;
template <> struct LimbFor<8> { using type = std::uint8_t; };
template <> struct LimbFor<16> { using type = std::uint16_t; };
template <> struct LimbFor<32> { using type = std::uint32_t; };
template <> struct LimbFor<64> { using type = std::uint64_t; };

// -----------------------------------------------------------------
// Construction / conversion
// -----------------------------------------------------------------

template <typename Limb, int Count>
constexpr DigitVector<Limb, Count> digitsFrom(std::uint64_t v) {
  DigitVector<Limb, Count> r{};
  constexpr int LB = DigitVector<Limb, Count>::limb_bits;
  for (int i = 0; i < Count && v != 0; ++i) {
    r.d[i] = Limb(v);
    if constexpr (LB >= 64)
      v = 0;
    else
      v >>= LB;
  }
  return r;
}

// The low (up to) 64 bits — for diagnostics and small results.
template <typename Limb, int Count>
constexpr std::uint64_t lowUint64(const DigitVector<Limb, Count> &v) {
  constexpr int LB = DigitVector<Limb, Count>::limb_bits;
  std::uint64_t r = 0;
  for (int i = 0; i < Count && i * LB < 64; ++i)
    r |= std::uint64_t(v.d[i]) << (i * LB);
  return r;
}

// Zero-extend or truncate to a different limb count.
template <int NewCount, typename Limb, int Count>
constexpr DigitVector<Limb, NewCount>
resizeDigits(const DigitVector<Limb, Count> &v) {
  DigitVector<Limb, NewCount> r{};
  constexpr int N = NewCount < Count ? NewCount : Count;
  for (int i = 0; i < N; ++i)
    r.d[i] = v.d[i];
  return r;
}

// Limb-wise OR.
template <typename Limb, int Count>
constexpr DigitVector<Limb, Count> orDigits(const DigitVector<Limb, Count> &a,
                                            const DigitVector<Limb, Count> &b) {
  DigitVector<Limb, Count> r{};
  for (int i = 0; i < Count; ++i)
    r.d[i] = Limb(a.d[i] | b.d[i]);
  return r;
}

// Bridge from a scalar storage word (bits_t, any width) into a
// digit vector, moved in 64-bit chunks. The shift guards assume the
// storage type pads by fewer than 64 bits (true of every bits_t
// width the layouts use: exact powers of two, plus 80 → 128).
template <typename Limb, int Count, typename Storage>
constexpr DigitVector<Limb, Count> digitsFromStorage(Storage s) {
  constexpr int Chunks = (int(sizeof(Storage)) + 7) / 8;
  DigitVector<Limb, Count> r{};
  for (int c = 0; c < Chunks; ++c) {
    r = orDigits(r, shiftLeftDigits(digitsFrom<Limb, Count>(std::uint64_t(s)),
                                    c * 64));
    if constexpr (Chunks > 1) { // compile the shift only when in-width
      if (c + 1 < Chunks)
        s >>= 64; // never shifts on the final chunk
    }
  }
  return r;
}

// The reverse bridge. Precondition: the value fits the storage
// word.
template <typename Storage, typename Limb, int Count>
constexpr Storage storageFromDigits(const DigitVector<Limb, Count> &v) {
  constexpr int Chunks = (int(sizeof(Storage)) + 7) / 8;
  Storage r = Storage(lowUint64(v));
  if constexpr (Chunks > 1) { // compile the shift only when in-width
    for (int c = 1; c < Chunks; ++c) {
      std::uint64_t chunk = lowUint64(shiftRightDigits(v, c * 64));
      if (chunk != 0)
        r = Storage(r | (Storage(chunk) << (c * 64)));
    }
  }
  return r;
}

// The n lowest bits set (n clamped to [0, total_bits]).
template <typename Limb, int Count>
constexpr DigitVector<Limb, Count> maskLowDigits(int n) {
  using DV = DigitVector<Limb, Count>;
  constexpr int LB = DV::limb_bits;
  DigitVector<Limb, Count> r{};
  if (n <= 0)
    return r;
  if (n > DV::total_bits)
    n = DV::total_bits;
  int full = n / LB;
  for (int i = 0; i < full; ++i)
    r.d[i] = Limb(~Limb{0});
  if (int rem = n % LB; rem != 0)
    r.d[full] = Limb((Limb{1} << rem) - 1);
  return r;
}

// -----------------------------------------------------------------
// Queries
// -----------------------------------------------------------------

template <typename Limb, int Count>
constexpr bool isZero(const DigitVector<Limb, Count> &v) {
  for (int i = 0; i < Count; ++i)
    if (v.d[i] != 0)
      return false;
  return true;
}

// Three-way unsigned magnitude order: -1, 0, +1.
template <typename Limb, int Count>
constexpr int compareDigits(const DigitVector<Limb, Count> &a,
                            const DigitVector<Limb, Count> &b) {
  for (int i = Count - 1; i >= 0; --i) {
    if (a.d[i] != b.d[i])
      return a.d[i] < b.d[i] ? -1 : 1;
  }
  return 0;
}

// Position of the most significant set bit, or -1 if zero. The
// per-limb scan is where a Platform's has_clz capability plugs in;
// std::countl_zero is the portable spelling.
template <typename Limb, int Count>
constexpr int topBitPos(const DigitVector<Limb, Count> &v) {
  constexpr int LB = DigitVector<Limb, Count>::limb_bits;
  for (int i = Count - 1; i >= 0; --i) {
    if (v.d[i] != 0)
      return i * LB + (LB - 1 - std::countl_zero(v.d[i]));
  }
  return -1;
}

template <typename Limb, int Count>
constexpr bool bitAt(const DigitVector<Limb, Count> &v, int pos) {
  constexpr int LB = DigitVector<Limb, Count>::limb_bits;
  if (pos < 0 || pos >= DigitVector<Limb, Count>::total_bits)
    return false;
  return ((v.d[pos / LB] >> (pos % LB)) & 1) != 0;
}

// Any set bit strictly below position pos — the sticky test.
template <typename Limb, int Count>
constexpr bool anyBitsBelow(const DigitVector<Limb, Count> &v, int pos) {
  using DV = DigitVector<Limb, Count>;
  constexpr int LB = DV::limb_bits;
  if (pos <= 0)
    return false;
  if (pos >= DV::total_bits)
    return !isZero(v);
  int full = pos / LB;
  for (int i = 0; i < full; ++i)
    if (v.d[i] != 0)
      return true;
  if (int rem = pos % LB; rem != 0)
    return (v.d[full] & Limb((Limb{1} << rem) - 1)) != 0;
  return false;
}

// -----------------------------------------------------------------
// Bit surgery
// -----------------------------------------------------------------

template <typename Limb, int Count>
constexpr DigitVector<Limb, Count> withBit(DigitVector<Limb, Count> v,
                                           int pos) {
  constexpr int LB = DigitVector<Limb, Count>::limb_bits;
  if (pos >= 0 && pos < DigitVector<Limb, Count>::total_bits)
    v.d[pos / LB] = Limb(v.d[pos / LB] | (Limb{1} << (pos % LB)));
  return v;
}

// -----------------------------------------------------------------
// Addition / subtraction (mod 2^total_bits)
// -----------------------------------------------------------------

// Carry ripple. Written so integer promotion of narrow limbs can't
// leak: every partial is truncated back to Limb before the carry
// test.
template <typename Limb, int Count>
constexpr DigitVector<Limb, Count> addDigits(const DigitVector<Limb, Count> &a,
                                             const DigitVector<Limb, Count> &b) {
  DigitVector<Limb, Count> r{};
  Limb carry = 0;
  for (int i = 0; i < Count; ++i) {
    Limb s = Limb(a.d[i] + b.d[i]);
    bool c1 = s < a.d[i];
    Limb s2 = Limb(s + carry);
    bool c2 = s2 < s;
    r.d[i] = s2;
    carry = Limb(c1 || c2);
  }
  return r;
}

// Borrow ripple; two's-complement wraparound on underflow, exactly
// like unsigned integers (divModDigits' overflow-tracked remainder
// path relies on this).
template <typename Limb, int Count>
constexpr DigitVector<Limb, Count> subDigits(const DigitVector<Limb, Count> &a,
                                             const DigitVector<Limb, Count> &b) {
  DigitVector<Limb, Count> r{};
  Limb borrow = 0;
  for (int i = 0; i < Count; ++i) {
    Limb t = Limb(a.d[i] - b.d[i]);
    bool b1 = a.d[i] < b.d[i];
    Limb t2 = Limb(t - borrow);
    bool b2 = t < borrow;
    r.d[i] = t2;
    borrow = Limb(b1 || b2);
  }
  return r;
}

// -----------------------------------------------------------------
// Shifts
// -----------------------------------------------------------------

template <typename Limb, int Count>
constexpr DigitVector<Limb, Count>
shiftLeftDigits(const DigitVector<Limb, Count> &v, int shift) {
  using DV = DigitVector<Limb, Count>;
  constexpr int LB = DV::limb_bits;
  if (shift <= 0)
    return v;
  if (shift >= DV::total_bits)
    return DigitVector<Limb, Count>{};
  const int off = shift / LB;
  const int sh = shift % LB;
  DigitVector<Limb, Count> r{};
  for (int i = Count - 1; i >= off; --i) {
    Limb hi = Limb(sh == 0 ? v.d[i - off] : Limb(v.d[i - off] << sh));
    Limb lo = Limb((sh != 0 && i - off - 1 >= 0)
                       ? Limb(v.d[i - off - 1] >> (LB - sh))
                       : Limb{0});
    r.d[i] = Limb(hi | lo);
  }
  return r;
}

template <typename Limb, int Count>
constexpr DigitVector<Limb, Count>
shiftRightDigits(const DigitVector<Limb, Count> &v, int shift) {
  using DV = DigitVector<Limb, Count>;
  constexpr int LB = DV::limb_bits;
  if (shift <= 0)
    return v;
  if (shift >= DV::total_bits)
    return DigitVector<Limb, Count>{};
  const int off = shift / LB;
  const int sh = shift % LB;
  DigitVector<Limb, Count> r{};
  for (int i = 0; i + off < Count; ++i) {
    Limb lo = Limb(sh == 0 ? v.d[i + off] : Limb(v.d[i + off] >> sh));
    Limb hi = Limb((sh != 0 && i + off + 1 < Count)
                       ? Limb(v.d[i + off + 1] << (LB - sh))
                       : Limb{0});
    r.d[i] = Limb(hi | lo);
  }
  return r;
}

// Right shift folding every lost bit into an OR'd sticky at bit 0 —
// the digit-vector form of arith_detail::shiftRightSticky, same
// contract (shift <= 0 is a no-op; shift >= total_bits collapses to
// zero-or-one).
template <typename Limb, int Count>
constexpr DigitVector<Limb, Count>
shiftRightStickyDigits(const DigitVector<Limb, Count> &v, int shift) {
  if (shift <= 0)
    return v;
  const bool lost = anyBitsBelow(v, shift);
  DigitVector<Limb, Count> r = shiftRightDigits(v, shift);
  if (lost)
    r.d[0] = Limb(r.d[0] | Limb{1});
  return r;
}

// -----------------------------------------------------------------
// Multiplication
// -----------------------------------------------------------------

// Schoolbook over limbs: the exact (CountA + CountB)-limb product,
// no information lost. The row accumulation cannot overflow the
// double-width partial: (R-1)^2 + (R-1) + (R-1) == R^2 - 1. This is
// the generic tier; Karatsuba or hardware wide-multiply chains are
// Platform specializations that must produce identical digits.
template <typename Limb, int CountA, int CountB>
constexpr DigitVector<Limb, CountA + CountB>
mulDigits(const DigitVector<Limb, CountA> &a,
          const DigitVector<Limb, CountB> &b) {
  constexpr int LB = int(sizeof(Limb)) * 8;
  using Double = bits_t<2 * LB>;
  DigitVector<Limb, CountA + CountB> r{};
  for (int i = 0; i < CountA; ++i) {
    Limb carry = 0;
    for (int j = 0; j < CountB; ++j) {
      Double t = Double(a.d[i]) * Double(b.d[j]) + Double(r.d[i + j]) +
                 Double(carry);
      r.d[i + j] = Limb(t);
      carry = Limb(t >> LB);
    }
    // Position i + CountB is untouched before row i finishes (rows
    // i' < i reach at most i' + CountB - 1... + 1 = i + CountB - 1),
    // so a plain store is correct.
    r.d[i + CountB] = carry;
  }
  return r;
}

// -----------------------------------------------------------------
// Division with remainder
// -----------------------------------------------------------------

template <typename Limb, int Count> struct DivModResult {
  DigitVector<Limb, Count> quot;
  DigitVector<Limb, Count> rem;
};

// Restoring bit-serial long division: one quotient bit per step.
// Precondition: den != 0. The remainder's left shift can push its
// top bit out of the vector when den occupies the full width; the
// shifted-out bit is tracked explicitly, and the wrapped
// subtraction still yields the true remainder because subDigits is
// exact mod 2^total_bits.
template <typename Limb, int Count>
constexpr DivModResult<Limb, Count>
divModDigits(const DigitVector<Limb, Count> &num,
             const DigitVector<Limb, Count> &den) {
  using DV = DigitVector<Limb, Count>;
  constexpr int LB = DV::limb_bits;
  DivModResult<Limb, Count> r{};
  for (int i = topBitPos(num); i >= 0; --i) {
    const bool shifted_out = bitAt(r.rem, DV::total_bits - 1);
    r.rem = shiftLeftDigits(r.rem, 1);
    if (bitAt(num, i))
      r.rem.d[0] = Limb(r.rem.d[0] | Limb{1});
    if (shifted_out || compareDigits(r.rem, den) >= 0) {
      r.rem = subDigits(r.rem, den);
      r.quot.d[i / LB] = Limb(r.quot.d[i / LB] | (Limb{1} << (i % LB)));
    }
  }
  return r;
}

} // namespace detail
} // namespace opine

#endif // OPINE_CORE_DIGITS_HPP
