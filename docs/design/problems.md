# Problems with the Five-Axis Design

## The claim

The design document states: "Every format that has ever existed or will
ever exist is a point in this space." This is not currently true.

## Unstated assumptions

The five-axis design silently assumes:

1. **The significand is a binary integer.** Woven into Format (bit
   fields), Encoding (`has_implicit_bit`), ComputeFormat
   (`product_bits = 2 * mant_bits`), and UnpackedFloat
   (`mantissa_type` is `uint_t<N>`). Never declared.

2. **The exponent base is 2.** Every exponent comparison, mantissa
   alignment, and normalization step assumes one exponent step equals
   one bit position. Never parameterized.

3. **Field boundaries are compile-time constants.** Format requires
   `ExpBits`, `MantBits`, etc. to be template parameters.

4. **The sign is a single bit.** `SignBits <= 1` is enforced. Several
   real formats use wider sign fields or have no explicit sign field
   at all.

5. **An exponent field exists.** `ExpBits >= 1` is enforced. Fixed-
   point and integer formats have no exponent.

6. **The format has three named fields.** Format assumes sign, exponent,
   and mantissa as the complete structural decomposition. This is an
   act of interpretation embedded in what is supposed to be pure
   geometry.

## Catalog of formats the current design cannot express

### Non-base-2 exponents

**IBM Hexadecimal Floating Point (System/360, 1964).** `[S1][E7][M24]`,
32 bits. Value = `(-1)^S × 0.M × 16^(E-64)`. Sign-magnitude, no
implicit bit, bias 64, no NaN, no Inf, no denormals. The bit geometry
and all Encoding sub-parameters have valid values — the type system
accepts it. But every operation would silently produce wrong results
because one exponent step is 4 bit positions, not 1. Double precision
is `[S1][E7][M56]`, 64 bits. Extended is 128 bits.

**Xerox Sigma (1966).** Base-16 exponent, two's complement sign
encoding. Same exponent base problem as IBM HFP, compounded with a
sign encoding the current design does handle.

**Burroughs B5500 (1964).** Base-8 exponent. 48-bit word, 39-bit
mantissa. Also notable: stores all integers as floating-point values
with exponent 0.

**Burroughs Medium Systems (B2500/B3500).** Base-10 exponent. Up to
100 decimal mantissa digits. 48-bit or 80-bit floating point.

### BCD and decimal formats

**COMP-3 / Packed Decimal (IBM, COBOL).** Each nibble encodes one
decimal digit (0-9), two per byte. Trailing nibble is a sign indicator
(0xC = positive, 0xD = negative, 0xF = unsigned). PIC S9(5) COMP-3 is
3 bytes: 5 digit nibbles + 1 sign nibble. No exponent — the radix
point position is external metadata (the PIC clause). Format cannot
express this: `ExpBits >= 1` fails, `SignBits <= 1` fails, the
significand is BCD not binary.

**Zoned Decimal (COBOL DISPLAY).** One decimal digit per byte. Each
byte has a zone nibble (0xF for EBCDIC, 0x3 for ASCII) and a digit
nibble. Sign is encoded in the zone nibble of the last byte. PIC S9(5)
DISPLAY is 5 bytes. COBOL supports five different zoned decimal sign
conventions.

**Ten's complement packed decimal.** No sign field at all. The sign is
implicit: if the most significant digit is >= 5, the value is negative.
Analogous to two's complement for binary. Used in some BCD systems.

**Nine's complement packed decimal.** Analogous to one's complement
for binary. Negative values are the digit-wise complement (each digit
subtracted from 9). Negative zero exists.

**HP calculator format.** BCD mantissa, but sign nibble values are
0 (positive) and 9 (negative) — different from COMP-3's C/D/F. 56-bit
format: 12 BCD digits of mantissa, sign nibble, 3-digit exponent in
thousand's complement.

**TI-89 calculator format.** 10-byte BCD format. 14 decimal mantissa
digits. But the sign is a single BIT (not a nibble), with a 15-bit
biased exponent. The sign field is sub-digit-width.

**8087 packed BCD.** 80-bit format, 18 BCD digits packed two per byte,
sign in the most significant byte. Used by x87 FBLD/FBSTP instructions.

