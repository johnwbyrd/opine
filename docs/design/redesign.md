# OPINE Policy Redesign: Five Axes

## Problem

The current policy decomposition is starting to sprawl. `FormatDescriptor`
mixes bit geometry with semantic choices (implicit bit, exponent bias).
`DenormalPolicy` is a separate policy that interacts with representation
and special-value choices. The proposed `RepresentationPolicy` would add
another policy that couples tightly with special-value handling. A future
`SpecialValuePolicy` would add a fifth policy that depends on
representation. The result is a set of policies with hidden dependencies
— combinatorial in appearance but constrained in practice.

The goal is a correct, minimal set of policies that can express:

- Every IEEE 754 binary format (binary16, binary32, binary64)
- Historical formats (PDP-10, Xerox Sigma, CDC 6600, Apple II ROM)
- ML accelerator formats (E4M3, E5M2, E4M3FNUZ, bfloat16, TensorFloat-32,
  MXFP formats)
- rbj's two's complement representation with minimal special values
- "Fast approximate" configurations (no NaN, no Inf, flush denormals,
  truncation rounding)
- SWAR (SIMD Within A Register) vectorization
- BLAS-level mixed-precision operations

All within a single well-chosen set of policies, without combinatorial
explosion or hidden constraints.

## The Five Axes

The design space of floating-point implementations has five genuinely
independent axes. Every format that has ever existed or will ever exist
is a point in this space.

```
Float<Format, Encoding, Rounding, Exceptions, Platform>
```

Three of these (Rounding, Exceptions, Platform) have sensible defaults.
For most users:

```cpp
using MyFloat = Float<IEEE_Layout<8, 23>, IEEE754Encoding>;
```

Or even:

```cpp
using MyFloat = IEEE754Float<8, 23>;  // convenience alias
```

### Axis 1: Format (Bit Geometry)

**What it specifies:** The physical layout of bits in the storage word.
Field widths (sign bits, exponent bits, mantissa bits), field positions
(offsets from LSB), total storage width, and padding.

**What it does NOT specify:** What any bit pattern means. Whether there
is an implicit bit. What the exponent bias is. How negative values are
encoded. What special values exist.

This is a simplification of the current `FormatDescriptor`. It loses
`HasImplicitBit` and `ExponentBias`, which move to Encoding. It becomes
pure geometry — a map of which bits are which field.

```cpp
template <int SignBits, int SignOffset,
          int ExpBits, int ExpOffset,
          int MantBits, int MantOffset,
          int TotalBits>
struct Format {
    static constexpr int sign_bits = SignBits;
    static constexpr int sign_offset = SignOffset;
    static constexpr int exp_bits = ExpBits;
    static constexpr int exp_offset = ExpOffset;
    static constexpr int mant_bits = MantBits;
    static constexpr int mant_offset = MantOffset;
    static constexpr int total_bits = TotalBits;

    static constexpr bool is_standard_layout() {
        return SignBits == 1
            && SignOffset == ExpOffset + ExpBits
            && ExpOffset == MantOffset + MantBits
            && MantOffset == 0
            && TotalBits == SignBits + ExpBits + MantBits;
    }

    // Compile-time validation: fields fit, don't overlap, etc.
    // (same static_asserts as current FormatDescriptor)
};
```

Convenience alias for the standard IEEE 754 field ordering:

```cpp
template <int ExpBits, int MantBits>
using IEEE_Layout = Format<
    1,                          // SignBits
    ExpBits + MantBits,         // SignOffset (MSB)
    ExpBits,                    // ExpBits
    MantBits,                   // ExpOffset
    MantBits,                   // MantBits
    0,                          // MantOffset (LSB)
    1 + ExpBits + MantBits      // TotalBits
>;
```

Format says nothing about meaning. `IEEE_Layout<8, 23>` describes a
32-bit word with a 1-bit field at position 31, an 8-bit field at
positions 23-30, and a 23-bit field at positions 0-22. It does not
say this is IEEE 754 binary32. That's Encoding's job.

### Axis 2: Encoding (What Bit Patterns Mean)

