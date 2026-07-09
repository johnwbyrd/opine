// DigitVector primitive verification.
//
// The digit layer is the only genuinely new arithmetic code in the
// multi-limb plan, so it gets its own battery, independent of any
// floating-point machinery:
//
//   1. constexpr sanity — the primitives evaluate at compile time.
//   2. Exhaustive small-limb sweeps (uint8 limbs, 16-bit vectors)
//      against plain scalar reference arithmetic. Runs everywhere.
//   3. Structural invariants at 128/256/1024 bits (divmod
//      reconstruction, shift algebra, carry-chain adversaries).
//      Runs everywhere — no reference integer needed.
//   4. (Clang) Randomized differential tests against unsigned
//      _BitInt at widths 40–2048, including direct bridges to the
//      scalar production helpers arith_detail::shiftRightSticky and
//      arith_detail::msbPos. _BitInt is the reference; the digit
//      layer must reproduce it bit-for-bit.

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include <cstdint>
#include <random>
#include <vector>

#include "opine/core/arith_detail.hpp"
#include "opine/core/digits.hpp"

using namespace opine;
using opine::detail::DigitVector;

// -----------------------------------------------------------------
// 1. constexpr sanity
// -----------------------------------------------------------------
namespace {
using DV8x2 = DigitVector<std::uint8_t, 2>;

constexpr DV8x2 kA = detail::digitsFrom<std::uint8_t, 2>(0x01FF);
constexpr DV8x2 kB = detail::digitsFrom<std::uint8_t, 2>(0x0001);

static_assert(detail::lowUint64(detail::addDigits(kA, kB)) == 0x0200);
static_assert(detail::lowUint64(detail::subDigits(kB, kA)) == 0xFE02);
static_assert(detail::lowUint64(detail::shiftLeftDigits(kA, 4)) == 0x1FF0);
static_assert(detail::lowUint64(detail::shiftRightDigits(kA, 4)) == 0x001F);
static_assert(detail::lowUint64(detail::shiftRightStickyDigits(kA, 4)) ==
              0x001F); // lost 0xF -> sticky already set in bit 0
static_assert(detail::topBitPos(kA) == 8);
static_assert(detail::compareDigits(kA, kB) == 1);
static_assert(detail::anyBitsBelow(kA, 1));
static_assert(!detail::anyBitsBelow(detail::digitsFrom<std::uint8_t, 2>(0x0100), 8));
static_assert(detail::lowUint64(detail::mulDigits(kA, kB)) == 0x01FF);
static_assert(detail::lowUint64(
                  detail::divModDigits(kA, kB).quot) == 0x01FF);
static_assert(detail::lowUint64(
                  detail::divModDigits(
                      detail::digitsFrom<std::uint8_t, 2>(0x0064),
                      detail::digitsFrom<std::uint8_t, 2>(0x0007))
                      .rem) == 2); // 100 % 7
} // namespace

// -----------------------------------------------------------------
// 2. Exhaustive small-limb sweeps vs scalar reference
// -----------------------------------------------------------------
// DigitVector<uint8_t, 2> mirrors uint16_t exactly (mod-2^16
// semantics), so plain integer arithmetic is the reference. Every
// 16-bit value is swept; pairs use a targeted partner list plus
// per-value random partners so carry chains at the limb boundary
// get hit from both sides.

namespace {
DV8x2 fromU16(std::uint16_t v) {
  return detail::digitsFrom<std::uint8_t, 2>(v);
}
std::uint16_t toU16(const DV8x2 &v) {
  return std::uint16_t(detail::lowUint64(v));
}
} // namespace

