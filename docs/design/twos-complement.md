# Representation Policy: Sign-Magnitude vs. Two's Complement

## Overview

IEEE 754 encodes negative numbers using sign-magnitude: a dedicated sign bit
indicates polarity, and the remaining bits encode the absolute value. This is
not the only option. Two's complement representation, universal for integers,
can also encode floating-point values — and on processors without FPUs, it
offers significant practical advantages.

This approach has historical precedent. The DEC PDP-10 (and its relatives,
the PDP-6 and DECSYSTEM-20), the Xerox Sigma computers, and the Control
Data 1604/6600 series all used complemented representations for negative
floating-point values. On these machines, integer compare instructions
produced correct results for floating-point operands. IEEE 754 abandoned
this property when it standardized on sign-magnitude in 1985.

OPINE treats representation as a policy dimension. However, unlike rounding
or denormal policies, the two's complement representation is not fully
orthogonal to special-value handling. The two's complement representation
determines how infinity and NaN are encoded (see "rbj's Design" below).
Sign-magnitude representation remains freely composable with any
special-value policy.

## Motivation

On a machine with no floating-point hardware, every FP comparison is a
function call. In IEEE 754 sign-magnitude, comparing two floats requires
examining the sign bits and conditionally reversing the comparison direction
for negative values. This is a branch-heavy operation on architectures where
branches are expensive.

Robert Bristow-Johnson (rbj) proposed a two's complement floating-point
representation that eliminates this overhead. The core requirement: the
mapping from floating-point value to the corresponding integer value, given
exactly the same bits in the word, must be a monotonic, strictly-increasing
function. Then the integer compare operation produces valid results for
floating-point numbers. With IEEE 754 sign-magnitude, integer comparison
gives backwards results when both operands are negative.

This is a rediscovery, not an invention. The PDP-10 did exactly this: a
negative floating-point value was generated with the same two's complement
instruction as a negative integer, and fixed-point compare instructions
were used for floating-point values. The Xerox Sigma computers did the
same. The Control Data machines used one's complement instead of two's
complement, achieving the same property with slightly different trade-offs.
These formats worked in production hardware for decades before IEEE 754
displaced them.

## Historical Precedent

### DEC PDP-10 (1966)

36-bit word. 1 sign bit, 8-bit exponent (excess-128), 27-bit mantissa
(no hidden bit). Negative floating-point numbers were the two's complement
of the entire word. Integer compare instructions worked correctly for all
normalized floating-point values.

The PDP-10 designers noted that two's complement negation could in principle
cause a carry from the mantissa into the exponent. This did not occur in
practice because the PDP-10 had no hidden bit: the mantissa of a normalized
value never had all bits set, so the "+1" step of two's complement could
not overflow the mantissa field. (See "Carry Propagation" below for how
this interacts with formats that do have a hidden bit.)

### Xerox Sigma (1966)

Also two's complemented the combined exponent and mantissa fields for
negative values. Used a hexadecimal exponent (like System/360). Integer
compare gave correct floating-point ordering.

### Control Data 1604/6600 (1960/1964)

Used one's complement rather than two's complement for the entire word.
Each positive exponent value had a corresponding complemented negative
value. This avoided the carry-propagation question entirely but introduced
two representations of zero (positive zero and negative zero), mirroring
the trade-off in one's complement integer arithmetic.

## rbj's Design

rbj's proposal is not just a representation choice — it is a coherent
package of representation and special-value decisions that are
interdependent. The decisions below apply specifically to the two's
complement representation; they do not constrain sign-magnitude formats.

### The complete specification

For a format with 1 sign bit, E exponent bits, and M mantissa bits
(with implicit leading 1 for normalized values), the word width is
N = E + M + 1 bits. The exponent uses offset binary with bias = 2^(E-1).

**Positive values** (MSB = 0): The bit pattern directly encodes
sign=0, exponent, mantissa in the usual layout:

```
[0][E exponent bits][M mantissa bits]
```

