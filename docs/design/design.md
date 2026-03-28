# OPINE Design: The Six Axes

## Goal

A correct, minimal architecture that can express every numeric format
that has ever existed: IEEE 754 binary and decimal, historical formats
(IBM HFP, PDP-10, CDC 6600, Xerox Sigma, Burroughs), BCD packed and
zoned decimal, calculator formats (HP, TI, Casio), ML accelerator
formats (bfloat16, FP8, TensorFloat-32), block/microscaling formats
(MXFP, NF4), integer-ordered two's complement, Microsoft Binary
Format, Motorola FFP, posits, string representations, fixed-point,
and integers — in scalar, vector, and matrix arrangements — within
a single compositional framework.

See `problems.md` for the failures of the previous five-axis design.
See `menagerie.md` for an encyclopedic catalog of formats.
See `decision-tree.md` for a decision tree that identifies any format.
See `catalog.md` for the Number decomposition of every known format.

## Core Insight

Three structural concerns, not two.

**Number** — what one numeric value is.  Pure semantics.  A
floating-point value is a composite Number: it contains a significand
(a number) and an exponent (a number), assembled by a composition
rule.  The significand and exponent can have different radixes,
different digit widths, and different sign methods.

**Box** — how many Numbers, in what arrangement, stored how.
Scalar, vector, matrix, tensor.  Box carries both the logical
dimensions and the physical memory arrangement (stride, ordering,
SIMD lane mapping).  These are inseparable: you cannot describe a
stride without knowing the dimensions.

**Layout** — how one Number maps to bits.  Field positions, byte
order, packing codecs.  This is bit-level geometry for a single
value, paired with Number.

Number and Layout are paired at the element level.  Box is
independent of both — any Number in any Layout can fill any Box.

## The Six Axes