TEST_CASE("digits: uint8-limb unary ops, exhaustive 16-bit") {
  int failed = 0;
  for (std::uint32_t i = 0; i <= 0xFFFF; ++i) {
    const std::uint16_t x = std::uint16_t(i);
    const DV8x2 v = fromU16(x);

    // topBitPos
    int want_top = -1;
    for (int b = 15; b >= 0; --b)
      if (x & (1u << b)) {
        want_top = b;
        break;
      }
    if (detail::topBitPos(v) != want_top)
      ++failed;
    if (detail::isZero(v) != (x == 0))
      ++failed;

    // Shifts, all meaningful amounts plus out-of-range.
    for (int k = 0; k <= 17; ++k) {
      const std::uint16_t shl =
          k >= 16 ? 0 : std::uint16_t(std::uint32_t(x) << k);
      const std::uint16_t shr = k >= 16 ? 0 : std::uint16_t(x >> k);
      const std::uint16_t lost_mask =
          k >= 16 ? 0xFFFF : std::uint16_t((1u << k) - 1);
      const bool lost = (x & lost_mask) != 0;
      const std::uint16_t shr_sticky =
          std::uint16_t((k >= 16 ? (x != 0 ? 1 : 0) : shr) |
                        (k < 16 && lost ? 1 : 0));

      if (toU16(detail::shiftLeftDigits(v, k)) != shl)
        ++failed;
      if (toU16(detail::shiftRightDigits(v, k)) != shr)
        ++failed;
      if (toU16(detail::shiftRightStickyDigits(v, k)) != shr_sticky)
        ++failed;

      // bitAt / anyBitsBelow
      if (k <= 15 && detail::bitAt(v, k) != (((x >> k) & 1) != 0))
        ++failed;
      if (detail::anyBitsBelow(v, k) != ((x & lost_mask) != 0))
        ++failed;
    }
  }
  CHECK(failed == 0);
}

TEST_CASE("digits: uint8-limb binary ops, exhaustive x targeted+random") {
  const std::uint16_t targeted[] = {
      0x0000, 0x0001, 0x0002, 0x007F, 0x0080, 0x00FF, 0x0100, 0x0101,
      0x01FF, 0x5555, 0xAAAA, 0x7FFF, 0x8000, 0x8001, 0xFF00, 0xFFFE,
      0xFFFF, 0x00FE, 0x0200, 0xFE00};
  std::mt19937_64 rng(0xD161757EED);

  int failed = 0;
  for (std::uint32_t i = 0; i <= 0xFFFF; ++i) {
    const std::uint16_t a = std::uint16_t(i);
    const DV8x2 va = fromU16(a);

    auto checkPair = [&](std::uint16_t b) {
      const DV8x2 vb = fromU16(b);

      if (toU16(detail::addDigits(va, vb)) != std::uint16_t(a + b))
        ++failed;
      if (toU16(detail::subDigits(va, vb)) != std::uint16_t(a - b))
        ++failed;

      const int want_cmp = a < b ? -1 : (a == b ? 0 : 1);
      if (detail::compareDigits(va, vb) != want_cmp)
        ++failed;

      // Full 32-bit product in a 4-limb vector.
      const std::uint32_t prod = std::uint32_t(a) * std::uint32_t(b);
      if (detail::lowUint64(detail::mulDigits(va, vb)) != prod)
        ++failed;

      if (b != 0) {
        auto dm = detail::divModDigits(va, vb);
        if (toU16(dm.quot) != a / b || toU16(dm.rem) != a % b)
          ++failed;
      }
    };

    for (std::uint16_t b : targeted)
      checkPair(b);
    for (int r = 0; r < 8; ++r)
      checkPair(std::uint16_t(rng()));
  }
  CHECK(failed == 0);
}

// -----------------------------------------------------------------
// 3. Wide-width structural invariants (both compilers)
// -----------------------------------------------------------------
// No reference integer exists portably at 1024 bits, so these
// checks are self-reconstructive: quot * den + rem must rebuild the
// numerator exactly (a lie requires add, mul, sub, and compare to
// conspire), shifts must obey their algebra, and carries must
// ripple across every limb boundary.

