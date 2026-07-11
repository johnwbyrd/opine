// Fused multiply-add: one rounding, and why it matters.
//
// fma(a, b, c) computes a×b + c keeping the FULL product — twice
// the format's precision — alive until a single final rounding.
//
// Act one, the classic: with ε one unit in the last place of 1.0,
//
//     (1+ε)² − (1+2ε)  =  ε²   ...exactly.
//
// A rounded multiply must squeeze (1+ε)² = 1 + 2ε + ε² into the
// format first, and ε² sits far below the last bit: it rounds
// away, and the unfused pipeline answers 0. fma keeps it.
//
// Act two, the workhorse: for any x, y in the format,
//
//     err = fma(x, y, −(x·y rounded))
//
// is the EXACT rounding error of the multiply — not an estimate,
// the true value, always representable (Dekker's theorem). One
// instruction recovers what the multiply discarded; this identity
// is the foundation of double-double arithmetic and compensated
// summation.

#include <cstdio>

#include <opine/opine.hpp>

int main() {
  using namespace opine;

  // ---- Act one: the vanishing eps^2 ----------------------------
  using f32 = float32;
  auto one_eps = fromNative<f32>(1.0f + 1.19209290e-7f);   // 1 + 2^-23
  auto minus_one_2eps = neg<f32>(fromNative<f32>(1.0f + 2.38418579e-7f));

  auto unfused = add<f32>(mul<f32>(one_eps, one_eps), minus_one_2eps);
  auto fused = fma<f32>(one_eps, one_eps, minus_one_2eps);

  std::printf("(1+eps)^2 - (1+2eps), eps = 2^-23, in binary32:\n");
  std::printf("  mul then add : %s\n", toString<f32>(unfused).c_str());
  std::printf("  fma          : %s   (= eps^2 = 2^-46, exactly)\n\n",
              toString<f32>(fused).c_str());

  // ---- Act two: the exact error of a multiply ------------------
  // 1/3 can't be stored exactly; call the stored neighbor d. Then
  // 3 × d = 0.99999999999999994..., which the multiply rounds UP
  // to exactly 1.0. How big was that rounding step? fma says:
  using f64 = float64;
  auto third = fromNative<f64>(1.0 / 3.0);
  auto three = fromNative<f64>(3.0);

  auto p = mul<f64>(third, three);            // rounds to 1.0 exactly
  auto err = fma<f64>(third, three, neg<f64>(p)); // 3*d - 1.0, exact

  std::printf("p   = 3 * nearest(1/3)      : %s\n", toString<f64>(p).c_str());
  std::printf("err = fma(1/3, 3, -p)       : %s   (= -2^-54, the exact\n"
              "                              amount the multiply rounded up)\n",
              toString<f64>(err).c_str());

  // Sanity: p + err IS the true product — but adding them back in
  // double would just round to 1.0 again (that's why double-double
  // keeps the pair). Reconstruct in binary128, where the sum fits:
  using f128 = float128;
  auto true_product = add<f128>(convert<f128, f64>(p),
                                convert<f128, f64>(err));
  std::printf("p + err, summed in binary128: %s\n",
              toString<f128>(true_product, 20).c_str());
  return 0;
}
