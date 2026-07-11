// What your floats REALLY are: correctly rounded text, both ways.
//
// Three demonstrations of the string machinery:
//
//   1. "0.1" is a lie. No binary format stores it; each stores its
//      own nearest neighbor. toString with enough digits prints
//      that neighbor EXACTLY — every digit correct, at any width.
//
//   2. The round-trip guarantee: toString's default digit count is
//      chosen so fromString gives back the identical bit pattern.
//
//   3. sqrt across the precision ladder: sqrt(2) computed by the
//      library at five widths, printed to each format's full
//      accuracy. Every printed digit is a true digit of √2 —
//      correctly rounded arithmetic and correctly rounded printing
//      composing across three orders of magnitude of precision.

#include <cstdio>
#include <string>

#include <opine/opine.hpp>

int main() {
  using namespace opine;

  // ---- 1. What "0.1" actually stores ---------------------------
  std::printf("the value stored for \"0.1\", exactly:\n");
  {
    auto show = [](auto tag, const char *name, int digits) {
      using T = decltype(tag);
      auto x = fromString<T>("0.1");
      std::printf("  %-8s %s\n", name, toString<T>(x, digits).c_str());
    };
    show(fp8_e4m3{}, "fp8", 10);
    show(float16{}, "float16", 20);
    show(float32{}, "float32", 30);
    show(float64{}, "float64", 60);
  }

  // ---- 2. The round-trip guarantee ------------------------------
  {
    using f64 = float64;
    auto x = fromNative<f64>(0.1);
    std::string s = toString<f64>(x); // default: round-trip digits
    auto back = fromString<f64>(s);
    std::printf("\ntoString(0.1) -> \"%s\" -> fromString: bits %s\n",
                s.c_str(), x == back ? "identical" : "DIFFERENT (bug!)");
  }

  // ---- 3. sqrt(2) up the precision ladder -----------------------
  std::printf("\nsqrt(2), computed and printed at each width:\n");
  {
    auto ladder = [](auto tag, const char *name, int digits) {
      using T = decltype(tag);
      auto two = fromString<T>("2");
      std::printf("  %-9s %s\n", name, toString<T>(sqrt<T>(two), digits).c_str());
    };
    ladder(float32{}, "float32", 9);
    ladder(float64{}, "float64", 17);
    ladder(float128{}, "float128", 36);
    ladder(float256{}, "float256", 60);
    // binary1024: 997 bits ≈ 300 decimal digits, all of them right.
    using f1k = float1024;
    auto root = sqrt<f1k>(fromString<f1k>("2"));
    std::printf("  float1024 %s\n", toString<f1k>(root, 100).c_str());
  }
  return 0;
}