namespace {

template <typename Limb, int Count>
DigitVector<Limb, Count> randomDigits(std::mt19937_64 &rng) {
  DigitVector<Limb, Count> v{};
  for (int i = 0; i < Count; ++i)
    v.d[i] = Limb(rng());
  return v;
}

// Adversarial shapes: carry-chain and boundary patterns.
template <typename Limb, int Count>
std::vector<DigitVector<Limb, Count>> adversarialDigits() {
  using DV = DigitVector<Limb, Count>;
  std::vector<DV> out;
  out.push_back(DV{});                                       // zero
  out.push_back(detail::digitsFrom<Limb, Count>(1));         // one
  out.push_back(detail::maskLowDigits<Limb, Count>(DV::total_bits)); // all-ones
  for (int pos : {0, DV::limb_bits - 1, DV::limb_bits, DV::limb_bits + 1,
                  DV::total_bits / 2, DV::total_bits - 1}) {
    out.push_back(detail::withBit(DV{}, pos));               // single bit
    out.push_back(detail::maskLowDigits<Limb, Count>(pos));  // bit-1 ones
  }
  return out;
}

template <typename Limb, int Count>
void checkDivModInvariant(const DigitVector<Limb, Count> &num,
                          const DigitVector<Limb, Count> &den, int &failed) {
  if (detail::isZero(den))
    return;
  auto dm = detail::divModDigits(num, den);
  // rem < den
  if (detail::compareDigits(dm.rem, den) >= 0)
    ++failed;
  // quot * den + rem == num, exactly, in 2N limbs
  auto wide = detail::mulDigits(dm.quot, den); // 2N limbs
  wide = detail::addDigits(wide, detail::resizeDigits<2 * Count>(dm.rem));
  if (detail::compareDigits(wide, detail::resizeDigits<2 * Count>(num)) != 0)
    ++failed;
}

template <typename Limb, int Count> void wideInvariants(std::uint64_t seed) {
  using DV = DigitVector<Limb, Count>;
  std::mt19937_64 rng(seed);
  int failed = 0;

  auto adv = adversarialDigits<Limb, Count>();

  // Carry chains: all-ones + 1 wraps to zero; 0 - 1 is all-ones;
  // (2^k - 1) + 1 is the single bit at k.
  const DV ones = detail::maskLowDigits<Limb, Count>(DV::total_bits);
  const DV one = detail::digitsFrom<Limb, Count>(1);
  if (!detail::isZero(detail::addDigits(ones, one)))
    ++failed;
  if (detail::compareDigits(detail::subDigits(DV{}, one), ones) != 0)
    ++failed;
  for (int k = 1; k < DV::total_bits; ++k) {
    DV low = detail::maskLowDigits<Limb, Count>(k);
    DV want = detail::withBit(DV{}, k);
    if (detail::compareDigits(detail::addDigits(low, one), want) != 0)
      ++failed;
    if (detail::topBitPos(low) != k - 1)
      ++failed;
  }

  for (int iter = 0; iter < 400; ++iter) {
    DV a = randomDigits<Limb, Count>(rng);
    DV b = randomDigits<Limb, Count>(rng);

    // add/sub inverse
    if (detail::compareDigits(detail::subDigits(detail::addDigits(a, b), b),
                              a) != 0)
      ++failed;

    // shift algebra: shr(shl(v,k),k) == v & maskLow(total-k)
    int k = int(rng() % (DV::total_bits + 8));
    const int kc = k > DV::total_bits ? DV::total_bits : k;
    const DV mask = detail::maskLowDigits<Limb, Count>(DV::total_bits - kc);
    DV masked = a;
    for (int i = 0; i < Count; ++i)
      masked.d[i] = Limb(masked.d[i] & mask.d[i]);
    if (detail::compareDigits(
            detail::shiftRightDigits(detail::shiftLeftDigits(a, k), k),
            masked) != 0)
      ++failed;

    // sticky definition
    DV sr = detail::shiftRightDigits(a, k);
    if (detail::anyBitsBelow(a, k))
      sr.d[0] = Limb(sr.d[0] | Limb{1});
    if (detail::compareDigits(detail::shiftRightStickyDigits(a, k), sr) != 0)
      ++failed;

    // divmod reconstruction, mixed shapes
    checkDivModInvariant(a, b, failed);
    checkDivModInvariant(a, detail::digitsFrom<Limb, Count>(1 + (rng() & 0xFF)),
                         failed);
    checkDivModInvariant(a, a, failed);
    for (const DV &p : adv) {
      checkDivModInvariant(a, p, failed);
      checkDivModInvariant(p, a, failed);
    }
  }
  CHECK(failed == 0);
}

} // namespace