```
Type<Number, Box, Layout, Rounding, Exceptions, Platform>
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

`radix` is the arithmetic radix — the base of the number system.
It determines what "a digit" means, what arithmetic operations look
like, and what complement operations do.  It can be negative
(negabinary, base -2).

`digit_width` is the physical width of one digit cell in bits: 1 for
binary, 4 for BCD, 8 for zoned decimal or character-encoded digits.

`digit_count` is how many digits the number has.  Usually a
compile-time constant.  Variable for string formats and
bounded-variable formats.

`sign_method` is how the sign is encoded.  Five methods cover every
known format:

- **Explicit**: A dedicated field encodes the sign independently from
  the magnitude.  IEEE 754 uses a 1-bit field (0/1).  COMP-3 uses a
  4-bit nibble (0xC/0xD/0xF).  HP calculators use a nibble (0/9).
  String formats use a character (+/-).  The specific code values are
  a secondary parameter; the method is the same.

- **RadixComplement**: Negate by complementing relative to the radix
  (subtract each digit from radix-1, add 1).  When radix=2, this is
  two's complement.  When radix=10, this is ten's complement.  Same
  method, different radix — no per-radix enum values needed.

- **DiminishedRadixComplement**: Like RadixComplement but without the
  +1.  When radix=2, this is one's complement.  When radix=10, this
  is nine's complement.  Negative zero exists.

- **Inherent**: The number system itself encodes sign without any
  explicit mechanism.  Balanced ternary (digits are {-1, 0, +1}) and
  negabinary (base -2) both represent positive and negative values
  natively.

- **Unsigned**: No sign.

A **composite Number** contains sub-Numbers assembled by a
composition rule:

**FloatingPoint** composition:

```
significand:     Number (primitive)
exponent:        Number (primitive)
exponent_base:   int (2, 8, 10, 16)
value_sign:      sign_method
implicit_digit:  bool
special_values:  { nan, inf, denormal_mode, negative_zero }
```

**FixedPoint** composition:

```
significand:     Number (primitive)
radix_position:  int | External
```

**SharedExponent** composition (block/microscaling formats):

```
significand:     Number (primitive)
count:           int (number of significands sharing the exponent)
exponent:        Number (primitive)
exponent_base:   int
```

The count is part of Number — not Box — because the coupling is
semantic.  The shared exponent constrains all significands to the
same magnitude range.  They are not independent.  Test: can you
remove one element without changing the meaning of the others?  If
no, the coupling is Number.  If yes, the multiplicity is Box.

**Codebook** composition (lookup-table formats):

```
table:           array of values (each a Number or literal constant)
index_width:     int (bits per index)
```

The "number" is an index into a fixed table of precomputed values.
NF4 (NormalFloat4, used in QLoRA) maps 4-bit indices to 16 values
chosen to approximate a normal distribution.  The bit pattern has
no sign/exponent/significand structure — it is purely an address.

#### Why the sub-Numbers matter

The significand and exponent are separate Numbers because real
formats give them different properties:

- **TI-89**: significand is radix=10, digit_width=4, 14 digits.
  Exponent is radix=2, digit_width=1, 15 digits.  Different radix
  AND different digit_width.

- **HP calculator**: significand sign is Explicit (nibble, 0/9).
  Exponent sign is RadixComplement (radix=10, making it thousand's
  complement on a 3-digit field).  Different sign methods.

- **IEEE binary32**: significand and exponent happen to share
  radix=2, digit_width=1.  But the exponent's sign is Biased(127)
  while the significand is Unsigned (the value sign is at the
  composite level).

A flat design that assigns one digit_width and one sign_method to
the whole format cannot express these.  The compositional design
handles them naturally.

### Axis 2: Box

How many Numbers, in what arrangement, stored how.  Box has two
faces: logical (dimensions) and physical (memory arrangement).

**Logical dimensions**:

```
Box<>          scalar — one Number
Box<8>         vector — 8 Numbers
Box<4,4>       square matrix — 16 Numbers
Box<4,4,4>     cube — 64 Numbers
Box<M,N>       rectangular matrix
```

Box is orthogonal to Number.  `float32 × Box<8>` is 8 float32s.
`COMP3<5> × Box<1000>` is 1000 packed decimals.
`MXFPBlock<16> × Box<4>` is 4 independent MXFP blocks (each block
is internally coupled via SharedExponent; the 4 blocks are
independent — that independence is what makes it Box, not Number).

In practice, most Boxes are `Box<>` (scalar), `Box<N>` (vector), or
`Box<N,N>` (square matrix).  Cubes and higher are rare.

**Physical arrangement** (how the dimensions map to memory):

```
element_stride:    int | Packed
memory_order:      RowMajor | ColumnMajor | Blocked(tile_size)
simd_mapping:      Contiguous | Interleaved | SWAR(word_width)
alignment:         int (byte alignment requirement)
padding:           int (bytes between rows/blocks)
```

The physical properties are inseparable from the dimensions.  You
cannot say "row-major" about a scalar.  Stride is meaningless without
knowing the shape.  Collection-level geometry belongs with the
collection.

`Box<>` (scalar) has no physical properties — it's a single element.
`Box<8>` has a stride and SIMD mapping.  `Box<4,4>` adds memory
order.  Physical complexity scales with logical dimensionality.

**What Box determines for operations:**

Box is what Platform specializations match on for SIMD width.
`<AVX2, Add, float32, Box<8>>` matches VADDPS.
`<Generic, Add, float32, Box<8>>` matches a loop of 8 scalar adds.

Scalar operations produce one result.  Vector operations produce N
independent results.  Matrix operations may involve cross-element
interaction (matrix multiply), defined by the operation, not by Box.

### Axis 3: Layout

How one Number maps to bits.  Period.

Layout is bit-level geometry for a single value.  It pairs with
Number at the element level only.

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
sub_layouts:        { name → Layout }
packing:            Direct | DPD | BID | ImplicitDigit | ...
field_boundaries:   Fixed | Dynamic(parsing_rule)
```

`packing` handles storage codecs.  DPD (3 decimal digits in 10
bits) is a Layout concern — the Number is "10 decimal digits"; the
Layout says how to compress them.  The implicit leading digit is
also a Layout concern — the Number has 24 digits, the Layout stores
23.  BID (Binary Integer Decimal) is a Layout concern — the Number
is "16 decimal digits" (radix=10), the Layout packs them as a
single binary integer.  Unpack converts binary to decimal.

`field_boundaries` is Fixed for most formats and Dynamic for posits,
tapered FP, and Type I Unums.  A dynamic layout includes a parsing
rule (regime scan for posits, size-field extraction for Type I
Unums).

**Layout principle**: Layout is pure geometry for one value.  If
you're labeling something with semantic meaning ("this is the
exponent"), that's Number's job.  If you're describing how a
collection of values is arranged in memory, that's Box's job.
Layout says where one Number's cells go in bits.

#### Independence of Number and Layout

Same Number, different Layout:
- IEEE binary32 and FFP have the same semantic structure (binary,
  sign-magnitude, implicit leading digit, similar precision) but
  different field positions.  IEEE: `[S][E][M]`.  FFP: `[M][S][E]`.

- A float32 stored big-endian and the same float32 stored
  little-endian.  Same Number, different byte ordering.

- BCD digits stored as nibbles vs. DPD-compressed.  Same Number
  (decimal digits), different packing.

Same Layout, different Number:
- IEEE binary32 and integer-ordered two's complement float32 use the
  same `[S1][E8][M23]` layout at the same bit offsets.  Different
  Number (different sign method, different special values).

### Axis 4: Rounding

