#ifndef OPINE_CORE_NUMBER_HPP
#define OPINE_CORE_NUMBER_HPP

// Axis 1: Number — what one numeric value IS.
//
// A Number is either primitive (a digit sequence) or composite (an
// assembly of sub-Numbers). This slice implements:
//
//   Primitive:      radix, digit_width, digit_count, sign_method.
//   FloatingPoint:  significand + exponent, plus exponent_base,
//                   exponent_bias, value_sign, and special_values.
//
// Sub-Numbers of a composite carry their own radix, digit_width,
// and sign_method — that's what lets TI-89 (BCD significand, binary
// exponent) or HP calculator (explicit-nibble significand sign,
// thousand's-complement exponent sign) be expressed at all.
//
// Not implemented in this slice:
//   - FixedPoint, SharedExponent, Codebook composites.
//   - Non-binary arithmetic (radix != 2 in the compute pipeline).
//   - Variable digit_count.

#include <concepts>

namespace opine {

// -----------------------------------------------------------------
// SignMethod — how a Number encodes negative values
// -----------------------------------------------------------------
enum class SignMethod {
  Explicit,                  // Dedicated field. Bit, nibble, or char
                             // — the code values differ per format,
                             // the structural choice is the same.
  RadixComplement,           // Radix 2 → two's; radix 10 → ten's.
  DiminishedRadixComplement, // Radix 2 → one's; radix 10 → nine's.
  Inherent,                  // Balanced ternary, negabinary.
  Unsigned,                  // No sign representation.
};

// -----------------------------------------------------------------
// Special-value properties of a floating-point Number
// -----------------------------------------------------------------
enum class NegativeZero { Exists, DoesNotExist };

enum class NanEncoding {
  ReservedExponent,       // IEEE 754: max exponent, non-zero significand
  TrapValue,              // rbj: two's-complement trap value (0x80…0)
  NegativeZeroBitPattern, // E4M3FNUZ: sign=1, exp=0, sig=0
  None,                   // No NaN
};

enum class InfEncoding {
  ReservedExponent, // IEEE 754: max exponent, zero significand
  IntegerExtremes,  // rbj: max/min signed-integer bit patterns
  None,             // No infinity
};

enum class DenormalMode {
  Full,        // Gradual underflow (IEEE 754)
  FlushToZero, // Output flushing: denormal results → 0
  FlushInputs, // Input flushing: denormal inputs → 0
  FlushBoth,
  None, // Format has no denormals
};

// -----------------------------------------------------------------
// Primitive — a homogeneous sequence of digits
// -----------------------------------------------------------------
// radix determines arithmetic (2 for binary, 10 for BCD, etc.).
// digit_width is the physical bit width of one digit cell.
// digit_count is how many digits this Number has, semantically.
// sign_method is how negatives are encoded.
template <int Radix, int DigitWidth, int DigitCount, SignMethod Sign>
struct Primitive {
  static constexpr int radix = Radix;
  static constexpr int digit_width = DigitWidth;
  static constexpr int digit_count = DigitCount;
  static constexpr SignMethod sign_method = Sign;
  static constexpr bool is_composite = false;

  static_assert(Radix >= 2 || Radix == -2, "radix out of supported range");
  static_assert(DigitWidth >= 1, "digit_width must be positive");
  static_assert(DigitCount >= 1, "digit_count must be positive");
};

// Convenience: binary primitive with radix=2, digit_width=1.
template <int Digits, SignMethod Sign = SignMethod::Unsigned>
using Binary = Primitive<2, 1, Digits, Sign>;

// -----------------------------------------------------------------
// SpecialValues — special-value bundle for FloatingPoint composites
// -----------------------------------------------------------------
template <NegativeZero NegZero, NanEncoding Nan, InfEncoding Inf,
          DenormalMode Denormal>
struct SpecialValues {
  static constexpr NegativeZero negative_zero = NegZero;
  static constexpr NanEncoding nan_encoding = Nan;
  static constexpr InfEncoding inf_encoding = Inf;
  static constexpr DenormalMode denormal_mode = Denormal;
};

// -----------------------------------------------------------------
// FloatingPoint — significand + exponent + composition rule
// -----------------------------------------------------------------
// Significand and Exponent are independent primitive Numbers.
// exponent_base is the base the exponent multiplies (2, 8, 10, 16).
// exponent_bias is the offset applied to the stored exponent value.
// value_sign is how the whole value's sign is encoded (Explicit for
//   IEEE, RadixComplement for rbj / PDP-10, DiminishedRadixComplement
//   for CDC 6600).
// Specials carries { negative_zero, nan_encoding, inf_encoding,
//   denormal_mode }.
template <typename Significand, typename Exponent, int ExponentBase,
          int ExponentBias, SignMethod ValueSign, typename Specials>
struct FloatingPoint {
  using significand = Significand;
  using exponent = Exponent;
  using special_values = Specials;

  static constexpr int exponent_base = ExponentBase;
  static constexpr int exponent_bias = ExponentBias;
  static constexpr SignMethod value_sign = ValueSign;
  static constexpr bool is_composite = true;

  // Flat access for common special-value properties.
  static constexpr NegativeZero negative_zero = Specials::negative_zero;
  static constexpr NanEncoding nan_encoding = Specials::nan_encoding;
  static constexpr InfEncoding inf_encoding = Specials::inf_encoding;
  static constexpr DenormalMode denormal_mode = Specials::denormal_mode;

