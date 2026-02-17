#ifndef OPINE_TESTS_HARNESS_IMPL_NATIVE_HPP
#define OPINE_TESTS_HARNESS_IMPL_NATIVE_HPP

// Native hardware FPU adapter: one implementation among equals.
//
// Provides NativeAdapter<FloatType> satisfying the adapter interface:
//   dispatch(Op, BitsType, BitsType)              -> TestOutput<BitsType>
//   dispatchUnary(Op, BitsType)                   -> TestOutput<BitsType>
//   dispatchTernary(Op, BitsType, BitsType, BitsType) -> TestOutput<BitsType>
//
// Specialized for float32 and float64 only — the formats where the
// host CPU has native IEEE 754 operations.

#include <cmath>
#include <cstring>

#include "harness/ops.hpp"
#include "opine/opine.hpp"

namespace opine::testing {

template <typename FloatType> struct NativeAdapter;

template <> struct NativeAdapter<opine::float32> {
  using BitsType = opine::float32::storage_type;

  static constexpr const char *name() { return "Native"; }

  static float toFloat(BitsType V) {
    float F;
    uint32_t U = static_cast<uint32_t>(V);
    std::memcpy(&F, &U, sizeof(float));
    return F;
  }

  static BitsType fromFloat(float F) {
    uint32_t U;
    std::memcpy(&U, &F, sizeof(float));
    return BitsType(U);
  }

  TestOutput<BitsType> dispatch(Op O, BitsType A, BitsType B) const {
    float Fa = toFloat(A), Fb = toFloat(B);
    switch (O) {
    case Op::Add: return {fromFloat(Fa + Fb), 0};
    case Op::Sub: return {fromFloat(Fa - Fb), 0};
    case Op::Mul: return {fromFloat(Fa * Fb), 0};
    case Op::Div: return {fromFloat(Fa / Fb), 0};
    case Op::Rem: return {fromFloat(std::remainder(Fa, Fb)), 0};
    case Op::Eq:  return {BitsType(Fa == Fb ? 1 : 0), 0};
    case Op::Lt:  return {BitsType(Fa < Fb ? 1 : 0), 0};
    case Op::Le:  return {BitsType(Fa <= Fb ? 1 : 0), 0};
    default: return {0, 0};
    }
  }

  TestOutput<BitsType> dispatchUnary(Op O, BitsType A) const {
    float Fa = toFloat(A);
    switch (O) {
    case Op::Sqrt: return {fromFloat(std::sqrt(Fa)), 0};
    case Op::Neg:  return {fromFloat(-Fa), 0};
    case Op::Abs:  return {fromFloat(std::fabs(Fa)), 0};
    default: return {0, 0};
    }
  }

  TestOutput<BitsType> dispatchTernary(Op O, BitsType A, BitsType B,
                                       BitsType C) const {
    float Fa = toFloat(A), Fb = toFloat(B), Fc = toFloat(C);
    switch (O) {
    case Op::MulAdd: return {fromFloat(std::fma(Fa, Fb, Fc)), 0};
    default: return {0, 0};
    }
  }
};

template <> struct NativeAdapter<opine::float64> {
  using BitsType = opine::float64::storage_type;

  static constexpr const char *name() { return "Native"; }

  static double toDouble(BitsType V) {
    double D;
    uint64_t U = static_cast<uint64_t>(V);
    std::memcpy(&D, &U, sizeof(double));
    return D;
  }

  static BitsType fromDouble(double D) {
    uint64_t U;
    std::memcpy(&U, &D, sizeof(double));
    return BitsType(U);
  }

  TestOutput<BitsType> dispatch(Op O, BitsType A, BitsType B) const {
    double Da = toDouble(A), Db = toDouble(B);
    switch (O) {
    case Op::Add: return {fromDouble(Da + Db), 0};
    case Op::Sub: return {fromDouble(Da - Db), 0};
    case Op::Mul: return {fromDouble(Da * Db), 0};
    case Op::Div: return {fromDouble(Da / Db), 0};
    case Op::Rem: return {fromDouble(std::remainder(Da, Db)), 0};
    case Op::Eq:  return {BitsType(Da == Db ? 1 : 0), 0};
    case Op::Lt:  return {BitsType(Da < Db ? 1 : 0), 0};
    case Op::Le:  return {BitsType(Da <= Db ? 1 : 0), 0};
    default: return {0, 0};
    }
  }

  TestOutput<BitsType> dispatchUnary(Op O, BitsType A) const {
    double Da = toDouble(A);
    switch (O) {
    case Op::Sqrt: return {fromDouble(std::sqrt(Da)), 0};
    case Op::Neg:  return {fromDouble(-Da), 0};
    case Op::Abs:  return {fromDouble(std::fabs(Da)), 0};
    default: return {0, 0};
    }
  }

  TestOutput<BitsType> dispatchTernary(Op O, BitsType A, BitsType B,
                                       BitsType C) const {
    double Da = toDouble(A), Db = toDouble(B), Dc = toDouble(C);
    switch (O) {
    case Op::MulAdd: return {fromDouble(std::fma(Da, Db, Dc)), 0};
    default: return {0, 0};
    }
  }
};

} // namespace opine::testing

#endif // OPINE_TESTS_HARNESS_IMPL_NATIVE_HPP