### IEEE 754 decimal floating point

**Decimal32/64/128 with DPD encoding.** Densely Packed Decimal encodes
3 decimal digits in 10 bits. The combination field (5 bits) interleaves
exponent MSBs with the leading significand digit. No clean separation
into independent exponent and significand fields. The logical digit
width is 4 (BCD), but the physical packing is denser.

**Decimal32/64/128 with BID encoding.** Binary Integer Decimal stores
the significand as a binary integer whose value represents a decimal
number. The significand storage is binary (digit_width=1), but the
number system is decimal (exponent base 10). The radix of the storage
and the radix of the arithmetic are different.

### Variable-width fields

**Posits (Type III Unums).** Fixed-width word, but internal field
boundaries depend on the value. Sign: 1 bit (MSB, always). Regime:
variable-length unary run (1 to n-1 bits). Exponent: up to `es` bits.
Fraction: remaining bits (may be zero). Two's complement negation.
Single NaR (Not a Real) at the trap value (most-negative integer).
No infinity.

Posit arithmetic is binary — once unpacked, computation is identical
to IEEE. The regime is a storage encoding for a variable-width
exponent, not a different number system. But Format cannot express
value-dependent field boundaries.

Posit rounding precision is value-dependent: the number of output
fraction bits depends on the result's magnitude (because the output
regime length depends on the output exponent).

**Tapered floating point (Morris, 1971).** The exponent length is
itself encoded in the word. Extreme values get more exponent bits at
the expense of significand bits. Posits are a refined version.

**Type I Unums (Gustafson).** Both exponent and fraction widths are
variable, with explicit size fields in the word.

### String and character formats

**Decimal character strings.** "-1.23e4", "123.45", "+0.007". Sign
is a character. Digits are character codes (ASCII 0x30-0x39, EBCDIC
0xF0-0xF9). Decimal point is a character. Exponent is itself a string.
Variable-length storage.

### Alternative number systems

**Balanced ternary.** Radix 3 with digit values {-1, 0, +1}. No sign
representation needed — the sign is inherent in the most significant
non-zero trit. Used in real hardware (Setun computer, 1958). Proposed
floating-point formats exist (Ternary27: 27-trit format with 5-trit
exponent and 19-trit significand). No implicit digit is possible
because the significand can never have a "hidden" leading trit.

**Negabinary (base -2).** No sign needed — the negative base encodes
sign implicitly. Used in zfp floating-point compression (Lawrence
Livermore National Laboratory). The leftmost one-bit simultaneously
encodes sign and approximate magnitude.

### Formats the current design handles but should be cataloged

**IEEE 754 binary (all widths).** binary16, binary32, binary64,
binary128, and the x87 80-bit extended (explicit integer bit).

**ML accelerator formats.** bfloat16, FP8 E5M2, FP8 E4M3, FP8
E4M3FNUZ, TensorFloat-32, PXR24 (Pixar), fp24 (AMD).

**Motorola FFP (Amiga).** `[M24][S1][E7]`, 32 bits. Non-standard
field ordering. Sign-magnitude, implicit bit, bias 64 (excess-64),
no NaN/Inf/denormals. The Amiga also supported IEEE single and double
precision via separate math libraries.

**Microsoft Binary Format (MBF).** `[E8][S1][M23]` (32-bit) and
`[E8][S1][M31]` (40-bit). The 40-bit variant exists because the 6502
ROM had extra space. Sign-magnitude, implicit bit, bias 129, no
NaN/Inf/denormals. Used in MBASIC, GW-BASIC, Applesoft BASIC,
Commodore BASIC.

**PDP-10 (DEC, 1966).** 36-bit word, `[S1][E8][M27]`. Two's complement
of entire word, no implicit bit, bias 128, no NaN/Inf/denormals.

**CDC 6600 (Control Data, 1964).** 60-bit word, `[S1][E11][M48]`.
One's complement of entire word, no implicit bit, bias 1024, no
NaN/Inf/denormals.

**rbj's two's complement.** Two's complement of entire word, implicit
bit, bias 2^(E-1), NaN at trap value (0x80...0), infinity at integer
extremes.

