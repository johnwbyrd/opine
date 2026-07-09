// Rump's polynomial — the canonical precision-ladder disaster.
//
// f(x, y) = 333.75·y⁶ + x²·(11·x²·y² − y⁶ − 121·y⁴ − 2) + 5.5·y⁸ + x/(2y)
// at x = 77617, y = 33096.
//
// True value: −0.827396059946821368141165095479816291999...
//
// Rump chose the polynomial in 1988 to punish floating-point
// arithmetic. Every term is enormous; every partial cancels
// against another partial to within a rounding error of the true
// answer. IEEE double confidently returns +1.17…, wrong sign.
// binary128 doesn't save you either. binary256 finally gets it,
// binary1024 gets it and then some.
//
// The library has one arithmetic pipeline — this demo just
// instantiates it at half a dozen widths and prints what falls out.

#include <cstdio>

#include <opine/opine.hpp>

// The wide bundles (float256/512/1024) store their bits in a
// DigitVector of limbs — available on every compiler.
#define HAS_WIDE 1

// Cheap x^N via repeated squaring — only + and * needed.
template <typename T>
static typename T::storage_type pow_int(typename T::storage_type x,
                                        unsigned n) {
  using namespace opine;
  using S = typename T::storage_type;
  S r = fromNative<T>(1.0f);
  S b = x;
  while (n) {
    if (n & 1u)
      r = mul<T>(r, b);
    b = mul<T>(b, b);
    n >>= 1u;
  }
  return r;
}

template <typename T> static double rump() {
  using namespace opine;
  using S = typename T::storage_type;

  // Constants: 77617, 33096, 333.75, 11, 121, 5.5, 2, 0.5, 1 —
  // every one exactly representable in float32, so fromNative<T>
  // of the float literal is exact.
  S x = fromNative<T>(77617.0f);
  S y = fromNative<T>(33096.0f);
  S c1 = fromNative<T>(333.75f);
  S c2 = fromNative<T>(11.0f);
  S c3 = fromNative<T>(121.0f);
  S c4 = fromNative<T>(5.5f);
  S c5 = fromNative<T>(2.0f);
  S c6 = fromNative<T>(0.5f); // 1/2 exact, avoid /2

  S x2 = mul<T>(x, x);
  S y2 = mul<T>(y, y);
  S y4 = mul<T>(y2, y2);
  S y6 = mul<T>(y4, y2);
  S y8 = mul<T>(y4, y4);

  // A = 333.75·y^6
  S A = mul<T>(c1, y6);

  // B = x^2·(11·x^2·y^2 − y^6 − 121·y^4 − 2)
  S t1 = mul<T>(mul<T>(c2, x2), y2);
  S t2 = sub<T>(t1, y6);
  S t3 = sub<T>(t2, mul<T>(c3, y4));
  S inner = sub<T>(t3, c5);
  S B = mul<T>(x2, inner);

  // C = 5.5·y^8
  S C = mul<T>(c4, y8);

  // D = x / (2y) = x · 0.5 / y
  S D = div<T>(mul<T>(x, c6), y);

  S result = add<T>(add<T>(add<T>(A, B), C), D);
  return toDouble<T>(result);
}

int main() {
  using namespace opine;

  std::printf("Rump's polynomial at (x, y) = (77617, 33096).\n");
  std::printf("True value ≈ -0.82739605994682136814116509547981629...\n\n");

  std::printf("  %-14s %-30s %s\n", "Format", "Computed", "Verdict");
  std::printf("  %-14s %-30s %s\n", "------", "--------", "-------");

  auto row = [](const char *name, double v) {
    const char *verdict;
    if (v > 0.0)                       verdict = "wrong sign";
    else if (v < -100.0 || v > -0.5)   verdict = "wrong magnitude";
    else                                verdict = "correct";
    std::printf("  %-14s %-30.16g %s\n", name, v, verdict);
  };

  row("float32", rump<float32>());
  row("float64", rump<float64>());
  row("float128", rump<float128>());
#if HAS_WIDE
  row("float256", rump<float256>());
  row("float512", rump<float512>());
  row("float1024", rump<float1024>());
#endif

#if HAS_WIDE
  std::printf("\nfloat1024 gets ~300 correct decimal digits — the printout\n");
  std::printf("is capped at ~16 by toDouble; the extra digits are inside\n");
  std::printf("the storage word, they just have nowhere to go on stdout.\n");
#else
  std::printf("\nWider precisions (binary256/512/1024) drop in on Clang;\n");
  std::printf("this GCC build stops at binary128.\n");
#endif
  return 0;
}
