# Problems with the Five-Axis Design

## The claim

The design document states: "Every format that has ever existed or will
ever exist is a point in this space." This is not currently true. The
five-axis architecture handles binary floating-point with
sign/exponent/mantissa decomposition extremely well, but it silently
assumes binary radix throughout, and it cannot express several important
classes of numeric format.

## What works

The current axes cover all binary floating-point formats with fixed-width
fields:

- IEEE 754 binary (all widths)
- ML accelerator formats (bfloat16, FP8 E5M2, E4M3, E4M3FNUZ)
- Historical binary formats (PDP-10, CDC 6600)
- rbj's two's complement representation
- Motorola FFP (Amiga Fast Floating Point)
- Microsoft Binary Format (MBF)
- "Fast approximate" configurations (no NaN, no Inf, flush denormals)
- Any format with fixed-width sign, exponent, and mantissa bit fields

The Encoding axis handles the diversity within this family well: sign-
magnitude vs two's complement vs one's complement, implicit bit or not,
arbitrary biases, and a rich set of special-value and denormal options
with compile-time constraint enforcement.

## What doesn't work

### IBM Hexadecimal Floating Point (System/360)

Bit geometry: `[S1][E7][M24]` — Format handles this.

Encoding: sign-magnitude, no implicit bit, bias 64, no NaN, no Inf, no
denormals — every Encoding sub-parameter has a valid value. An IBM360
encoding bundle would satisfy `ValidEncoding`.

The problem: the value formula is `(-1)^S × 0.M × 16^(E-64)`. The
exponent base is 16, not 2. One exponent step shifts the mantissa by
4 bit positions, not 1. Every operation that aligns mantissas, compares
exponents, or normalizes results would silently produce wrong answers
because base-2 is assumed throughout and never parameterized.

The Xerox Sigma computers had the same base-16 exponent, combined with
two's complement sign encoding. Same problem.

There is no `exponent_base` parameter anywhere in the design.

### COMP-3 / Packed Decimal

COMP-3 stores decimal digits as BCD nibbles: each 4-bit nibble encodes
one decimal digit (0-9), two digits per byte. The last nibble is a sign
indicator (0xC = positive, 0xD = negative, 0xF = unsigned). A PIC S9(5)
COMP-3 field is 3 bytes: 5 digit nibbles plus 1 sign nibble.

Format cannot express this:
- `ExpBits >= 1` is enforced, but COMP-3 has no exponent.
- `SignBits <= 1` is enforced, but the sign is a 4-bit nibble.
- The "mantissa" is BCD-encoded, not a binary integer.

Encoding cannot express this:
- `sign_encoding` has no BCD sign nibble option.
- `has_implicit_bit` is a binary concept.
- All exponent-related parameters are inapplicable.

ComputeFormat is wrong:
- `product_bits = 2 * mant_bits` assumes binary multiplication.
  BCD multiplication of N decimal digits produces 2N decimal digits,
  which is 2N nibbles = 8N bits in packed BCD.

### Zoned Decimal

Zoned decimal stores one decimal digit per byte. Each byte has a zone
nibble (typically 0xF) and a digit nibble. The sign is encoded in the
zone nibble of the last byte (0xC = positive, 0xD = negative). A
PIC S9(5) DISPLAY field is 5 bytes: 5 digit bytes with the sign
overpunched in the last byte's zone.

Same problems as COMP-3, plus the digit encoding is byte-oriented
rather than nibble-oriented.

### IEEE 754 Decimal Floating Point

IEEE 754-2008 defines decimal floating-point formats (Decimal32,
Decimal64, Decimal128) with two encodings:

**DPD (Densely Packed Decimal):** Three decimal digits are encoded in
10 bits. The combination field (5 bits) interleaves exponent MSBs with
the leading significand digit. There is no clean separation into
independent exponent and mantissa bit fields.

**BID (Binary Integer Decimal):** The significand is stored as a binary
integer, but its value represents a decimal number. The exponent base
is 10. Pack/unpack and arithmetic need to know the value is decimal.

Both variants have the wrong radix for the current design.

### Posits (Type III Unums)

Posits have variable-width fields. In a posit<n, es>:

