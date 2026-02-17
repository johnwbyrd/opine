#ifndef OPINE_TESTS_HARNESS_OPS_HPP
#define OPINE_TESTS_HARNESS_OPS_HPP

// Shared vocabulary for the test harness.
//
// Provides:
//   Op              — arithmetic operation enum
//   TestOutput      — result of an operation (bits + flags)
//   opName()        — string name for an Op
//   extractField()  — extract a bitfield from a bit pattern
//
// These types belong to no adapter. They are the common language that
// every adapter and the harness itself speaks.

#include <cstdint>

namespace opine::testing {

// ===================================================================
// Op — the arithmetic operations under test
// ===================================================================

enum class Op { Add, Sub, Mul, Div };

inline const char *opName(Op O) {
  switch (O) {
  case Op::Add:
    return "add";
  case Op::Sub:
    return "sub";
  case Op::Mul:
    return "mul";
  case Op::Div:
    return "div";
  }
  return "???";
}

// ===================================================================
// TestOutput — result of dispatching an operation
// ===================================================================

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
