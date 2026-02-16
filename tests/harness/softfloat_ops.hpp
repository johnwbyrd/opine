#ifndef OPINE_TESTS_HARNESS_SOFTFLOAT_OPS_HPP
#define OPINE_TESTS_HARNESS_SOFTFLOAT_OPS_HPP

// SoftFloat dispatch traits: maps opine Float types to Berkeley SoftFloat
// functions. Used as one pluggable implementation in the test harness.

#include <cstdint>

#include "harness/test_harness.hpp"

extern "C" {
#include "softfloat.h"
}

namespace opine::testing {

// Primary template — undefined. Specialize for each supported format.
template <typename FloatType> struct SoftFloatOps;

template <> struct SoftFloatOps<opine::float16> {
  using SfType = float16_t;

  static SfType fromBits(uint64_t Bits) {
    return {static_cast<uint16_t>(Bits)};
  }
  static uint64_t toBits(SfType V) { return V.v; }

  static SfType add(SfType A, SfType B) { return f16_add(A, B); }
  static SfType sub(SfType A, SfType B) { return f16_sub(A, B); }
  static SfType mul(SfType A, SfType B) { return f16_mul(A, B); }
  static SfType div(SfType A, SfType B) { return f16_div(A, B); }
};

template <> struct SoftFloatOps<opine::float32> {
  using SfType = float32_t;

  static SfType fromBits(uint64_t Bits) {
    return {static_cast<uint32_t>(Bits)};
  }
  static uint64_t toBits(SfType V) { return V.v; }

  static SfType add(SfType A, SfType B) { return f32_add(A, B); }
  static SfType sub(SfType A, SfType B) { return f32_sub(A, B); }
  static SfType mul(SfType A, SfType B) { return f32_mul(A, B); }
  static SfType div(SfType A, SfType B) { return f32_div(A, B); }
};

template <> struct SoftFloatOps<opine::float64> {
  using SfType = float64_t;

  static SfType fromBits(uint64_t Bits) { return {Bits}; }
  static uint64_t toBits(SfType V) { return V.v; }

  static SfType add(SfType A, SfType B) { return f64_add(A, B); }
  static SfType sub(SfType A, SfType B) { return f64_sub(A, B); }
  static SfType mul(SfType A, SfType B) { return f64_mul(A, B); }
  static SfType div(SfType A, SfType B) { return f64_div(A, B); }
};

// Wrap a SoftFloat binary op into a harness-compatible callable.
template <typename FloatType, typename SfBinOp>
auto makeSoftFloatOp(SfBinOp SfFn) {
  using Sf = SoftFloatOps<FloatType>;
  return [SfFn](uint64_t A, uint64_t B) -> TestOutput {
    softfloat_exceptionFlags = 0;
    auto Sa = Sf::fromBits(A);
    auto Sb = Sf::fromBits(B);
    auto Sr = SfFn(Sa, Sb);
    return {Sf::toBits(Sr), 0};
  };
}

} // namespace opine::testing

#endif // OPINE_TESTS_HARNESS_SOFTFLOAT_OPS_HPP