**IBM 1130 (1965).** 32-bit and 40-bit formats, two's complement
significand.

**BRLESC.** 68-bit word, base-16 exponent, 3-bit tag field, 56-bit
mantissa, 8-bit exponent.

**Fixed-point Q formats.** Q15.16, Q1.31, etc. No exponent. The radix
point position is part of the type. Binary significand, typically two's
complement. Currently blocked by `ExpBits >= 1`.

**Integers.** Signed and unsigned binary integers are the degenerate
case: no exponent, no fractional digits. Currently cannot be expressed.

## Observations toward a solution

### Digit width

The significand, across all known formats, can be described as an array
of N fields, each `digit_width` bits wide:

- `digit_width = 1`: Binary. Covers IEEE, FFP, MBF, IBM HFP, PDP-10,
  CDC 6600, rbj, posit fractions, all ML formats, BID significands,
  fixed-point Q formats, integers.
- `digit_width = 4`: Nibble. Covers COMP-3, packed BCD, HP/TI
  calculator BCD, 8087 packed BCD, DPD (as compressed nibble pairs).
- `digit_width = 8`: Byte. Covers zoned decimal, ASCII/EBCDIC digit
  strings.

What the digits MEAN (valid values, extraction rules) is determined
by the radix, not by a separate "digit interpretation" parameter.
Radix 2 + digit_width 1 = binary. Radix 10 + digit_width 4 = BCD.
Radix 10 + digit_width 8 = character digits (where the digit value
is the low nibble and the high nibble is a zone constant — 0x3 for
ASCII, 0xF for EBCDIC). The "interpretation" is not a separate axis;
it falls out of (radix, digit_width).

### Sign methods

The current `SignEncoding` enum — `{SignMagnitude, TwosComplement,
OnesComplement}` — is both incomplete and non-compositional. The
formats above require at minimum:

- **Explicit**: A dedicated field encodes the sign. The field values
  vary (0/1 for a bit, C/D for COMP-3, 0/9 for HP calculators, +/-
  for strings), but the structural property is the same.
- **Radix complement**: The entire word is complemented. Two's
  complement when radix=2. Ten's complement when radix=10. These are
  the same sign method parameterized by the radix.
- **Diminished radix complement**: Complement minus one. One's
  complement when radix=2. Nine's complement when radix=10. Same
  method, different radix.
- **Inherent**: No sign representation — the number system itself
  handles sign. Balanced ternary. Negabinary.
- **Unsigned**: No sign.

The named values "TwosComplement" and "OnesComplement" are special
cases of RadixComplement and DiminishedRadixComplement where radix=2.
"TensComplement" and "NinesComplement" are the same methods where
radix=10. The sign method composes with the radix rather than
enumerating every combination.

### Exponent base

The exponent base is an independent parameter, never explicitly 2,
that takes real values from the historical record:

- 2: IEEE 754 binary, FFP, MBF, PDP-10, CDC 6600, rbj, posits
- 8: Burroughs B5500, Ferranti Atlas
- 10: Burroughs Medium Systems, IEEE 754 decimal
- 16: IBM System/360 HFP, Xerox Sigma, BRLESC

Or "none" for fixed-point and integer formats.

### Sketch of reconsidered axes

The five-axis count appears to remain correct. The axes should be:

1. **Container** (currently Format): Total storage size, digit width
   (1, 4, 8), byte ordering. Pure physical structure. No field names,
   no semantic labels.

2. **Interpretation** (currently Encoding): Radix, exponent base,
   scaling model (fixed exponent / regime / fixed-point / integer),
   sign method (explicit / radix complement / diminished radix
   complement / inherent / unsigned), field decomposition (which cells
   are sign, exponent, significand — compile-time for fixed formats,
   runtime for posits), has_implicit_digit, exponent_bias, special
   values (NaN, Inf, negative zero, denormals).

3. **Rounding**: Unchanged in concept, but needs to accommodate
   decimal rounding and value-dependent rounding precision (posits).

4. **Exceptions**: Unchanged.

5. **Platform**: Unchanged in concept, extended with BCD hardware
   capability flags.

These are sketches, not specifications. The right decomposition should
emerge from thorough analysis of the problems above, not be designed
in advance.
