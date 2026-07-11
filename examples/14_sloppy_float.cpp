// The sloppy soft-float, measured.
//
// WithComputePrecision<float32, K> stores ordinary binary32 bits
// but computes every operation on operands truncated to their top
// K significand bits. On a CPU with no hardware multiplier (the
// 6502s of the world), K=8 turns a 24×24-bit software multiply
// into a single-byte one — IF the accuracy holds up for the
// workload. This program is the "if": the same computations at
// every K, with the error against full precision measured, so the
// K you ship is a decision made on data.
//
//   1. Per-operation error at each K: multiply relative error
//      (max and mean — note the mean's sign: truncation is biased
//      low, errors do not cancel) and addition error per unit of
//      the larger operand.
//
//   2. A real computation: naive Mandelbrot escape iteration at
//      the seahorse valley, counting how many of 60 iterations
//      match the full-precision escape trajectory.
//
// Everything runs on the host: this is the study you do BEFORE
// writing a single line of target assembly, against the same
// bit-exact semantics that assembly will be tested against.

#include <cmath>
#include <cstdio>

#include <opine/opine.hpp>

using namespace opine;

template <int K> using Sloppy = WithComputePrecision<float32, K>;

// xorshift for reproducible operand streams.
static std::uint64_t rngState = 0x243F6A8885A308D3ULL;
static std::uint64_t next64() {
  rngState ^= rngState << 13;
  rngState ^= rngState >> 7;
  rngState ^= rngState << 17;
  return rngState;
}
// A random float in [0.5, 2) x random sign x spread exponents: the
// regime where truncation error, not over/underflow, dominates.
static float randomOperand() {
  const float mant = 0.5f + float(next64() & 0xFFFFFF) / float(0x2000000);
  const int e = int(next64() % 21) - 10; // 2^-10 .. 2^10
  const float sign = (next64() & 1) ? 1.0f : -1.0f;
  return sign * std::ldexp(mant, e);
}

// Multiply error is relative to the exact product — the clean
// ~2^-(K-1) story. Addition error is normalized by the larger
// operand: near-cancelling adds have unbounded RELATIVE error at
// ANY reduced precision (bfloat16 included), so "error per unit of
// input" is the honest per-op metric there.
template <int K> static void opError(const char *label) {
  using S = Sloppy<K>;
  double maxMul = 0, sumMul = 0, maxAdd = 0;
  const int N = 20000;
  rngState = 0x243F6A8885A308D3ULL; // same operands at every K
  for (int i = 0; i < N; ++i) {
    const float a = randomOperand(), b = randomOperand();
    const double exactMul = double(a) * double(b);
    const double exactAdd = double(a) + double(b);
    const double gotMul =
        double(toFloat<S>(mul<S>(fromNative<S>(a), fromNative<S>(b))));
    const double gotAdd =
        double(toFloat<S>(add<S>(fromNative<S>(a), fromNative<S>(b))));
    const double rm = (gotMul - exactMul) / exactMul;
    const double na = (gotAdd - exactAdd) /
                      std::fmax(std::fabs(double(a)), std::fabs(double(b)));
    if (std::fabs(rm) > maxMul)
      maxMul = std::fabs(rm);
    if (std::fabs(na) > maxAdd)
      maxAdd = std::fabs(na);
    sumMul += rm;
  }
  std::printf("  %-6s mul max %.2e  mul mean %+.2e   add max %.2e\n", label,
              maxMul, sumMul / N, maxAdd);
}

// Naive Mandelbrot escape at c in the seahorse valley: iterate
// z = z^2 + c and compare the escape trajectory against double.
template <int K> static int mandelMatches() {
  using S = Sloppy<K>;
  const double cr = -0.74543, ci = 0.11301;
  double zr = 0, zi = 0;
  auto szr = fromNative<S>(0.0f), szi = fromNative<S>(0.0f);
  const auto scr = fromNative<S>(float(cr)), sci = fromNative<S>(float(ci));
  int matches = 0;
  for (int i = 0; i < 60; ++i) {
    // double reference
    const double r2 = zr * zr - zi * zi + cr;
    zi = 2 * zr * zi + ci;
    zr = r2;
    // sloppy
    const auto zr2 = sub<S>(mul<S>(szr, szr), mul<S>(szi, szi));
    const auto nzi =
        add<S>(mul<S>(add<S>(szr, szr), szi), sci); // 2*zr*zi + ci
    szr = add<S>(zr2, scr);
    szi = nzi;
    // do the trajectories still agree to ~1%?
    const double gr = double(toFloat<S>(szr)), gi = double(toFloat<S>(szi));
    const double mag = std::sqrt(zr * zr + zi * zi);
    if (mag > 0 && std::hypot(gr - zr, gi - zi) / mag < 0.01)
      ++matches;
  }
  return matches;
}

int main() {
  std::printf("binary32 storage, K-bit compute — per-op error over\n");
  std::printf("20000 random pairs (mul: relative; add: per unit of\n");
  std::printf("the larger operand; mul mean's sign is the bias):\n\n");
  opError<4>("K=4");
  opError<6>("K=6");
  opError<8>("K=8");
  opError<12>("K=12");
  opError<16>("K=16");
  opError<24>("K=24");

  std::printf("\nMandelbrot z=z^2+c at the seahorse valley: iterations\n");
  std::printf("(of 60) where the sloppy trajectory stays within 1%% of\n");
  std::printf("the double-precision one:\n\n");
  std::printf("  K=4    %d\n", mandelMatches<4>());
  std::printf("  K=8    %d\n", mandelMatches<8>());
  std::printf("  K=12   %d\n", mandelMatches<12>());
  std::printf("  K=16   %d\n", mandelMatches<16>());
  std::printf("  K=24   %d\n", mandelMatches<24>());

  std::printf("\nPick the cheapest K your workload tolerates; the\n");
  std::printf("semantics are bit-exact and testable, so a target\n");
  std::printf("assembly implementation has a spec to match.\n");
  return 0;
}