Normalized (exp > 0): value = 2^(exp - bias) * (1 + mant * 2^(-M))
Denormalized (exp = 0): value = 2^(1 - bias) * (mant * 2^(-M))

**Negative values** (MSB = 1): The entire N-bit word is the two's
complement of the corresponding positive value. To extract fields,
negate the word first, then read the fields as above.

### Special values

These decisions follow from the two's complement representation and
are not independent policy choices:

**Zero**: 0x00000000 (all bits zero). There is only one zero. No
negative zero exists — this is inherent to two's complement.

**Positive infinity**: 0x7FFFFFFF (maximum positive signed integer).
This is the upper bound of the monotonic integer mapping.

**Negative infinity**: 0x80000001 (minimum negative signed integer
that has a valid positive counterpart). This is the two's complement
of 0x7FFFFFFF.

**NaN**: 0x80000000. The single two's complement trap value — the
one bit pattern that cannot be negated. This is the only non-number
in the entire format. One bit pattern out of 2^N.

**Maximum finite positive value**: 0x7FFFFFFE. One below positive
infinity.

**Maximum finite negative value**: 0x80000002. One above negative
infinity (i.e., the two's complement of 0x7FFFFFFE).

### Why infinity lives at the integer extremes

In IEEE 754, infinity is encoded by reserving the maximum exponent
value. All 2^M bit patterns with exponent = all-1s are consumed by
infinity (2 patterns) and NaN (2^(M+1) - 2 patterns). This wastes
a substantial fraction of the representable range.

In rbj's design, infinity occupies exactly 2 bit patterns (the
integer extremes), and NaN occupies exactly 1 bit pattern (the
two's complement trap value). All other bit patterns — including
all combinations with the maximum exponent value — represent finite
numbers or denormals. This recovers approximately 2^M representable
values at the top of the range compared to IEEE 754.

This design is only coherent with two's complement representation.
The "integer extremes as infinity" concept requires that the bit
patterns sort as signed integers, which is the defining property of
the two's complement representation. A sign-magnitude format would
need a different infinity encoding.

### Why there is only one NaN

rbj's position: at most one bit pattern should signal an exception.
IEEE 754 uses an entire exponent value (2^(M+1) - 2 bit patterns)
for NaN, supporting quiet vs. signaling NaN and NaN payloads. rbj
considers this an overallocation.

With only one NaN, the format cannot use NaN propagation as an error
channel — you can detect that an error occurred (the result is
0x80000000), but you cannot determine from the result alone what
went wrong. rbj suggests a separate "result code" mechanism: a
status register or return value that, when queried after a NaN
result, indicates the specific error (invalid operation, division
by zero, etc.).

This maps to OPINE's error-handling policy dimension. The two's
complement representation determines the NaN encoding (0x80000000);
the error-handling policy determines what supplementary information
is available when NaN is produced.

### Max exponent is not reserved

This is worth stating explicitly because it is a significant
departure from IEEE 754 and interacts with implementation.

In IEEE 754, the maximum biased exponent (all 1s) is entirely
reserved for infinity and NaN. No finite value has this exponent.
This means that when detecting special values, checking
`exponent == max_exponent` is sufficient to identify non-finite
values.

In rbj's design, the maximum exponent encodes ordinary finite
values. Special values are identified by their complete bit pattern
(the integer extremes and the trap value), not by exponent alone.
The detection logic is different:

```
is_nan(x):        x == 0x80000000
is_pos_inf(x):    x == 0x7FFFFFFF
is_neg_inf(x):    x == 0x80000001
is_inf(x):        is_pos_inf(x) || is_neg_inf(x)
is_finite(x):     !is_nan(x) && !is_inf(x)
```

These are cheaper checks than IEEE 754's exponent-based detection
(simple equality vs. field extraction and comparison), but they
must be applied in the right places.

### Denormals

All denormals exist. Exponent = 0 with nonzero mantissa represents
a denormal in the usual way (no implicit leading 1, minimum exponent).
No denormal bit patterns are wasted or reserved.

### Rounding

rbj advocates round-to-nearest for determinism. He is ambivalent
about ties-to-even ("convergent rounding"): it provides better
statistical properties but adds implementation complexity. This is
an independent choice from the representation — OPINE's existing
rounding policies apply to two's complement formats without
modification, since rounding operates on the unpacked mantissa.

## Carry Propagation During Negation

The PDP-10 avoided the carry question because it had no hidden bit:
the mantissa of a normalized value could not be all zeros in the
stored field (the leading 1 was explicit), so the +1 step of two's
complement negation could not overflow the mantissa into the
exponent.

rbj's format has a hidden bit, and both normalized values (where the
stored mantissa can be all zeros, meaning the actual mantissa is
exactly 1.000...0) and denormals (where the stored mantissa can be
all zeros, meaning the value is zero) can have all-zero mantissa
fields.

