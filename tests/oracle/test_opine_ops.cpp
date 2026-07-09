// Exhaustive OPINE-vs-oracle tests for the IEEE 754 §5 operations
// added alongside arithmetic: the remaining quiet predicates
// (gt/ge/ne/unordered), the §5.7.2 classification predicates, the
// §9.6 minimum/maximum family, §5.3.1 nextUp/nextDown, and §5.5.1
// copySign.
//
// Oracle strategy varies by operation:
//   predicates      — mpfr_greater_p / mpfr_greaterequal_p /
//                     mpfr_equal_p / mpfr_unordered_p on decoded
//                     values (all NaN-aware).
//   classification  — category facts read off the decoded MPFR
//                     value; subnormal/normal split by comparing
//                     |x| against the format's decoded minNormal.
//   minimum/maximum — decision logic re-derived from MPFR values
//                     (NaN rules, signed-zero rule, mpfr_cmp),
//                     result canonicalized through pack∘unpack.
//   nextUp/nextDown — exhaustive value scan: the reference answer
//                     is the encodable value strictly greater
//                     (resp. less) than x that is closest to x,
//                     found by brute force over all 256 patterns.
//                     No stepping logic is shared with the library.
//   copySign        — raw sign-transform reference in scalar
//                     arithmetic (same style as the neg/abs oracle),
//                     exercising the library's word-ops path
//                     differentially.
//
// Everything runs exhaustively at FP8 across the seven encodings;
// nextUp/nextDown additionally cross-check binary64 against the
// host libm's nextafter on random and targeted patterns.

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include <bit>
#include <cmath>
#include <cstdint>
#include <vector>

#include "harness/impl_mpfr.hpp"
#include "harness/impl_opine.hpp"
#include "harness/test_harness.hpp"

using namespace opine;
using namespace opine::testing;

namespace {

// Decode all 256 FP8 patterns once; the individual tests index in.
template <typename T> std::vector<MpfrFloat> decodeAll() {
  using BitsType = typename T::storage_type;
  std::vector<MpfrFloat> V;
  V.reserve(256);
  for (unsigned I = 0; I < 256; ++I)
    V.push_back(decodeToMpfr<T>(BitsType(I)));
  return V;
}

// Canonical bits of a pattern: what any computational op would
// return for that value (redundant encodings collapse, input
// denormals flush, unnormals normalize).
template <typename T>
typename T::storage_type canon(typename T::storage_type bits) {
  return pack<T>(opine::detail::unpackOperand<T>(bits));
}

} // namespace

// -----------------------------------------------------------------
// gt / ge / ne / unordered — exhaustive pairs vs MPFR
// -----------------------------------------------------------------
template <typename T> void verifyPredicates() {
  using BitsType = typename T::storage_type;
  auto Dec = decodeAll<T>();

  int Failed = 0;
  for (unsigned I = 0; I < 256; ++I) {
    for (unsigned J = 0; J < 256; ++J) {
      BitsType A = BitsType(I), B = BitsType(J);
      const auto &Ma = Dec[I];
      const auto &Mb = Dec[J];
      const bool WantGt = mpfr_greater_p(Ma, Mb) != 0;
      const bool WantGe = mpfr_greaterequal_p(Ma, Mb) != 0;
      const bool WantNe = mpfr_equal_p(Ma, Mb) == 0; // NaN → ne true
      const bool WantUn = mpfr_unordered_p(Ma, Mb) != 0;
      if (gt<T>(A, B) != WantGt || ge<T>(A, B) != WantGe ||
          ne<T>(A, B) != WantNe || unordered<T>(A, B) != WantUn) {
        if (Failed < 5)
          MESSAGE("predicate mismatch at a=0x" << std::hex << I << " b=0x"
                                               << J);
        ++Failed;
      }
    }
  }
  CHECK(Failed == 0);
}

TEST_CASE_TEMPLATE("gt/ge/ne/unordered: OPINE vs MPFR (exhaustive FP8)", T,
                   fp8_e5m2, fp8_e4m3, fp8_e4m3fnuz, RbjType<5, 2>,
                   RbjType<4, 3>, FastType<5, 2>, FastType<4, 3>) {
  verifyPredicates<T>();
}

