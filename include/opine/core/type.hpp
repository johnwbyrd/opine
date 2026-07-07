#ifndef OPINE_CORE_TYPE_HPP
#define OPINE_CORE_TYPE_HPP

// Root six-axis template.
//
// Type<Number, Layout, Rounding, Exceptions, Platform>
//
// Box is deferred in this slice — the axis exists in the design
// but no scalar/vector distinction is expressed yet. All values
// are implicitly scalar.
//
// Type does two jobs:
//   - Cross-validate Number and Layout (widths agree, sign
//     encoding agrees).
//   - Bundle the storage_type, compute_format, and SWAR lane count
//     as compile-time constants derived from the axes.

#include "opine/core/bits.hpp"
#include "opine/core/compute_format.hpp"
#include "opine/core/exceptions.hpp"
#include "opine/core/layout.hpp"
#include "opine/core/number.hpp"
#include "opine/core/platform.hpp"
#include "opine/core/rounding.hpp"

namespace opine {

template <typename Number, typename Layout,
          typename Rounding = rounding::Default,
          typename Exceptions = exceptions::Default,
          typename Platform = platforms::Default>
  requires ValidNumber<Number> && RoundingPolicy<Rounding> &&
           ExceptionPolicy<Exceptions> && PlatformPolicy<Platform>
struct Type {
  using number = Number;
  using layout = Layout;
  using rounding = Rounding;
  using exceptions = Exceptions;
  using platform = Platform;

  using storage_type = bits_t<Layout::total_bits>;

  using compute_format = DefaultComputeFormat<Number, Rounding>;

  static constexpr int swar_lanes =
      Platform::machine_word_bits / Layout::total_bits;

  // -------------------------------------------------------------
  // Number-Layout consistency (FloatingPoint composites)
  // -------------------------------------------------------------
  // The stored significand width plus the implicit digit, if any,
  // must equal the semantic significand digit count. The stored
  // exponent width must equal the exponent digit count. The sign
  // field must exist iff value_sign is Explicit.
  static_assert(!Number::is_composite ||
                    Layout::sig_bits + (Layout::implicit_digit ? 1 : 0) ==
                        Number::significand::digit_count,
                "layout stored significand + implicit digit must equal "
                "number's semantic significand digit count");
  static_assert(!Number::is_composite ||
                    Layout::exp_bits == Number::exponent::digit_count,
                "layout exponent width must equal number's exponent "
                "digit count");
  static_assert(!Number::is_composite ||
                    Number::value_sign != SignMethod::Explicit ||
                    Layout::sign_bits == 1,
                "Explicit value_sign requires a 1-bit sign field");
  // RadixComplement and DiminishedRadixComplement value_signs may
  // share the IEEE-shaped layout (sign_bits == 1): the MSB acts as
  // the sign discriminator, and the complement scheme covers the
  // whole word — that is rbj's / PDP-10's structural choice.
};

// -----------------------------------------------------------------
// Predefined Type bundles
// -----------------------------------------------------------------
// IEEE 754 binary. E = exponent bits, M = stored significand bits.
template <int E, int M>
using IEEE754Type = Type<numbers::IEEE754<E, M>, layouts::IEEE<E, M, true>>;

using float16 = IEEE754Type<5, 10>;
using float32 = IEEE754Type<8, 23>;
using float64 = IEEE754Type<11, 52>;
using float128 = IEEE754Type<15, 112>;

using bfloat16 = IEEE754Type<8, 7>;
using fp8_e5m2 = IEEE754Type<5, 2>;
using fp8_e4m3 = IEEE754Type<4, 3>;

// x87 80-bit extended: explicit leading bit stored in the layout.
using extFloat80 = Type<numbers::IEEE754ExplicitBit<15, 64>,
                        layouts::IEEE<15, 64, false>>;

// FP8 E4M3FNUZ (AMD variant).
using fp8_e4m3fnuz = Type<numbers::E4M3FNUZ, layouts::IEEE<4, 3, true>>;

// rbj's integer-ordered two's complement binary FP.
template <int E, int M>
using RbjType =
    Type<numbers::RbjTwosComplement<E, M>, layouts::IEEE<E, M, true>>;

// Fast approximate: no NaN/Inf, flush denormals, truncation.
template <int E, int M>
using FastType = Type<numbers::Relaxed<E, M>, layouts::IEEE<E, M, true>,
                      rounding::TowardZero, exceptions::Silent>;

} // namespace opine

#endif // OPINE_CORE_TYPE_HPP
