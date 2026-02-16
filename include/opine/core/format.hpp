#ifndef OPINE_CORE_FORMAT_HPP
#define OPINE_CORE_FORMAT_HPP

namespace opine {

// Axis 1: Format (Bit Geometry)
//
// Describes the physical layout of bits in the storage word.
// Says nothing about meaning — that's Encoding's job.
template <int SignBits, int SignOffset, int ExpBits, int ExpOffset,
          int MantBits, int MantOffset, int TotalBits>
struct Format {
  static constexpr int sign_bits = SignBits;
  static constexpr int sign_offset = SignOffset;
  static constexpr int exp_bits = ExpBits;
  static constexpr int exp_offset = ExpOffset;
  static constexpr int mant_bits = MantBits;
  static constexpr int mant_offset = MantOffset;
  static constexpr int total_bits = TotalBits;

  static constexpr int padding_bits =
      TotalBits - SignBits - ExpBits - MantBits;

  static constexpr bool is_standard_layout() {
    return SignBits == 1 && SignOffset == ExpOffset + ExpBits &&
           ExpOffset == MantOffset + MantBits && MantOffset == 0 &&
           TotalBits == SignBits + ExpBits + MantBits;
  }

  // Compile-time validation
  static_assert(SignBits >= 0 && SignBits <= 1, "sign field is 0 or 1 bit");
  static_assert(ExpBits >= 1, "exponent field must be at least 1 bit");
  static_assert(MantBits >= 1, "mantissa field must be at least 1 bit");
  static_assert(TotalBits >= SignBits + ExpBits + MantBits,
                "total bits must accommodate all fields");
  static_assert(SignOffset >= 0 && ExpOffset >= 0 && MantOffset >= 0,
                "field offsets must be non-negative");
  static_assert(SignBits == 0 || SignOffset + SignBits <= TotalBits,
                "sign field must fit in storage word");
  static_assert(ExpOffset + ExpBits <= TotalBits,
                "exponent field must fit in storage word");
  static_assert(MantOffset + MantBits <= TotalBits,
                "mantissa field must fit in storage word");
};

// Convenience alias for standard IEEE 754 field ordering: [S][E][M]
template <int ExpBits, int MantBits>
using IEEE_Layout =
    Format<1,                      // SignBits
           ExpBits + MantBits,     // SignOffset (MSB)
           ExpBits,                // ExpBits
           MantBits,               // ExpOffset
           MantBits,               // MantBits
           0,                      // MantOffset (LSB)
           1 + ExpBits + MantBits  // TotalBits
           >;

// Named standard layouts
using fp8_e5m2_layout = IEEE_Layout<5, 2>;
using fp8_e4m3_layout = IEEE_Layout<4, 3>;
using fp16_layout = IEEE_Layout<5, 10>;
using bfloat16_layout = IEEE_Layout<8, 7>;
using fp32_layout = IEEE_Layout<8, 23>;
using fp64_layout = IEEE_Layout<11, 52>;

} // namespace opine

#endif // OPINE_CORE_FORMAT_HPP