  static_assert(ExponentBase >= 2, "exponent_base must be at least 2");
  // Structural consistency: NegativeZeroBitPattern encoding cannot
  // coexist with negative zero (the pattern would be ambiguous).
  static_assert(Specials::nan_encoding != NanEncoding::NegativeZeroBitPattern ||
                    Specials::negative_zero == NegativeZero::DoesNotExist,
                "NaN-at-negative-zero requires no negative zero");
  // rbj's two's-complement encoding requires the coupled special-value
  // layout (Trap NaN, IntegerExtremes Inf, no negative zero).
  static_assert(ValueSign != SignMethod::RadixComplement ||
                    Specials::nan_encoding == NanEncoding::TrapValue ||
                    Specials::nan_encoding == NanEncoding::None,
                "two's-complement value_sign requires TrapValue NaN or None");
  static_assert(ValueSign != SignMethod::RadixComplement ||
                    Specials::inf_encoding == InfEncoding::IntegerExtremes ||
                    Specials::inf_encoding == InfEncoding::None,
                "two's-complement value_sign requires IntegerExtremes Inf or None");
  static_assert(ValueSign != SignMethod::RadixComplement ||
                    Specials::negative_zero == NegativeZero::DoesNotExist,
                "two's-complement value_sign has no negative zero");
};

// -----------------------------------------------------------------
// ValidNumber concept
// -----------------------------------------------------------------
template <typename N>
concept ValidNumber = requires {
  { N::is_composite } -> std::convertible_to<bool>;
};

// -----------------------------------------------------------------
// Predefined Number bundles
// -----------------------------------------------------------------
namespace numbers {

// IEEE 754 binary. E = exponent bits, M = stored significand bits
// (the semantic significand has M+1 digits; the leading digit is
// implicit — that's a Layout concern).
using IEEESpecials =
    SpecialValues<NegativeZero::Exists, NanEncoding::ReservedExponent,
                  InfEncoding::ReservedExponent, DenormalMode::Full>;

template <int E, int M>
using IEEE754 = FloatingPoint<Binary<M + 1>, Binary<E>, /*base=*/2,
                              /*bias=*/(1 << (E - 1)) - 1,
                              SignMethod::Explicit, IEEESpecials>;

// x87 80-bit extended. Explicit leading bit — semantic significand
// is 64 digits and the Layout stores all of them.
template <int E, int Sig>
using IEEE754ExplicitBit = FloatingPoint<Binary<Sig>, Binary<E>, 2,
                                         /*bias=*/(1 << (E - 1)) - 1,
                                         SignMethod::Explicit, IEEESpecials>;

// rbj's integer-ordered two's complement binary FP.
using RbjSpecials =
    SpecialValues<NegativeZero::DoesNotExist, NanEncoding::TrapValue,
                  InfEncoding::IntegerExtremes, DenormalMode::Full>;

template <int E, int M>
using RbjTwosComplement =
    FloatingPoint<Binary<M + 1>, Binary<E>, 2,
                  /*bias=*/1 << (E - 1),
                  SignMethod::RadixComplement, RbjSpecials>;

// FP8 E4M3FNUZ: no negative zero, no Inf, NaN at the negative-zero
// bit pattern, non-standard bias 8.
using E4M3FNUZSpecials =
    SpecialValues<NegativeZero::DoesNotExist,
                  NanEncoding::NegativeZeroBitPattern, InfEncoding::None,
                  DenormalMode::Full>;

using E4M3FNUZ = FloatingPoint<Binary<4>, Binary<4>, 2, /*bias=*/8,
                               SignMethod::Explicit, E4M3FNUZSpecials>;

// Relaxed: sign-magnitude, no NaN or Inf, flush both denormals.
using RelaxedSpecials =
    SpecialValues<NegativeZero::DoesNotExist, NanEncoding::None,
                  InfEncoding::None, DenormalMode::FlushBoth>;

template <int E, int M>
using Relaxed = FloatingPoint<Binary<M + 1>, Binary<E>, 2,
                              /*bias=*/(1 << (E - 1)) - 1,
                              SignMethod::Explicit, RelaxedSpecials>;

// GPUStyle: IEEE special values but flush denormals.
using GPUSpecials =
    SpecialValues<NegativeZero::Exists, NanEncoding::ReservedExponent,
                  InfEncoding::ReservedExponent, DenormalMode::FlushBoth>;

template <int E, int M>
using GPUStyle = FloatingPoint<Binary<M + 1>, Binary<E>, 2,
                               /*bias=*/(1 << (E - 1)) - 1,
                               SignMethod::Explicit, GPUSpecials>;

// PDP-10: two's complement whole word, 27-bit significand (no
// implicit digit), 8-bit exponent, bias 128, no NaN / Inf / denormals.
using PDP10Specials =
    SpecialValues<NegativeZero::DoesNotExist, NanEncoding::None,
                  InfEncoding::None, DenormalMode::None>;

using PDP10 = FloatingPoint<Binary<27>, Binary<8>, 2, /*bias=*/128,
                            SignMethod::RadixComplement, PDP10Specials>;

// CDC 6600: one's complement whole word, 48-bit significand, 11-bit
// exponent, bias 1024, negative zero exists.
using CDC6600Specials =
    SpecialValues<NegativeZero::Exists, NanEncoding::None, InfEncoding::None,
                  DenormalMode::None>;

using CDC6600 =
    FloatingPoint<Binary<48>, Binary<11>, 2, /*bias=*/1024,
                  SignMethod::DiminishedRadixComplement, CDC6600Specials>;

// Static verification.
static_assert(ValidNumber<IEEE754<8, 23>>);
static_assert(ValidNumber<RbjTwosComplement<8, 23>>);
static_assert(ValidNumber<E4M3FNUZ>);
static_assert(ValidNumber<Relaxed<5, 2>>);
static_assert(ValidNumber<GPUStyle<8, 23>>);
static_assert(ValidNumber<PDP10>);
static_assert(ValidNumber<CDC6600>);

} // namespace numbers
} // namespace opine

#endif // OPINE_CORE_NUMBER_HPP
