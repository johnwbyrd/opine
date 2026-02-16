#ifndef OPINE_CORE_COMPUTE_FORMAT_HPP
#define OPINE_CORE_COMPUTE_FORMAT_HPP

#include <concepts>

namespace opine {

template <typename CF>
concept ValidComputeFormat = requires {
  { CF::exp_bits } -> std::convertible_to<int>;
  { CF::mant_bits } -> std::convertible_to<int>;
  { CF::guard_bits } -> std::convertible_to<int>;
} && (CF::exp_bits >= 2) &&  // minimum for overflow detection
    (CF::mant_bits >= 1) &&  // at least the implicit bit
    (CF::guard_bits >= 0);

// ComputeFormat specifies the bit widths of every field in the
// computation pipeline. It is a parameter of operations, not of values.
template <int ExponentBits, int MantissaBits, int GuardBits>
struct ComputeFormat {
  static constexpr int exp_bits = ExponentBits;
  static constexpr int mant_bits = MantissaBits; // including implicit bit
  static constexpr int guard_bits = GuardBits;

  // Derived properties
  static constexpr int product_bits = 2 * mant_bits;          // for multiply
  static constexpr int aligned_bits = mant_bits + guard_bits;  // for addition

  // Total intermediate state (determines register pressure)
  static constexpr int total_bits = 1 + exp_bits + product_bits;
  static constexpr int total_bytes = (total_bits + 7) / 8;

  static_assert(ExponentBits >= 2, "exponent needs at least 2 bits for overflow detection");
  static_assert(MantissaBits >= 1, "mantissa needs at least 1 bit");
  static_assert(GuardBits >= 0, "guard bits cannot be negative");
};

// DefaultComputeFormat derives sensible defaults from a Float type's
// properties. The exponent gets 2 overflow bits, the mantissa includes
// the implicit bit if present, and guard bits come from the rounding policy.
template <typename Format, typename Encoding, typename Rounding>
struct DefaultComputeFormat {
  static constexpr int exp_bits = Format::exp_bits + 2;
  static constexpr int mant_bits =
      Format::mant_bits + (Encoding::has_implicit_bit ? 1 : 0);
  static constexpr int guard_bits = Rounding::guard_bits;

  static constexpr int product_bits = 2 * mant_bits;
  static constexpr int aligned_bits = mant_bits + guard_bits;
  static constexpr int total_bits = 1 + exp_bits + product_bits;
  static constexpr int total_bytes = (total_bits + 7) / 8;
};

} // namespace opine

#endif // OPINE_CORE_COMPUTE_FORMAT_HPP
