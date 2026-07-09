#ifndef OPINE_CORE_SUB_HPP
#define OPINE_CORE_SUB_HPP

// Subtraction for FloatingPoint composites.
//
// IEEE 754 defines x − y as x + (−y) for every operand class, and
// the signed-zero rules of §6.3 fall out of add's zero handling —
// so sub shares add's pipeline with b's sign flipped. The flip
// happens on the UNPACKED operand rather than via opine::neg: a
// bit-level negate of +0 has nowhere to go in fnuz formats (their
// sign-set zero pattern encodes NaN), while the unpacked form
// carries the sign out-of-band.

#include "opine/core/add.hpp"

namespace opine {

template <typename T>
constexpr auto sub(typename T::storage_type a, typename T::storage_type b) {
  return detail::addWithSign<T>(a, b, true);
}

} // namespace opine

#endif // OPINE_CORE_SUB_HPP
