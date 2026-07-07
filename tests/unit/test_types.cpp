// Compile-time exercise of the six-axis Type template.

#include "opine/opine.hpp"

using namespace opine;

// -----------------------------------------------------------------
// Axis 1: Number — primitive + FloatingPoint composite
// -----------------------------------------------------------------

using I32 = numbers::IEEE754<8, 23>;
static_assert(ValidNumber<I32>);
static_assert(I32::is_composite);
static_assert(I32::significand::digit_count == 24); // 23 stored + 1 implicit
static_assert(I32::exponent::digit_count == 8);
static_assert(I32::exponent_bias == 127);
static_assert(I32::exponent_base == 2);
static_assert(I32::value_sign == SignMethod::Explicit);

// Sub-Number properties are directly reachable.
static_assert(I32::significand::radix == 2);
static_assert(I32::significand::digit_width == 1);
static_assert(I32::significand::sign_method == SignMethod::Unsigned);

using I16 = numbers::IEEE754<5, 10>;
static_assert(I16::exponent_bias == 15);
static_assert(I16::significand::digit_count == 11);

using Rbj = numbers::RbjTwosComplement<8, 23>;
static_assert(Rbj::value_sign == SignMethod::RadixComplement);
static_assert(Rbj::exponent_bias == 128);
static_assert(Rbj::negative_zero == NegativeZero::DoesNotExist);
static_assert(Rbj::nan_encoding == NanEncoding::TrapValue);
static_assert(Rbj::inf_encoding == InfEncoding::IntegerExtremes);

static_assert(numbers::E4M3FNUZ::exponent_bias == 8);
static_assert(numbers::E4M3FNUZ::nan_encoding ==
              NanEncoding::NegativeZeroBitPattern);
static_assert(numbers::E4M3FNUZ::inf_encoding == InfEncoding::None);
static_assert(numbers::E4M3FNUZ::negative_zero == NegativeZero::DoesNotExist);

static_assert(numbers::PDP10::value_sign == SignMethod::RadixComplement);
static_assert(numbers::PDP10::exponent_bias == 128);
static_assert(numbers::PDP10::significand::digit_count == 27);

static_assert(numbers::CDC6600::value_sign ==
              SignMethod::DiminishedRadixComplement);

// -----------------------------------------------------------------
// Axis 3: Layout
// -----------------------------------------------------------------

using L32 = layouts::IEEE<8, 23, true>;
static_assert(L32::sign_bits == 1);
static_assert(L32::sign_offset == 31);
static_assert(L32::exp_bits == 8);
static_assert(L32::exp_offset == 23);
static_assert(L32::sig_bits == 23);
static_assert(L32::sig_offset == 0);
static_assert(L32::total_bits == 32);
static_assert(L32::implicit_digit);
static_assert(L32::is_standard());

// x87 extended80: explicit leading bit, all 64 significand bits stored.
using L80 = layouts::IEEE<15, 64, false>;
static_assert(L80::sig_bits == 64);
static_assert(L80::total_bits == 80);
static_assert(!L80::implicit_digit);

// Padded layout: sign at bit 10, other fields lower, 12-bit word.
using PaddedL = Layout<1, 10, 4, 3, 3, 0, 12, true>;
static_assert(PaddedL::padding_bits == 4);
static_assert(!PaddedL::is_standard());

// -----------------------------------------------------------------
// Axis 4: Rounding — unchanged
// -----------------------------------------------------------------

static_assert(RoundingPolicy<rounding::TowardZero>);
static_assert(RoundingPolicy<rounding::ToNearestTiesToEven>);
static_assert(rounding::TowardZero::guard_bits == 0);
static_assert(rounding::ToNearestTiesToEven::guard_bits == 3);
static_assert(rounding::TowardPositive::guard_bits == 1);

// -----------------------------------------------------------------
// Axis 5: Exceptions — unchanged
// -----------------------------------------------------------------

static_assert(ExceptionPolicy<exceptions::Silent>);
static_assert(ExceptionPolicy<exceptions::StatusFlags>);
static_assert(ExceptionPolicy<exceptions::ReturnStatus>);
static_assert(ExceptionPolicy<exceptions::Trap>);

// -----------------------------------------------------------------
// Axis 6: Platform — unchanged
// -----------------------------------------------------------------

static_assert(PlatformPolicy<platforms::Generic32>);
static_assert(PlatformPolicy<platforms::MOS6502>);
static_assert(platforms::MOS6502::machine_word_bits == 8);
static_assert(platforms::Generic32::machine_word_bits == 32);

// -----------------------------------------------------------------
// Type composition
// -----------------------------------------------------------------

static_assert(float16::number::exponent_bias == 15);
static_assert(float32::number::exponent_bias == 127);
static_assert(float32::layout::total_bits == 32);
static_assert(float64::number::exponent_bias == 1023);

// rbj two's complement
static_assert(RbjType<8, 23>::number::exponent_bias == 128);
static_assert(RbjType<8, 23>::number::value_sign ==
              SignMethod::RadixComplement);
static_assert(RbjType<8, 23>::layout::total_bits == 32);

// FP8 E4M3FNUZ
static_assert(fp8_e4m3fnuz::number::exponent_bias == 8);

// extFloat80: explicit-bit layout
static_assert(!extFloat80::layout::implicit_digit);
static_assert(extFloat80::layout::sig_bits == 64);
static_assert(extFloat80::layout::total_bits == 80);

// SWAR lane counts
static_assert(fp8_e5m2::swar_lanes == 4);
static_assert(float16::swar_lanes == 2);
static_assert(float32::swar_lanes == 1);

using fp8_6502 = Type<numbers::IEEE754<5, 2>, layouts::IEEE<5, 2>,
                      rounding::TowardZero, exceptions::Silent,
                      platforms::MOS6502>;
static_assert(fp8_6502::swar_lanes == 1);

// -----------------------------------------------------------------
// ComputeFormat
// -----------------------------------------------------------------

using F32_RTE = Type<numbers::IEEE754<8, 23>, layouts::IEEE<8, 23>,
                     rounding::ToNearestTiesToEven>;
static_assert(F32_RTE::compute_format::exp_bits == 10);
static_assert(F32_RTE::compute_format::mant_bits == 24);
static_assert(F32_RTE::compute_format::guard_bits == 3);

using FP8_Trunc = Type<numbers::IEEE754<5, 2>, layouts::IEEE<5, 2>,
                       rounding::TowardZero>;
static_assert(FP8_Trunc::compute_format::exp_bits == 7);
static_assert(FP8_Trunc::compute_format::mant_bits == 3);
static_assert(FP8_Trunc::compute_format::guard_bits == 0);

using FastCompute = ComputeFormat<10, 8, 0>;
static_assert(FastCompute::product_bits == 16);
static_assert(FastCompute::total_bits == 27);

// -----------------------------------------------------------------
// Fast approximate bundle
// -----------------------------------------------------------------

using FastFp8 = FastType<5, 2>;
static_assert(FastFp8::layout::total_bits == 8);
static_assert(FastFp8::number::nan_encoding == NanEncoding::None);
static_assert(FastFp8::number::inf_encoding == InfEncoding::None);

int main() { return 0; }