How precision is managed when a result cannot be exactly represented.

```
mode:   TowardZero | ToNearestTiesToEven | ToNearestTiesAway |
        TowardPositive | TowardNegative | ToOdd
```

Rounding mode is independent of Number, Box, Layout, and Platform.

For posits, the rounding **precision** (how many digits to round to)
is value-dependent — derived from the output exponent and regime
length at runtime.  The rounding **mode** is still a type-level
constant.  This creates a data dependency from Compute to Round that
does not exist for fixed-field formats: the output exponent (computed
during the Compute step) determines the output regime length, which
determines how many fraction bits to round to.

Decimal rounding (rounding to a decimal digit boundary) uses the
same modes as binary rounding.  The difference is what constitutes a
"digit boundary," which is determined by the Number's radix and
digit_width.

### Axis 5: Exceptions

What happens when an operation encounters an exceptional condition.

```
Silent:         Best-effort result, no side effects
StatusFlags:    Queryable flags (FE_INVALID, FE_DIVBYZERO, ...)
ReturnStatus:   Every operation returns {result, status}
Trap:           Call a handler
```

Independent of Number, Box, Layout, Rounding, and Platform.

### Axis 6: Platform

The target hardware.  Platform has two roles: it provides structural
parameters that affect algorithmic decisions, and it serves as an
identity for template specialization.

**Structural parameters** — properties the code generator needs for
algorithmic decisions above the individual-operation level:

```
type_policy:            ExactWidth | LeastWidth | Fastest
machine_word_bits:      int
register_file_depth:    int
```

`machine_word_bits` determines SWAR feasibility and multi-word
strategies.  `register_file_depth` determines BLAS tile sizes and
spill thresholds.

**Hardware capabilities** — expressed through the existence of
template specializations, not through boolean flags.  All operations
have generic software implementations.  Platforms provide
specializations that the compiler prefers via partial template
matching:

```cpp
// Generic: any platform, any radix, any width. Slow but correct.
template <typename Platform, typename Op,
          typename In, typename Out, typename Box>
struct Implementation;

// MOS6502 + Add + radix=10 → hardware BCD mode (SED; ADC)
// CortexM4 + Multiply + 32-bit binary → hardware MUL
// AVX512 + FMA + float32 + Box<16> → VFMADD231PS (16-wide)
// zArch + Multiply + radix=10, digit_width=4 → decimal MP
// CUDA_SM90 + MatMul + FP8 + Box<16,16> → tensor core
```

The template matching keys on Platform identity, Operation, the
input/output Number types (which carry radix, digit_width, and
width), and the Box (which carries the SIMD/vector width).  New
hardware features are new specializations — no struct modification
needed.  If no specialization exists for a given combination, the
generic implementation provides correct (but slow) behavior.

The generic implementations also serve as the test oracle: they
define correct behavior by construction.  Specializations are
optimizations that must produce identical results.

## ComputeFormat

ComputeFormat is NOT an axis.  It is a parameter of operations, not
of values.  It describes the working precision at which arithmetic
executes.

ComputeFormat works in **digits**, not bits:

```
exp_digits:         int
significand_digits: int
guard_digits:       int
```

The physical width of intermediate state is derived:
`significand_digits * digit_width`.  For binary (digit_width=1),
this equals `mant_bits` as in the current design.  For BCD
(digit_width=4), it equals `num_digits * 4`.

`product_digits = 2 * significand_digits` is true in any radix.
Multiplying two N-digit numbers produces a 2N-digit result
regardless of the base.

The storage digit_width and the compute digit_width do not have to
match.  A COMP-3 value (digit_width=4) could be unpacked into a
binary integer (digit_width=1) for computation, then packed back.
This is a Platform decision, not an axis decision.

For SharedExponent (MXFP) composition, the shared exponent affects
all significands' compute precision simultaneously.  The
intermediate accumulator is typically wider than any individual
element (e.g., FP8 elements with FP32 accumulation).

## The Operation Pipeline

Every arithmetic operation follows:

```
unpack → compute → round → pack
```

**Unpack** (governed by Layout + Number): Extracts sign, exponent,
and significand from the storage representation.  Produces a
canonical unpacked form: sign + integer exponent + significand
digits.  The implementation depends on the Layout (bit masking for
IEEE, nibble extraction for BCD, regime scanning for posits,
character parsing for strings).  The result is the same structure
regardless of source format.

