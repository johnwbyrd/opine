// Oracle Part 1 validation: verify that decodeToMpfr + exact MPFR
// arithmetic, when rounded to the target format, matches Berkeley
// SoftFloat for 10,000+ random input pairs per operation.
//
// This is an instance of the "this against that" harness (tdd.md):
//   ImplA = MPFR oracle (decode -> exact op -> round to format)
//   ImplB = SoftFloat
//   Both are opaque callables; the harness doesn't know what backs them.

#include "harness/softfloat_ops.hpp"
#include "harness/test_harness.hpp"
#include "oracle/mpfr_exact.hpp"

#include <cstdio>

using namespace opine;
using namespace opine::oracle;
using namespace opine::testing;

// ===================================================================
// MPFR oracle callable: decode -> exact op -> round to format
// ===================================================================

template <typename FloatType>
auto makeOracleOp(Op Operation) {
  using BitsType = typename FloatType::storage_type;
  return [Operation](BitsType A, BitsType B) -> TestOutput<BitsType> {
    MpfrFloat Ma = decodeToMpfr<FloatType>(A);
    MpfrFloat Mb = decodeToMpfr<FloatType>(B);
    MpfrFloat Exact = mpfrExactOp(Operation, Ma, Mb);
    return {mpfrRoundToFormat<FloatType>(Exact), 0};
  };
}

// ===================================================================
// Per-format test runner
// ===================================================================

template <typename FloatType>
int runFormatTests() {
  using Sf = SoftFloatOps<FloatType>;
  using SfType = typename Sf::SfType;
  using BitsType = typename FloatType::storage_type;
  using SfBinOp = SfType (*)(SfType, SfType);
  constexpr int TotalBits = FloatType::format::total_bits;
  constexpr int HexWidth = (TotalBits + 3) / 4;

  // Iteration: targeted edge cases + 10,000 random pairs
  constexpr auto Interesting = interestingValues<FloatType>();
  auto Iter =
      combined(TargetedPairs<BitsType>{Interesting.data(),
                             static_cast<int>(Interesting.size())},
               RandomPairs<BitsType, TotalBits>{42, 10000});

  NanAwareBitExact<FloatType> Cmp;

  struct OpDesc {
    const char *Name;
    Op OracleOp;
    SfBinOp SfFn;
  };

  OpDesc Tests[] = {
      {"add", Op::Add, &Sf::add},
      {"sub", Op::Sub, &Sf::sub},
      {"mul", Op::Mul, &Sf::mul},
      {"div", Op::Div, &Sf::div},
  };

  int TotalFailures = 0;

  for (auto &T : Tests) {
    auto Oracle = makeOracleOp<FloatType>(T.OracleOp);
    auto SfImpl = makeSoftFloatOp<FloatType>(T.SfFn);
    auto R = testAgainst<BitsType>(T.Name, HexWidth, Iter, Oracle, SfImpl, Cmp);
    TotalFailures += R.Failed;
  }

  return TotalFailures;
}

// ===================================================================
// Main
// ===================================================================

int main() {
  softfloat_roundingMode = softfloat_round_near_even;
  softfloat_detectTininess = softfloat_tininess_afterRounding;

  int Failures = 0;

  std::printf("=== float16 (IEEE 754 binary16) ===\n");
  Failures += runFormatTests<float16>();

  std::printf("\n=== float32 (IEEE 754 binary32) ===\n");
  Failures += runFormatTests<float32>();

  std::printf("\n=== float64 (IEEE 754 binary64) ===\n");
  Failures += runFormatTests<float64>();

  std::printf("\n=== extFloat80 (x87 80-bit extended) ===\n");
  Failures += runFormatTests<extFloat80>();

  std::printf("\n=== float128 (IEEE 754 binary128) ===\n");
  Failures += runFormatTests<float128>();

  if (Failures > 0) {
    std::fprintf(stderr, "\nFAILED: %d total failures\n", Failures);
    return 1;
  }

  std::printf("\nPASS: All oracle results match SoftFloat\n");
  return 0;
}
