// OPINE-vs-MPFR conversion tests (TDD step 11).
//
// The oracle for convert<Dst, Src> is the composition of the two
// existing oracle halves: decodeToMpfr<Src> produces the exact
// value of the source bit pattern, and mpfrRoundToFormat<Dst>
// renders it under Dst's Number + Layout + Rounding policies.
// Conversion is the only operation whose oracle needed zero new
// code.
//
// Coverage:
//   - Exhaustive FP8 → FP8 across every ordered pair of the seven
//     FP8 encodings (the first tests to cross Number semantics:
//     IEEE → fnuz, rbj → Relaxed, ...).
//   - Exhaustive FP8 → {float16, bfloat16, float32} and
//     float16 / bfloat16 → smaller and larger formats.
//   - Structural + exponent-stratified + random sampling for the
//     wide pairs (float32/float64/extFloat80/float128), both
//     widening and narrowing.
//   - Round-trip theorems: where exact_conversion<Src, Dst> holds,
//     convert<Src, Dst>(convert<Dst, Src>(x)) == x for every
//     non-NaN pattern, and NaN patterns come back as Src's NaN.

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include <cstdint>
#include <cstdio>
#include <random>
#include <vector>

#include "harness/wide_formats.hpp"

#include "harness/impl_mpfr.hpp"
#include "harness/test_harness.hpp"
#include "opine/core/convert.hpp"

using namespace opine;
using namespace opine::testing;

// -----------------------------------------------------------------
// exact_conversion trait: compile-time expectations
// -----------------------------------------------------------------
// Positive: the IEEE widening chain, bfloat16 into binary32,
// extFloat80 into float128, and every FP8 encoding into float16.
static_assert(exact_conversion<fp8_e5m2, float16>);
static_assert(exact_conversion<fp8_e4m3, float16>);
static_assert(exact_conversion<fp8_e4m3fnuz, float16>);
static_assert(exact_conversion<RbjType<4, 3>, float16>);
static_assert(exact_conversion<float16, float32>);
static_assert(exact_conversion<bfloat16, float32>);
static_assert(exact_conversion<float32, float64>);
static_assert(exact_conversion<float64, float128>);
static_assert(exact_conversion<extFloat80, float128>);
// Negative: bfloat16 and float16 each lack a dimension the other
// has (range vs precision); narrowing is never exact.
static_assert(!exact_conversion<float16, bfloat16>);
static_assert(!exact_conversion<bfloat16, float16>);
static_assert(!exact_conversion<float32, float16>);
static_assert(!exact_conversion<float64, float32>);
static_assert(!exact_conversion<float128, extFloat80>);

// -----------------------------------------------------------------
// Failure printing (two different storage widths, so the harness's
// single-BitsType reporters don't fit)
// -----------------------------------------------------------------
template <typename SrcBits, typename DstBits>
void reportFailure(const char *Name, SrcBits In, DstBits Got, DstBits Want,
                   int SrcHexWidth, int DstHexWidth) {
  std::fprintf(stderr, "  FAIL %s: x=0x", Name);
  printHex(stderr, In, SrcHexWidth);
  std::fprintf(stderr, "  opine=0x");
  printHex(stderr, Got, DstHexWidth);
  std::fprintf(stderr, " oracle=0x");
  printHex(stderr, Want, DstHexWidth);
  std::fprintf(stderr, "\n");
}

// -----------------------------------------------------------------
// verifyConvert — compare opine::convert against the composed
// oracle for every value the iterator produces
// -----------------------------------------------------------------
template <typename Src, typename Dst, typename Iter>
void verifyConvert(const char *Name, Iter &&Values) {
  using SrcBits = typename Src::storage_type;
  using DstBits = typename Dst::storage_type;
  constexpr int SrcHexWidth = (Src::layout::total_bits + 3) / 4;
  constexpr int DstHexWidth = (Dst::layout::total_bits + 3) / 4;

  int Failed = 0;
  int Total = 0;
  Values([&](SrcBits X) {
    ++Total;
    DstBits Got = convert<Dst, Src>(X);
    DstBits Want = mpfrRoundToFormat<Dst>(decodeToMpfr<Src>(X));
    if (Got != Want) {
      if (Failed < 5)
        reportFailure(Name, X, Got, Want, SrcHexWidth, DstHexWidth);
      ++Failed;
    }
  });
  std::printf("%s: %d/%d passed\n", Name, Total - Failed, Total);
  CHECK(Failed == 0);
}

template <typename Src, typename Dst>
void verifyConvertExhaustive(const char *Name) {
  constexpr int TotalBits = Src::layout::total_bits;
  static_assert(TotalBits <= 16, "exhaustive sweep only up to 16 bits");
  verifyConvert<Src, Dst>(
      Name, ExhaustiveSingles<typename Src::storage_type, TotalBits>{});
}

