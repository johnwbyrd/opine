# OPINE Design: Number, Layout, and the Five Axes

## Goal

A correct, minimal architecture that can express every numeric format
that has ever existed: IEEE 754 binary and decimal, historical formats
(IBM HFP, PDP-10, CDC 6600, Xerox Sigma, Burroughs), BCD packed and
zoned decimal, calculator formats (HP, TI, Casio), ML accelerator
formats (bfloat16, FP8, TensorFloat-32), integer-ordered two's complement,
Microsoft Binary Format, Motorola FFP, posits, string representations,
fixed-point, and integers — within a single compositional framework.

See `problems.md` for the full catalog of formats and the specific
failures of the previous five-axis design.

## Core Insight

A floating-point value is a **composite number**. It contains a
significand (a number) and an exponent (a number), assembled by a
composition rule. The significand and the exponent can have different
radixes, different digit widths, and different sign methods.

The physical arrangement of a number in storage — where the fields go,
what order the digits are in, how they are packed — is a separate
concern from what the number IS.

These two concerns — **Number** (what it is) and **Layout** (where it
goes) — are independent, recursive, and paired at every level of
nesting.

## The Five Axes

```
Float<Number, Layout, Rounding, Exceptions, Platform>
```

### Axis 1: Number

A Number is either primitive or composite.

A **primitive Number** is a sequence of digits:

```
radix:        int     (2, 3, 10, -2, ...)
digit_width:  int     (1, 4, or 8 bits per digit)
digit_count:  int | Variable
sign_method:  Explicit | RadixComplement |
              DiminishedRadixComplement | Inherent | Unsigned
```

`radix` is the base of the number system. It determines what "a digit"
means, what arithmetic operations look like, and what complement
operations do. It can be negative (negabinary, base -2).

`digit_width` is the physical width of one digit cell in bits: 1 for
binary, 4 for BCD, 8 for zoned decimal or character-encoded digits.

`digit_count` is how many digits the number has. Usually a compile-time
constant. Variable for string formats and bounded-variable formats.

`sign_method` is how the sign is encoded. Five methods cover every
known format:

- **Explicit**: A dedicated field encodes the sign independently from
  the magnitude. IEEE 754 uses a 1-bit field (0/1). COMP-3 uses a
  4-bit nibble (0xC/0xD/0xF). HP calculators use a nibble (0/9).
  String formats use a character (+/-). The specific code values are
  a secondary parameter; the method is the same.

- **RadixComplement**: Negate by complementing relative to the radix
  (subtract each digit from radix-1, add 1). When radix=2, this is
  two's complement. When radix=10, this is ten's complement. Same
  method, different radix — no per-radix enum values needed.

- **DiminishedRadixComplement**: Like RadixComplement but without the
  +1. When radix=2, this is one's complement. When radix=10, this is
  nine's complement. Negative zero exists.

- **Inherent**: The number system itself encodes sign without any
  explicit mechanism. Balanced ternary (digits are {-1, 0, +1}) and
  negabinary (base -2) both represent positive and negative values
  natively.

- **Unsigned**: No sign.

A **composite Number** contains sub-Numbers assembled by a composition
rule:

```
composition:     FloatingPoint | FixedPoint | HomogeneousArray(N)
value_sign:      sign_method (for the composite value, not its parts)
```

For FloatingPoint composition:

```
significand:     Number (primitive)
exponent:        Number (primitive)
exponent_base:   int (2, 8, 10, 16)
implicit_digit:  bool
special_values:  { nan, inf, denormal_mode, negative_zero }
```

For FixedPoint composition:

```
significand:     Number (primitive)
radix_position:  int | External
```

For HomogeneousArray composition (SWAR, SIMD vectors):

```
element:         Number (composite or primitive)
count:           int
```

The recursion is shallow in practice: a SIMD vector is an array of
composite Numbers (floats), each containing primitive Numbers
(significand, exponent). Three levels.

#### Why the sub-Numbers matter

The significand and exponent are separate Numbers because real formats
give them different properties:

- **TI-89**: significand is radix=10, digit_width=4, 14 digits.
  Exponent is radix=2, digit_width=1, 15 digits. Different radix AND
  different digit_width.

- **HP calculator**: significand sign is Explicit (nibble, 0/9).
  Exponent sign is RadixComplement (radix=10, making it thousand's
  complement on a 3-digit field). Different sign methods.

- **IEEE binary32**: significand and exponent happen to share radix=2,
  digit_width=1. But the exponent's sign is Biased(127) while the
  significand is Unsigned (the value sign is at the composite level).

A flat design that assigns one digit_width and one sign_method to the
whole format cannot express these. The compositional design handles
them naturally.

### Axis 2: Layout

Layout describes where a Number's parts go in physical storage. It
pairs with Number at every level of the recursion but is independent
of it.

