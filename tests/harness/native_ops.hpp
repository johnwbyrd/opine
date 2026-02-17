#ifndef OPINE_TESTS_HARNESS_NATIVE_OPS_HPP
#define OPINE_TESTS_HARNESS_NATIVE_OPS_HPP

// Native hardware FPU dispatch: wraps the compiler's float/double
// arithmetic as a harness-compatible callable. Provides a third
// independent implementation for cross-checking MPFR and SoftFloat.
//
// Only specialized for float32 and float64 — the formats where the
// host CPU has native IEEE 754 operations.

#include <cstring>

#include "harness/test_harness.hpp"

namespace opine::testing {

template <typename FloatType> struct NativeOps;

template <> struct NativeOps<opine::float32> {
  using BitsType = opine::float32::storage_type;

  static TestOutput<BitsType> add(BitsType A, BitsType B) {
    return binop(A, B, [](float X, float Y) { return X + Y; });
  }
  static TestOutput<BitsType> sub(BitsType A, BitsType B) {
    return binop(A, B, [](float X, float Y) { return X - Y; });
  }
  static TestOutput<BitsType> mul(BitsType A, BitsType B) {
    return binop(A, B, [](float X, float Y) { return X * Y; });
  }
  static TestOutput<BitsType> div(BitsType A, BitsType B) {
    return binop(A, B, [](float X, float Y) { return X / Y; });
  }

private:
  template <typename Op>
  static TestOutput<BitsType> binop(BitsType A, BitsType B, Op Fn) {
    float Fa, Fb;
    uint32_t Ua = static_cast<uint32_t>(A);
    uint32_t Ub = static_cast<uint32_t>(B);
    std::memcpy(&Fa, &Ua, sizeof(float));
    std::memcpy(&Fb, &Ub, sizeof(float));
    float Fr = Fn(Fa, Fb);
    uint32_t Ur;
    std::memcpy(&Ur, &Fr, sizeof(float));
    return {BitsType(Ur), 0};
  }
};

template <> struct NativeOps<opine::float64> {
  using BitsType = opine::float64::storage_type;

  static TestOutput<BitsType> add(BitsType A, BitsType B) {
    return binop(A, B, [](double X, double Y) { return X + Y; });
  }
  static TestOutput<BitsType> sub(BitsType A, BitsType B) {
    return binop(A, B, [](double X, double Y) { return X - Y; });
  }
  static TestOutput<BitsType> mul(BitsType A, BitsType B) {
    return binop(A, B, [](double X, double Y) { return X * Y; });
  }
  static TestOutput<BitsType> div(BitsType A, BitsType B) {
    return binop(A, B, [](double X, double Y) { return X / Y; });
  }

private:
  template <typename Op>
  static TestOutput<BitsType> binop(BitsType A, BitsType B, Op Fn) {
    double Da, Db;
    uint64_t Ua = static_cast<uint64_t>(A);
    uint64_t Ub = static_cast<uint64_t>(B);
    std::memcpy(&Da, &Ua, sizeof(double));
    std::memcpy(&Db, &Ub, sizeof(double));
    double Dr = Fn(Da, Db);
    uint64_t Ur;
    std::memcpy(&Ur, &Dr, sizeof(double));
    return {BitsType(Ur), 0};
  }
};

} // namespace opine::testing

#endif // OPINE_TESTS_HARNESS_NATIVE_OPS_HPP
