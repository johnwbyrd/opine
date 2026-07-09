#ifndef OPINE_TESTS_HARNESS_IMPL_OPINE_HPP
#define OPINE_TESTS_HARNESS_IMPL_OPINE_HPP

// OPINE adapter: dispatch the harness's Ops through OPINE's own
// library implementations. This adapter grows over time as more
// operations are implemented in the library. For each op that is
// implemented, the adapter converts the harness's uint bit
// patterns into an OPINE call and back.
//
// Currently implemented: Add, Sub, Mul, Div, Sqrt, Eq, Lt, Le,
// Neg, Abs.
// Unimplemented ops return {0, 0} so the harness can still
// dispatch mixed op sets without special-casing.

#include <type_traits>

#include "harness/ops.hpp"
#include "opine/opine.hpp"

namespace opine::testing {

template <typename FloatType> struct OpineAdapter {
  using BitsType = typename FloatType::storage_type;

  static constexpr const char *name() { return "OPINE"; }

  // Rounding ops return bare bits under Silent/StatusFlags and
  // WithStatus under ReturnStatus; either way the harness sees
  // {bits, flags}.
  template <typename R> static TestOutput<BitsType> wrap(const R &Res) {
    if constexpr (std::is_same_v<R, opine::WithStatus<FloatType>>)
      return {Res.bits, Res.flags};
    else
      return {Res, 0};
  }

  TestOutput<BitsType> dispatch(Op O, BitsType A, BitsType B) const {
    switch (O) {
    case Op::Add: return wrap(opine::add<FloatType>(A, B));
    case Op::Sub: return wrap(opine::sub<FloatType>(A, B));
    case Op::Mul: return wrap(opine::mul<FloatType>(A, B));
    case Op::Div: return wrap(opine::div<FloatType>(A, B));
    case Op::Eq:
      return {opine::detail::wordFromUint<BitsType>(
                  opine::eq<FloatType>(A, B) ? 1 : 0), 0};
    case Op::Lt:
      return {opine::detail::wordFromUint<BitsType>(
                  opine::lt<FloatType>(A, B) ? 1 : 0), 0};
    case Op::Le:
      return {opine::detail::wordFromUint<BitsType>(
                  opine::le<FloatType>(A, B) ? 1 : 0), 0};
    default: return {BitsType{}, 0};
    }
  }

  TestOutput<BitsType> dispatchUnary(Op O, BitsType A) const {
    switch (O) {
    case Op::Sqrt: return wrap(opine::sqrt<FloatType>(A));
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