- Sign: 1 bit (fixed, always the MSB)
- Regime: variable length (1 to n-1 bits). A unary-coded run of
  identical bits terminated by the opposite bit (or end of word).
  Encodes a coarse scaling factor.
- Exponent: up to `es` bits (or fewer if the regime consumed them).
  Encodes a fine scaling factor.
- Fraction: remaining bits (could be zero). Binary significand with
  implicit leading 1.

The regime length depends on the value. A posit near 1.0 has a short
regime and many fraction bits. A posit near maxpos has a long regime
and few (or zero) fraction bits. The field boundaries cannot be
expressed as compile-time constants.

Format cannot express this: `ExpBits`, `ExpOffset`, `MantBits`,
`MantOffset` must be compile-time constants, but for posits they vary
per value.

Rounding has a complication: the number of output fraction bits depends
on the result's magnitude (because the output regime length depends on
the output exponent). Rounding precision is value-dependent for posits,
whereas it is a type-level constant for all fixed-field formats.

Note: posit arithmetic is otherwise identical to binary floating-point.
The significand is a binary integer. The computation pipeline (multiply
significands, add exponents) is the same as IEEE. The regime is purely
a storage encoding — once unpacked, a posit is just sign + exponent +
binary significand.

### String Formats

A decimal number represented as a character string: optional sign
character, digit characters, optional decimal point, optional exponent
(e.g., "-1.23e4").

The "significand" is an array of byte-sized character codes. The sign is
a character, not a bit. The exponent (if present) is itself a character
string. Storage is variable-length.

None of the current axes can express character-encoded digits or
character-encoded signs.

## Root causes

All of these failures trace to two unstated assumptions:

**1. The significand is a binary integer.**

This is never declared. It is woven into Format (contiguous bit field),
Encoding (`has_implicit_bit`), ComputeFormat (`product_bits = 2 *
mant_bits` in binary), and UnpackedFloat (`mantissa_type` is
`uint_t<N>`).

**2. The exponent base is 2.**

Also never declared. Every exponent comparison, mantissa alignment, and
normalization step assumes one exponent step equals one bit position.
IBM HFP (base 16) means one exponent step equals four bit positions.

A third issue specific to posits:

**3. Field boundaries are compile-time constants.**

Format requires `ExpBits`, `MantBits`, etc. to be template parameters.
Posits have value-dependent field boundaries.

## A key observation: digit width

The significand, across all known formats, can be described as an array
of N fields, each `digit_width` bits wide:

- `digit_width = 1`: Binary digits (bits). Covers IEEE, FFP, MBF,
  IBM HFP, PDP-10, CDC 6600, rbj, all ML formats, and posit fractions.
- `digit_width = 4`: BCD digits (nibbles). Covers COMP-3, packed
  decimal.
- `digit_width = 8`: Byte-sized digits. Covers zoned decimal (zone +
  digit per byte), character/string formats (ASCII digit per byte).

This is a single parameter, not a taxonomy of format families. And it
cleanly separates two concerns:

- The digit width is a **structural** property — the cell size of the
  storage container. It belongs in whatever axis describes physical
  geometry.
- The digit **interpretation** (binary 0-1, BCD 0-9, ASCII character
  code) is a **semantic** property. It belongs in whatever axis
  describes meaning.

The sign also fits this model. In IEEE (digit_width=1), the sign is one
1-bit cell. In COMP-3 (digit_width=4), the sign nibble is one 4-bit
cell. In string formats (digit_width=8), the sign character is one
8-bit cell. The sign is always one cell of digit_width.

For posits, digit_width=1 (the fraction is binary). The variable-width
regime is an exponent encoding strategy, not a significand structure
issue — it belongs in the semantic axis, not the structural one.

## Reconsidering the axes

The five-axis count is correct. The decomposition into five independent
concerns — physical structure, semantic interpretation, rounding, error
handling, and hardware capabilities — is sound. What needs to change is
the content of the first two axes.

### Axis 1: the physical container

Currently called "Format." Specifies bit field positions for three named
fields (sign, exponent, mantissa).

The problem: naming the fields is an act of interpretation. Knowing that
some bits are "exponent" and others are "mantissa" is semantic, not
geometric. And it forces fixed field boundaries, which excludes posits.

