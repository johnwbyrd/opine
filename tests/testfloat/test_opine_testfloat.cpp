// TDD step 5: TestFloat conformance shim.
//
// Berkeley TestFloat's genCases generator supplies the adversarial
// input patterns used to validate hardware FPUs; the patched
// SoftFloat fork (itself cross-validated against slowfloat and the
// MPFR oracle) supplies the reference results; OPINE must match
// bit-for-bit, NaN-aware (OPINE canonicalizes NaNs, SoftFloat
// propagates payloads — any-NaN-matches-any-NaN).
//
// Coverage: FP16 and FP32, all four supported rounding modes for
// add, sub, mul, and div, plus the mode-independent comparisons
// (eq, lt, le).
// Exception flags are not compared — OPINE doesn't implement them
// yet (TDD step 12).

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <type_traits>

extern "C" {
#include "softfloat.h"
#include "genCases.h"
}

#include "opine/opine.hpp"

using namespace opine;

namespace {

// ---------------------------------------------------------------
// Per-width bindings to SoftFloat and the genCases sequence
// ---------------------------------------------------------------

struct F16Gen {
  static constexpr const char *name() { return "f16"; }
  static constexpr int E = 5, M = 10;
  using SfType = float16_t;
  static void init() { genCases_f16_ab_init(); }
  static bool done() { return genCases_done; }
  static void next() { genCases_f16_ab_next(); }
  static SfType a() { return genCases_f16_a; }
  static SfType b() { return genCases_f16_b; }
  static uint64_t bits(SfType s) { return s.v; }
  static SfType add(SfType x, SfType y) { return f16_add(x, y); }
  static SfType sub(SfType x, SfType y) { return f16_sub(x, y); }
  static SfType mul(SfType x, SfType y) { return f16_mul(x, y); }
  static SfType div(SfType x, SfType y) { return f16_div(x, y); }
  static bool eq(SfType x, SfType y) { return f16_eq(x, y); }
  static bool lt(SfType x, SfType y) { return f16_lt_quiet(x, y); }
  static bool le(SfType x, SfType y) { return f16_le_quiet(x, y); }
};

struct F32Gen {
  static constexpr const char *name() { return "f32"; }
  static constexpr int E = 8, M = 23;
  using SfType = float32_t;
  static void init() { genCases_f32_ab_init(); }
  static bool done() { return genCases_done; }
  static void next() { genCases_f32_ab_next(); }
  static SfType a() { return genCases_f32_a; }
  static SfType b() { return genCases_f32_b; }
  static uint64_t bits(SfType s) { return s.v; }
  static SfType add(SfType x, SfType y) { return f32_add(x, y); }
  static SfType sub(SfType x, SfType y) { return f32_sub(x, y); }
  static SfType mul(SfType x, SfType y) { return f32_mul(x, y); }
  static SfType div(SfType x, SfType y) { return f32_div(x, y); }
  static bool eq(SfType x, SfType y) { return f32_eq(x, y); }
  static bool lt(SfType x, SfType y) { return f32_lt_quiet(x, y); }
  static bool le(SfType x, SfType y) { return f32_le_quiet(x, y); }
};

template <typename Rnd> constexpr uint_fast8_t softfloatMode() {
  if constexpr (std::is_same_v<Rnd, rounding::TowardZero>)
    return softfloat_round_minMag;
  else if constexpr (std::is_same_v<Rnd, rounding::TowardPositive>)
    return softfloat_round_max;
  else if constexpr (std::is_same_v<Rnd, rounding::TowardNegative>)
    return softfloat_round_min;
  else
    return softfloat_round_near_even;
}

template <typename Rnd> constexpr const char *roundingName() {
  if constexpr (std::is_same_v<Rnd, rounding::TowardZero>)
    return "TowardZero";
  else if constexpr (std::is_same_v<Rnd, rounding::TowardPositive>)
    return "TowardPositive";
  else if constexpr (std::is_same_v<Rnd, rounding::TowardNegative>)
    return "TowardNegative";
  else
    return "ToNearestTiesToEven";
}

template <typename T> bool isNanBits(typename T::storage_type bits) {
  return unpack<T>(bits).category == ValueCategory::NaN;
}

// Run one binary op through the full genCases ab sequence.
// RefFn: SoftFloat reference; OpineFn: bits → bits.
template <typename Gen, typename T, typename RefFn, typename OpineFn>
void runBinary(const char *opName, const char *rndName, RefFn ref,
               OpineFn opineFn) {
  using Bits = typename T::storage_type;

  genCases_setLevel(1);
  srand(1); // TestFloat's random cases come from libc rand()
  Gen::init();

  long total = 0, failed = 0;
  while (!Gen::done()) {
    Gen::next();
    const uint64_t ua = Gen::bits(Gen::a());
    const uint64_t ub = Gen::bits(Gen::b());
    const uint64_t want = Gen::bits(ref(Gen::a(), Gen::b()));
    const Bits got = opineFn(Bits(ua), Bits(ub));
    total++;
    const bool ok = uint64_t(got) == want ||
                    (isNanBits<T>(Bits(want)) && isNanBits<T>(got));
    if (!ok) {
      failed++;
      if (failed <= 10)
        std::printf("  FAIL %s %s %s: a=0x%llX b=0x%llX ref=0x%llX "
                    "opine=0x%llX\n",
                    Gen::name(), opName, rndName,
                    (unsigned long long)ua, (unsigned long long)ub,
                    (unsigned long long)want, (unsigned long long)got);
    }
  }
  std::printf("%s %s %s: %ld/%ld passed\n", Gen::name(), opName, rndName,
              total - failed, total);
  CHECK(failed == 0);
}

// Comparisons return bool; run against SoftFloat's quiet predicates.
template <typename Gen, typename T, typename RefFn, typename OpineFn>
void runCompare(const char *opName, RefFn ref, OpineFn opineFn) {
  using Bits = typename T::storage_type;

  genCases_setLevel(1);
  srand(1);
  Gen::init();

  long total = 0, failed = 0;
  while (!Gen::done()) {
    Gen::next();
    const uint64_t ua = Gen::bits(Gen::a());
    const uint64_t ub = Gen::bits(Gen::b());
    const bool want = ref(Gen::a(), Gen::b());
    const bool got = opineFn(Bits(ua), Bits(ub));
    total++;
    if (want != got) {
      failed++;
      if (failed <= 10)
        std::printf("  FAIL %s %s: a=0x%llX b=0x%llX ref=%d opine=%d\n",
                    Gen::name(), opName, (unsigned long long)ua,
                    (unsigned long long)ub, int(want), int(got));
    }
  }
  std::printf("%s %s: %ld/%ld passed\n", Gen::name(), opName, total - failed,
              total);
  CHECK(failed == 0);
}

template <typename Gen, typename Rnd> void runArithSweep() {
  using T = Type<numbers::IEEE754<Gen::E, Gen::M>,
                 layouts::IEEE<Gen::E, Gen::M, true>, Rnd>;
  using Bits = typename T::storage_type;
  softfloat_roundingMode = softfloatMode<Rnd>();
  const char *rnd = roundingName<Rnd>();
  runBinary<Gen, T>("add", rnd, Gen::add,
                    [](Bits x, Bits y) { return opine::add<T>(x, y); });
  runBinary<Gen, T>("sub", rnd, Gen::sub,
                    [](Bits x, Bits y) { return opine::sub<T>(x, y); });
  runBinary<Gen, T>("mul", rnd, Gen::mul,
                    [](Bits x, Bits y) { return opine::mul<T>(x, y); });
  runBinary<Gen, T>("div", rnd, Gen::div,
                    [](Bits x, Bits y) { return opine::div<T>(x, y); });
}

template <typename Gen> void runCompareSweep() {
  using T = Type<numbers::IEEE754<Gen::E, Gen::M>,
                 layouts::IEEE<Gen::E, Gen::M, true>>;
  using Bits = typename T::storage_type;
  softfloat_roundingMode = softfloat_round_near_even;
  runCompare<Gen, T>("eq", Gen::eq,
                     [](Bits x, Bits y) { return opine::eq<T>(x, y); });
  runCompare<Gen, T>("lt", Gen::lt,
                     [](Bits x, Bits y) { return opine::lt<T>(x, y); });
  runCompare<Gen, T>("le", Gen::le,
                     [](Bits x, Bits y) { return opine::le<T>(x, y); });
}

} // namespace

TEST_CASE_TEMPLATE("TestFloat: add/mul across rounding modes", Rnd,
                   rounding::ToNearestTiesToEven, rounding::TowardZero,
                   rounding::TowardPositive, rounding::TowardNegative) {
  runArithSweep<F16Gen, Rnd>();
  runArithSweep<F32Gen, Rnd>();
}

TEST_CASE("TestFloat: comparisons") {
  runCompareSweep<F16Gen>();
  runCompareSweep<F32Gen>();
}
