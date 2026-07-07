// Exhaustive FP8 round-trip: for every 8-bit pattern in every
// supported FP8 encoding, verify that pack(unpack(x)) either
// reproduces x exactly (non-NaN) or maps to a NaN (NaN).
//
// This is TDD step 4's pass gate — the first proof that
// Number + Layout + pack/unpack is coherent for an entire format.

#include <cstdio>

#include "opine/opine.hpp"

using namespace opine;

template <typename T> struct RoundTripResult {
  int total = 0;
  int failed_exact = 0; // non-NaN inputs where pack(unpack(x)) != x
  int failed_nan = 0;   // NaN inputs whose repack decoded to non-NaN
};

template <typename T> RoundTripResult<T> runExhaustive(const char *label) {
  using Storage = typename T::storage_type;
  constexpr int TotalBits = T::layout::total_bits;
  constexpr int Count = 1 << TotalBits;

  RoundTripResult<T> R;

  for (int i = 0; i < Count; ++i) {
    Storage bits = Storage(i);
    auto u = unpack<T>(bits);
    Storage repacked = pack<T>(u);
    R.total++;

    if (u.category == ValueCategory::NaN) {
      auto u2 = unpack<T>(repacked);
      if (u2.category != ValueCategory::NaN) {
        R.failed_nan++;
        if (R.failed_nan <= 5) {
          std::fprintf(stderr,
                       "  %s: NaN input 0x%02X repacked to non-NaN 0x%02X\n",
                       label, unsigned(bits), unsigned(repacked));
        }
      }
    } else if (repacked != bits) {
      R.failed_exact++;
      if (R.failed_exact <= 5) {
        std::fprintf(stderr,
                     "  %s: 0x%02X → cat=%d sign=%d exp=%d sig=0x%X → repack "
                     "0x%02X\n",
                     label, unsigned(bits), int(u.category), int(u.sign),
                     u.biased_exp, unsigned(u.significand), unsigned(repacked));
      }
    }
  }

  int failures = R.failed_exact + R.failed_nan;
  std::printf("%s: %d/%d exact + NaN-preserving", label,
              R.total - failures, R.total);
  if (failures > 0)
    std::printf("  (FAIL exact=%d nan=%d)", R.failed_exact, R.failed_nan);
  std::printf("\n");
  return R;
}

int main() {
  int total_failures = 0;

  {
    auto r = runExhaustive<fp8_e5m2>("fp8_e5m2");
    total_failures += r.failed_exact + r.failed_nan;
  }
  {
    auto r = runExhaustive<fp8_e4m3>("fp8_e4m3");
    total_failures += r.failed_exact + r.failed_nan;
  }
  {
    auto r = runExhaustive<fp8_e4m3fnuz>("fp8_e4m3fnuz");
    total_failures += r.failed_exact + r.failed_nan;
  }
  {
    auto r = runExhaustive<RbjType<5, 2>>("rbj_5_2");
    total_failures += r.failed_exact + r.failed_nan;
  }
  {
    auto r = runExhaustive<RbjType<4, 3>>("rbj_4_3");
    total_failures += r.failed_exact + r.failed_nan;
  }

  return total_failures == 0 ? 0 : 1;
}
