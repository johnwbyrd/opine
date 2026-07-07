#ifndef OPINE_CORE_COMPUTE_FORMAT_HPP
#define OPINE_CORE_COMPUTE_FORMAT_HPP

// ComputeFormat: parameter of *operations*, not values.
//
// Describes the working precision at which arithmetic executes.
// Currently expressed in bits — the design calls for digit units
// so that decimal (digit_width=4) and character (digit_width=8)
// arithmetic can share the shape. That generalization is deferred
// until non-binary arithmetic is implemented.

#include <concepts>

#include "opine/core/number.hpp"

namespace opine {

template <typename CF>
concept ValidComputeFormat = requires {
  { CF::exp_bits } -> std::convertible_to<int>;
  { CF::mant_bits } -> std::convertible_to<int>;
  { CF::guard_bits } -> std::convertible_to<int>;
} && (CF::exp_bits >= 2) && (CF::mant_bits >= 1) && (CF::guard_bits >= 0);

// Explicit ComputeFormat. mant_bits is the full significand
// precision (including the implicit digit for IEEE binary).
template <int ExponentBits, int MantissaBits, int GuardBits>
struct ComputeFormat {
  static constexpr int exp_bits = ExponentBits;
  static constexpr int mant_bits = MantissaBits;
  static constexpr int guard_bits = GuardBits;

  static constexpr int product_bits = 2 * mant_bits;
  static constexpr int aligned_bits = mant_bits + guard_bits;
  static constexpr int total_bits = 1 + exp_bits + product_bits;
  static constexpr int total_bytes = (total_bits + 7) / 8;

  static_assert(ExponentBits >= 2,
                "exponent needs at least 2 bits for overflow detection");
  static_assert(MantissaBits >= 1, "mantissa needs at least 1 bit");
  static_assert(GuardBits >= 0, "guard bits cannot be negative");
};

// DefaultComputeFormat derives from the Number's semantic widths
// plus the Rounding policy's guard bits. Two extra exponent bits
// provide overflow headroom during arithmetic.
template <typename Number, typename Rounding>
struct DefaultComputeFormat {
  static constexpr int exp_bits = Number::exponent::digit_count + 2;
  static constexpr int mant_bits = Number::significand::digit_count;
  static constexpr int guard_bits = Rounding::guard_bits;

  static constexpr int product_bits = 2 * mant_bits;
  static constexpr int aligned_bits = mant_bits + guard_bits;
  static constexpr int total_bits = 1 + exp_bits + product_bits;
  static constexpr int total_bytes = (total_bits + 7) / 8;
};

} // namespace opine

#endif // OPINE_CORE_COMPUTE_FORMAT_HPP