**Compute** (governed by Number's radix + ComputeFormat): Multiply
significands, add exponents.  The compute step does not know or care
about the storage format.  It sees sign + exponent + significand
digits.  Binary Numbers use binary arithmetic.  Decimal Numbers use
decimal arithmetic.

**Round** (governed by Rounding + output Number): Reduce the
significand to the output precision.  For fixed-field formats, the
output precision is a compile-time constant.  For posits, it is
computed from the output exponent at runtime: the output exponent
determines the regime length, which determines how many bits remain
for the exponent supplement and fraction.  This means Pack must
collaborate with Round — the packing structure determines the
rounding target.

**Pack** (governed by Layout + Number): Converts sign + exponent +
significand digits back into the storage representation.  The
inverse of unpack.

For Box operations, the pipeline runs independently per element
(or per SIMD lane).  Platform specializations may fuse multiple
elements into a single wide pipeline (VADDPS processes 8 pipelines
simultaneously).

See `catalog.md` for the full format catalog showing the Number
decomposition for every known format.

## Predefined Bundles

Most users should never specify sub-parameters directly.  They pick
a bundle:

```cpp
// IEEE 754 binary32: the common case
using float32 = Type<IEEE754Number<8, 23>,
                     Box<>,
                     IEEE754Layout<8, 23>,
                     rounding::ToNearestTiesToEven>;

// Integer-ordered two's complement: same Layout, different Number
using io32 = Type<IntegerOrderedNumber<8, 23>,
                  Box<>,
                  IEEE754Layout<8, 23>,
                  rounding::ToNearestTiesToEven>;

// FFP: same-ish Number, different Layout
using ffp = Type<FFPNumber,
                 Box<>,
                 FFPLayout,
                 rounding::TowardZero>;

// COMP-3: decimal, fixed-point, BCD
using comp3_5 = Type<Comp3Number<5>,
                     Box<>,
                     Comp3Layout<5>>;

// Posit: variable fields
using posit32 = Type<PositNumber<32, 2>,
                     Box<>,
                     PositLayout<32, 2>>;

// AVX vector of 8 float32s
using float32x8 = Type<IEEE754Number<8, 23>,
                       Box<8, Contiguous, 32>,
                       IEEE754Layout<8, 23>,
                       rounding::ToNearestTiesToEven,
                       exceptions::Silent,
                       platform::AVX2>;

// SWAR: 4 FP8 values in a 32-bit word
using fp8x4 = Type<FP8Number<4, 3>,
                   Box<4, SWAR<32>>,
                   FP8Layout<4, 3>,
                   rounding::TowardZero>;

// One MXFP4 block (16 coupled elements — the block IS the Number)
using mxfp4_block = Type<MXFPBlock<FP4_E2M1, 16>,
                         Box<>,
                         MXFPLayout<16>>;

// 4 independent MXFP4 blocks
using mxfp4x4 = Type<MXFPBlock<FP4_E2M1, 16>,
                     Box<4>,
                     MXFPLayout<16>>;
```

## Relationship to Current Code

The existing `Format` template is a specific composite Layout for
digit_width=1 formats with fixed field boundaries.  The existing
encoding bundles (IEEE754, IntegerOrdered, PDP10, CDC6600, etc.)
are specific composite Numbers.  The existing Rounding, Exceptions,
and Platform axes are unchanged.  Box is new.

The existing code covers the radix=2, digit_width=1, fixed-field,
scalar region of the design space.  The generalized architecture
extends to cover the full space without disturbing that region.

## What This Design Does Not Cover

**Arithmetic implementation.**  No operations are implemented yet.
Those will be designed once the type framework is in place.

**BLAS kernel tiling, register allocation, or loop structure.**

**Assembly-level optimizations for specific platforms.**

**Known representational gaps:**

*Layout gaps:*
- Zoned decimal embedded sign — the last byte's zone nibble serves
  double duty as both sign and digit.  Not a clean dedicated field.
- DPD combination field — 5 bits simultaneously encode exponent MSBs
  and the leading significand digit.  Multi-purpose bits.
- String delimiters — decimal point (.) and exponent indicator (e/E)
  are structural markers that are neither digits, sign, nor exponent.
- BRLESC tag fields — a 3-bit type discriminant selects which Number
  interpretation applies.  Union/tagged-union concept.

*Number gaps:*
- HP exponent thousand's complement — radix complement sign on the
  exponent is expressible (RadixComplement, radix=10), but the
  current Biased() is the only exponent sign option shown in most
  catalog entries.  Needs explicit support for non-biased exponent
  sign methods.
- Negative radix arithmetic — negabinary (base -2) is in the type
  system, but no arithmetic pipeline exists for negative radix.

*Structural gaps:*
- Variable-length storage — strings and Burroughs Medium Systems (up
  to 100 digits) have no fixed total size.
- Multiple dynamic-field mechanisms — posit regime scan, Type I Unum
  size fields, and tapered FP length prefix are three different
  solutions to dynamic field allocation.  The design acknowledges
  all three but a unified treatment is aspirational.
