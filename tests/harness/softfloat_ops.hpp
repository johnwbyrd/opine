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
  using BitsType = opine::float16::storage_type;

  static SfType fromBits(BitsType Bits) {
    return {static_cast<uint16_t>(Bits)};
  }
  static BitsType toBits(SfType V) { return BitsType(V.v); }

  static SfType add(SfType A, SfType B) { return f16_add(A, B); }
  static SfType sub(SfType A, SfType B) { return f16_sub(A, B); }
  static SfType mul(SfType A, SfType B) { return f16_mul(A, B); }
  static SfType div(SfType A, SfType B) { return f16_div(A, B); }
};

template <> struct SoftFloatOps<opine::float32> {
  using SfType = float32_t;
  using BitsType = opine::float32::storage_type;

  static SfType fromBits(BitsType Bits) {
    return {static_cast<uint32_t>(Bits)};
  }
  static BitsType toBits(SfType V) { return BitsType(V.v); }

  static SfType add(SfType A, SfType B) { return f32_add(A, B); }
  static SfType sub(SfType A, SfType B) { return f32_sub(A, B); }
  static SfType mul(SfType A, SfType B) { return f32_mul(A, B); }
  static SfType div(SfType A, SfType B) { return f32_div(A, B); }
};

template <> struct SoftFloatOps<opine::float64> {
  using SfType = float64_t;
  using BitsType = opine::float64::storage_type;

  static SfType fromBits(BitsType Bits) {
    return {static_cast<uint64_t>(Bits)};
  }
  static BitsType toBits(SfType V) { return BitsType(V.v); }

  static SfType add(SfType A, SfType B) { return f64_add(A, B); }
  static SfType sub(SfType A, SfType B) { return f64_sub(A, B); }
  static SfType mul(SfType A, SfType B) { return f64_mul(A, B); }
  static SfType div(SfType A, SfType B) { return f64_div(A, B); }
};

template <> struct SoftFloatOps<opine::extFloat80> {
  using SfType = extFloat80_t;
  using BitsType = opine::extFloat80::storage_type;

  static SfType fromBits(BitsType Bits) {
    SfType R;
    R.signif = static_cast<uint64_t>(Bits);
    R.signExp = static_cast<uint16_t>(Bits >> 64);
    return R;
  }
  static BitsType toBits(SfType V) {
    return (BitsType(V.signExp) << 64) | BitsType(V.signif);
  }

  static SfType add(SfType A, SfType B) { return extF80_add(A, B); }
  static SfType sub(SfType A, SfType B) { return extF80_sub(A, B); }
  static SfType mul(SfType A, SfType B) { return extF80_mul(A, B); }
  static SfType div(SfType A, SfType B) { return extF80_div(A, B); }
};

template <> struct SoftFloatOps<opine::float128> {
  using SfType = float128_t;
  using BitsType = opine::float128::storage_type;

  static SfType fromBits(BitsType Bits) {
    SfType R;
    R.v[0] = static_cast<uint64_t>(Bits);
    R.v[1] = static_cast<uint64_t>(Bits >> 64);
    return R;
  }
  static BitsType toBits(SfType V) {
    return (BitsType(V.v[1]) << 64) | BitsType(V.v[0]);
  }

  static SfType add(SfType A, SfType B) { return f128_add(A, B); }
  static SfType sub(SfType A, SfType B) { return f128_sub(A, B); }
  static SfType mul(SfType A, SfType B) { return f128_mul(A, B); }
  static SfType div(SfType A, SfType B) { return f128_div(A, B); }
};

// Wrap a SoftFloat binary op into a harness-compatible callable.
template <typename FloatType, typename SfBinOp>
auto makeSoftFloatOp(SfBinOp SfFn) {
  using Sf = SoftFloatOps<FloatType>;
  using BitsType = typename Sf::BitsType;
  return [SfFn](BitsType A, BitsType B) -> TestOutput<BitsType> {
    softfloat_exceptionFlags = 0;
    auto Sa = Sf::fromBits(A);
    auto Sb = Sf::fromBits(B);
    auto Sr = SfFn(Sa, Sb);
    return {Sf::toBits(Sr), 0};
  };
}

} // namespace opine::testing

#endif // OPINE_TESTS_HARNESS_SOFTFLOAT_OPS_HPP