// -----------------------------------------------------------------
// Classification predicates — exhaustive singles
// -----------------------------------------------------------------
template <typename T> void verifyClassify() {
  using BitsType = typename T::storage_type;
  using Storage = typename T::storage_type;
  constexpr int P = T::number::significand::digit_count;
  auto Dec = decodeAll<T>();

  // The format's smallest normal, decoded — the subnormal/normal
  // boundary. Built from the unpacked form so it is encoding-generic.
  UnpackedFloat<Storage> Mn{};
  Mn.category = ValueCategory::Finite;
  Mn.sign = false;
  Mn.biased_exp = 1;
  Mn.significand = opine::detail::wordBit<Storage>(P - 1);
  MpfrFloat MinNorm = decodeToMpfr<T>(pack<T>(Mn));

  int Failed = 0;
  for (unsigned I = 0; I < 256; ++I) {
    BitsType X = BitsType(I);
    const auto &Mx = Dec[I];
    const bool N = mpfr_nan_p(Mx) != 0;
    const bool Inf = mpfr_inf_p(Mx) != 0;
    const bool Z = mpfr_zero_p(Mx) != 0;
    const bool FiniteNonzero = !N && !Inf && !Z;
    const bool Sub = FiniteNonzero && mpfr_cmpabs(Mx, MinNorm) < 0;

    bool Ok = isNan<T>(X) == N && isInfinite<T>(X) == Inf &&
              isZero<T>(X) == Z && isFinite<T>(X) == (!N && !Inf) &&
              isSubnormal<T>(X) == Sub &&
              isNormal<T>(X) == (FiniteNonzero && !Sub);
    // isSignMinus is defined on the raw pattern; the decoded value
    // carries the sign for everything except NaN (whose sign MPFR
    // does not model) and zeros in formats without a negative zero,
    // where a sign-set pattern still decodes to +0 (FastType's
    // flushed negative subnormals). Compare where they agree.
    const bool RawSignDropped =
        Z && T::number::negative_zero == NegativeZero::DoesNotExist;
    if (!N && !RawSignDropped)
      Ok = Ok && isSignMinus<T>(X) == (mpfr_signbit(Mx) != 0);
    if (!Ok) {
      if (Failed < 5)
        MESSAGE("classify mismatch at 0x" << std::hex << I);
      ++Failed;
    }
  }
  CHECK(Failed == 0);
}

TEST_CASE_TEMPLATE("classification: OPINE vs MPFR (exhaustive FP8)", T,
                   fp8_e5m2, fp8_e4m3, fp8_e4m3fnuz, RbjType<5, 2>,
                   RbjType<4, 3>, FastType<5, 2>, FastType<4, 3>) {
  verifyClassify<T>();
}

// -----------------------------------------------------------------
// minimum / maximum / minimumNumber / maximumNumber — exhaustive
// pairs against a reference that re-derives §9.6 from MPFR values
// -----------------------------------------------------------------
template <typename T> void verifyMinMax() {
  using BitsType = typename T::storage_type;
  auto Dec = decodeAll<T>();

  auto Ref = [&](unsigned I, unsigned J, bool WantMin,
                 bool NanProp) -> BitsType {
    BitsType A = BitsType(I), B = BitsType(J);
    const auto &Ma = Dec[I];
    const auto &Mb = Dec[J];
    const bool An = mpfr_nan_p(Ma) != 0;
    const bool Bn = mpfr_nan_p(Mb) != 0;
    if (An || Bn) {
      if (NanProp || (An && Bn))
        return opine::detail::packSpecial<T>(ValueCategory::NaN, false);
      return canon<T>(An ? B : A); // the number wins
    }
    if (mpfr_zero_p(Ma) && mpfr_zero_p(Mb)) {
      // §9.6: -0 orders below +0.
      const bool PickA =
          WantMin ? mpfr_signbit(Ma) != 0 : mpfr_signbit(Ma) == 0;
      return canon<T>(PickA ? A : B);
    }
    const int C = mpfr_cmp(Ma, Mb);
    const bool AWins = WantMin ? C <= 0 : C >= 0;
    return canon<T>(AWins ? A : B);
  };

  int Failed = 0;
  for (unsigned I = 0; I < 256; ++I) {
    for (unsigned J = 0; J < 256; ++J) {
      BitsType A = BitsType(I), B = BitsType(J);
      bool Ok = minimum<T>(A, B) == Ref(I, J, true, true) &&
                maximum<T>(A, B) == Ref(I, J, false, true) &&
                minimumNumber<T>(A, B) == Ref(I, J, true, false) &&
                maximumNumber<T>(A, B) == Ref(I, J, false, false);
      if (!Ok) {
        if (Failed < 5)
          MESSAGE("min/max mismatch at a=0x" << std::hex << I << " b=0x" << J);
        ++Failed;
      }
    }
  }
  CHECK(Failed == 0);
}

