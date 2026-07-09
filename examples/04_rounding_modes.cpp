// Rounding mode divergence.
//
// The value 1.0 + 2^−24 is exactly halfway between two FP32 values:
// 1.0 (bit pattern 0x3F800000) and 1.0 + ULP (0x3F800001). Under
// each rounding mode this halfway becomes a different output.
//
// Same Type, same expression, different Rounding axis — the only
// thing that changes is one template parameter.

#include <bit>
#include <cstdint>
#include <cstdio>

#include <opine/opine.hpp>

template <typename Rnd> static void run(const char *label) {
  using namespace opine;
  using T =
      Type<numbers::IEEE754<8, 23>, layouts::IEEE<8, 23, true>, Rnd>;
  using S = typename T::storage_type;

  // 2^−24 = 0x33800000.
  S one = fromNative<T>(1.0f);
  S half_ulp = fromNative<T>(std::bit_cast<float>(std::uint32_t{0x33800000}));
  S sum = add<T>(one, half_ulp);

  // And the negative version, which flips directed modes.
  S nsum = add<T>(fromNative<T>(-1.0f),
                   fromNative<T>(std::bit_cast<float>(std::uint32_t{0xB3800000})));

  std::printf("  %-22s  +1.0 + 2^-24 = 0x%08X (%.9g)   -1.0 - 2^-24 = 0x%08X\n",
              label, unsigned(sum), double(toFloat<T>(sum)), unsigned(nsum));
}

int main() {
  using namespace opine;
  std::printf("FP32 halfway between 1.0 and 1.0 + ULP, under each rounding mode:\n\n");
  run<rounding::ToNearestTiesToEven>("ToNearestTiesToEven");
  run<rounding::TowardZero>("TowardZero");
  run<rounding::TowardPositive>("TowardPositive");
  run<rounding::TowardNegative>("TowardNegative");
  std::printf("\nNote how the two directed modes swap sides between the +/- rows.\n");
  return 0;
}
