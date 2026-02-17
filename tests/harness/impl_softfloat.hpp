#ifndef OPINE_TESTS_HARNESS_IMPL_SOFTFLOAT_HPP
#define OPINE_TESTS_HARNESS_IMPL_SOFTFLOAT_HPP

// SoftFloat adapter: one implementation among equals.
//
// Provides SoftFloatAdapter<FloatType> satisfying the adapter interface:
//   dispatch(Op, BitsType, BitsType) -> TestOutput<BitsType>
//
// Specialized for float16, float32, float64, extFloat80, float128.

#include <cstdint>

#include "harness/ops.hpp"

extern "C" {
#include "softfloat.h"
}

namespace opine::testing {

template <typename FloatType> struct SoftFloatAdapter;

template <> struct SoftFloatAdapter<opine::float16> {
  using BitsType = opine::float16::storage_type;

  static constexpr const char *name() { return "SoftFloat"; }

  TestOutput<BitsType> dispatch(Op O, BitsType A, BitsType B) const {
    softfloat_exceptionFlags = 0;
    float16_t Sa{static_cast<uint16_t>(A)}, Sb{static_cast<uint16_t>(B)};
    float16_t Sr;
    switch (O) {
    case Op::Add: Sr = f16_add(Sa, Sb); break;
    case Op::Sub: Sr = f16_sub(Sa, Sb); break;
    case Op::Mul: Sr = f16_mul(Sa, Sb); break;
    case Op::Div: Sr = f16_div(Sa, Sb); break;
    }
    return {BitsType(Sr.v), 0};
  }
};

template <> struct SoftFloatAdapter<opine::float32> {
  using BitsType = opine::float32::storage_type;

  static constexpr const char *name() { return "SoftFloat"; }

  TestOutput<BitsType> dispatch(Op O, BitsType A, BitsType B) const {
    softfloat_exceptionFlags = 0;
    float32_t Sa{static_cast<uint32_t>(A)}, Sb{static_cast<uint32_t>(B)};
    float32_t Sr;
    switch (O) {
    case Op::Add: Sr = f32_add(Sa, Sb); break;
    case Op::Sub: Sr = f32_sub(Sa, Sb); break;
    case Op::Mul: Sr = f32_mul(Sa, Sb); break;
    case Op::Div: Sr = f32_div(Sa, Sb); break;
    }
    return {BitsType(Sr.v), 0};
  }
};

template <> struct SoftFloatAdapter<opine::float64> {
  using BitsType = opine::float64::storage_type;

  static constexpr const char *name() { return "SoftFloat"; }

  TestOutput<BitsType> dispatch(Op O, BitsType A, BitsType B) const {
    softfloat_exceptionFlags = 0;
    float64_t Sa{static_cast<uint64_t>(A)}, Sb{static_cast<uint64_t>(B)};
    float64_t Sr;
    switch (O) {
    case Op::Add: Sr = f64_add(Sa, Sb); break;
    case Op::Sub: Sr = f64_sub(Sa, Sb); break;
    case Op::Mul: Sr = f64_mul(Sa, Sb); break;
    case Op::Div: Sr = f64_div(Sa, Sb); break;
    }
    return {BitsType(Sr.v), 0};
  }
};

template <> struct SoftFloatAdapter<opine::extFloat80> {
  using BitsType = opine::extFloat80::storage_type;

  static constexpr const char *name() { return "SoftFloat"; }

  TestOutput<BitsType> dispatch(Op O, BitsType A, BitsType B) const {
    softfloat_exceptionFlags = 0;
    extFloat80_t Sa, Sb;
    Sa.signif = static_cast<uint64_t>(A);
    Sa.signExp = static_cast<uint16_t>(A >> 64);
    Sb.signif = static_cast<uint64_t>(B);
    Sb.signExp = static_cast<uint16_t>(B >> 64);
    extFloat80_t Sr;
    switch (O) {
    case Op::Add: Sr = extF80_add(Sa, Sb); break;
    case Op::Sub: Sr = extF80_sub(Sa, Sb); break;
    case Op::Mul: Sr = extF80_mul(Sa, Sb); break;
    case Op::Div: Sr = extF80_div(Sa, Sb); break;
    }
    return {(BitsType(Sr.signExp) << 64) | BitsType(Sr.signif), 0};
  }
};

template <> struct SoftFloatAdapter<opine::float128> {
  using BitsType = opine::float128::storage_type;

  static constexpr const char *name() { return "SoftFloat"; }

  TestOutput<BitsType> dispatch(Op O, BitsType A, BitsType B) const {
    softfloat_exceptionFlags = 0;
    float128_t Sa, Sb;
    Sa.v[0] = static_cast<uint64_t>(A);
    Sa.v[1] = static_cast<uint64_t>(A >> 64);
    Sb.v[0] = static_cast<uint64_t>(B);
    Sb.v[1] = static_cast<uint64_t>(B >> 64);
    float128_t Sr;
    switch (O) {
    case Op::Add: Sr = f128_add(Sa, Sb); break;
    case Op::Sub: Sr = f128_sub(Sa, Sb); break;
    case Op::Mul: Sr = f128_mul(Sa, Sb); break;
    case Op::Div: Sr = f128_div(Sa, Sb); break;
    }
    return {(BitsType(Sr.v[1]) << 64) | BitsType(Sr.v[0]), 0};
  }
};

} // namespace opine::testing

#endif // OPINE_TESTS_HARNESS_IMPL_SOFTFLOAT_HPP
