// Roll your own floating-point format.
//
// Not every workload benefits from a standard width. Suppose you
// wanted a 12-bit float: enough exponent to cover the FP16 dynamic
// range, but with more mantissa than FP8. Spell it as one line of
// Type<...>, and every operation just works.
//
// The compiler produces a specialized implementation for the exact
// widths; nothing here is dispatched at runtime.

#include <cmath>
#include <cstdio>

#include <opine/opine.hpp>

int main() {
  using namespace opine;

  // 12 bits total: 1 sign + 5 exponent + 6 mantissa.
  using MyFp12 = Type<numbers::IEEE754<5, 6>, layouts::IEEE<5, 6, true>>;

  // Compile-time properties, verifiable via static_assert.
  static_assert(MyFp12::layout::total_bits == 12);
  static_assert(MyFp12::number::exponent_bias == 15);
  static_assert(MyFp12::number::significand::digit_count == 7);

  auto compare = [](const char *name, double v) {
    using S8 = fp8_e5m2::storage_type;
    using S12 = MyFp12::storage_type;
    using S16 = float16::storage_type;

    S8 fp8 = fromNative<fp8_e5m2>(v);
    S12 fp12 = fromNative<MyFp12>(v);
    S16 fp16 = fromNative<float16>(v);

    std::printf("  %-10s target=%-17.10g  fp8_e5m2=%-13.6g  MyFp12=%-13.6g  "
                "float16=%-13.6g\n",
                name, v,
                double(toFloat<fp8_e5m2>(fp8)),
                double(toFloat<MyFp12>(fp12)),
                double(toFloat<float16>(fp16)));
  };

  std::printf("MyFp12 = Type<IEEE754<5,6>, IEEE<5,6>>: 5-bit exp, 6-bit mant, 12 bits total.\n");
  std::printf("Same exponent range as fp8_e5m2 and float16; precision between them.\n\n");

  compare("pi", 3.14159265358979);
  compare("e", 2.71828182845905);
  compare("1/3", 1.0 / 3.0);
  compare("1e4", 1e4);
  compare("1e-4", 1e-4);
  compare("max", 65504.0); // float16 max
  compare("2^-14", std::ldexp(1.0, -14));
  return 0;
}
