// Compute π via Bailey–Borwein–Plouffe, at every precision the
// library has a bundle for.
//
// BBP: π = Σ_{k=0}^∞ 16⁻ᵏ ( 4/(8k+1) − 2/(8k+4) − 1/(8k+5) − 1/(8k+6) )
//
// Each term contributes ~4 bits, so an N-bit format wants ~N/4
// terms. Same computeBbp<T>() below; only the Type changes.
//
// To show how many digits landed we compare each format's answer
// to the float1024 answer (converted up, subtracted, unpacked). The
// magnitude of the difference lives in the biased exponent — one
// unpack call reports "correct bits" without needing decimal I/O.

#include <cstdio>

#include <opine/opine.hpp>

// binary256 / binary512 / binary1024 store their bits in a
// DigitVector of limbs — available on every compiler.
#define HAS_WIDE 1

template <typename T>
static typename T::storage_type computeBbp(int terms) {
  using namespace opine;
  using S = typename T::storage_type;

  // Every constant here is exact in float32, so fromNative on the
  // float literal is exact at every T.
  S pi = fromNative<T>(0.0f);
  S scale = fromNative<T>(1.0f);
  const S sixteenth = fromNative<T>(0.0625f); // 1/16 exactly
  const S one = fromNative<T>(1.0f);
  const S two = fromNative<T>(2.0f);
  const S four = fromNative<T>(4.0f);
  const S five = fromNative<T>(5.0f);
  const S six = fromNative<T>(6.0f);
  const S eight = fromNative<T>(8.0f);

  S k8 = fromNative<T>(0.0f); // 8k, walks up by 8 each step
  for (int k = 0; k < terms; ++k) {
    S term = sub<T>(
        sub<T>(sub<T>(div<T>(four, add<T>(k8, one)),
                      div<T>(two, add<T>(k8, four))),
               div<T>(one, add<T>(k8, five))),
        div<T>(one, add<T>(k8, six)));
    pi = add<T>(pi, mul<T>(term, scale));
    scale = mul<T>(scale, sixteenth);
    k8 = add<T>(k8, eight);
  }
  return pi;
}

#if HAS_WIDE
// How many leading bits of `computed` agree with the float1024
// reference. π sits in [2, 4) so its leading bit is at unbiased
// exponent 1; the diff's unbiased exponent is where the first
// disagreement lives, so agreed bits ≈ 1 − unbiased_exp(|diff|).
template <typename Src>
static int correctBits(typename Src::storage_type computed,
                       typename opine::float1024::storage_type reference) {
  using namespace opine;
  auto up = convert<float1024, Src>(computed);
  auto d = sub<float1024>(reference, up);
  auto ad = abs<float1024>(d);
  auto u = unpack<float1024>(ad);
  if (u.category == ValueCategory::Zero)
    return int(float1024::number::significand::digit_count);
  int biased = u.biased_exp == 0 ? 1 : u.biased_exp;
  int unbiased = biased - int(float1024::number::exponent_bias);
  int bits = 1 - unbiased;
  return bits < 0 ? 0 : bits;
}

template <typename T>
static void row(const char *name, int terms,
                typename opine::float1024::storage_type reference) {
  using namespace opine;
  auto v = computeBbp<T>(terms);
  std::printf("  %-11s %6d %-24.16g %d\n", name, terms,
              double(toDouble<T>(v)), correctBits<T>(v, reference));
}
#else
template <typename T> static void row(const char *name, int terms) {
  using namespace opine;
  auto v = computeBbp<T>(terms);
  std::printf("  %-11s %6d %-24.16g\n", name, terms, double(toDouble<T>(v)));
}
#endif

int main() {
  using namespace opine;

  std::printf("Bailey-Borwein-Plouffe: π = Σ 16^-k · (4/(8k+1) − 2/(8k+4)\n");
  std::printf("                                       − 1/(8k+5) − 1/(8k+6))\n\n");
  std::printf("Same code at every precision. Convergence is ~4 bits per\n");
  std::printf("term, capped at whatever the format itself can carry.\n\n");

#if HAS_WIDE
  const auto reference = computeBbp<float1024>(260);

  std::printf("  %-11s %-6s %-24s %s\n", "Format", "Terms",
              "π (first ~16 digits)", "Correct bits");
  std::printf("  %-11s %-6s %-24s %s\n", "-----------", "------",
              "------------------------", "------------");
  row<float32>("float32", 12, reference);
  row<float64>("float64", 18, reference);
  row<float128>("float128", 32, reference);
  row<float256>("float256", 63, reference);
  row<float512>("float512", 125, reference);
  row<float1024>("float1024", 255, reference);

  std::printf("\nThe printed digits show only what fits in a double; the\n");
  std::printf("computed value carries the reported number of bits internally.\n");
  std::printf("float1024 matches the reference exactly — computed with\n");
  std::printf("itself.\n");
#else
  std::printf("  %-11s %-6s %-24s\n", "Format", "Terms",
              "π (first ~16 digits)");
  std::printf("  %-11s %-6s %-24s\n", "-----------", "------",
              "------------------------");
  row<float32>("float32", 12);
  row<float64>("float64", 18);
  row<float128>("float128", 32);
  std::printf("\nWider precisions (binary256/512/1024) drop in on Clang;\n");
  std::printf("this GCC build stops at binary128.\n");
#endif
  return 0;
}
