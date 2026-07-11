// IEEE 754 §7.4 overflow: Inf or saturation, mode-dependent.
//
// When a result overflows the format's largest finite magnitude,
// §7.4 says it becomes ±∞ only when the rounding mode carries the
// magnitude *upward*: to-nearest always, TowardZero never, and the
// directed modes only on their own side of zero. Otherwise the
// result saturates to the largest finite value.
//
// Both inputs to the multiplications below are exact powers of two,
// so the mode has no effect on their conversion from `float`; only
// the overflow behavior of the result varies.

#include <cstdio>

#include <opine/opine.hpp>

// Reparameterize IEEE E5M2 and E4M3FNUZ on the Rounding axis.
template <typename Rnd>
using IeeeRnd = opine::Type<opine::numbers::IEEE754<5, 2>,
                            opine::layouts::IEEE<5, 2, true>, Rnd>;

template <typename Rnd>
using FnuzRnd = opine::Type<opine::numbers::E4M3FNUZ,
                            opine::layouts::IEEE<4, 3, true>, Rnd>;

template <typename T>
static void run(const char *label, float a, float b) {
  using namespace opine;
  using S = typename T::storage_type;

  S ap = fromNative<T>(a),  bp = fromNative<T>(b);
  S an = fromNative<T>(-a);
  S pos = mul<T>(ap, bp);
  S neg = mul<T>(an, bp);

  std::printf("  %-20s  +overflow → 0x%02llX (%9g)    -overflow → 0x%02llX (%9g)\n",
              label,
              static_cast<unsigned long long>(pos), double(toFloat<T>(pos)),
              static_cast<unsigned long long>(neg), double(toFloat<T>(neg)));
}

int main() {
  using namespace opine;

  std::printf("Overflow behavior under §7.4, rounding-mode by rounding-mode:\n");
  std::printf("  RNDN → always ±Inf         RNDZ → always saturate\n");
  std::printf("  RNDU → +Inf on +, sat on −  RNDD → sat on +, −Inf on −\n\n");

  // FP8 E5M2 (max finite = 57344). 128 × 512 = 65536 overflows;
  // both inputs are exact powers of two → mode-independent.
  std::printf("IEEE FP8 E5M2 (inf_encoding = ReservedExponent), 128 × 512:\n");
  run<IeeeRnd<rounding::ToNearestTiesToEven>>("ToNearestTiesToEven", 128.f, 512.f);
  run<IeeeRnd<rounding::ToNearestTiesAway>>("ToNearestTiesAway", 128.f, 512.f);
  run<IeeeRnd<rounding::TowardZero>>("TowardZero", 128.f, 512.f);
  run<IeeeRnd<rounding::TowardPositive>>("TowardPositive", 128.f, 512.f);
  run<IeeeRnd<rounding::TowardNegative>>("TowardNegative", 128.f, 512.f);
  run<IeeeRnd<rounding::ToOdd>>("ToOdd", 128.f, 512.f);

  // FP8 E4M3FNUZ (max finite = 240, no Inf). 16 × 32 = 512
  // overflows; the format has no Inf encoding so §7.4's "Inf"
  // outcome saturates too — every mode gives ±max_finite.
  std::printf("\nFP8 E4M3FNUZ (inf_encoding = None), 16 × 32:\n");
  run<FnuzRnd<rounding::ToNearestTiesToEven>>("ToNearestTiesToEven", 16.f, 32.f);
  run<FnuzRnd<rounding::TowardZero>>("TowardZero", 16.f, 32.f);
  run<FnuzRnd<rounding::TowardPositive>>("TowardPositive", 16.f, 32.f);
  run<FnuzRnd<rounding::TowardNegative>>("TowardNegative", 16.f, 32.f);

  // Whatever the bits say, the Exceptions axis says WHY. The same
  // overflowing multiply under the ReturnStatus policy hands back
  // {bits, flags}; saturation without a flag would be silent data
  // corruption, saturation WITH a flag is a diagnosable event.
  using E5M2Status =
      Type<numbers::IEEE754<5, 2>, layouts::IEEE<5, 2, true>,
           rounding::TowardZero, exceptions::ReturnStatus>;
  auto r = mul<E5M2Status>(fromNative<E5M2Status>(128.f).bits,
                           fromNative<E5M2Status>(512.f).bits);
  std::printf("\nSame overflow under exceptions::ReturnStatus (TowardZero):\n"
              "  bits 0x%02llX (%g)  overflow=%d inexact=%d\n",
              static_cast<unsigned long long>(r.bits),
              double(toFloat<E5M2Status>(r.bits)),
              int((r.flags & FlagOverflow) != 0),
              int((r.flags & FlagInexact) != 0));
  return 0;
}
