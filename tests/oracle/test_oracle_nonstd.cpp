// Oracle validation for non-IEEE encodings.
//
// SoftFloat cannot validate these — it only knows IEEE 754. Instead
// we run two kinds of check:
//
//   1. Round-trip semantics: for every FP8 bit pattern, decode →
//      re-encode → decode must yield the same real value (both NaN,
//      or both zero, or bit-equal MPFR values). This is stronger
//      than "bit-exact round trip" (which would be violated by
//      non-canonical NaN canonicalization and by encodings that
//      collapse -0 or flush subnormals), and it validates that
//      decode and re-encode are semantically inverse.
//
//   2. Encoding-specific structural invariants — the tdd.md step 3
//      property tests. rbj: signed integer comparison equals value
//      comparison (monotonic ordering); exactly 1 NaN + 2 Infs.
//      E4M3FNUZ: exactly one NaN, at 0x80. Relaxed: no NaN, no Inf.
//
// Both together substitute for the external cross-check that
// SoftFloat provides for IEEE formats.

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <cstdint>
#include <cstdio>
#include <doctest/doctest.h>

#include "harness/impl_mpfr.hpp"
#include "harness/test_harness.hpp"

using namespace opine;
using namespace opine::testing;

// -----------------------------------------------------------------
// Semantic round-trip
// -----------------------------------------------------------------
template <typename T> void exhaustiveRoundTrip() {
  using Storage = typename T::storage_type;
  constexpr int TotalBits = T::layout::total_bits;
  constexpr int Count = 1 << TotalBits;

  int failures = 0;
  for (int i = 0; i < Count; ++i) {
    Storage p = Storage(i);
    MpfrFloat v1 = decodeToMpfr<T>(p);
    Storage p2 = mpfrRoundToFormat<T>(v1);
    MpfrFloat v2 = decodeToMpfr<T>(p2);

    bool match;
    if (v1.isNan() && v2.isNan()) {
      match = true;
    } else if (v1.isNan() || v2.isNan()) {
      match = false;
    } else if (v1.isInf() && v2.isInf()) {
      match = (v1.isNegative() == v2.isNegative());
    } else if (v1.isInf() || v2.isInf()) {
      match = false;
    } else if (v1.isZero() && v2.isZero()) {
      // -0/+0 distinction only matters when the format has -0.
      if constexpr (T::number::negative_zero == NegativeZero::Exists)
        match = (v1.isNegative() == v2.isNegative());
      else
        match = true;
    } else if (v1.isZero() || v2.isZero()) {
      match = false;
    } else {
      match = (mpfr_cmp(v1.get(), v2.get()) == 0);
    }

    if (!match) {
      failures++;
      if (failures <= 5) {
        char b1[64], b2[64];
        mpfr_snprintf(b1, sizeof(b1), "%.20Rg", v1.get());
        mpfr_snprintf(b2, sizeof(b2), "%.20Rg", v2.get());
        std::fprintf(stderr,
                     "  round-trip 0x%02X → 0x%02X  v1=%s  v2=%s\n",
                     unsigned(p), unsigned(p2), b1, b2);
      }
    }
  }
  CHECK(failures == 0);
}

TEST_CASE_TEMPLATE("round-trip: non-IEEE FP8", T, fp8_e4m3fnuz,
                   RbjType<5, 2>, RbjType<4, 3>, FastType<5, 2>,
                   FastType<4, 3>) {
  exhaustiveRoundTrip<T>();
}

// -----------------------------------------------------------------
// Property: rbj monotonic ordering
// -----------------------------------------------------------------
// For all non-NaN bit patterns a, b:
//   signed_int(a) < signed_int(b)   iff   real_val(a) < real_val(b)
//   signed_int(a) == signed_int(b)  iff   real_val(a) == real_val(b)
//
// This is rbj's defining property — it's what makes float
// comparison degenerate to signed-integer comparison.

template <typename T> void monotonicOrdering() {
  using Storage = typename T::storage_type;
  constexpr int TotalBits = T::layout::total_bits;
  static_assert(TotalBits == 8, "8-bit only for now");
  constexpr int Count = 256;

  int failures = 0;

  for (int i = 0; i < Count; ++i) {
    Storage a = Storage(i);
    MpfrFloat va = decodeToMpfr<T>(a);
    if (va.isNan())
      continue;
    int sa = int(int8_t(uint8_t(a)));

    for (int j = 0; j < Count; ++j) {
      Storage b = Storage(j);
      MpfrFloat vb = decodeToMpfr<T>(b);
      if (vb.isNan())
        continue;
      int sb = int(int8_t(uint8_t(b)));

      bool int_lt = sa < sb;
      bool val_lt = mpfr_less_p(va.get(), vb.get()) != 0;
      bool int_eq = (sa == sb);
      bool val_eq = mpfr_equal_p(va.get(), vb.get()) != 0;

      if (int_lt != val_lt || int_eq != val_eq) {
        failures++;
        if (failures <= 5) {
          char ba[64], bb[64];
          mpfr_snprintf(ba, sizeof(ba), "%.10Rg", va.get());
          mpfr_snprintf(bb, sizeof(bb), "%.10Rg", vb.get());
          std::fprintf(stderr,
                       "  ordering: 0x%02X (%s) vs 0x%02X (%s)  "
                       "int_lt=%d val_lt=%d int_eq=%d val_eq=%d\n",
                       unsigned(a), ba, unsigned(b), bb, int_lt, val_lt,
                       int_eq, val_eq);
        }
      }
    }
  }
  CHECK(failures == 0);
}

TEST_CASE_TEMPLATE("property: rbj monotonic ordering", T, RbjType<5, 2>,
                   RbjType<4, 3>) {
  monotonicOrdering<T>();
}

// -----------------------------------------------------------------
// Property: special-value counts
// -----------------------------------------------------------------
template <typename T>
void countSpecials(int expected_nan, int expected_inf) {
  using Storage = typename T::storage_type;
  constexpr int TotalBits = T::layout::total_bits;
  constexpr int Count = 1 << TotalBits;
  int nans = 0, infs = 0;
  Storage nan_pattern = 0;

  for (int i = 0; i < Count; ++i) {
    Storage p = Storage(i);
    MpfrFloat v = decodeToMpfr<T>(p);
    if (v.isNan()) {
      nans++;
      nan_pattern = p;
    } else if (v.isInf()) {
      infs++;
    }
  }
  CHECK(nans == expected_nan);
  CHECK(infs == expected_inf);
  if (expected_nan == 1 &&
      T::number::nan_encoding == NanEncoding::NegativeZeroBitPattern) {
    // NaN must be at the -0 bit pattern.
    CHECK(unsigned(nan_pattern) ==
          (1u << T::layout::sign_offset));
  }
  if (expected_nan == 1 &&
      T::number::nan_encoding == NanEncoding::TrapValue) {
    // NaN must be at the trap value (MSB set, all others zero).
    CHECK(unsigned(nan_pattern) == (1u << (TotalBits - 1)));
  }
}

TEST_CASE("property: E4M3FNUZ has 1 NaN and 0 Infs") {
  countSpecials<fp8_e4m3fnuz>(1, 0);
}

TEST_CASE_TEMPLATE("property: rbj FP8 has 1 NaN + 2 Infs", T, RbjType<5, 2>,
                   RbjType<4, 3>) {
  countSpecials<T>(1, 2);
}

TEST_CASE_TEMPLATE("property: Relaxed FP8 has 0 NaN + 0 Inf", T,
                   FastType<5, 2>, FastType<4, 3>) {
  countSpecials<T>(0, 0);
}
