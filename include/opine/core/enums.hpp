#ifndef OPINE_CORE_ENUMS_HPP
#define OPINE_CORE_ENUMS_HPP

namespace opine {

enum class SignEncoding { SignMagnitude, TwosComplement, OnesComplement };

enum class NegativeZero { Exists, DoesNotExist };

enum class NanEncoding {
  ReservedExponent,       // IEEE 754: all-ones exponent, non-zero mantissa
  TrapValue,              // Two's complement: 0x80...0 (most negative integer)
  NegativeZeroBitPattern, // E4M3FNUZ: sign=1, exp=0, mant=0
  None                    // No NaN representation
};

enum class InfEncoding {
  ReservedExponent,  // IEEE 754: all-ones exponent, zero mantissa
  IntegerExtremes,   // Two's complement: max/min representable integers
  None               // No infinity representation
};

enum class DenormalMode {
  Full,      // Gradual underflow, IEEE 754 compliant
  FlushToZero,    // Output flushing: denormal results become zero
  FlushInputs,    // Input flushing: denormal inputs treated as zero
  FlushBoth,      // Both input and output flushing
  None            // Format has no denormals (e.g., no implicit bit)
};

// Sentinel for "compute bias from exponent width" (2^(E-1) - 1 for
// sign-magnitude, 2^(E-1) for two's complement).
inline constexpr int AutoBias = -1;

} // namespace opine

#endif // OPINE_CORE_ENUMS_HPP