This is the axis that absorbs the current sprawl. It replaces
`DenormalPolicy`, the proposed `RepresentationPolicy`, and the
implicit future `SpecialValuePolicy`. It takes `HasImplicitBit`
and `ExponentBias` from the current `FormatDescriptor`.

The sub-parameters of Encoding are:

```
sign_encoding:    SignMagnitude | TwosComplement | OnesComplement
has_implicit_bit: bool
exponent_bias:    int (or auto-calculated)
negative_zero:    Exists | DoesNotExist
nan_encoding:     ReservedExponent | TrapValue | NegativeZeroBitPattern | None
inf_encoding:     ReservedExponent | IntegerExtremes | None
denormal_mode:    Full | FlushToZero | FlushInputs | FlushBoth | None
```

These sub-parameters interact, and the constraints are enforceable
at compile time with `static_assert`:

- `TwosComplement` requires `negative_zero = DoesNotExist`
- `TwosComplement` requires `nan_encoding = TrapValue` or `None`
- `TwosComplement` requires `inf_encoding = IntegerExtremes` or `None`
- `OnesComplement` requires `negative_zero = Exists` (inherent)
- `has_implicit_bit = false` means denormals don't exist as a distinct
  concept (`denormal_mode` is ignored)
- `nan_encoding = NegativeZeroBitPattern` requires
  `negative_zero = DoesNotExist`
