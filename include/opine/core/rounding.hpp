#ifndef OPINE_CORE_ROUNDING_HPP
#define OPINE_CORE_ROUNDING_HPP

#include <concepts>

namespace opine {

template <typename R>
concept RoundingPolicy = requires {
  { R::guard_bits } -> std::convertible_to<int>;
};

namespace rounding {

// Round toward zero (truncation). No guard bits needed.
struct TowardZero {
  static constexpr int guard_bits = 0;
};

// Round to nearest, ties to even. IEEE 754 default.
// Requires 3 guard bits: Guard, Round, Sticky.
struct ToNearestTiesToEven {
  static constexpr int guard_bits = 3;
};

// Round to nearest, ties away from zero.
struct ToNearestTiesAway {
  static constexpr int guard_bits = 3;
};

// Round toward positive infinity (ceiling).
struct TowardPositive {
  static constexpr int guard_bits = 1;
};

// Round toward negative infinity (floor).
struct TowardNegative {
  static constexpr int guard_bits = 1;
};

// Round to odd (jamming). If the result is inexact, set the LSB to 1.
// Used for intermediate computations in extended precision to avoid
// double rounding: round-to-odd at the wide intermediate guarantees
// that the final round-to-nearest at the target precision is correct.
struct ToOdd {
  static constexpr int guard_bits = 1; // sticky: was anything lost?
};

using Default = TowardZero;

static_assert(RoundingPolicy<TowardZero>);
static_assert(RoundingPolicy<ToNearestTiesToEven>);
static_assert(RoundingPolicy<ToNearestTiesAway>);
static_assert(RoundingPolicy<TowardPositive>);
static_assert(RoundingPolicy<TowardNegative>);
static_assert(RoundingPolicy<ToOdd>);

} // namespace rounding
} // namespace opine

#endif // OPINE_CORE_ROUNDING_HPP
