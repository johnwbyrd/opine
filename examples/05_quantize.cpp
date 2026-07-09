// Cross-format quantization tour.
//
// For a handful of interesting values, convert into every currently
// supported small format and report the round-tripped value plus
// the relative error. This is the shape you'd use to pick a
// quantization format for an ML workload.

#include <cmath>
#include <cstdio>

#include <opine/opine.hpp>

template <typename T> static void quantize(const char *name, double v) {
  using namespace opine;
  using S = typename T::storage_type;

  S bits = fromNative<T>(v);
  double back = toDouble<T>(bits);
  double err = back - v;
  double relerr = (v == 0.0) ? 0.0 : err / v;

  const int hex_digits = (T::layout::total_bits + 3) / 4;
  std::printf("    %-14s bits=0x%0*llX  value=%-17.10g  relerr=%+.3e\n",
              name, hex_digits,
              static_cast<unsigned long long>(bits),
              back, relerr);
}

static void tour(const char *label, double v) {
  using namespace opine;
  std::printf("\n== %s = %.15g ==\n", label, v);
  quantize<fp8_e5m2>("fp8_e5m2", v);
  quantize<fp8_e4m3>("fp8_e4m3", v);
  quantize<fp8_e4m3fnuz>("fp8_e4m3fnuz", v);
  quantize<bfloat16>("bfloat16", v);
  quantize<float16>("float16", v);
  quantize<float32>("float32", v);
}

int main() {
  std::printf("Quantize a few values into every supported small format.\n");
  tour("pi", 3.14159265358979323846);
  tour("1/3", 1.0 / 3.0);
  tour("e", 2.71828182845904523536);
  tour("1e-5", 1e-5);
  tour("1e5", 1e5);
  return 0;
}
