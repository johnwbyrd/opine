#ifndef OPINE_TESTS_HARNESS_IMPL_OPINE_HPP
#define OPINE_TESTS_HARNESS_IMPL_OPINE_HPP

// OPINE adapter: dispatch the harness's Ops through OPINE's own
// library implementations. This adapter grows over time as more
// operations are implemented in the library. For each op that is
// implemented, the adapter converts the harness's uint bit
// patterns into an OPINE call and back.
//
// Currently implemented: Eq, Lt, Le (from opine::eq/lt/le).
// Unimplemented ops return {0, 0} so the harness can still
// dispatch mixed op sets without special-casing.

#include "harness/ops.hpp"
#include "opine/opine.hpp"

namespace opine::testing {

template <typename FloatType> struct OpineAdapter {
  using BitsType = typename FloatType::storage_type;

  static constexpr const char *name() { return "OPINE"; }

  TestOutput<BitsType> dispatch(Op O, BitsType A, BitsType B) const {
    switch (O) {
    case Op::Eq: return {BitsType(opine::eq<FloatType>(A, B) ? 1 : 0), 0};
    case Op::Lt: return {BitsType(opine::lt<FloatType>(A, B) ? 1 : 0), 0};
    case Op::Le: return {BitsType(opine::le<FloatType>(A, B) ? 1 : 0), 0};
    default: return {BitsType{0}, 0};
    }
  }

  TestOutput<BitsType> dispatchUnary(Op O, BitsType A) const {
    switch (O) {
    case Op::Neg: return {opine::neg<FloatType>(A), 0};
    case Op::Abs: return {opine::abs<FloatType>(A), 0};
    default: return {BitsType{0}, 0};
    }
  }

  TestOutput<BitsType> dispatchTernary(Op /*O*/, BitsType /*A*/,
                                       BitsType /*B*/, BitsType /*C*/) const {
    return {BitsType{0}, 0};
  }
};

} // namespace opine::testing

#endif // OPINE_TESTS_HARNESS_IMPL_OPINE_HPP
