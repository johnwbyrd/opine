#ifndef OPINE_TESTS_HARNESS_IMPL_NATIVE_HPP
#define OPINE_TESTS_HARNESS_IMPL_NATIVE_HPP

// Native hardware FPU adapter: one implementation among equals.
//
// Provides NativeAdapter<FloatType> satisfying the adapter interface:
//   dispatch(Op, BitsType, BitsType) -> TestOutput<BitsType>
//
// Specialized for float32 and float64 only — the formats where the
// host CPU has native IEEE 754 operations.

#include <cstring>

#include "harness/ops.hpp"
#include "opine/opine.hpp"

namespace opine::testing {

template <typename FloatType> struct NativeAdapter;

template <> struct NativeAdapter<opine::float32> {
  using BitsType = opine::float32::storage_type;

  static constexpr const char *name() { return "Native"; }

  TestOutput<BitsType> dispatch(Op O, BitsType A, BitsType B) const {
    float Fa, Fb;
    uint32_t Ua = static_cast<uint32_t>(A);
    uint32_t Ub = static_cast<uint32_t>(B);
    std::memcpy(&Fa, &Ua, sizeof(float));
    std::memcpy(&Fb, &Ub, sizeof(float));
    float Fr;
    switch (O) {
    case Op::Add: Fr = Fa + Fb; break;
    case Op::Sub: Fr = Fa - Fb; break;
    case Op::Mul: Fr = Fa * Fb; break;
    case Op::Div: Fr = Fa / Fb; break;
    }
    uint32_t Ur;
    std::memcpy(&Ur, &Fr, sizeof(float));
    return {BitsType(Ur), 0};
  }
};

template <> struct NativeAdapter<opine::float64> {
  using BitsType = opine::float64::storage_type;

  static constexpr const char *name() { return "Native"; }

  TestOutput<BitsType> dispatch(Op O, BitsType A, BitsType B) const {
    double Da, Db;
    uint64_t Ua = static_cast<uint64_t>(A);
    uint64_t Ub = static_cast<uint64_t>(B);
    std::memcpy(&Da, &Ua, sizeof(double));
    std::memcpy(&Db, &Ub, sizeof(double));
    double Dr;
    switch (O) {
    case Op::Add: Dr = Da + Db; break;
    case Op::Sub: Dr = Da - Db; break;
    case Op::Mul: Dr = Da * Db; break;
    case Op::Div: Dr = Da / Db; break;
    }
    uint64_t Ur;
    std::memcpy(&Ur, &Dr, sizeof(double));
    return {BitsType(Ur), 0};
  }
};

} // namespace opine::testing

#endif // OPINE_TESTS_HARNESS_IMPL_NATIVE_HPP