A **primitive Layout** maps a primitive Number's digits to physical
positions:

```
width:              int (bits)
offset:             int (from LSB of containing layout)
digit_order:        MSBFirst | LSBFirst
byte_order:         BigEndian | LittleEndian | PDP
```

A **composite Layout** maps a composite Number's sub-Numbers to
sub-regions:

```
total_bits:         int
sub_layouts:        { name → Layout } or array of Layouts
packing:            Direct | DPD | other codec
field_boundaries:   Fixed | Dynamic(parsing_rule)
```

`packing` handles storage codecs. DPD (3 decimal digits in 10 bits)
is a Layout concern — the Number is "10 decimal digits"; the Layout
says how to pack them. The implicit leading digit is also a Layout
concern — the Number has 24 digits, the Layout stores 23.

`field_boundaries` is Fixed for most formats and Dynamic for posits,
tapered FP, and Type I Unums. A dynamic layout includes a parsing
rule (regime scan for posits, size-field extraction for Type I Unums).

Layout is where the current design's `Format` template lives. The
existing `Format<SignBits, SignOffset, ExpBits, ExpOffset, MantBits,
MantOffset, TotalBits>` is a specific composite Layout for
digit_width=1 formats with fixed field boundaries.

#### Independence of Number and Layout

Same Number, different Layout:
- IEEE binary32 and FFP have the same semantic structure (binary,
  sign-magnitude, implicit leading digit, similar precision) but
  different field positions. IEEE: `[S][E][M]`. FFP: `[M][S][E]`.

- A float32 stored big-endian and the same float32 stored
  little-endian. Same Number, different byte ordering.

- BCD digits stored as nibbles vs. DPD-compressed. Same Number
  (decimal digits), different packing.

Same Layout, different Number:
- IEEE binary32 and integer-ordered two's complement float32 use the same
  `[S1][E8][M23]` layout at the same bit offsets. Different Number
  (different sign method, different special values).

### Axis 3: Rounding

How precision is managed when a result cannot be exactly represented.

```
mode:   TowardZero | ToNearestTiesToEven | ToNearestTiesAway |
        TowardPositive | TowardNegative | ToOdd
```

Rounding mode is independent of Number, Layout, and Platform.

For posits, the rounding **precision** (how many digits to round to)
is value-dependent — derived from the output exponent and regime
length. The rounding **mode** is still a type-level constant.

Decimal rounding (rounding to a decimal digit boundary) uses the same
modes as binary rounding. The difference is what constitutes a
"digit boundary," which is determined by the Number's radix and
digit_width.

### Axis 4: Exceptions

What happens when an operation encounters an exceptional condition.

```
Silent:         Best-effort result, no side effects
StatusFlags:    Queryable flags (FE_INVALID, FE_DIVBYZERO, ...)
ReturnStatus:   Every operation returns {result, status}
Trap:           Call a handler
```

Independent of Number, Layout, Rounding, and Platform.

### Axis 5: Platform

The target hardware. Platform has two roles: it provides structural
parameters that affect algorithmic decisions, and it serves as an
identity for template specialization.

**Structural parameters** — properties the code generator needs
for algorithmic decisions above the individual-operation level:

```
type_policy:            ExactWidth | LeastWidth | Fastest
machine_word_bits:      int (determines SWAR lane count, multi-word strategies)
register_file_depth:    int (determines BLAS tile sizes, spill thresholds)
```

**Hardware capabilities** — expressed through the existence of
template specializations, not through boolean flags. All operations
have generic software implementations. Platforms provide
specializations that the compiler prefers via partial template
matching:

```cpp
// Generic: any platform, any radix, any width. Slow but correct.
template <typename Platform, typename Op, typename In, typename Out>
struct Implementation;

// MOS6502 + Add + radix=10 → hardware BCD mode (SED; ADC)
// CortexM4 + Multiply + 32-bit binary → hardware MUL
// AVX512 + FMA + 32-bit binary → VFMADD231PS
// zArch + Multiply + radix=10, digit_width=4 → decimal MP instruction
// CUDA_SM90 + MatMul + FP8×FP8→FP32 → tensor core
```

The template matching keys on Platform identity, Operation,
and the input/output Number types (which carry radix, digit_width,
and width). New hardware features are new specializations — no
struct modification needed. If no specialization exists for a
given combination, the generic implementation provides correct
(but slow) behavior.

The generic implementations also serve as the test oracle: they
define correct behavior by construction. Specializations are
optimizations that must produce identical results.

## ComputeFormat

ComputeFormat is NOT an axis. It is a parameter of operations, not of
values. It describes the working precision at which arithmetic
executes.

ComputeFormat works in **digits**, not bits:

```
exp_digits:        int
significand_digits: int
guard_digits:      int
```