TEST_CASE_TEMPLATE("minimum/maximum family: OPINE vs MPFR (exhaustive FP8)", T,
                   fp8_e5m2, fp8_e4m3, fp8_e4m3fnuz, RbjType<5, 2>,
                   RbjType<4, 3>, FastType<5, 2>, FastType<4, 3>) {
  verifyMinMax<T>();
}

// -----------------------------------------------------------------
// nextUp / nextDown — exhaustive value-scan reference
// -----------------------------------------------------------------
// The reference answer for nextUp(x) is found by brute force: scan
// all 256 patterns for non-NaN values strictly greater than x and
// take the least. Ties at zero prefer the -0 pattern (§5.3.1:
// nextUp of the largest negative subnormal is -0) — a preference
// that only ever finds -0 in formats that encode one, so fnuz gets
// +0 automatically. No strictly-greater value means x is the top of
// the range: nextUp(+Inf) = +Inf, and formats without an Inf
// encoding saturate at maxFinite. nextDown mirrors everything.
template <typename T> void verifyNextUpDown() {
  using BitsType = typename T::storage_type;
  auto Dec = decodeAll<T>();

  auto Ref = [&](unsigned I, bool Up) -> BitsType {
    if (mpfr_nan_p(Dec[I]))
      return opine::detail::packSpecial<T>(ValueCategory::NaN, false);
    int Best = -1;
    for (unsigned B = 0; B < 256; ++B) {
      if (mpfr_nan_p(Dec[B]))
        continue;
      const bool Beyond = Up ? mpfr_greater_p(Dec[B], Dec[I]) != 0
                             : mpfr_less_p(Dec[B], Dec[I]) != 0;
      if (!Beyond)
        continue;
      if (Best < 0) {
        Best = int(B);
        continue;
      }
      const bool Closer = Up ? mpfr_less_p(Dec[B], Dec[Best]) != 0
                             : mpfr_greater_p(Dec[B], Dec[Best]) != 0;
      if (Closer) {
        Best = int(B);
      } else if (mpfr_equal_p(Dec[B], Dec[Best]) && mpfr_zero_p(Dec[B])) {
        // Zero tie: nextUp lands on -0, nextDown on +0.
        const bool Prefer = Up ? mpfr_signbit(Dec[B]) != 0
                               : mpfr_signbit(Dec[B]) == 0;
        if (Prefer)
          Best = int(B);
      }
    }
    if (Best < 0)
      return canon<T>(BitsType(I)); // top/bottom of range: stays put
    return canon<T>(BitsType(unsigned(Best)));
  };

  int Failed = 0;
  for (unsigned I = 0; I < 256; ++I) {
    BitsType X = BitsType(I);
    bool Ok = nextUp<T>(X) == Ref(I, true) && nextDown<T>(X) == Ref(I, false);
    if (!Ok) {
      if (Failed < 5)
        MESSAGE("nextUp/nextDown mismatch at 0x" << std::hex << I);
      ++Failed;
    }
  }
  CHECK(Failed == 0);
}

TEST_CASE_TEMPLATE("nextUp/nextDown: OPINE vs value scan (exhaustive FP8)", T,
                   fp8_e5m2, fp8_e4m3, fp8_e4m3fnuz, RbjType<5, 2>,
                   RbjType<4, 3>, FastType<5, 2>, FastType<4, 3>) {
  verifyNextUpDown<T>();
}

