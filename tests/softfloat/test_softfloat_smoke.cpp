// Smoke test: verify that SoftFloat builds and links correctly.
//
// IEEE 754 binary16:  1.0 = 0x3C00, 2.0 = 0x4000, 3.0 = 0x4200
// f16_add(1.0, 2.0) must produce 3.0.

#include <cstdint>
#include <cstdio>

extern "C" {
#include "softfloat.h"
}

int main() {
  softfloat_roundingMode = softfloat_round_near_even;
  softfloat_detectTininess = softfloat_tininess_afterRounding;
  softfloat_exceptionFlags = 0;

  float16_t a;
  a.v = 0x3C00;
  float16_t b;
  b.v = 0x4000;

  float16_t result = f16_add(a, b);

  if (result.v != 0x4200) {
    std::fprintf(stderr,
                 "FAIL: f16_add(0x%04X, 0x%04X) = 0x%04X, expected 0x4200\n",
                 a.v, b.v, result.v);
    return 1;
  }

  if (softfloat_exceptionFlags != 0) {
    std::fprintf(stderr, "FAIL: unexpected exceptions: 0x%02X\n",
                 static_cast<unsigned>(softfloat_exceptionFlags));
    return 1;
  }

  std::printf("PASS: f16_add(1.0, 2.0) = 3.0 (0x4200)\n");
  return 0;
}