Consider negating a positive value where the stored mantissa is all
zeros and the exponent is E:

```
Original:     [0][E bits][000...0]
Bit invert:   [1][~E bits][111...1]
Add 1:        [1][(~E)+1 bits][000...0]    (carry propagates through mantissa into exponent)
```

The carry increments the inverted exponent by 1. The result is that
the negative representation of a value with exponent E and zero
mantissa has the bit pattern where the sign is 1, the exponent field
contains `(~E) + 1` (interpreting the field as unsigned), and the
mantissa is all zeros.

When we negate this back to extract fields:

```
Negate:       [0][E bits][000...0]
```

We recover the original exponent and mantissa. The round-trip is
exact. The carry propagation is not a problem — it is an expected
consequence of two's complement arithmetic, and negation correctly
inverts it.

The case to watch is **denormal zero**: the bit pattern 0x00000000.
Its negation is 0x00000000 (two's complement of zero is zero). This
is correct: -0 = +0 = 0 in two's complement.

The case that **cannot** occur: the trap value 0x80000000 is defined
as NaN, and negating it would overflow. The format must never attempt
to negate NaN. This is enforced by checking for NaN before arithmetic.

For all other values, two's complement negation is well-defined and
produces the correct negative representation.

This should be verified exhaustively for small formats (FP8: 256
values) as a test case.

## What Two's Complement Representation Buys

### Comparison becomes integer comparison

This is the primary advantage and rbj's core requirement. With
sign-magnitude, a floating-point comparison function must:

1. Check if both values are zero (positive and negative zero are equal)
2. Compare sign bits
3. If signs differ, the positive value is larger
4. If both positive, compare magnitudes directly
5. If both negative, compare magnitudes in reverse order

With two's complement, the bit pattern sorts correctly as a signed
integer across the entire number line. A float comparison is one
integer comparison instruction. No branches, no special cases.

### No negative zero

Two's complement has a single zero representation (all bits zero).
The IEEE 754 requirement that `-0.0 == +0.0` yet
`1.0 / -0.0 != 1.0 / +0.0` is a source of special-case handling
throughout every operation. Eliminating negative zero removes an
entire category of edge cases.

### `nextafter()` becomes integer increment

To find the next representable value above a given float, increment
the integer representation by 1. This works across the entire number
line, including across zero and into negatives (skipping NaN at
0x80000000 and the infinities). In sign-magnitude, `nextafter` must
handle the sign-magnitude split and treat negative values specially.

### ULP distance is integer subtraction

The number of representable values between two floats (the ULP
distance) is the absolute difference of their integer representations,
minus any skipped special values. This works for any pair of values,
including across zero. In sign-magnitude, this requires special
handling for mixed-sign pairs.

### Sorting is native integer sort

Radix sort, comparison sort, or any integer sorting algorithm applied
directly to the bit patterns produces correctly ordered floating-point
values. No preprocessing needed. In IEEE 754, you must apply the
"flip sign bit and conditionally invert" transform first.

### More representable finite values

Because the maximum exponent is not reserved for infinity/NaN, the
format represents approximately 2^M more finite values at the top of
the range compared to IEEE 754 with the same field widths. Infinity
and NaN consume 3 bit patterns total instead of 2^(M+1).

## What Two's Complement Representation Costs

### Absolute value is more expensive

In sign-magnitude, `abs` is a single AND instruction that clears the
sign bit. In two's complement, `abs` requires checking the sign and
conditionally negating the entire word — typically a comparison, a
conditional branch or select, and a negate (invert and add 1). On a
6502 this is several instructions; on a 32-bit machine with a
conditional negate instruction it can be two instructions.

### Arithmetic entry/exit requires conditional negate

Before performing arithmetic on the exponent and mantissa fields,
negative operands must be converted to their positive magnitude
(negated). After the operation, a negative result must be converted
back to two's complement. This adds a conditional negate at the entry
and exit of each arithmetic operation.

However, this cost should be weighed against the sign-handling logic
that sign-magnitude arithmetic already requires. IEEE 754 addition
must determine the effective operation (add vs. subtract),
conditionally swap operands based on magnitude, and branch on sign
combinations. Two's complement replaces that logic with a fixed-cost
negate on entry and exit. The total cost is comparable; the structure
is different.

### No interoperability with IEEE 754 hardware

Any interaction with hardware that expects IEEE 754 encoding (FPUs,
peripherals, sensors, network protocols) requires an explicit format
conversion. On the processors OPINE targets — those without FPUs —
this is typically not a concern, because there is no IEEE 754
hardware to interface with.

Conversion between sign-magnitude and two's complement is inexpensive:
check the sign bit, conditionally two's complement the word. It is a
few instructions, not a restructuring of the data.

### Special-value detection differs from IEEE 754

Code that checks `exponent == max_exponent` to detect infinity/NaN
will not work. Special values are detected by whole-word comparison
against specific bit patterns. This is actually cheaper per check,
but it is different, and any code ported from an IEEE 754
implementation must be updated.

## Architectural Fit in OPINE

### Representation as a policy dimension

The representation policy is partially orthogonal to other policy
dimensions. It composes freely with format, rounding, type selection,
and error handling. It does **not** compose freely with special-value
handling — the two's complement representation determines the
special-value encoding.

| Dimension       | Controls                                      | Interaction with representation |
|-----------------|-----------------------------------------------|---------------------------------|
| Format          | Bit widths and positions of fields             | None — fields are the same |
| Rounding        | How mantissa is rounded during pack            | None — operates on unpacked values |
| Denormal        | Whether denormals exist and how they're handled| None — denormal detection operates on unpacked fields |
| Special values  | NaN, Inf, signed zero encoding and behavior    | **Coupled** — TC determines encoding |
| Error handling  | How errors are reported                        | Minor — single NaN requires status mechanism |
| Type selection  | Integer types used for storage                 | None |

### The coupling between representation and special values

For `SignMagnitude`, the special-value policy is independent. You can
choose IEEE 754-style NaN/Inf, no NaN, no Inf, or any combination.
The representation does not constrain these choices.

For `TwosComplement`, the special-value encoding is determined by the
representation:

- NaN is the trap value (0x80...0)
- Inf is the integer extremes (0x7F...F and 0x80...1)
- No negative zero

These are not design preferences — they are structural consequences
of the monotonic integer mapping requirement. Moving infinity or NaN
to different bit patterns would break the ordering property that
motivates the representation.

OPINE should model this as a representation policy that carries its
own special-value semantics, rather than trying to compose two's
complement with an independent special-value policy.

### Proposed policy structure

```cpp
namespace opine::inline v1::representation_policies {

// Concept: A representation policy provides encode/decode semantics
// and declares its special-value properties
template <typename T>
concept RepresentationPolicy = requires {
    { T::name } -> std::convertible_to<const char*>;
    { T::has_negative_zero } -> std::convertible_to<bool>;
    { T::nan_count } -> std::convertible_to<int>;
    { T::inf_uses_max_exponent } -> std::convertible_to<bool>;
};

// IEEE 754 sign-magnitude (current behavior)
//
// MSB is the sign bit. Remaining bits encode the magnitude.
// Positive and negative values with the same magnitude differ
// only in the sign bit.
//
// Special-value handling is determined by a separate policy.
//
// Properties:
// - Negative zero exists (sign=1, exp=0, mant=0)
// - abs() is a single bit clear
// - Comparison requires sign-aware logic
// - Max exponent reserved for inf/NaN (when using IEEE special values)
struct SignMagnitude {
    static constexpr const char* name = "SignMagnitude";
    static constexpr bool has_negative_zero = true;

    // Special-value encoding is independent; these are defaults
    // that can be overridden by a separate special-value policy.
    static constexpr int nan_count = -1;  // determined by special-value policy
    static constexpr bool inf_uses_max_exponent = true;  // IEEE 754 default
};

// Two's complement representation (rbj's design)
//
// Positive values are stored with MSB=0.
// Negative values are the two's complement of the positive
// representation. To extract fields from a negative value,
// negate the storage word first.
//
// Special-value encoding is determined by the representation
// and cannot be overridden independently:
// - NaN:  the trap value (MSB set, all other bits zero)
// - +Inf: maximum positive signed integer (all bits set except MSB)
// - -Inf: two's complement of +Inf (MSB set, LSB set, rest zero)
// - No negative zero
// - Max exponent is NOT reserved; used for finite values
//
// Properties:
// - No negative zero
// - Comparison is plain signed integer comparison
// - nextafter() is integer increment
// - abs() requires conditional negate
// - Exactly 1 NaN and 2 Inf bit patterns (3 total non-finite)
struct TwosComplement {
    static constexpr const char* name = "TwosComplement";
    static constexpr bool has_negative_zero = false;

    // Special-value encoding is fixed by the representation.
    // These are not configurable.
    static constexpr int nan_count = 1;
    static constexpr bool inf_uses_max_exponent = false;
};

using DefaultRepresentationPolicy = SignMagnitude;

} // namespace opine::inline v1::representation_policies
```

### Impact on `FormatDescriptor`

None. `FormatDescriptor` describes the bit layout of the fields within
the positive-magnitude form of the number. For sign-magnitude, this is
the direct layout. For two's complement, this is the layout after
negation of negative values. The descriptor does not change.

### Impact on `UnpackedFloat`

None. `UnpackedFloat` already stores sign as a bool and exponent/mantissa
as unsigned values. This is the positive-magnitude decomposition regardless
of the packed encoding. The unpacked representation is
representation-invariant by design.

### Impact on `unpack()`

For `SignMagnitude`, the implementation is unchanged from the current code.

For `TwosComplement`:

```
1. Check for NaN: if storage word == trap value (0x80...0), return NaN
2. Check for +Inf: if storage word == max positive (0x7F...F), return +Inf
3. Read the MSB of the storage word to determine sign
4. If negative (MSB set):
   a. Two's complement the entire storage word to get positive magnitude
   b. Check for -Inf: if original word was 0x80...1, return -Inf
5. Extract exponent, mantissa, implicit bit exactly as current code does
6. Set result.sign from step 3
7. Return UnpackedFloat
```

The field extraction logic (steps 5-6) is identical for both
representations. The two's complement path adds a NaN/Inf check
and a conditional negate.

Note: the ordering of checks matters. NaN (0x80000000) must be
detected before attempting negation, because negating the trap value
is undefined.

### Impact on `pack()`

For `SignMagnitude`, the implementation is unchanged from the current code.

For `TwosComplement`:

```
1. If the unpacked value is NaN, return the trap value (0x80...0)
2. If the unpacked value is +Inf, return max positive (0x7F...F)
3. If the unpacked value is -Inf, return 0x80...1
4. Assemble exponent and rounded mantissa into storage word
   exactly as current code does (producing positive-magnitude form)
5. If unpacked.sign is true:
   a. Two's complement the entire storage word
6. Return storage word
```

The core assembly logic (step 4) is identical. The additions are
special-value encoding (steps 1-3) and the conditional negate
(step 5).

### Impact on future operations

**Comparison** (`<`, `<=`, `==`, `!=`, `>=`, `>`):

For `SignMagnitude`: Must check signs, handle negative zero, conditionally
reverse comparison direction for negative values. Typical implementation
is 5-10 instructions with branches.

For `TwosComplement`: Cast storage word to signed integer type and compare.
One instruction on most architectures. No branches. NaN handling: any
comparison involving NaN returns false (except `!=`), which requires a
check for the trap value before comparison.

**Negate**:

For `SignMagnitude`: XOR the sign bit. One instruction.

For `TwosComplement`: Two's complement the storage word. Invert and add 1.
Two to three instructions. Must check for NaN (trap value) first — negating
NaN is undefined.

**Absolute value**:

For `SignMagnitude`: AND-mask to clear sign bit. One instruction.

For `TwosComplement`: Check sign; if negative, two's complement. Three to
five instructions depending on architecture.

**Conversion between representations**:

Conversion from sign-magnitude to two's complement (or vice versa) is:

```
1. If sign bit is set:
   a. Clear sign bit (or set it, depending on direction)
   b. Two's complement the magnitude bits
2. Done
```

This enables interoperability with IEEE 754 encoded data from sensors,
networks, or file formats at a cost of a few instructions per conversion.

## Interaction with Denormal Handling

Denormal detection (`exponent == 0 && mantissa != 0`) operates on the
unpacked fields. Since unpacking always produces positive-magnitude
exponent and mantissa regardless of representation, denormal policies
work identically for both representations.

One edge case to verify: for the `TwosComplement` representation, the
conditional negate of a negative denormal's storage word must produce a
valid positive-magnitude form with `exponent == 0` and the correct
mantissa. This should hold by construction — two's complement negation
is a bit-level operation that preserves the field structure after
accounting for carry propagation (see "Carry Propagation" above) —
but it must be tested exhaustively for small formats.

## Error Handling with a Single NaN

IEEE 754 uses NaN propagation as a default error channel: NaN payloads
can carry information about the original error, and quiet NaN
propagation ensures that a chain of operations produces a NaN result
if any input was invalid, without requiring the programmer to check
status flags.

rbj's format has exactly one NaN. It signals that an error occurred,
but carries no information about what the error was. This means:

1. NaN propagation still works as an error indicator — if any
   operation in a chain produces NaN, the final result is NaN.

2. Determining the *cause* of the NaN requires a separate mechanism.
   rbj suggests a "result code" — a status register or auxiliary return
   value that records the specific error condition (invalid operation,
   division by zero, overflow, underflow, inexact). This is
   analogous to IEEE 754's exception flags, but it is the *primary*
   error information channel rather than a supplement to NaN payloads.

3. The error-handling policy in OPINE determines what this mechanism
   looks like. Options include: a thread-local status word (like
   `fenv.h`), a return-value struct (like `{result, status}`), a
   callback/exception, or simply ignoring errors (NaN appears in the
   output and the programmer checks for it). This is an independent
   policy choice that works with either representation.

## Testing Strategy

### Exhaustive round-trip testing for small formats

For all FP8 formats (256 values), verify:

- `pack(unpack(x))` preserves the value for all valid encodings
- For two's complement: every positive encoding and its negation
  round-trip correctly
- The trap value (0x80 for 8-bit) is correctly identified as NaN
- The integer extremes (0x7F and 0x81 for 8-bit) are correctly
  identified as +Inf and -Inf

### Monotonic ordering verification

For all FP8 values (excluding NaN), verify that the signed integer
ordering matches the floating-point value ordering. This is the
defining property of the representation and is the most important
test.

### Carry propagation verification

For all FP8 values where the stored mantissa is all zeros (the carry
case), verify that negation produces the correct bit pattern and
that re-negation recovers the original value.

### Comparison correctness

For all FP8 value pairs, verify that signed integer comparison of the
two's complement storage words produces the same ordering as IEEE 754
magnitude comparison (excluding negative zero cases, which don't exist
in two's complement).

### Conversion round-trip

For all FP8 values, verify:

```
to_twos_complement(to_sign_magnitude(x)) == x
to_sign_magnitude(to_twos_complement(x)) == x
```

### Cross-representation arithmetic consistency

For all FP8 value pairs where the result is representable, verify that:

```
unpack_tc(pack_tc(unpack_sm(pack_sm(result_sm)))) == result_tc
```

That is: arithmetic on sign-magnitude unpacked values and arithmetic on
two's complement unpacked values produce the same unpacked result, because
the unpacked representation is the same for both.

## Performance Expectations

The performance case for two's complement is workload-dependent.

**Comparison-heavy workloads** (sorting, searching, threshold comparisons,
decision trees, control flow): Two's complement wins. Each comparison
saves 5-10 instructions and eliminates branches. On workloads where
comparisons are 30%+ of FP operations, this is a significant net gain.

**Arithmetic-heavy workloads** (matrix multiply, convolution, DSP inner
loops): Roughly neutral. The conditional negate at entry/exit of each
operation replaces, rather than adds to, the sign-handling logic that
sign-magnitude already requires.

**Absolute-value-heavy workloads** (distance calculations, magnitude
computations): Sign-magnitude may win slightly. `abs` is one instruction
vs. a conditional negate.

The net effect on any given workload is empirically measurable and should
be benchmarked on the target architecture before choosing a representation.

## References

- DEC PDP-10 floating-point specification:
  http://www.inwap.com/pdp10/hbaker/pdp-10/Floating-Point.html
- Quadibloc, "Floating-Point Formats" (survey of historical FP
  representations including PDP-10, Xerox Sigma, Control Data):
  http://www.quadibloc.com/comp/cp0201.htm
- comp.dsp discussion of two's complement floats, with contributions
  from rbj and Glen Herrmannsfeldt (April 2009):
  https://www.dsprelated.com/showthread/comp.dsp/111932-1.php
- rbj, Stack Exchange question on historical use of two's complement
  floating-point:
  https://retrocomputing.stackexchange.com/questions/2664/
- Boldo, S. and Daumas, M. "Properties of Two's Complement Floating
  Point Notations." Int J Softw Tools Technol Transfer (2003).
  https://hal.science/hal-00157268v1/document
- Goldberg, D. "What Every Computer Scientist Should Know About
  Floating-Point Arithmetic" (1991)
- Herf, M. "Radix Tricks" — describes the integer-comparison trick for
  IEEE 754 floats (which two's complement makes unnecessary)
- Private communication: Robert Bristow-Johnson (rbj), specification of
  two's complement floating-point format, 2025

## Summary

Two's complement floating-point representation is a historically proven
technique that the PDP-10, Xerox Sigma, and Control Data machines used
in production hardware. rbj's proposal modernizes this approach with a
hidden bit, denormal support, and a minimal special-value allocation:
1 NaN (the trap value), 2 infinities (the integer extremes), and no
negative zero — 3 non-finite bit patterns total vs. IEEE 754's 2^(M+1).

In OPINE, the two's complement representation is a policy that carries
its own special-value semantics. It composes freely with format,
rounding, denormal, and error-handling policies. It does not compose
independently with special-value policies — the representation
determines the encoding.

The primary implementation impact is on `pack` and `unpack` (conditional
negate and special-value checks) and on comparison (becomes signed
integer comparison). `FormatDescriptor` and `UnpackedFloat` are
unchanged. All arithmetic operates on the unpacked representation,
which is identical for both sign-magnitude and two's complement.
