// Hello, OPINE.
//
// The simplest program: pick a Type, evaluate an expression on
// storage bits, and print the result via the native bridge.

#include <cstdio>

#include <opine/opine.hpp>

int main() {
  using namespace opine;

  // float32 is Type<numbers::IEEE754<8,23>, layouts::IEEE<8,23>>
  // with the default Rounding (ToNearestTiesToEven).
  using f32 = float32;

  // fromNative<T> constructs the OPINE bit pattern from a native
  // float literal. add<T> operates on bit patterns and returns bit
  // patterns; the bits are the currency of the library.
  f32::storage_type x = fromNative<f32>(1.5f);
  f32::storage_type y = fromNative<f32>(2.25f);
  f32::storage_type z = add<f32>(x, y);

  // toFloat<T> converts back to a native float for display.
  std::printf("1.5 + 2.25 = %g   (bit pattern 0x%08X)\n",
              double(toFloat<f32>(z)), unsigned(z));
  return 0;
}