The physical width of intermediate state is derived:
`significand_digits * digit_width`. For binary (digit_width=1), this
equals `mant_bits` as in the current design. For BCD (digit_width=4),
it equals `num_digits * 4`.

`product_digits = 2 * significand_digits` is true in any radix. This
is because multiplying two N-digit numbers produces a 2N-digit result
regardless of the base.

The storage digit_width and the compute digit_width do not have to
match. A COMP-3 value (digit_width=4) could be unpacked into a binary
integer (digit_width=1) for computation, then packed back. This is a
Platform decision, not an axis decision.

## The Operation Pipeline

Every arithmetic operation follows:

```
unpack → compute → round → pack
```

**Unpack** (governed by Layout + Number): Extracts sign, exponent, and
significand from the storage representation. Produces a canonical
unpacked form: sign + integer exponent + significand digits. The
implementation depends on the Layout (bit masking for IEEE, nibble
extraction for BCD, regime scanning for posits, character parsing for
strings). The result is the same structure regardless of source format.

**Compute** (governed by Number's radix + ComputeFormat): Multiply
significands, add exponents. The compute step does not know or care
about the storage format. It sees sign + exponent + significand digits.
Binary Numbers use binary arithmetic. Decimal Numbers use decimal
arithmetic.

**Round** (governed by Rounding + output Number): Reduce the
significand to the output precision. For fixed-field formats, the
output precision is a compile-time constant. For posits, it is
computed from the output exponent at runtime.

**Pack** (governed by Layout + Number): Converts sign + exponent +
significand digits back into the storage representation. The inverse
of unpack.

## Format Catalog

Every format is a (Number, Layout) pair. The catalog below shows the
Number decomposition for each known format.

### Binary floating-point with fixed fields

| Format | Sig radix | Sig dw | Sig digits | Exp radix | Exp dw | Exp digits | Exp base | Value sign | Sig sign | Exp sign |
|---|---|---|---|---|---|---|---|---|---|---|
| IEEE binary32 | 2 | 1 | 24* | 2 | 1 | 8 | 2 | Explicit | Unsigned | Biased(127) |
| IEEE binary64 | 2 | 1 | 53* | 2 | 1 | 11 | 2 | Explicit | Unsigned | Biased(1023) |
| x87 extended80 | 2 | 1 | 64 | 2 | 1 | 15 | 2 | Explicit | Unsigned | Biased(16383) |
| bfloat16 | 2 | 1 | 8* | 2 | 1 | 8 | 2 | Explicit | Unsigned | Biased(127) |
| FP8 E5M2 | 2 | 1 | 3* | 2 | 1 | 5 | 2 | Explicit | Unsigned | Biased(15) |
| FP8 E4M3FNUZ | 2 | 1 | 4* | 2 | 1 | 4 | 2 | Explicit | Unsigned | Biased(8) |
| FFP (Amiga) | 2 | 1 | 24* | 2 | 1 | 7 | 2 | Explicit | Unsigned | Biased(64) |
| MBF-32 | 2 | 1 | 24* | 2 | 1 | 8 | 2 | Explicit | Unsigned | Biased(129) |
| MBF-40 | 2 | 1 | 32* | 2 | 1 | 8 | 2 | Explicit | Unsigned | Biased(129) |
| IntegerOrdered float32 | 2 | 1 | 24* | 2 | 1 | 8 | 2 | RC(2) | Unsigned | Biased(128) |
| PDP-10 | 2 | 1 | 27 | 2 | 1 | 8 | 2 | RC(2) | Unsigned | Biased(128) |
| CDC 6600 | 2 | 1 | 48 | 2 | 1 | 11 | 2 | DRC(2) | Unsigned | Biased(1024) |

\* includes implicit leading digit (Layout stores one fewer)

RC = RadixComplement, DRC = DiminishedRadixComplement

### Non-base-2 exponents

| Format | Sig radix | Sig dw | Sig digits | Exp radix | Exp dw | Exp digits | Exp base | Value sign | Exp sign |
|---|---|---|---|---|---|---|---|---|---|
| IBM HFP single | 2 | 1 | 24 | 2 | 1 | 7 | 16 | Explicit | Biased(64) |
| IBM HFP double | 2 | 1 | 56 | 2 | 1 | 7 | 16 | Explicit | Biased(64) |
| Xerox Sigma | 2 | 1 | 24 | 2 | 1 | 7 | 16 | RC(2) | Biased(64) |
| Burroughs B5500 | 2 | 1 | 39 | 2 | 1 | 6 | 8 | Explicit | Biased(32) |

### BCD and calculator formats

| Format | Sig radix | Sig dw | Sig digits | Exp radix | Exp dw | Exp digits | Exp base | Value sign | Exp sign |
|---|---|---|---|---|---|---|---|---|---|
| COMP-3 S9(5) | 10 | 4 | 5 | — | — | — | — | Explicit | — |
| Zoned S9(5) | 10 | 8 | 5 | — | — | — | — | Explicit | — |
| 10s comp BCD | 10 | 4 | N | — | — | — | — | RC(10) | — |
| 9s comp BCD | 10 | 4 | N | — | — | — | — | DRC(10) | — |
| HP calculator | 10 | 4 | 12 | 10 | 4 | 3 | 10 | Explicit | RC(10) |
| TI-89 | 10 | 4 | 14 | 2 | 1 | 15 | 10 | Explicit | Biased(0x4000) |
| 8087 packed BCD | 10 | 4 | 18 | — | — | — | — | Explicit | — |

Note: TI-89 has radix=10, digit_width=4 for significand but radix=2,
digit_width=1 for exponent. This is only expressible because
significand and exponent are separate Numbers.

### IEEE 754 decimal floating point

| Format | Sig radix | Sig dw | Sig digits | Exp radix | Exp dw | Exp digits | Exp base | Value sign |
|---|---|---|---|---|---|---|---|---|
| Decimal32 DPD | 10 | 4 | 7 | 2 | 1 | 8 | 10 | Explicit |
| Decimal64 DPD | 10 | 4 | 16 | 2 | 1 | 10 | 10 | Explicit |
| Decimal64 BID | 10 | 1** | 16 | 2 | 1 | 10 | 10 | Explicit |

\** BID stores the significand as a binary integer representing a
decimal value. Storage digit_width=1, arithmetic radix=10.

DPD packing is a Layout concern — the Number's digits are BCD
(digit_width=4), the Layout compresses them.

### Variable-width field formats

| Format | Sig radix | Sig dw | Sig digits | Exp base | Value sign | Field boundaries |
|---|---|---|---|---|---|---|
| posit\<32,2\> | 2 | 1 | Variable | 2 | RC(2) | Dynamic (regime scan) |
| posit\<8,2\> | 2 | 1 | Variable | 2 | RC(2) | Dynamic (regime scan) |
| Tapered FP | 2 | 1 | Variable | 2 | Explicit | Dynamic (length prefix) |
| Type I Unum | 2 | 1 | Variable | 2 | Explicit | Dynamic (size fields) |

### String formats

| Format | Sig radix | Sig dw | Sig digits | Exp base | Value sign | Storage |
|---|---|---|---|---|---|---|
| "-1.23e4" | 10 | 8 | Variable | 10 | Explicit | Variable-length |
| "123.45" | 10 | 8 | Variable | — | Explicit | Variable-length |

### Fixed-point and integer

| Format | Sig radix | Sig dw | Sig digits | Value sign |
|---|---|---|---|---|
| Q15.16 | 2 | 1 | 32 | RC(2) |
| int32_t | 2 | 1 | 32 | RC(2) |
| uint8_t | 2 | 1 | 8 | Unsigned |

### Alternative number systems

| Format | Sig radix | Sig dw | Sig digits | Value sign |
|---|---|---|---|---|
| Balanced ternary | 3 | trit | Variable | Inherent |
| Negabinary | -2 | 1 | Variable | Inherent |

## Predefined Bundles

Most users should never specify sub-parameters directly. They pick a
bundle:

```cpp
// IEEE 754 binary32: the common case
using float32 = Float<IEEE754Number<8, 23>,
                      IEEE754Layout<8, 23>,
                      rounding::ToNearestTiesToEven>;

// Integer-ordered two's complement: same Layout, different Number
using io32 = Float<IntegerOrderedNumber<8, 23>,
                    IEEE754Layout<8, 23>,
                    rounding::ToNearestTiesToEven>;

// FFP: same-ish Number, different Layout
using ffp = Float<FFPNumber,
                  FFPLayout,
                  rounding::TowardZero>;

// COMP-3: decimal, fixed-point, BCD
using comp3_5 = Float<Comp3Number<5>,
                      Comp3Layout<5>>;

// Posit: variable fields
using posit32 = Float<PositNumber<32, 2>,
                      PositLayout<32, 2>>;
```

## Relationship to Current Code

The existing `Format` template is a specific composite Layout for
digit_width=1 formats with fixed field boundaries. The existing
encoding bundles (IEEE754, IntegerOrdered, PDP10, CDC6600, etc.)
are specific composite Numbers. The existing Rounding, Exceptions,
and Platform axes are unchanged.

The existing code covers the radix=2, digit_width=1, fixed-field
region of the design space. The generalized architecture extends to
cover the full space without disturbing that region.

## What This Design Does Not Cover

- Implementation of any arithmetic operation. Those will be designed
  once the type framework is in place.

- BLAS kernel tiling, register allocation, or loop structure.

- Assembly-level optimizations for specific platforms.

- MX/microscaling block format (block of values sharing an exponent).

- The specific convenience syntax for ComputeFormat overrides.
