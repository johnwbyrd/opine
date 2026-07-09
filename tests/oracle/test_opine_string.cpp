// String conversion verification.
//
//   - toString's digit decomposition vs mpfr_get_str: both produce
//     correctly rounded D-digit decimals from the same exact value,
//     so the digit strings and exponents must match exactly.
//   - fromString vs mpfr_set_str + mpfrRoundToFormat: bit-exact.
//   - Round-trip theorem: fromString(toString(x)) == x for every
//     non-NaN pattern (roundTripDigits guarantees it), exhaustively
//     at FP8/FP16 and sampled at wider widths including binary1024.
//   - toHexString vs strtod("%a"): exact by construction.
//   - Flags through the parse path (inexact / overflow / underflow /
//     invalid), since fromString feeds the shared epilogue.

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include <cstdint>
#include <cstdlib>
#include <random>
#include <string>

#include "harness/impl_mpfr.hpp"
#include "harness/test_harness.hpp"
#include "opine/core/convert.hpp"
#include "opine/core/string.hpp"

using namespace opine;
using namespace opine::testing;

// -----------------------------------------------------------------
// toString decomposition vs mpfr_get_str
// -----------------------------------------------------------------
template <typename T>
void verifyDigitsVsMpfr(const char *Name, int D, int samples,
                        std::uint64_t seed, bool all_in_window = true) {
  using Bits = typename T::storage_type;
  int failed = 0, total = 0, skipped = 0;
  auto check = [&](Bits x) {
    const auto u = unpack<T>(x);
    if (u.category != ValueCategory::Finite)
      return;
    ++total;
    auto mine = opine::detail::decimalDigits<T>(x, D);
    if (!mine.ok) {
      // Out of the decimal window: a failure for formats the window
      // fully covers, an expected skip for the widest ones.
      if (all_in_window)
        ++failed;
      else
        ++skipped;
      return;
    }
    MpfrFloat v = decodeToMpfr<T>(x);
    mpfr_exp_t exp10 = 0;
    char *str = mpfr_get_str(nullptr, &exp10, 10, size_t(D), v, MPFR_RNDN);
    std::string want(str[0] == '-' ? str + 1 : str);
    bool wneg = str[0] == '-';
    mpfr_free_str(str);
    if (mine.digits != want || mine.k10 != long(exp10) - 1 ||
        mine.neg != wneg) {
      if (failed < 5)
        std::fprintf(stderr, "  FAIL %s: got %s e%ld want %s e%ld\n", Name,
                     mine.digits.c_str(), mine.k10, want.c_str(),
                     long(exp10) - 1);
      ++failed;
    }
  };
  std::vector<Bits> vals = structuralValues<T>();
  RandomSingles<Bits, T::layout::total_bits> rnd{seed, samples};
  rnd([&](Bits x) { vals.push_back(x); });
  for (Bits x : vals)
    check(x);
  std::printf("%s: %d/%d digit decompositions (%d outside window)\n", Name,
              total - failed - skipped, total, skipped);
  CHECK(failed == 0);
}

// In-window binary1024 samples: random float64 values embedded
// exactly (exact_conversion holds), so every one is printable.
inline std::vector<float1024::storage_type> wideInWindowSamples(int n,
                                                                std::uint64_t seed) {
  std::vector<float1024::storage_type> out;
  RandomSingles<float64::storage_type, 64> rnd{seed, n};
  rnd([&](float64::storage_type x) {
    if (unpack<float64>(x).category == ValueCategory::NaN)
      return;
    out.push_back(convert<float1024, float64>(x));
  });
  return out;
}

TEST_CASE("string: toString digits vs mpfr_get_str") {
  verifyDigitsVsMpfr<float32>("f32 x9", 9, 3000, 0x51);
  verifyDigitsVsMpfr<float64>("f64 x17", 17, 3000, 0x52);
  verifyDigitsVsMpfr<float64>("f64 x3", 3, 1000, 0x53);
  verifyDigitsVsMpfr<float128>("f128 x36", 36, 500, 0x54);
  verifyDigitsVsMpfr<extFloat80>("extF80 x21", 21, 500, 0x55);
  verifyDigitsVsMpfr<fp8_e4m3>("e4m3 x4", 4, 300, 0x56);
  verifyDigitsVsMpfr<float1024>("f1024 x50", 50, 20, 0x57,
                                /*all_in_window=*/false);
  // Guaranteed-in-window binary1024 coverage: embedded doubles.
  int failed = 0;
  for (auto x : wideInWindowSamples(150, 0x58)) {
    auto mine = opine::detail::decimalDigits<float1024>(x, 40);
    MpfrFloat v = decodeToMpfr<float1024>(x);
    mpfr_exp_t exp10 = 0;
    char *str = mpfr_get_str(nullptr, &exp10, 10, 40, v, MPFR_RNDN);
    std::string want(str[0] == '-' ? str + 1 : str);
    mpfr_free_str(str);
    if (!mine.ok || mine.digits != want || mine.k10 != long(exp10) - 1)
      ++failed;
  }
  CHECK(failed == 0);
}

