// Same code, different Type.
//
// One templated helper evaluates the same expression under several
// encodings. Each result reflects that format's precision,
// denormal handling, and special-value encoding. The library's
// `add` / `sub` calls are the same call every time; the Type is
// what changes.

#include <cstdio>

#include <opine/opine.hpp>

// A printf field-width big enough for any storage_type up to 32
// bits (all Types used here).
template <typename T>
static unsigned long long asHex(typename T::storage_type bits) {
  return static_cast<unsigned long long>(bits);
}

template <typename T> static void run(const char *name) {
  using namespace opine;
  using S = typename T::storage_type;

  // The famous (0.1 + 0.2) − 0.3 ≠ 0 case, but in every format.
  S a = fromNative<T>(0.1f);
  S b = fromNative<T>(0.2f);
  S c = fromNative<T>(0.3f);

  S sum = add<T>(a, b);
  S err = sub<T>(sum, c);

  std::printf("  %-15s  0.1+0.2 = %-10g  (0.1+0.2)-0.3 = %-12g "
              "  bits: 0x%02llX 0x%02llX 0x%02llX\n",
              name, double(toFloat<T>(sum)), double(toFloat<T>(err)),
              asHex<T>(sum), asHex<T>(err), asHex<T>(c));
}

// Wider formats have wider bit patterns — separate helper for them.
template <typename T> static void runWide(const char *name) {
  using namespace opine;
  using S = typename T::storage_type;

  S a = fromNative<T>(0.1f);
  S b = fromNative<T>(0.2f);
  S c = fromNative<T>(0.3f);

  S sum = add<T>(a, b);
  S err = sub<T>(sum, c);

  std::printf("  %-15s  0.1+0.2 = %-15.10g  (0.1+0.2)-0.3 = %.5g\n",
              name, double(toFloat<T>(sum)), double(toFloat<T>(err)));
}

int main() {
  using namespace opine;

  std::printf("Same code, different Type — evaluate (0.1 + 0.2) − 0.3\n\n");

  std::printf("FP8 (8-bit storage):\n");
  run<fp8_e5m2>("fp8_e5m2");
  run<fp8_e4m3>("fp8_e4m3");
  run<fp8_e4m3fnuz>("fp8_e4m3fnuz");
  run<RbjType<4, 3>>("RbjType<4,3>");
  run<FastType<4, 3>>("FastType<4,3>");

  std::printf("\nWider IEEE 754:\n");
  runWide<bfloat16>("bfloat16");
  runWide<float16>("float16");
  runWide<float32>("float32");
  runWide<float64>("float64");

  return 0;
}
