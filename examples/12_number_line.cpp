// Walking the number line: nextUp, nextDown, classification, and
// the minimum/maximum family.
//
// Floating-point values aren't a continuum — they're a finite,
// walkable set of points. nextUp<T>(x) is the very next point
// above x; in an 8-bit format the WHOLE line is small enough to
// stroll end to end and census on the way.
//
// Along the way: ulp sizes across formats (how far apart the
// points are near 1.0), the IEEE minimum/maximum rules for NaN and
// signed zero, and copySign.

#include <cstdio>

#include <opine/opine.hpp>

int main() {
  using namespace opine;

  // ---- Census: walk all of fp8_e4m3, -max to +max --------------
  using fp8 = fp8_e4m3;
  {
    const auto top = fromNative<fp8>(240.0f); // the format's maxFinite
    auto x = neg<fp8>(top);
    int values = 0, subnormals = 0, normals = 0, zeros = 0;
    for (;;) {
      ++values;
      if (isSubnormal<fp8>(x)) ++subnormals;
      else if (isNormal<fp8>(x)) ++normals;
      else if (isZero<fp8>(x)) ++zeros;
      if (eq<fp8>(x, top))
        break;
      x = nextUp<fp8>(x);
    }
    std::printf("fp8_e4m3, walked with nextUp from -240 to +240:\n"
                "  %d points on the line "
                "(%d normal, %d subnormal, %d zero)\n\n",
                values, normals, subnormals, zeros);
  }

  // ---- ulp: the gap between neighbors, format by format --------
  std::printf("distance from 1.0 to the next value up:\n");
  {
    auto gap = [](auto tag, const char *name) {
      using T = decltype(tag);
      auto one = fromNative<T>(1.0f);
      auto up = nextUp<T>(one);
      auto ulp = sub<T>(up, one);
      std::printf("  %-10s %s\n", name, toString<T>(ulp).c_str());
    };
    gap(fp8_e4m3{}, "fp8_e4m3");
    gap(bfloat16{}, "bfloat16");
    gap(float16{}, "float16");
    gap(float32{}, "float32");
    gap(float64{}, "float64");
  }

  // ---- minimum/maximum: the NaN and signed-zero rules ----------
  using f32 = float32;
  {
    auto one = fromNative<f32>(1.0f);
    auto nan = opine::detail::packSpecial<f32>(ValueCategory::NaN, false);
    auto pz = fromNative<f32>(0.0f);
    auto nz = neg<f32>(pz);

    // minimum propagates NaN; minimumNumber prefers the number.
    std::printf("\nminimum(1, NaN)        = %s   (NaN wins)\n",
                toString<f32>(minimum<f32>(one, nan)).c_str());
    std::printf("minimumNumber(1, NaN)  = %s   (the number wins)\n",
                toString<f32>(minimumNumber<f32>(one, nan)).c_str());

    // -0 and +0 compare equal, but minimum/maximum can tell them
    // apart: -0 orders below +0.
    std::printf("eq(-0, +0)             = %d   (equal as values...)\n",
                int(eq<f32>(nz, pz)));
    std::printf("minimum(-0, +0)        = %s  (...but -0 sorts first)\n",
                toString<f32>(minimum<f32>(nz, pz)).c_str());
  }

  // ---- copySign: sign surgery, no arithmetic -------------------
  {
    auto x = fromNative<f32>(2.5f);
    auto y = fromNative<f32>(-7.0f);
    std::printf("copySign(2.5, -7)      = %s\n",
                toString<f32>(copySign<f32>(x, y)).c_str());
  }
  return 0;
}