What it should specify:
- Total storage size (in cells)
- Cell width (`digit_width`: 1, 4, or 8 bits)
- Ordering convention (MSB-first, LSB-first)

This is thinner than the current Format. The field decomposition —
which cells are sign, which are exponent, which are significand — moves
to axis 2. For fixed-field formats, axis 2 can expose those boundaries
as compile-time constants (enabling the same shift-and-mask optimizations
the current design has). For posits, axis 2 determines them at runtime.

### Axis 2: what the cells mean

Currently called "Encoding." Specifies sign convention, implicit bit,
exponent bias, and special values.

What it should additionally specify:

- **Digit interpretation**: What values are valid in each digit_width
  cell? Binary (0 or 1), BCD (0-9), ASCII ('0'-'9'), zone+digit, etc.
  Constrained by digit_width: BCD requires digit_width=4, ASCII
  requires digit_width=8, binary requires digit_width=1.

- **Exponent base**: 2, 8, 10, or 16. Currently implicit (always 2).
  Determines how many digit positions one exponent step represents.
  IBM HFP needs 16. IEEE decimal needs 10.

- **Scaling model**: How is the significand scaled?
  - Fixed-width exponent field at known bit positions (IEEE, FFP, MBF,
    IBM HFP). Field positions are compile-time constants.
  - Regime-based (posits): unary-coded regime + binary exponent
    supplement. Field positions are value-dependent.
  - Fixed-point: implicit radix point at a position determined by
    external metadata (COMP-3, zoned decimal). No exponent field.
  - None: integer.

- **Sign encoding** (expanded): SignMagnitude, TwosComplement,
  OnesComplement, BCDSignNibble, ZoneOverpunch, SignCharacter, Unsigned.

- **Field decomposition**: Which cells are sign, which are exponent,
  which are significand. For fixed-field formats, this is compile-time
  constants (equivalent to the current Format's ExpBits/ExpOffset/
  MantBits/MantOffset). For posits, this is a runtime decoding rule.
  For fixed-point, there is no exponent field.

The existing sub-parameters remain:
- `has_implicit_bit` (generalized: `has_implicit_digit`)
- `exponent_bias`
- `negative_zero`
- `nan_encoding`
- `inf_encoding`
- `denormal_mode`

The existing `ValidEncoding` constraints remain and are extended:
- `digit_interpretation = BCD` requires `sign_encoding ∈
  {BCDSignNibble, Unsigned}`
- `digit_interpretation = ASCII` requires `sign_encoding ∈
  {SignCharacter, Unsigned}`
- `scaling_model = FixedPoint` requires no exponent field
- `scaling_model = Regime` requires `digit_interpretation = Binary`
- `nan_encoding = ReservedExponent` requires an exponent field to exist
- All existing constraints (TwosComplement → DoesNotExist, etc.)
  continue to hold

### Axes 3-5: unchanged in structure

**Rounding** (axis 3): The rounding mode (toward zero, ties-to-even,
etc.) is independent of all other axes. Rounding should be extended
with decimal rounding awareness — rounding to a decimal digit boundary
rather than a binary bit boundary — but the axis structure is the same.

Note: for posits, the rounding **precision** (how many digits to round
to) is value-dependent, derived from the output exponent and regime
length. The rounding **mode** is still a type-level constant. This
means the pipeline has a dependency from "compute output exponent" to
"determine rounding precision" that does not exist for fixed-field
formats, but it does not change the axis decomposition.

**Exceptions** (axis 4): Unchanged. Error handling is independent of
digit encoding, radix, and scaling model.

**Platform** (axis 5): Mostly unchanged. May benefit from additional
capability flags:
- `has_bcd_arithmetic` (x86 DAA/DAS, S/360 AP/SP)
- `has_regime_hardware` (hypothetical posit hardware, some RISC-V
  extensions)

### ComputeFormat

ComputeFormat should work in **digits** rather than bits. The current
`product_bits = 2 * mant_bits` is a special case of `product_digits =
2 * significand_digits` where digit_width = 1.

The physical width of the intermediate state is
`product_digits * digit_width`. For binary (digit_width=1), this equals
`2 * mant_bits` (unchanged). For BCD (digit_width=4), it equals
`2 * num_digits * 4`.