// -----------------------------------------------------------------
// fromString vs mpfr_set_str + round-to-format
// -----------------------------------------------------------------
template <typename T>
void verifyParseVsMpfr(const char *Name, int samples, std::uint64_t seed) {
  using Bits = typename T::storage_type;
  std::mt19937_64 rng(seed);
  int failed = 0;
  for (int i = 0; i < samples; ++i) {
    // Random decimal literal: 1-25 digits, point somewhere, e±k.
    std::string t;
    if (rng() & 1)
      t += '-';
    int nd = 1 + int(rng() % 25);
    for (int j = 0; j < nd; ++j)
      t += char('0' + rng() % 10);
    if (rng() & 1)
      t.insert(t.size() - rng() % nd, ".");
    if (rng() & 1) {
      t += 'e';
      t += (rng() & 1) ? '+' : '-';
      t += std::to_string(rng() % 60);
    }

    Bits got;
    if constexpr (std::is_same_v<typename T::exceptions,
                                 exceptions::ReturnStatus>)
      got = fromString<T>(t).bits;
    else
      got = fromString<T>(t);

    MpfrFloat v{oraclePrecision<T>};
    mpfr_set_str(v, t.c_str(), 10, MPFR_RNDN);
    Bits want = mpfrRoundToFormat<T>(v);
    if (!(got == want)) {
      if (failed < 5)
        std::fprintf(stderr, "  FAIL %s: \"%s\"\n", Name, t.c_str());
      ++failed;
    }
  }
  std::printf("%s: %d/%d parses\n", Name, samples - failed, samples);
  CHECK(failed == 0);
}

TEST_CASE("string: fromString vs mpfr_set_str") {
  verifyParseVsMpfr<float32>("f32", 5000, 0x61);
  verifyParseVsMpfr<float64>("f64", 5000, 0x62);
  verifyParseVsMpfr<float128>("f128", 1500, 0x63);
  verifyParseVsMpfr<fp8_e4m3>("e4m3", 2000, 0x64);
  verifyParseVsMpfr<bfloat16>("bf16", 2000, 0x65);
  using F32Up = Type<numbers::IEEE754<8, 23>, layouts::IEEE<8, 23, true>,
                     rounding::TowardPositive>;
  verifyParseVsMpfr<F32Up>("f32/RU", 3000, 0x66);
  using F32Dn = Type<numbers::IEEE754<8, 23>, layouts::IEEE<8, 23, true>,
                     rounding::TowardZero>;
  verifyParseVsMpfr<F32Dn>("f32/RZ", 3000, 0x67);
}

// -----------------------------------------------------------------
// Round-trip theorem
// -----------------------------------------------------------------
template <typename T>
void verifyRoundTrip(const char *Name, int samples, std::uint64_t seed) {
  using Bits = typename T::storage_type;
  int failed = 0, total = 0;
  // roundTripDigits guarantees recovery under round-to-NEAREST
  // parsing; formats whose Rounding axis is directed (FastType)
  // parse through a nearest-rounding sibling for the theorem.
  using Nearest = Type<typename T::number, typename T::layout,
                       rounding::ToNearestTiesToEven, typename T::exceptions,
                       typename T::platform>;
  auto check = [&](Bits x) {
    ++total;
    std::string s = toString<T>(x);
    Bits back = fromString<Nearest>(s);
    // The string pipeline sees the canonical value: x87 unnormals
    // canonicalize and FlushInputs formats flush, exactly as in
    // arithmetic. The theorem is bit-identity with that form (the
    // identity itself for every canonical pattern).
    Bits expected = pack<T>(opine::detail::unpackOperand<T>(x));
    bool ok;
    if (unpack<T>(x).category == ValueCategory::NaN)
      ok = unpack<T>(back).category == ValueCategory::NaN;
    else
      ok = back == expected;
    if (!ok) {
      if (failed < 5)
        std::fprintf(stderr, "  FAIL %s roundtrip: \"%s\"\n", Name, s.c_str());
      ++failed;
    }
  };
  if constexpr (T::layout::total_bits <= 16) {
    constexpr std::uint64_t N = std::uint64_t{1} << T::layout::total_bits;
    for (std::uint64_t i = 0; i < N; ++i)
      check(Bits(std::uint64_t(i)));
    (void)samples;
    (void)seed;
  } else {
    for (Bits x : structuralValues<T>())
      check(x);
    RandomSingles<Bits, T::layout::total_bits> rnd{seed, samples};
    rnd([&](Bits x) { check(x); });
  }
  std::printf("%s: %d/%d round-trips\n", Name, total - failed, total);
  CHECK(failed == 0);
}

