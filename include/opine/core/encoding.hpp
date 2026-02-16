#ifndef OPINE_CORE_ENCODING_HPP
#define OPINE_CORE_ENCODING_HPP

#include <concepts>

#include "opine/core/enums.hpp"

namespace opine {

// ValidEncoding concept: enforces internal consistency of encoding
// sub-parameters at compile time.
template <typename E>
concept ValidEncoding =
    requires {
      { E::sign_encoding } -> std::convertible_to<SignEncoding>;
      { E::has_implicit_bit } -> std::convertible_to<bool>;
      { E::exponent_bias } -> std::convertible_to<int>;
      { E::negative_zero } -> std::convertible_to<NegativeZero>;
      { E::nan_encoding } -> std::convertible_to<NanEncoding>;
      { E::inf_encoding } -> std::convertible_to<InfEncoding>;
      { E::denormal_mode } -> std::convertible_to<DenormalMode>;
    } &&
    // Two's complement constraints
    (E::sign_encoding != SignEncoding::TwosComplement ||
     E::negative_zero == NegativeZero::DoesNotExist) &&
    (E::sign_encoding != SignEncoding::TwosComplement ||
     E::nan_encoding == NanEncoding::TrapValue ||
     E::nan_encoding == NanEncoding::None) &&
    (E::sign_encoding != SignEncoding::TwosComplement ||
     E::inf_encoding == InfEncoding::IntegerExtremes ||
     E::inf_encoding == InfEncoding::None) &&
    // One's complement constraints
    (E::sign_encoding != SignEncoding::OnesComplement ||
     E::negative_zero == NegativeZero::Exists) &&
    // NegativeZeroBitPattern requires no negative zero
    (E::nan_encoding != NanEncoding::NegativeZeroBitPattern ||
     E::negative_zero == NegativeZero::DoesNotExist) &&
    // ReservedExponent Inf requires ReservedExponent NaN
    (E::inf_encoding != InfEncoding::ReservedExponent ||
     E::nan_encoding == NanEncoding::ReservedExponent);

namespace encodings {

struct IEEE754 {
  static constexpr auto sign_encoding = SignEncoding::SignMagnitude;
  static constexpr bool has_implicit_bit = true;
  static constexpr int exponent_bias = AutoBias; // 2^(E-1) - 1
  static constexpr auto negative_zero = NegativeZero::Exists;
  static constexpr auto nan_encoding = NanEncoding::ReservedExponent;
  static constexpr auto inf_encoding = InfEncoding::ReservedExponent;
  static constexpr auto denormal_mode = DenormalMode::Full;
};

struct RbjTwosComplement {
  static constexpr auto sign_encoding = SignEncoding::TwosComplement;
  static constexpr bool has_implicit_bit = true;
  static constexpr int exponent_bias = AutoBias; // 2^(E-1)
  static constexpr auto negative_zero = NegativeZero::DoesNotExist;
  static constexpr auto nan_encoding = NanEncoding::TrapValue;
  static constexpr auto inf_encoding = InfEncoding::IntegerExtremes;
  static constexpr auto denormal_mode = DenormalMode::Full;
};

struct PDP10 {
  static constexpr auto sign_encoding = SignEncoding::TwosComplement;
  static constexpr bool has_implicit_bit = false;
  static constexpr int exponent_bias = 128;
  static constexpr auto negative_zero = NegativeZero::DoesNotExist;
  static constexpr auto nan_encoding = NanEncoding::None;
  static constexpr auto inf_encoding = InfEncoding::None;
  static constexpr auto denormal_mode = DenormalMode::None;
};

struct CDC6600 {
  static constexpr auto sign_encoding = SignEncoding::OnesComplement;
  static constexpr bool has_implicit_bit = false;
  static constexpr int exponent_bias = 1024;
  static constexpr auto negative_zero = NegativeZero::Exists;
  static constexpr auto nan_encoding = NanEncoding::None;
  static constexpr auto inf_encoding = InfEncoding::None;
  static constexpr auto denormal_mode = DenormalMode::None;
};

struct E4M3FNUZ {
  static constexpr auto sign_encoding = SignEncoding::SignMagnitude;
  static constexpr bool has_implicit_bit = true;
  static constexpr int exponent_bias = 8; // non-standard
  static constexpr auto negative_zero = NegativeZero::DoesNotExist;
  static constexpr auto nan_encoding = NanEncoding::NegativeZeroBitPattern;
  static constexpr auto inf_encoding = InfEncoding::None;
  static constexpr auto denormal_mode = DenormalMode::Full;
};

struct Relaxed {
  static constexpr auto sign_encoding = SignEncoding::SignMagnitude;
  static constexpr bool has_implicit_bit = true;
  static constexpr int exponent_bias = AutoBias;
  static constexpr auto negative_zero = NegativeZero::DoesNotExist;
  static constexpr auto nan_encoding = NanEncoding::None;
  static constexpr auto inf_encoding = InfEncoding::None;
  static constexpr auto denormal_mode = DenormalMode::FlushBoth;
};

struct GPUStyle {
  static constexpr auto sign_encoding = SignEncoding::SignMagnitude;
  static constexpr bool has_implicit_bit = true;
  static constexpr int exponent_bias = AutoBias;
  static constexpr auto negative_zero = NegativeZero::Exists;
  static constexpr auto nan_encoding = NanEncoding::ReservedExponent;
  static constexpr auto inf_encoding = InfEncoding::ReservedExponent;
  static constexpr auto denormal_mode = DenormalMode::FlushBoth;
};

// Static verification that all predefined bundles satisfy the concept
static_assert(ValidEncoding<IEEE754>);
static_assert(ValidEncoding<RbjTwosComplement>);
static_assert(ValidEncoding<PDP10>);
static_assert(ValidEncoding<CDC6600>);
static_assert(ValidEncoding<E4M3FNUZ>);
static_assert(ValidEncoding<Relaxed>);
static_assert(ValidEncoding<GPUStyle>);

} // namespace encodings
} // namespace opine

#endif // OPINE_CORE_ENCODING_HPP
