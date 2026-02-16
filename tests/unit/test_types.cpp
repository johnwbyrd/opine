#include "opine/opine.hpp"

using namespace opine;

// --- Axis 1: Format geometry ---

static_assert(fp32_layout::sign_bits == 1);
static_assert(fp32_layout::exp_bits == 8);
static_assert(fp32_layout::mant_bits == 23);
static_assert(fp32_layout::total_bits == 32);
static_assert(fp32_layout::is_standard_layout());

static_assert(fp8_e5m2_layout::total_bits == 8);
static_assert(fp8_e4m3_layout::total_bits == 8);
static_assert(fp16_layout::total_bits == 16);
static_assert(bfloat16_layout::total_bits == 16);
static_assert(fp64_layout::total_bits == 64);

// Padded format: fields don't fill the word
using padded = Format<1, 10, 4, 3, 3, 0, 12>;
static_assert(padded::total_bits == 12);
static_assert(padded::padding_bits == 4);
static_assert(!padded::is_standard_layout());

// --- Axis 2: Encoding concept ---

static_assert(ValidEncoding<encodings::IEEE754>);
static_assert(ValidEncoding<encodings::RbjTwosComplement>);
static_assert(ValidEncoding<encodings::E4M3FNUZ>);
static_assert(ValidEncoding<encodings::Relaxed>);
static_assert(ValidEncoding<encodings::GPUStyle>);
static_assert(ValidEncoding<encodings::PDP10>);
static_assert(ValidEncoding<encodings::CDC6600>);

// Verify encoding properties
static_assert(encodings::IEEE754::has_implicit_bit);
static_assert(encodings::IEEE754::negative_zero == NegativeZero::Exists);
static_assert(encodings::RbjTwosComplement::sign_encoding ==
              SignEncoding::TwosComplement);
static_assert(encodings::RbjTwosComplement::negative_zero ==
              NegativeZero::DoesNotExist);
static_assert(encodings::E4M3FNUZ::nan_encoding ==
              NanEncoding::NegativeZeroBitPattern);
static_assert(encodings::E4M3FNUZ::inf_encoding == InfEncoding::None);
static_assert(encodings::Relaxed::nan_encoding == NanEncoding::None);
static_assert(encodings::Relaxed::inf_encoding == InfEncoding::None);
static_assert(encodings::Relaxed::denormal_mode == DenormalMode::FlushBoth);

// --- Axis 3: Rounding ---

static_assert(RoundingPolicy<rounding::TowardZero>);
static_assert(RoundingPolicy<rounding::ToNearestTiesToEven>);
static_assert(rounding::TowardZero::guard_bits == 0);
static_assert(rounding::ToNearestTiesToEven::guard_bits == 3);
static_assert(rounding::TowardPositive::guard_bits == 1);

// --- Axis 4: Exceptions ---

static_assert(ExceptionPolicy<exceptions::Silent>);
static_assert(ExceptionPolicy<exceptions::StatusFlags>);
static_assert(ExceptionPolicy<exceptions::ReturnStatus>);
static_assert(ExceptionPolicy<exceptions::Trap>);

// --- Axis 5: Platform ---

static_assert(PlatformPolicy<platforms::Generic32>);
static_assert(PlatformPolicy<platforms::MOS6502>);
static_assert(PlatformPolicy<platforms::RV32IM>);
static_assert(PlatformPolicy<platforms::CortexM0>);
static_assert(platforms::MOS6502::machine_word_bits == 8);
static_assert(platforms::Generic32::machine_word_bits == 32);

// --- Float type composition ---

// IEEE 754 binary32
using f32 = float32;
static_assert(f32::format::exp_bits == 8);
static_assert(f32::format::mant_bits == 23);
static_assert(f32::exponent_bias == 127); // 2^(8-1) - 1

// IEEE 754 binary16
using f16 = float16;
static_assert(f16::exponent_bias == 15); // 2^(5-1) - 1

// rbj two's complement binary32
using rbj32 = RbjFloat<8, 23>;
static_assert(rbj32::exponent_bias == 128); // 2^(8-1)
static_assert(rbj32::format::total_bits == 32);

// FP8 E4M3FNUZ
using fnuz = fp8_e4m3fnuz;
static_assert(fnuz::exponent_bias == 8); // explicit, non-standard

// SWAR lane counts
static_assert(fp8_e5m2::swar_lanes == 4);  // 32-bit word / 8-bit format
static_assert(float16::swar_lanes == 2);   // 32-bit word / 16-bit format
static_assert(float32::swar_lanes == 1);   // 32-bit word / 32-bit format

// FP8 on 6502: no SWAR benefit
using fp8_6502 = Float<IEEE_Layout<5, 2>, encodings::IEEE754,
                       rounding::TowardZero, exceptions::Silent,
                       platforms::MOS6502>;
static_assert(fp8_6502::swar_lanes == 1); // 8-bit word / 8-bit format

// --- ComputeFormat ---

// Default for binary32 with round-to-nearest-even
using f32_rte = Float<IEEE_Layout<8, 23>, encodings::IEEE754,
                      rounding::ToNearestTiesToEven>;
static_assert(f32_rte::compute_format::exp_bits == 10);   // 8 + 2
static_assert(f32_rte::compute_format::mant_bits == 24);  // 23 + 1 (implicit)
static_assert(f32_rte::compute_format::guard_bits == 3);   // G, R, S

// Default for FP8 E5M2 with truncation
using fp8_trunc = Float<IEEE_Layout<5, 2>, encodings::IEEE754,
                        rounding::TowardZero>;
static_assert(fp8_trunc::compute_format::exp_bits == 7);   // 5 + 2
static_assert(fp8_trunc::compute_format::mant_bits == 3);  // 2 + 1 (implicit)
static_assert(fp8_trunc::compute_format::guard_bits == 0);

// Explicit ComputeFormat for fast 6502 multiply
using fast_compute = ComputeFormat<10, 8, 0>;
static_assert(fast_compute::product_bits == 16);
static_assert(fast_compute::total_bits == 27);
static_assert(fast_compute::total_bytes == 4);

// Full-precision binary32 multiply intermediate
using full_compute = ComputeFormat<10, 24, 3>;
static_assert(full_compute::product_bits == 48);
static_assert(full_compute::total_bits == 59);
static_assert(full_compute::total_bytes == 8);

// --- Fast approximate alias ---

using fast_fp8 = FastFloat<5, 2>;
static_assert(fast_fp8::format::total_bits == 8);

int main() { return 0; }