// Structural + every-binade-stratified + uniform-random singles,
// the same battery generic_binary_test uses for wide formats.
template <typename Src, typename Dst>
void verifyConvertSampled(const char *Name) {
  using SrcBits = typename Src::storage_type;
  std::vector<SrcBits> Values = structuralValues<Src>();
  ExponentStratifiedSingles<Src, 2>{0xC0417E47ULL}([&](SrcBits X) {
    Values.push_back(X);
  });
  RandomSingles<SrcBits, Src::layout::total_bits>{0x0417E5EEDULL, 2000}(
      [&](SrcBits X) { Values.push_back(X); });

  verifyConvert<Src, Dst>(Name, [&](auto &&Callback) {
    for (SrcBits X : Values)
      Callback(X);
  });
}

// -----------------------------------------------------------------
// Round-trip theorem
// -----------------------------------------------------------------
// Where exact_conversion<Src, Dst> holds, going out and back is the
// identity on every non-NaN bit pattern (NaN payloads canonicalize,
// so NaN patterns must merely come back as NaN).
template <typename Src, typename Dst> void verifyRoundTrip(const char *Name) {
  static_assert(exact_conversion<Src, Dst>,
                "round-trip theorem requires an exact conversion");
  using SrcBits = typename Src::storage_type;
  constexpr int TotalBits = Src::layout::total_bits;
  static_assert(TotalBits <= 16, "exhaustive sweep only up to 16 bits");
  constexpr int HexWidth = (TotalBits + 3) / 4;
  constexpr uint64_t Count = uint64_t{1} << TotalBits;

  int Failed = 0;
  for (uint64_t I = 0; I < Count; ++I) {
    SrcBits X = SrcBits(I);
    SrcBits Back = convert<Src, Dst>(convert<Dst, Src>(X));
    bool Ok;
    if (unpack<Src>(X).category == ValueCategory::NaN)
      Ok = unpack<Src>(Back).category == ValueCategory::NaN;
    else
      Ok = Back == X;
    if (!Ok) {
      if (Failed < 5) {
        std::fprintf(stderr, "  FAIL %s round-trip: x=0x", Name);
        printHex(stderr, X, HexWidth);
        std::fprintf(stderr, " back=0x");
        printHex(stderr, Back, HexWidth);
        std::fprintf(stderr, "\n");
      }
      ++Failed;
    }
  }
  std::printf("%s: %llu round-trips\n", Name,
              (unsigned long long)(Count - Failed));
  CHECK(Failed == 0);
}

// -----------------------------------------------------------------
// Exhaustive FP8 → FP8, all ordered encoding pairs
// -----------------------------------------------------------------
template <typename Src> void convertToAllFp8(const char *SrcName) {
  char Name[96];
  auto run = [&](auto DstTag, const char *DstName) {
    using Dst = decltype(DstTag);
    std::snprintf(Name, sizeof(Name), "%s->%s", SrcName, DstName);
    verifyConvertExhaustive<Src, Dst>(Name);
  };
  run(fp8_e5m2{}, "e5m2");
  run(fp8_e4m3{}, "e4m3");
  run(fp8_e4m3fnuz{}, "e4m3fnuz");
  run(RbjType<5, 2>{}, "rbj52");
  run(RbjType<4, 3>{}, "rbj43");
  run(FastType<5, 2>{}, "fast52");
  run(FastType<4, 3>{}, "fast43");
}

TEST_CASE("convert: FP8 -> FP8, all encoding pairs (exhaustive)") {
  convertToAllFp8<fp8_e5m2>("e5m2");
  convertToAllFp8<fp8_e4m3>("e4m3");
  convertToAllFp8<fp8_e4m3fnuz>("e4m3fnuz");
  convertToAllFp8<RbjType<5, 2>>("rbj52");
  convertToAllFp8<RbjType<4, 3>>("rbj43");
  convertToAllFp8<FastType<5, 2>>("fast52");
  convertToAllFp8<FastType<4, 3>>("fast43");
}

// -----------------------------------------------------------------
// Exhaustive FP8 <-> 16/32-bit formats
// -----------------------------------------------------------------
TEST_CASE("convert: FP8 -> float16/bfloat16/float32 (exhaustive)") {
  verifyConvertExhaustive<fp8_e5m2, float16>("e5m2->f16");
  verifyConvertExhaustive<fp8_e4m3, float16>("e4m3->f16");
  verifyConvertExhaustive<fp8_e4m3fnuz, float16>("e4m3fnuz->f16");
  verifyConvertExhaustive<RbjType<4, 3>, float16>("rbj43->f16");
  verifyConvertExhaustive<fp8_e5m2, bfloat16>("e5m2->bf16");
  verifyConvertExhaustive<fp8_e4m3, bfloat16>("e4m3->bf16");
  verifyConvertExhaustive<fp8_e5m2, float32>("e5m2->f32");
  verifyConvertExhaustive<fp8_e4m3, float32>("e4m3->f32");
}

TEST_CASE("convert: float16 -> narrower and wider (exhaustive)") {
  verifyConvertExhaustive<float16, fp8_e5m2>("f16->e5m2");
  verifyConvertExhaustive<float16, fp8_e4m3>("f16->e4m3");
  verifyConvertExhaustive<float16, fp8_e4m3fnuz>("f16->e4m3fnuz");
  verifyConvertExhaustive<float16, RbjType<4, 3>>("f16->rbj43");
  verifyConvertExhaustive<float16, bfloat16>("f16->bf16");
  verifyConvertExhaustive<float16, float32>("f16->f32");
}