The storage digit_width and the compute digit_width do not have to
match. A COMP-3 value (digit_width=4) could be unpacked into a binary
integer (digit_width=1) for computation, then packed back into BCD.
Or it could be computed in BCD directly on hardware with BCD arithmetic
support. This is a Platform × digit_encoding decision, not an axis
decision.

### The operation pipeline

The pipeline — unpack → compute → round → pack — is unchanged in
structure. What changes is what each step does:

**Unpack**: Governed by axes 1 and 2. Extracts sign, exponent, and
significand from the storage representation, producing a canonical
unpacked form (sign + integer exponent + significand digits). For
IEEE, this is bit masking and shifting. For COMP-3, this is nibble
extraction. For posits, this is regime scanning. For strings, this is
character parsing. All produce the same unpacked representation.

**Compute**: Governed by the radix and ComputeFormat. Multiply the
significands (binary or decimal integer multiply), add the exponents
(integer add). The compute step does not know or care about the storage
format. It sees sign + exponent + significand digits.

**Round**: Governed by axis 3. Apply the rounding mode to reduce the
significand to the output precision. For fixed-field formats, the
output precision is a compile-time constant. For posits, it is
computed from the output exponent at runtime.

**Pack**: Governed by axes 1 and 2. Converts sign + exponent +
significand digits back into the storage representation. For IEEE,
this is bit shifting and OR-ing. For COMP-3, this is nibble packing.
For posits, this is regime encoding. For strings, this is character
formatting.

## What this means for the existing code

The existing implementation covers radix-2 with binary digit encoding
and fixed-width fields. Under the generalized design, this is:

- Axis 1: digit_width=1, fixed total size
- Axis 2: digit_interpretation=Binary, exponent_base=2,
  scaling=FixedExponent, plus existing sign/special-value parameters

All existing encoding bundles (IEEE754, RbjTwosComplement, PDP10,
CDC6600, E4M3FNUZ, Relaxed, GPUStyle) remain valid. The existing
Format template remains valid for digit_width=1 formats. The existing
ComputeFormat remains valid (it is the digit_width=1 specialization
of the generalized version).

Nothing is deleted. The generalization extends the design to cover
formats that the current axes cannot express, without disturbing the
formats they already handle.

## Formats under the generalized design

| Format | digit_width | digit interp | exp base | scaling | sign |
|---|---|---|---|---|---|
| IEEE binary32 | 1 | Binary | 2 | FixedExponent | SignMagnitude |
| FFP (Amiga) | 1 | Binary | 2 | FixedExponent | SignMagnitude |
| MBF-32 | 1 | Binary | 2 | FixedExponent | SignMagnitude |
| IBM HFP single | 1 | Binary | 16 | FixedExponent | SignMagnitude |
| Xerox Sigma | 1 | Binary | 16 | FixedExponent | TwosComplement |
| PDP-10 | 1 | Binary | 2 | FixedExponent | TwosComplement |
| CDC 6600 | 1 | Binary | 2 | FixedExponent | OnesComplement |
| rbj TC float32 | 1 | Binary | 2 | FixedExponent | TwosComplement |
| FP8 E4M3FNUZ | 1 | Binary | 2 | FixedExponent | SignMagnitude |
| posit<32,2> | 1 | Binary | 2 | Regime | TwosComplement |
| COMP-3 S9(5) | 4 | BCD | — | FixedPoint | BCDSignNibble |
| Zoned S9(5) | 8 | ZonedBCD | — | FixedPoint | ZoneOverpunch |
| Decimal32 DPD | 4* | DPD | 10 | FixedExponent | SignMagnitude |
| Decimal64 BID | 1 | BID | 10 | FixedExponent | SignMagnitude |
| String "-1.23e4" | 8 | ASCII | 10 | FixedExponent | SignCharacter |
| String "123.45" | 8 | ASCII | — | FixedPoint | SignCharacter |
| Q15.16 fixed | 1 | Binary | — | FixedPoint | TwosComplement |

\* DPD encodes 3 decimal digits in 10 bits, so the logical digit width
is 4 (BCD) but the physical packing is denser. DPD can be treated as
compressed BCD at the encoding level.

Every row is a point in the five-axis space. The claim "every format
that has ever existed or will ever exist" becomes defensible.
