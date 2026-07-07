#ifndef OPINE_CORE_LAYOUT_HPP
#define OPINE_CORE_LAYOUT_HPP

// Axis 3: Layout — how one Number maps to bits.
//
// Layout is pure geometry for a single value. What the fields
// *mean* is Number's job; Layout only says where the bits are.
//
// This slice implements a fixed-width composite Layout for
// FloatingPoint Numbers. Fields (sign, exponent, significand) live
// at compile-time-known offsets inside a fixed-size storage word.
//
// implicit_digit lives here — the design puts it in Layout, not
// Number: the significand *has* M+1 semantic digits (Number); the
// Layout *stores* M of them and hides the leading digit (Layout).
//
// Not implemented in this slice:
//   - Packing codecs other than direct (DPD, BID).
//   - Dynamic field boundaries (posits, Type I Unums).
//   - Variable total_size (strings, Burroughs decimal).
//   - Byte order other than the storage_type's native order.

namespace opine {

// -----------------------------------------------------------------
// Layout — composite fixed-width layout for FloatingPoint Numbers
// -----------------------------------------------------------------
// SignBits is 0 when the composite value_sign is RadixComplement or
// DiminishedRadixComplement (the sign is implicit in the MSB of the
// whole word, not a separate field). Otherwise SignBits is 1 and
// SignOffset gives its bit position.
//
// SigBits is the number of *stored* significand bits. When
// ImplicitDigit is true, the semantic significand is SigBits+1
// digits; when false, it is SigBits digits.
//
// TotalBits is the storage-word width. It may exceed the sum of
// the fields (padded formats) but never falls below.
template <int SignBits, int SignOffset, int ExpBits, int ExpOffset,
          int SigBits, int SigOffset, int TotalBits, bool ImplicitDigit>
struct Layout {
  static constexpr int sign_bits = SignBits;
  static constexpr int sign_offset = SignOffset;
  static constexpr int exp_bits = ExpBits;
  static constexpr int exp_offset = ExpOffset;
  static constexpr int sig_bits = SigBits;
  static constexpr int sig_offset = SigOffset;
  static constexpr int total_bits = TotalBits;
  static constexpr bool implicit_digit = ImplicitDigit;

  static constexpr int padding_bits =
      TotalBits - SignBits - ExpBits - SigBits;

  // "Standard" means IEEE-shaped: [S][E][M] MSB-to-LSB with no
  // padding. False for FFP, MBF, and any format with padding.
  static constexpr bool is_standard() {
    return SignBits == 1 && SignOffset == ExpOffset + ExpBits &&
           ExpOffset == SigOffset + SigBits && SigOffset == 0 &&
           TotalBits == SignBits + ExpBits + SigBits;
  }

  static_assert(SignBits == 0 || SignBits == 1,
                "sign field is 0 or 1 bit in this slice");
  static_assert(ExpBits >= 1, "exponent field must be at least 1 bit");
  static_assert(SigBits >= 1, "significand field must be at least 1 bit");
  static_assert(TotalBits >= SignBits + ExpBits + SigBits,
                "total_bits must accommodate all fields");
  static_assert(SignOffset >= 0 && ExpOffset >= 0 && SigOffset >= 0,
                "offsets must be non-negative");
  static_assert(SignBits == 0 || SignOffset + SignBits <= TotalBits,
                "sign field must fit in storage word");
  static_assert(ExpOffset + ExpBits <= TotalBits,
                "exponent field must fit in storage word");
  static_assert(SigOffset + SigBits <= TotalBits,
                "significand field must fit in storage word");
};

// -----------------------------------------------------------------
// Predefined Layout bundles
// -----------------------------------------------------------------
namespace layouts {

// IEEE 754 field order: [S(1)][E][M(stored)] MSB-to-LSB, no padding.
// Set ImplicitDigit=true for canonical IEEE binary; false for x87
// extended80 and other explicit-leading-bit formats.
template <int E, int M, bool ImplicitDigit = true>
using IEEE = Layout<1, /*SignOffset=*/E + M, E, /*ExpOffset=*/M, M,
                    /*SigOffset=*/0, /*TotalBits=*/1 + E + M, ImplicitDigit>;

// Whole-word-complemented layout: no explicit sign field. Sign is
// derived from the MSB of the whole word (two's or one's complement).
// The stored fields fill bits [0, E+SigBits).
template <int E, int SigBits, int TotalBits = 1 + E + SigBits,
          bool ImplicitDigit = false>
using Complemented =
    Layout</*SignBits=*/0, /*SignOffset=*/0, E, /*ExpOffset=*/SigBits,
           SigBits, /*SigOffset=*/0, TotalBits, ImplicitDigit>;

} // namespace layouts
} // namespace opine

#endif // OPINE_CORE_LAYOUT_HPP