// -----------------------------------------------------------------
// nextUp/nextDown at binary64 vs the host libm
// -----------------------------------------------------------------
TEST_CASE("nextUp/nextDown: binary64 vs std::nextafter") {
  using T = float64;
  using Storage = typename T::storage_type;

  auto CheckOne = [&](std::uint64_t U) {
    const double X = std::bit_cast<double>(U);
    if (std::isnan(X))
      return true;
    const Storage Bits = opine::detail::wordFromUint<Storage>(U);
    const std::uint64_t GotUp = opine::detail::lowUint64(nextUp<T>(Bits));
    const std::uint64_t GotDn = opine::detail::lowUint64(nextDown<T>(Bits));
    const std::uint64_t WantUp = std::bit_cast<std::uint64_t>(
        std::nextafter(X, std::numeric_limits<double>::infinity()));
    const std::uint64_t WantDn = std::bit_cast<std::uint64_t>(
        std::nextafter(X, -std::numeric_limits<double>::infinity()));
    return GotUp == WantUp && GotDn == WantDn;
  };

  const std::uint64_t Targeted[] = {
      0x0000000000000000ULL, // +0
      0x8000000000000000ULL, // -0
      0x0000000000000001ULL, // min subnormal
      0x8000000000000001ULL, // -min subnormal
      0x000FFFFFFFFFFFFFULL, // max subnormal
      0x0010000000000000ULL, // min normal
      0x7FEFFFFFFFFFFFFFULL, // max finite
      0xFFEFFFFFFFFFFFFFULL, // -max finite
      0x7FF0000000000000ULL, // +Inf
      0xFFF0000000000000ULL, // -Inf
      0x3FF0000000000000ULL, // 1.0
      0xBFF0000000000000ULL, // -1.0
  };
  int Failed = 0;
  for (std::uint64_t U : Targeted)
    if (!CheckOne(U))
      ++Failed;

  std::uint64_t S = 0x9E3779B97F4A7C15ULL;
  for (int I = 0; I < 100000; ++I) {
    S ^= S << 13;
    S ^= S >> 7;
    S ^= S << 17;
    if (!CheckOne(S))
      ++Failed;
  }
  CHECK(Failed == 0);
}

// -----------------------------------------------------------------
// copySign — exhaustive pairs vs a raw sign-transform reference
// -----------------------------------------------------------------
// Like neg/abs, copySign is non-computational: it rewrites the sign
// on the raw pattern without decoding, short-circuiting the fixed
// NaN patterns a sign transform would corrupt. The reference below
// states that in plain scalar arithmetic, so the library's word-ops
// path is exercised differentially.
template <typename T> void verifyCopySign() {
  using BitsType = typename T::storage_type;
  using Fmt = typename T::layout;
  using Num = typename T::number;
  constexpr int TotalBits = Fmt::total_bits;
  constexpr BitsType SignBit = BitsType{1} << Fmt::sign_offset;

  auto MaskWidth = [](BitsType Bt) -> BitsType {
    return Bt & maskLow<BitsType>(TotalBits);
  };
  auto IsFixedNan = [&](BitsType Bt) -> bool {
    if constexpr (Num::nan_encoding == NanEncoding::TrapValue)
      return Bt == BitsType{1} << (TotalBits - 1);
    else if constexpr (Num::nan_encoding ==
                       NanEncoding::NegativeZeroBitPattern)
      return Bt == SignBit;
    return false;
  };

  auto Ref = [&](BitsType X, BitsType Y) -> BitsType {
    if (IsFixedNan(X))
      return X;
    if constexpr (Num::value_sign == SignMethod::Explicit) {
      const bool Want = (Y & SignBit) != 0;
      return Want ? BitsType(X | SignBit) : BitsType(X & ~SignBit);
    } else if constexpr (Num::value_sign == SignMethod::RadixComplement) {
      constexpr BitsType Msb = BitsType{1} << (TotalBits - 1);
      if (((X & Msb) != 0) == ((Y & Msb) != 0))
        return X;
      return MaskWidth((~X) + BitsType{1});
    } else {
      constexpr BitsType Msb = BitsType{1} << (TotalBits - 1);
      if (((X & Msb) != 0) == ((Y & Msb) != 0))
        return X;
      return MaskWidth(~X);
    }
  };

  int Failed = 0;
  for (unsigned I = 0; I < 256; ++I) {
    for (unsigned J = 0; J < 256; ++J) {
      BitsType X = BitsType(I), Y = BitsType(J);
      if (copySign<T>(X, Y) != Ref(X, Y)) {
        if (Failed < 5)
          MESSAGE("copySign mismatch at x=0x" << std::hex << I << " y=0x"
                                              << J);
        ++Failed;
      }
    }
  }
  CHECK(Failed == 0);
}

TEST_CASE_TEMPLATE("copySign: OPINE vs raw transform (exhaustive FP8)", T,
                   fp8_e5m2, fp8_e4m3, fp8_e4m3fnuz, RbjType<5, 2>,
                   RbjType<4, 3>, FastType<5, 2>, FastType<4, 3>) {
  verifyCopySign<T>();
}