TEST_CASE("string: round-trip identity at roundTripDigits") {
  verifyRoundTrip<fp8_e5m2>("e5m2", 0, 0);
  verifyRoundTrip<fp8_e4m3>("e4m3", 0, 0);
  verifyRoundTrip<fp8_e4m3fnuz>("fnuz", 0, 0);
  verifyRoundTrip<RbjType<4, 3>>("rbj43", 0, 0);
  verifyRoundTrip<FastType<4, 3>>("fast43", 0, 0);
  verifyRoundTrip<float16>("f16", 0, 0);
  verifyRoundTrip<bfloat16>("bf16", 0, 0);
  verifyRoundTrip<float32>("f32", 20000, 0x71);
  verifyRoundTrip<float64>("f64", 10000, 0x72);
  verifyRoundTrip<extFloat80>("extF80", 3000, 0x73);
  verifyRoundTrip<float128>("f128", 2000, 0x74);
  // Wide formats: random bit patterns are mostly outside the decimal
  // window (their exponents reach 2^26), so round-trip embedded
  // doubles — exact in both directions, every one printable.
  int failed = 0, total = 0;
  for (auto x : wideInWindowSamples(400, 0x75)) {
    ++total;
    std::string s = toString<float1024>(x);
    if (!(fromString<float1024>(s) == x))
      ++failed;
  }
  std::printf("f1024/embedded: %d/%d round-trips\n", total - failed, total);
  CHECK(failed == 0);
}

// -----------------------------------------------------------------
// Hex floats: exact, verified against the platform's strtod
// -----------------------------------------------------------------
TEST_CASE("string: toHexString round-trips through strtod (f64)") {
  std::mt19937_64 rng(0x81);
  int failed = 0;
  RandomSingles<float64::storage_type, 64> rnd{0x81, 20000};
  rnd([&](float64::storage_type x) {
    if (unpack<float64>(x).category == ValueCategory::NaN)
      return;
    std::string s = toHexString<float64>(x);
    double d = std::strtod(s.c_str(), nullptr);
    if (!(fromNative<float64>(d) == x))
      ++failed;
  });
  CHECK(failed == 0);
}

// -----------------------------------------------------------------
// Formatting spot checks + flags through the parse path
// -----------------------------------------------------------------
TEST_CASE("string: formatting spot checks") {
  auto f = [](double v, int d = 17) {
    return toString<float64>(fromNative<float64>(v), d);
  };
  CHECK(f(1.5, 6) == "1.5");
  CHECK(f(0.0) == "0");
  CHECK(f(-0.0) == "-0");
  CHECK(f(1024.0, 6) == "1024");
  CHECK(f(0.1, 17) == "0.10000000000000001");
  CHECK(f(1e30, 6) == "1e+30");
  CHECK(f(-2.5e-7, 6) == "-2.5e-7");
  CHECK(toHexString<float64>(fromNative<float64>(1.5)) == "0x1.8p+0");
  CHECK(toHexString<float64>(fromNative<float64>(-2.0)) == "-0x1p+1");

  // binary1024: parse 50 digits of pi, print them back.
  const std::string pi50 =
      "3.1415926535897932384626433832795028841971693993751";
  auto p = fromString<float1024>(pi50);
  std::string out = toString<float1024>(p, 50);
  CHECK(out.substr(0, pi50.size()) == pi50);

  // float64 embeds: printing the embedded value gives the same text.
  auto wide = convert<float1024, float64>(fromNative<float64>(1.5));
  CHECK(toString<float1024>(wide, 6) == "1.5");
}

TEST_CASE("string: parse flags via ReturnStatus") {
  using T = Type<numbers::IEEE754<4, 3>, layouts::IEEE<4, 3, true>,
                 rounding::Default, exceptions::ReturnStatus>;
  auto exact = fromString<T>("1.5");
  CHECK(exact.flags == FlagNone);
  auto inx = fromString<T>("0.1");
  CHECK((inx.flags & FlagInexact) != 0);
  auto over = fromString<T>("1e30");
  CHECK((over.flags & FlagOverflow) != 0);
  CHECK((over.flags & FlagInexact) != 0);
  auto under = fromString<T>("1e-30");
  CHECK((under.flags & FlagUnderflow) != 0);
  auto bad = fromString<T>("pineapple");
  CHECK((bad.flags & FlagInvalid) != 0);
  CHECK(unpack<T>(bad.bits).category == ValueCategory::NaN);
  auto negzero = fromString<T>("-0.0");
  CHECK(unpack<T>(negzero.bits).category == ValueCategory::Zero);
}
