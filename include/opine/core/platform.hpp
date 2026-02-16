#ifndef OPINE_CORE_PLATFORM_HPP
#define OPINE_CORE_PLATFORM_HPP

#include <concepts>
#include <cstdint>

namespace opine {

// Type selection policies (carried forward from previous implementation)
namespace type_policies {

struct ExactWidth {
  // On Clang: uses _BitInt(N) for exact widths.
  // On GCC/MSVC: falls back to uint_fastN_t.
};

struct LeastWidth {
  // Uses uint_leastN_t / int_leastN_t.
};

struct Fastest {
  // Uses uint_fastN_t / int_fastN_t.
};

} // namespace type_policies

template <typename P>
concept PlatformPolicy = requires {
  { P::machine_word_bits } -> std::convertible_to<int>;
  { P::has_hardware_multiply } -> std::convertible_to<bool>;
  { P::has_barrel_shifter } -> std::convertible_to<bool>;
  { P::has_conditional_negate } -> std::convertible_to<bool>;
  { P::has_clz } -> std::convertible_to<bool>;
  { P::has_ctz } -> std::convertible_to<bool>;
};

namespace platforms {

struct Generic32 {
  using type_policy = type_policies::ExactWidth;
  static constexpr int machine_word_bits = 32;
  static constexpr bool has_hardware_multiply = true;
  static constexpr bool has_barrel_shifter = true;
  static constexpr bool has_conditional_negate = true;
  static constexpr bool has_clz = true;
  static constexpr bool has_ctz = true;
};

struct MOS6502 {
  using type_policy = type_policies::LeastWidth;
  static constexpr int machine_word_bits = 8;
  static constexpr bool has_hardware_multiply = false;
  static constexpr bool has_barrel_shifter = false;
  static constexpr bool has_conditional_negate = false;
  static constexpr bool has_clz = false;
  static constexpr bool has_ctz = false;
};

struct RV32IM {
  using type_policy = type_policies::ExactWidth;
  static constexpr int machine_word_bits = 32;
  static constexpr bool has_hardware_multiply = true;
  static constexpr bool has_barrel_shifter = true;
  static constexpr bool has_conditional_negate = false;
  static constexpr bool has_clz = false;
  static constexpr bool has_ctz = false;
};

struct CortexM0 {
  using type_policy = type_policies::ExactWidth;
  static constexpr int machine_word_bits = 32;
  static constexpr bool has_hardware_multiply = true;
  static constexpr bool has_barrel_shifter = false;
  static constexpr bool has_conditional_negate = false;
  static constexpr bool has_clz = false;
  static constexpr bool has_ctz = false;
};

using Default = Generic32;

static_assert(PlatformPolicy<Generic32>);
static_assert(PlatformPolicy<MOS6502>);
static_assert(PlatformPolicy<RV32IM>);
static_assert(PlatformPolicy<CortexM0>);

} // namespace platforms
} // namespace opine

#endif // OPINE_CORE_PLATFORM_HPP
