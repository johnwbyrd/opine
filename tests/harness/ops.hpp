#ifndef OPINE_TESTS_HARNESS_OPS_HPP
#define OPINE_TESTS_HARNESS_OPS_HPP

// Shared vocabulary for the test harness.
//
// Provides:
//   Op              — IEEE 754 operation enum (all arities)
//   TestOutput      — result of an operation (bits + flags)
//   opName()        — string name for an Op
//   extractField()  — extract a bitfield from a bit pattern
//
// These types belong to no adapter. They are the common language that
// every adapter and the harness itself speaks.

#include <cstdint>

namespace opine::testing {

// ===================================================================
// Op — IEEE 754 operations under test
// ===================================================================
// Organized by dispatch arity:
//   Binary:   dispatch(Op, a, b)         — Add..Le
//   Unary:    dispatchUnary(Op, a)       — Sqrt..Abs
//   Ternary:  dispatchTernary(Op, a,b,c) — MulAdd

enum class Op {
  // Binary arithmetic
  Add, Sub, Mul, Div, Rem,
  // Binary comparison (result is 0 or 1, not a float)
  Eq, Lt, Le,
  // Unary
  Sqrt, Neg, Abs,
  // Ternary
  MulAdd,
};

inline const char *opName(Op O) {
  switch (O) {
  case Op::Add:    return "add";
  case Op::Sub:    return "sub";
  case Op::Mul:    return "mul";
  case Op::Div:    return "div";
  case Op::Rem:    return "rem";
  case Op::Eq:     return "eq";
  case Op::Lt:     return "lt";
  case Op::Le:     return "le";
  case Op::Sqrt:   return "sqrt";
  case Op::Neg:    return "neg";
  case Op::Abs:    return "abs";
  case Op::MulAdd: return "mulAdd";
  }
  return "???";
}

// ===================================================================
// TestOutput — result of dispatching an operation
// ===================================================================
// For comparison ops, Bits is 0 (false) or 1 (true).

template <typename BitsType> struct TestOutput {
  BitsType Bits;
  uint8_t Flags; // 0 if implementation doesn't report flags
};

// ===================================================================
// extractField — extract a bitfield from a bit pattern
// ===================================================================

template <typename BitsType>
inline constexpr BitsType extractField(BitsType Bits, int Offset, int Width) {
  if (Width == 0)
    return BitsType{0};
  return (Bits >> Offset) & ((BitsType{1} << Width) - 1);
}

} // namespace opine::testing

#endif // OPINE_TESTS_HARNESS_OPS_HPP