TEST_CASE("digits: wide-width invariants (128/256/1024-bit)") {
  wideInvariants<std::uint64_t, 2>(0x11);
  wideInvariants<std::uint64_t, 4>(0x22);
  wideInvariants<std::uint64_t, 16>(0x33);
  wideInvariants<std::uint32_t, 3>(0x44); // odd width, narrow limbs
}

// -----------------------------------------------------------------
// 4. Differential vs _BitInt (Clang only)
// -----------------------------------------------------------------
// bits_t<W> on Clang is unsigned _BitInt(W) at any width, and the
// digit layer's declared semantics are exactly _BitInt's. Also
// bridges to the scalar production helpers: shiftRightSticky and
// msbPos must agree with their digit-vector counterparts.

#if defined(__clang__)

namespace {

template <typename Limb, int Count>
using BitsOf = bits_t<DigitVector<Limb, Count>::total_bits>;

template <typename Limb, int Count>
BitsOf<Limb, Count> toBits(const DigitVector<Limb, Count> &v) {
  using B = BitsOf<Limb, Count>;
  constexpr int LB = DigitVector<Limb, Count>::limb_bits;
  B r = 0;
  for (int i = Count - 1; i >= 0; --i)
    r = B(r << LB) | B(v.d[i]);
  return r;
}

template <typename Limb, int Count> void differential(std::uint64_t seed) {
  using DV = DigitVector<Limb, Count>;
  using B = BitsOf<Limb, Count>;
  using B2 = bits_t<2 * DV::total_bits>;
  std::mt19937_64 rng(seed);
  int failed = 0;

  auto adv = adversarialDigits<Limb, Count>();
  auto pick = [&](int iter) -> DV {
    if (iter % 5 == 0 && !adv.empty())
      return adv[rng() % adv.size()];
    return randomDigits<Limb, Count>(rng);
  };

  for (int iter = 0; iter < 3000; ++iter) {
    DV va = pick(iter);
    DV vb = pick(iter + 1);
    B a = toBits(va), b = toBits(vb);

    if (toBits(detail::addDigits(va, vb)) != B(a + b))
      ++failed;
    if (toBits(detail::subDigits(va, vb)) != B(a - b))
      ++failed;
    if (detail::compareDigits(va, vb) != (a < b ? -1 : (a == b ? 0 : 1)))
      ++failed;

    // Full product against _BitInt in double width.
    if (toBits(detail::mulDigits(va, vb)) != B2(B2(a) * B2(b)))
      ++failed;

    if (b != 0) {
      auto dm = detail::divModDigits(va, vb);
      if (toBits(dm.quot) != B(a / b) || toBits(dm.rem) != B(a % b))
        ++failed;
    }

    int k = int(rng() % (DV::total_bits + 8));
    if (toBits(detail::shiftLeftDigits(va, k)) !=
        B(k >= DV::total_bits ? B{0} : B(a << k)))
      ++failed;
    if (toBits(detail::shiftRightDigits(va, k)) !=
        B(k >= DV::total_bits ? B{0} : B(a >> k)))
      ++failed;

    // Bridge to the scalar production helpers. shiftRightSticky's
    // full-shift guard is sizeof-based, so it is only exact when
    // the _BitInt width matches its storage size — skip elsewhere.
    if constexpr (int(sizeof(B)) * 8 == DV::total_bits) {
      if (toBits(detail::shiftRightStickyDigits(va, k)) !=
          detail::shiftRightSticky(a, k))
        ++failed;
    }
    if (detail::topBitPos(va) != detail::msbPos(a))
      ++failed;

    if (int pos = int(rng() % DV::total_bits);
        detail::bitAt(va, pos) != (((a >> pos) & B{1}) != 0))
      ++failed;
  }
  CHECK(failed == 0);
}

} // namespace

TEST_CASE("digits: differential vs _BitInt (40..2048-bit, clang)") {
  differential<std::uint8_t, 5>(0xA1);   // 40-bit, narrow limbs
  differential<std::uint32_t, 3>(0xA2);  // 96-bit
  differential<std::uint64_t, 2>(0xA3);  // 128-bit
  differential<std::uint64_t, 4>(0xA4);  // 256-bit
  differential<std::uint64_t, 16>(0xA5); // 1024-bit
  differential<std::uint64_t, 32>(0xA6); // 2048-bit
}

#endif // __clang__