- `inf_encoding = ReservedExponent` requires
  `nan_encoding = ReservedExponent` (they share the max exponent in
  IEEE 754's scheme)
- `nan_encoding = ReservedExponent` does not strictly require
  `inf_encoding = ReservedExponent` (a format could use the max
  exponent for NaN only and have no infinity)

#### Why these sub-parameters belong together

rbj's format proves that representation determines special-value
encoding: choosing two's complement forces NaN to the trap value
and infinity to the integer extremes.

E4M3FNUZ proves it from the other direction: choosing "the negative-
zero bit pattern is NaN" forces no negative zero and exactly one NaN,
even in sign-magnitude.

The PDP-10 proves it again: choosing no implicit bit means there are
no denormals, and the mantissa can never be all-zeros for a normalized
value, which means two's complement negation never carries into the
exponent.

These are not independent choices that happen to constrain each other.
They are facets of a single decision: how does this format assign
meaning to bit patterns? That decision is one policy.

#### Predefined encoding bundles

Most users should never specify sub-parameters directly. They pick
a bundle:

```cpp
namespace encodings {

// IEEE 754 standard
//
// Sign-magnitude, implicit bit, standard bias, negative zero exists,
// max exponent reserved for NaN and infinity, full denormal support.
struct IEEE754 {
    static constexpr auto sign_encoding = SignEncoding::SignMagnitude;
    static constexpr bool has_implicit_bit = true;
    static constexpr int  exponent_bias = AutoBias;  // 2^(E-1) - 1
    static constexpr auto negative_zero = NegativeZero::Exists;
    static constexpr auto nan_encoding = NanEncoding::ReservedExponent;
    static constexpr auto inf_encoding = InfEncoding::ReservedExponent;
    static constexpr auto denormal_mode = DenormalMode::Full;
};

// rbj's two's complement
//
// Two's complement negation of entire word, implicit bit, single NaN
// at trap value (0x80...0), infinity at integer extremes, no negative
// zero, all denormals exist, max exponent used for finite values.
//
// See representation.md for full specification and historical context.
struct RbjTwosComplement {
    static constexpr auto sign_encoding = SignEncoding::TwosComplement;
    static constexpr bool has_implicit_bit = true;
    static constexpr int  exponent_bias = AutoBias;  // 2^(E-1)
    static constexpr auto negative_zero = NegativeZero::DoesNotExist;
    static constexpr auto nan_encoding = NanEncoding::TrapValue;
    static constexpr auto inf_encoding = InfEncoding::IntegerExtremes;
    static constexpr auto denormal_mode = DenormalMode::Full;
};

// DEC PDP-10 (1966)
//
// Two's complement, no implicit bit, no NaN, no infinity, no denormals.
// 36-bit word, 8-bit exponent excess-128, 27-bit mantissa.
struct PDP10 {
    static constexpr auto sign_encoding = SignEncoding::TwosComplement;
    static constexpr bool has_implicit_bit = false;
    static constexpr int  exponent_bias = 128;
    static constexpr auto negative_zero = NegativeZero::DoesNotExist;
    static constexpr auto nan_encoding = NanEncoding::None;
    static constexpr auto inf_encoding = InfEncoding::None;
    static constexpr auto denormal_mode = DenormalMode::None;
};

// Control Data 6600 (1964)
//
// One's complement, no implicit bit, no NaN, no infinity, no denormals.
// Negative zero exists (inherent to one's complement).
struct CDC6600 {
    static constexpr auto sign_encoding = SignEncoding::OnesComplement;
    static constexpr bool has_implicit_bit = false;
    static constexpr int  exponent_bias = 1024;
    static constexpr auto negative_zero = NegativeZero::Exists;
    static constexpr auto nan_encoding = NanEncoding::None;
    static constexpr auto inf_encoding = InfEncoding::None;
    static constexpr auto denormal_mode = DenormalMode::None;
};

// NVIDIA E4M3FNUZ (FP8 for ML inference)
//
// Sign-magnitude, implicit bit, no negative zero, NaN is the negative-
// zero bit pattern (sign=1, exp=0, mant=0), no infinity, full denormals.
// Bias is shifted by 1 relative to standard (8 instead of 7 for E=4).
struct E4M3FNUZ {
    static constexpr auto sign_encoding = SignEncoding::SignMagnitude;
    static constexpr bool has_implicit_bit = true;
    static constexpr int  exponent_bias = 8;  // non-standard
    static constexpr auto negative_zero = NegativeZero::DoesNotExist;
    static constexpr auto nan_encoding = NanEncoding::NegativeZeroBitPattern;
    static constexpr auto inf_encoding = InfEncoding::None;
    static constexpr auto denormal_mode = DenormalMode::Full;
};

// Relaxed: fast approximate math
//
// Sign-magnitude (simple), implicit bit, no negative zero, no NaN,
// no infinity (saturate on overflow), flush all denormals to zero.
// Every code path that checks for special values is eliminated by
// the compiler.
struct Relaxed {
    static constexpr auto sign_encoding = SignEncoding::SignMagnitude;
    static constexpr bool has_implicit_bit = true;
    static constexpr int  exponent_bias = AutoBias;
    static constexpr auto negative_zero = NegativeZero::DoesNotExist;
    static constexpr auto nan_encoding = NanEncoding::None;
    static constexpr auto inf_encoding = InfEncoding::None;
    static constexpr auto denormal_mode = DenormalMode::FlushBoth;
};

// GPU-style: IEEE 754 with performance shortcuts
//
// Like IEEE 754 but flush denormals on both input and output.
// Matches the behavior of CUDA's --ftz=true flag.
struct GPUStyle {
    static constexpr auto sign_encoding = SignEncoding::SignMagnitude;
    static constexpr bool has_implicit_bit = true;
    static constexpr int  exponent_bias = AutoBias;
    static constexpr auto negative_zero = NegativeZero::Exists;
    static constexpr auto nan_encoding = NanEncoding::ReservedExponent;
    static constexpr auto inf_encoding = InfEncoding::ReservedExponent;
    static constexpr auto denormal_mode = DenormalMode::FlushBoth;
};

} // namespace encodings
```

#### Custom encodings

For formats not covered by the predefined bundles, users can specify
sub-parameters directly. The compile-time constraints prevent invalid
combinations:

```cpp
// Hypothetical: sign-magnitude, no NaN, no Inf, but keep denormals
struct MyEncoding {
    static constexpr auto sign_encoding = SignEncoding::SignMagnitude;
    static constexpr bool has_implicit_bit = true;
    static constexpr int  exponent_bias = AutoBias;
    static constexpr auto negative_zero = NegativeZero::DoesNotExist;
    static constexpr auto nan_encoding = NanEncoding::None;
    static constexpr auto inf_encoding = InfEncoding::None;
    static constexpr auto denormal_mode = DenormalMode::Full;
};

// This would fail at compile time:
struct BadEncoding {
    static constexpr auto sign_encoding = SignEncoding::TwosComplement;
    // ...
    static constexpr auto nan_encoding = NanEncoding::ReservedExponent;
    // static_assert failure: TwosComplement requires TrapValue or None
};
```

### Axis 3: Rounding

How mantissa precision is managed when a result cannot be exactly
represented. Determines the number of guard bits in the unpacked
representation.

This is unchanged from the current implementation. Rounding is
genuinely independent of format, encoding, and platform.

```cpp
namespace rounding {

// Round toward zero (truncation). Simplest. No guard bits needed.
struct TowardZero {
    static constexpr int guard_bits = 0;

    template <typename Format, typename MantissaType>
    static constexpr auto round_mantissa(
        MantissaType wide_mantissa, bool is_negative);
};

// Round to nearest, ties to even. IEEE 754 default.
// Requires 3 guard bits: Guard, Round, Sticky.
struct ToNearestTiesToEven {
    static constexpr int guard_bits = 3;

    template <typename Format, typename MantissaType>
    static constexpr auto round_mantissa(
        MantissaType wide_mantissa, bool is_negative);
};

// Round to nearest, ties away from zero.
struct ToNearestTiesAway {
    static constexpr int guard_bits = 3;

    template <typename Format, typename MantissaType>
    static constexpr auto round_mantissa(
        MantissaType wide_mantissa, bool is_negative);
};

// Round toward positive infinity (ceiling).
struct TowardPositive {
    static constexpr int guard_bits = 1;

    template <typename Format, typename MantissaType>
    static constexpr auto round_mantissa(
        MantissaType wide_mantissa, bool is_negative);
};

// Round toward negative infinity (floor).
struct TowardNegative {
    static constexpr int guard_bits = 1;

    template <typename Format, typename MantissaType>
    static constexpr auto round_mantissa(
        MantissaType wide_mantissa, bool is_negative);
};

using Default = TowardZero;

} // namespace rounding
```

### Axis 4: Exceptions (Error Handling)

What happens when an operation encounters an exceptional condition:
invalid operation, division by zero, overflow, underflow, inexact
result.

Not yet implemented, but the axis exists and should be designed now
to avoid painting into a corner.

```cpp
namespace exceptions {

// Silently produce the best-effort result.
// NaN for invalid operations (if NaN exists in the encoding),
// saturated value or infinity for overflow (depending on encoding),
// zero or denormal for underflow (depending on encoding).
// No side effects. No status flags. No callbacks.
struct Silent {
    static constexpr bool has_status_flags = false;
    static constexpr bool has_traps = false;
};

// Set status flags that can be queried after operations.
// Analogous to <fenv.h> in C: FE_INVALID, FE_DIVBYZERO,
// FE_OVERFLOW, FE_UNDERFLOW, FE_INEXACT.
//
// For rbj's single-NaN format, this is the primary error
// information channel — the NaN itself carries no payload,
// so the status flags tell you what went wrong.
struct StatusFlags {
    static constexpr bool has_status_flags = true;
    static constexpr bool has_traps = false;
};

// Return a {result, status} pair from every operation.
// No global state. Thread-safe by construction.
// More expensive (larger return values) but explicit.
struct ReturnStatus {
    static constexpr bool has_status_flags = false;
    static constexpr bool has_traps = false;
    // Every operation returns a struct with .value and .status
};

// Call a handler on exceptional conditions.
// The handler can log, abort, throw, or produce a replacement value.
struct Trap {
    static constexpr bool has_status_flags = false;
    static constexpr bool has_traps = true;
};

using Default = Silent;

} // namespace exceptions
```

The exception policy determines the API surface of every arithmetic
function. With `Silent`, operations return a value. With `ReturnStatus`,
they return a value-plus-status pair. With `StatusFlags`, they return a
value and set thread-local state. This is a real interface difference
that must be decided at compile time.

The exception policy interacts mildly with the encoding: if the encoding
has no NaN (`nan_encoding = None`), then an invalid operation must do
something other than return NaN. The exception policy determines what
that something is (saturate, trap, set a flag, etc.). But this
interaction is handled by the code generator examining both policies
at compile time — the policies themselves are still independently
specified.

### Axis 5: Platform (Hardware Capabilities)

What the target machine can do. Determines type selection, available
optimization strategies, and derived properties like SWAR vector width.

This generalizes the current `TypeSelectionPolicy` to include hardware
capability information that the code generator uses to select
implementation strategies.

```cpp
namespace platforms {

struct Platform {
    // Type selection strategy
    // (ExactWidth uses _BitInt, LeastWidth uses uint_leastN_t, etc.)
    using type_policy = type_policies::ExactWidth;

    // Machine word width in bits.
    // Determines SWAR lane count: lanes = machine_word_bits / Format::total_bits
    static constexpr int machine_word_bits = 32;

    // Available hardware operations.
    // Used by the code generator to select implementation strategies.
    static constexpr bool has_hardware_multiply = true;
    static constexpr bool has_barrel_shifter = true;
    static constexpr bool has_conditional_negate = false;
    static constexpr bool has_clz = false;  // count leading zeros
    static constexpr bool has_ctz = false;  // count trailing zeros
};

// Predefined platforms

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
    static constexpr bool has_hardware_multiply = true;   // M extension
    static constexpr bool has_barrel_shifter = true;
    static constexpr bool has_conditional_negate = false;
    static constexpr bool has_clz = false;                // Zbb extension, not base
    static constexpr bool has_ctz = false;
};

struct CortexM0 {
    using type_policy = type_policies::ExactWidth;
    static constexpr int machine_word_bits = 32;
    static constexpr bool has_hardware_multiply = true;   // single-cycle 32x32
    static constexpr bool has_barrel_shifter = false;     // shift-by-1 only
    static constexpr bool has_conditional_negate = false;
    static constexpr bool has_clz = false;
    static constexpr bool has_ctz = false;
};

using Default = Generic32;

} // namespace platforms
```

#### SWAR and vectorization

Vector width is a derived property, not a policy choice:

```cpp
static constexpr int swar_lanes =
    Platform::machine_word_bits / Format::total_bits;
```

For FP8 on a 32-bit machine, `swar_lanes = 4`. For FP16 on a 32-bit
machine, `swar_lanes = 2`. For FP8 on a 6502, `swar_lanes = 1` (no
SWAR benefit).

SWAR implementation strategy is derived from the encoding. Two's
complement encoding enables SWAR comparison (one masked integer compare
for 4 packed FP8 values). Sign-magnitude encoding does not (each lane
needs independent sign-aware logic).

No separate vector policy is needed. The code generator has all the
information it needs from Format (lane width), Encoding (whether SWAR
comparison is possible), and Platform (register width, available
instructions).

## The Full Type

A fully specified OPINE float:

```cpp
template <typename Format,
          typename Encoding    = encodings::IEEE754,
          typename Rounding    = rounding::Default,
          typename Exceptions  = exceptions::Default,
          typename Platform    = platforms::Default>
struct Float;
```

### Convenience aliases

```cpp
// Standard IEEE 754 formats
template <int ExpBits, int MantBits>
using IEEE754Float = Float<IEEE_Layout<ExpBits, MantBits>, encodings::IEEE754>;

using float16  = IEEE754Float<5, 10>;
using float32  = IEEE754Float<8, 23>;
using float64  = IEEE754Float<11, 52>;

// ML formats
using bfloat16 = Float<IEEE_Layout<8, 7>, encodings::IEEE754>;
using fp8_e5m2 = Float<IEEE_Layout<5, 2>, encodings::IEEE754>;
using fp8_e4m3 = Float<IEEE_Layout<4, 3>, encodings::IEEE754>;

using fp8_e4m3fnuz = Float<IEEE_Layout<4, 3>, encodings::E4M3FNUZ>;

// rbj's two's complement format
template <int ExpBits, int MantBits>
using RbjFloat = Float<IEEE_Layout<ExpBits, MantBits>, encodings::RbjTwosComplement>;

// Fast approximate math
template <int ExpBits, int MantBits>
using FastFloat = Float<IEEE_Layout<ExpBits, MantBits>, encodings::Relaxed,
                        rounding::TowardZero, exceptions::Silent>;
```

### How it expresses what the current code expresses

Current `FormatDescriptor<1, 15, 5, 10, 10, 0, 16, true, -1, ExactWidth>`
becomes:

```cpp
Float<
    Format<1, 15, 5, 10, 10, 0, 16>,   // geometry (same numbers)
    encodings::IEEE754                   // meaning (was implicit)
>
```

The information content is identical. The improvement is that meaning
is now explicit and separately configurable.

## UnpackedFloat

`UnpackedFloat` is unchanged. It stores sign as a bool, exponent as an
unsigned integer, and mantissa as an unsigned integer with space for
the implicit bit and guard bits. This is the positive-magnitude
decomposition of the floating-point value, regardless of how the
value is packed.

```cpp
template <typename Format, typename Encoding, typename Rounding>
struct UnpackedFloat {
    bool sign;
    exponent_type exponent;     // biased
    mantissa_type mantissa;     // [implicit bit][stored bits][guard bits]
};
```

The unpacked representation depends on Format (field widths), Encoding
(whether there is an implicit bit), and Rounding (how many guard bits).
It does not depend on Exceptions or Platform. It does not depend on the
sign encoding — after unpacking, every value is in positive-magnitude
form with a separate sign flag.

## Pack and Unpack

`pack` and `unpack` are parameterized on Format, Encoding, and Rounding.
The Encoding determines:

- Whether to perform a conditional negate on entry (TwosComplement) or
  extract the sign bit directly (SignMagnitude)
- Whether to check for NaN/Inf before field extraction, and how
  (exponent-based for ReservedExponent, whole-word comparison for
  TrapValue/IntegerExtremes)
- Whether the implicit bit exists and how to handle denormals

```cpp
template <typename Format, typename Encoding, typename Rounding>
constexpr auto unpack(typename storage_type bits) -> UnpackedFloat<...>;

template <typename Format, typename Encoding, typename Rounding>
constexpr auto pack(const UnpackedFloat<...>& unpacked) -> storage_type;
```

The implementation uses `if constexpr` to select code paths based on
Encoding sub-parameters. Paths not taken are eliminated by the compiler.
A `Relaxed` encoding with no NaN, no Inf, and flushed denormals produces
a `pack`/`unpack` pair with no special-value checks at all — straight
field extraction and insertion.

## Arithmetic Operations

Arithmetic operations (add, subtract, multiply, divide) operate on
`UnpackedFloat` values. They are parameterized on all five axes:

- Format and Encoding determine the value range and special-value behavior
- Rounding determines precision management (guard bits, rounding step)
- Exceptions determine the error-reporting interface
- Platform determines implementation strategy (lookup table vs. shift-add
  multiply, etc.)

```cpp
template <typename Format, typename Encoding, typename Rounding,
          typename Exceptions, typename Platform>
constexpr auto multiply(
    const UnpackedFloat<...>& a,
    const UnpackedFloat<...>& b
) -> result_type;  // result_type depends on Exceptions policy
```

Where `result_type` is `UnpackedFloat<...>` for `Silent` exceptions,
or `{UnpackedFloat<...>, Status}` for `ReturnStatus` exceptions.

## BLAS-Level Operations

BLAS operations are parameterized on multiple Float types, not one.
The accumulator precision is a property of the operation, not of the
individual float format.

```cpp
template <typename InputFloat,        // Format+Encoding+Rounding for A and B
          typename AccumulatorFloat,   // Format+Encoding+Rounding for partial sums
          typename OutputFloat,        // Format+Encoding+Rounding for C
          typename Exceptions,
          typename Platform>
void gemm(
    int M, int N, int K,
    const typename InputFloat::storage_type* A,
    const typename InputFloat::storage_type* B,
    typename OutputFloat::storage_type* C
);
```

A concrete example — FP8 inference with FP16 accumulation:

```cpp
using Input  = Float<IEEE_Layout<5, 2>, encodings::Relaxed,
                     rounding::TowardZero>;
using Accum  = Float<IEEE_Layout<5, 10>, encodings::IEEE754,
                     rounding::ToNearestTiesToEven>;
using Output = Float<IEEE_Layout<5, 2>, encodings::Relaxed,
                     rounding::TowardZero>;

gemm<Input, Accum, Output, exceptions::Silent, platforms::RV32IM>(
    M, N, K, A, B, C);
```

The input and output use relaxed FP8 (no special values, truncation).
The accumulator uses IEEE 754 FP16 with proper rounding to maintain
accuracy during accumulation. The platform tells the code generator
that hardware multiply is available and registers are 32 bits (so
SWAR can pack 4 FP8 values or 2 FP16 values per register).

## SWAR in BLAS Kernels

SWAR vectorization is derived from Format and Platform, not specified
as a separate policy. The code generator computes:

```cpp
constexpr int input_lanes  = Platform::machine_word_bits / InputFloat::Format::total_bits;
constexpr int accum_lanes  = Platform::machine_word_bits / AccumFloat::Format::total_bits;
constexpr int output_lanes = Platform::machine_word_bits / OutputFloat::Format::total_bits;
```

For FP8 input on a 32-bit machine: 4 input lanes, meaning one 32-bit
load fetches 4 FP8 values. For FP16 accumulator: 2 accumulator lanes
per register. The BLAS kernel's inner loop is tiled to exploit this.

What SWAR accelerates in a BLAS kernel:

**Data movement.** Loading 4 FP8 values is one 32-bit load. Unpacking
all four sign bits is one AND. Packing 4 results is one OR of shifted
fields. Data movement often dominates compute time on small processors,
so 4x throughput on loads and stores is significant even if arithmetic
is per-element.

**Comparison and thresholding.** With two's complement encoding,
comparing 4 packed FP8 values against a threshold is one masked integer
compare. With sign-magnitude, each lane needs independent sign-aware
logic, which mostly destroys the SWAR advantage.

**Exponent operations.** Adding exponents for 4 multiplies is 4
parallel additions within a 32-bit word, with carry barriers between
lanes.

**What SWAR does not accelerate.** Mantissa multiply with normalization
and rounding is difficult to parallelize within a single register, because
the intermediate results are wider than the inputs and shift amounts vary
per lane. The realistic SWAR path for arithmetic is: batch unpack,
per-element multiply, batch repack.

## Refactoring Path from Current Code

The current code base contains: `FormatDescriptor`, `UnpackedFloat`,
`pack`, `unpack`, `RoundingPolicy` (TowardZero, ToNearestTiesToEven),
`DenormalPolicy`, and `TypeSelectionPolicy`.

The refactoring:

1. **Split `FormatDescriptor`** into `Format` (geometry only) and
   move `HasImplicitBit` and `ExponentBias` into Encoding.

2. **Create the Encoding policy** with sub-parameters. Initially
   only `IEEE754` and `Relaxed` bundles are needed. `RbjTwosComplement`
   and the historical formats can be added when the two's complement
   code path is implemented.

3. **Merge `DenormalPolicy` into Encoding** as the `denormal_mode`
   sub-parameter. The current `FullSupport`, `FlushToZero`,
   `FlushInputsToZero`, `FlushOnZero`, and `None` map directly to
   the sub-parameter values.

4. **`RoundingPolicy` stays** as Axis 3, unchanged.

5. **`TypeSelectionPolicy` moves** into Platform as
   `Platform::type_policy`. The current `ExactWidth`, `LeastWidth`,
   and `Fastest` policies are preserved.

6. **Add Exceptions** as a new policy (can be stubbed as `Silent`
   initially).

7. **Add Platform** with the hardware capability flags. Initially
   only `Generic32` is needed.

8. **Update `UnpackedFloat`** to take Encoding as a template parameter
   (for the implicit-bit decision).

9. **Update `pack` and `unpack`** to take Encoding as a template
   parameter and dispatch on `sign_encoding`, `nan_encoding`,
   `inf_encoding`, and `denormal_mode` via `if constexpr`.

Nothing is deleted. The existing implementations of `TowardZero`,
`ToNearestTiesToEven`, the type selection policies, and the denormal
policies are all preserved — they are reorganized, not rewritten.

## Enumeration of the Design Space

For reference, here is how every format mentioned in this document
maps to the five axes.

| Format name       | Layout         | Encoding            | Default rounding      |
|--------------------|----------------|----------------------|-----------------------|
| IEEE 754 binary16  | IEEE_Layout<5,10>  | IEEE754          | ToNearestTiesToEven   |
| IEEE 754 binary32  | IEEE_Layout<8,23>  | IEEE754          | ToNearestTiesToEven   |
| IEEE 754 binary64  | IEEE_Layout<11,52> | IEEE754          | ToNearestTiesToEven   |
| bfloat16           | IEEE_Layout<8,7>   | IEEE754          | ToNearestTiesToEven   |
| TensorFloat-32     | IEEE_Layout<8,10>  | IEEE754          | ToNearestTiesToEven   |
| FP8 E5M2           | IEEE_Layout<5,2>   | IEEE754          | ToNearestTiesToEven   |
| FP8 E4M3           | IEEE_Layout<4,3>   | IEEE754          | ToNearestTiesToEven   |
| FP8 E4M3FNUZ       | IEEE_Layout<4,3>   | E4M3FNUZ         | ToNearestTiesToEven   |
| rbj TC float32     | IEEE_Layout<8,23>  | RbjTwosComplement| ToNearestTiesToEven   |
| rbj TC float16     | IEEE_Layout<5,10>  | RbjTwosComplement| ToNearestTiesToEven   |
| PDP-10             | Format<1,35,8,27,27,0,36> | PDP10   | (platform-defined)    |
| CDC 6600           | Format<1,59,11,48,48,0,60> | CDC6600 | (platform-defined)    |
| Fast FP8 E5M2      | IEEE_Layout<5,2>   | Relaxed          | TowardZero            |
| GPU-style FP32     | IEEE_Layout<8,23>  | GPUStyle         | ToNearestTiesToEven   |

Every row in this table is fully specified by choosing a point on each
axis. No row requires information outside the five axes to determine
its behavior.

## Compile-Time Constraint Enforcement

The `Encoding` policy must enforce internal consistency. This is done
via `static_assert` in the Encoding concept or in the `Float` type
itself.

```cpp
template <typename E>
concept ValidEncoding = requires {
    // All sub-parameters must be present
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
```

An invalid combination produces a compile-time error naming the
violated constraint. Users see this only if they write a custom
encoding with contradictory sub-parameters. Predefined bundles
are pre-validated and cannot trigger these errors.

## What This Design Does Not Cover

This document defines the policy decomposition. It does not specify:

- The implementation of any arithmetic operation (add, subtract,
  multiply, divide). Those will be designed separately once the
  policy framework is in place.

- The BLAS kernel tiling strategy, register allocation, or loop
  structure. Those are implementation concerns within the BLAS
  layer that operate on the types defined here.

- The assembly-level optimizations for specific platforms. Those
  are pluggable implementations selected by the Platform policy
  but not defined by it.

- The MX/microscaling block format. Block scaling is a data layout
  concern (a block of values sharing an exponent) that sits above
  the individual-float level. It will need its own abstraction that
  composes with the Float type defined here.

## Summary

Five axes. Every floating-point format that has ever existed or will
ever exist is a point in this space:

1. **Format**: Bit geometry. Where the fields are. Pure structure.
2. **Encoding**: What bit patterns mean. Sign encoding, implicit bit,
   exponent bias, special values, denormals. One policy, internally
   constrained, with predefined bundles for common cases.
3. **Rounding**: How precision is managed. Guard bit count and
   rounding algorithm. Independent of everything else.
4. **Exceptions**: What happens on errors. Interface contract for
   every arithmetic function. Independent of everything else.
5. **Platform**: What hardware is available. Type selection, word
   width, instruction availability. Determines SWAR lane count and
   implementation strategy as derived properties.

SWAR and vectorization are not a policy axis — they are derived from
Format (lane width) and Platform (register width). BLAS operations
compose multiple Float types (input, accumulator, output) without
requiring additional policy machinery at the float level.

The refactoring from current code is mechanical: split FormatDescriptor,
merge DenormalPolicy into Encoding, move TypeSelectionPolicy into
Platform. No existing implementations are deleted. The policy count
goes from "4 and growing" to "5 and complete."