TEST_CASE("convert: bfloat16 -> float16/float32 (exhaustive)") {
  verifyConvertExhaustive<bfloat16, float16>("bf16->f16");
  verifyConvertExhaustive<bfloat16, float32>("bf16->f32");
}

// -----------------------------------------------------------------
// Wide formats: structural + stratified + random
// -----------------------------------------------------------------
TEST_CASE("convert: wide formats (sampled)") {
  verifyConvertSampled<float32, float16>("f32->f16");
  verifyConvertSampled<float32, float64>("f32->f64");
  verifyConvertSampled<float64, float32>("f64->f32");
  verifyConvertSampled<float64, float128>("f64->f128");
  verifyConvertSampled<float128, float64>("f128->f64");
  verifyConvertSampled<extFloat80, float128>("extF80->f128");
  verifyConvertSampled<float128, extFloat80>("f128->extF80");
  verifyConvertSampled<float128, fp8_e4m3>("f128->e4m3");
}

// -----------------------------------------------------------------
// Round-trip theorems
// -----------------------------------------------------------------
TEST_CASE("convert: round-trip identity where exact_conversion holds") {
  verifyRoundTrip<fp8_e5m2, float16>("e5m2<->f16");
  verifyRoundTrip<fp8_e4m3, float16>("e4m3<->f16");
  verifyRoundTrip<fp8_e4m3fnuz, float16>("e4m3fnuz<->f16");
  verifyRoundTrip<RbjType<4, 3>, float16>("rbj43<->f16");
  verifyRoundTrip<fp8_e5m2, float32>("e5m2<->f32");
  verifyRoundTrip<float16, float32>("f16<->f32");
  verifyRoundTrip<bfloat16, float32>("bf16<->f32");
}

// -----------------------------------------------------------------
// binary256/512/1024 (Clang lane: scalar wide storage)
// -----------------------------------------------------------------
#if OPINE_TEST_HAS_WIDE_STORAGE

// The binary{k} ladder widens exactly at every rung.
static_assert(exact_conversion<float128, float256>);
static_assert(exact_conversion<float256, float512>);
static_assert(exact_conversion<float512, float1024>);
static_assert(exact_conversion<float64, float1024>);
static_assert(exact_conversion<extFloat80, float256>);
static_assert(!exact_conversion<float1024, float512>);

TEST_CASE("convert: wide formats (sampled, binary256/1024)") {
  verifyConvertSampled<float64, float1024>("f64->f1024");
  verifyConvertSampled<float1024, float64>("f1024->f64");
  verifyConvertSampled<float128, float256>("f128->f256");
  verifyConvertSampled<float256, float128>("f256->f128");
  verifyConvertSampled<float1024, fp8_e4m3>("f1024->e4m3");
}

// The embedding theorem, realized: exact_conversion<f64, f1024>
// holds, so out-and-back is the identity on every non-NaN double —
// subnormals, max finite, and signed zeros included. This is the
// cross-width consistency check that catches limb-boundary bugs.
TEST_CASE("convert: f64 embeds exactly in f1024 (round-trip)") {
  std::mt19937_64 rng(0xE1BED);
  int failed = 0;
  auto check = [&](std::uint64_t bits) {
    using SrcBits = float64::storage_type;
    SrcBits x = SrcBits(bits);
    if (unpack<float64>(x).category == ValueCategory::NaN)
      return;
    if (convert<float64, float1024>(convert<float1024, float64>(x)) != x)
      ++failed;
  };
  for (int i = 0; i < 200000; ++i)
    check(rng());
  for (std::uint64_t v :
       {std::uint64_t(0), std::uint64_t(1), std::uint64_t(0x8000000000000000),
        std::uint64_t(0x7FEFFFFFFFFFFFFF), std::uint64_t(0x0010000000000000),
        std::uint64_t(0x000FFFFFFFFFFFFF), std::uint64_t(0x3FF0000000000000),
        std::uint64_t(0x7FF0000000000000)})
    check(v);
  CHECK(failed == 0);
}

#endif // OPINE_TEST_HAS_WIDE_STORAGE

// -----------------------------------------------------------------
// Native bridges
// -----------------------------------------------------------------
TEST_CASE("convert: native float/double bridges") {
  // 1.5 is exact in every binary format down to FP8.
  CHECK(toFloat<fp8_e4m3>(fromNative<fp8_e4m3>(1.5f)) == 1.5f);
  CHECK(toDouble<float16>(fromNative<float16>(-0.375)) == -0.375);
  // Round-trip through float32 is the identity on float.
  CHECK(toFloat<float32>(fromNative<float32>(3.14159265f)) == 3.14159265f);
  // Narrowing rounds: 1/3 in FP8 E4M3 is 0.34375 (0x2B).
  CHECK(uint64_t(fromNative<fp8_e4m3>(1.0f / 3.0f)) == 0x2B);
}
