# Opine Design: Recursive Number/Layout Five-Axis Model

## Overview

Opine represents arbitrary numeric formats as points in a five-dimensional
space. The first two axes — Number and Layout — are recursive compositional
structures that parallel each other. The remaining three axes — Rounding,
Exceptions, and Platform — are flat.

The claim is not that this design covers every format that has ever existed.
It covers a large and well-defined subset. The gaps are identified and
cataloged honestly at the end of this document.

## Axis 1: Number

A **Number** describes the mathematical semantics of a numeric value,
independent of how it is stored.

### Primitive Number

A primitive Number is defined by four properties:

- **radix**: The base of the number system. Usually 2 or 10. May be negative
  (negabinary: -2) or greater than 10 (base-16 for IBM HFP significands is
  still binary digits — the base-16 is the *exponent base*, not the
  significand radix).
- **digit_width**: The number of bits used to represent one digit. Binary
  digits: 1 bit. BCD digits: 4 bits. Byte-sized digits (zoned decimal,
  character formats): 8 bits.
- **digit_count**: How many digits the number contains.
- **sign_method**: How the sign of the number is represented. One of five
  methods (see below).

These four properties are independent. Changing one does not force a change
in any other.

### Composite Number

A composite Number contains sub-Numbers assembled by a composition rule.

A floating-point number is a composite Number with:
- A **significand**: a primitive Number
- An **exponent**: a primitive Number
- An **exponent_base**: the base used for scaling (2, 8, 10, 16)
- A **value_sign**: the sign method for the overall value
- **special_values**: encodings for NaN, infinities, denormals, negative
  zero, NaR, or none of the above

The mathematical value is: `sign × significand × exponent_base^exponent`

An array (SIMD) number is a composite Number with:
- An **element**: a (possibly composite) Number
- An **element_count**: how many elements

Composition is recursive. A 256-bit AVX register holding 8 float32s is three
levels deep: array → floating-point → primitive fields.

### The Five Sign Methods

1. **Explicit**: A dedicated field encodes the sign independently from the
   magnitude. The specific code values are a parameter: 0/1 (IEEE), 0xC/0xD
   (COMP-3), 0/9 (HP calculators), '+'/'-' (string formats). All of these
   are the same method with different code tables.

2. **RadixComplement**: Negate by complementing relative to the radix, then
   adding 1. The sign is implicit in the most significant digit. When
   radix=2, this is two's complement. When radix=10, this is ten's
   complement. Same method, different radix. No separate enumeration needed.

3. **DiminishedRadixComplement**: Like RadixComplement but without adding 1.
   Negative zero exists. When radix=2, this is one's complement. When
   radix=10, this is nine's complement.

4. **Inherent**: The number system encodes sign natively. Balanced ternary
   (digits {-1, 0, +1}) and negabinary (base -2) have no sign field and no
   complementation step. The sign is a consequence of the digit values and
   the radix.

5. **Unsigned**: No sign. The number is non-negative by definition.

Sign methods 2 and 3 are parameterized by the radix already present in the
Number. This is the key insight that collapses two's complement, ten's
complement, one's complement, and nine's complement into two methods instead
of four.

### Why digit_width and sign_method are Number properties

The TI-89 calculator stores a 1-bit sign, a 15-bit binary exponent, and a
14-nibble BCD mantissa in a single format. The exponent has digit_width=1;
the mantissa has digit_width=4. The HP calculator has an explicit-signed
significand and a radix-complement-signed exponent.

This means digit_width and sign_method are not properties of a *format*.
They are properties of each sub-Number within the format. The format is a
composition of sub-Numbers, each with its own digit_width and sign_method.

## Axis 2: Layout

A **Layout** describes where each Number's parts go in physical storage.
Layout is recursive, parallel to Number: every node in the Number tree has
a corresponding node in the Layout tree.

### What Layout contains

- **Field positions**: Offset and width of each sub-field in the storage
  word. For fixed-field formats (IEEE, IBM HFP, MBF, etc.), these are
  compile-time constants.
- **Digit ordering**: MSB-first, LSB-first, or platform-specific byte
  ordering (PDP-endian for VAX).
- **Packing codecs**: Transformations between the logical Number and its
  physical encoding. Two important examples:
  - **Implicit leading digit**: IEEE binary formats store 23 bits but
    represent 24 digits of significand. The leading 1 (or 0 for denormals)
    is not stored. This is a storage compression — the logical Number has
    24 digits; the Layout says one is implicit.
  - **Densely Packed Decimal (DPD)**: Packs 3 decimal digits into 10 bits.
    The logical Number has N decimal digits; the Layout says how to
    encode/decode them.
- **Variable field boundaries**: For posits, the regime run-length
  determines where the exponent and fraction fields start. For Type I Unums,
  explicit size fields encode the field widths. These are Layout concerns —
  the logical Number is the same kind of composite (significand + exponent),
  but the Layout's field-position rules are value-dependent instead of
  constant.

### Number and Layout are independent

This is not a theoretical claim — it has concrete instances:

- **rbj two's complement float32 vs. IEEE binary32**: Same Layout (same
  field positions, same total width), different Number (different sign
  method, different special value encodings).
- **IEEE binary32 vs. Amiga FFP**: Same Number (same radix, same digit
  counts, same sign method), different Layout (FFP puts the mantissa in the
  high bits and the sign+exponent in the low bits).
- **IEEE binary32 vs. MBF 32-bit**: Same Number properties, different Layout
  (MBF puts the exponent first, then sign, then mantissa; different bias
  value).

Separating Number from Layout means you can reason about mathematical
properties (precision, range, rounding behavior) using Number alone, without
knowing how the bits are arranged.

## Axis 3: Rounding

How intermediate results are rounded to fit the target Number's precision.

Rounding modes include:
- Round toward zero (truncation)
- Round to nearest, ties to even (IEEE default)
- Round to nearest, ties away from zero
- Round toward +∞ (ceiling)
- Round toward -∞ (floor)
- Stochastic rounding (used in some ML training)

The precision target may be fixed (IEEE: always round to the format's
digit_count) or value-dependent (posits: effective precision varies with
magnitude). This axis is unchanged from the original design.

## Axis 4: Exceptions

What happens when an operation produces a result outside the format's
representable range or encounters an invalid operation.

Exception behaviors include:
- Set a status flag (IEEE default)
- Trap to a handler
- Saturate to the nearest representable value
- Return a default (NaN, infinity, zero)
- Undefined behavior (some embedded platforms)

This axis is unchanged from the original design.

## Axis 5: Platform

The hardware and software capabilities available for arithmetic.

Platform concerns include:
- Native word width
- Availability of hardware multiply, divide, shift
- BCD arithmetic instructions (DAA/DAS on x86, ADC with BCD mode on 6502)
- SIMD/vector capabilities
- Memory constraints (especially on 6502 and similar)
- Interrupt behavior during multi-byte operations

This axis is unchanged from the original design.

## The ~7 Independent Properties

These are the minimal independent decisions that identify any numeric format.
They cut across the five axes:

1. **Arithmetic radix** — 2, 10, or other. Determines the ALU operations.
   IBM HFP is radix-2 here (binary significand arithmetic) despite base-16
   exponents. BID decimal is radix-10 here despite binary storage.
2. **Scaling** — Floating-point (explicit exponent with a base), fixed-point
   (implicit radix position), or integer. Plus exponent base if floating.
3. **Sign method** — Asked of EACH sub-Number independently.
4. **Digit width** — Asked of EACH sub-Number independently.
5. **Storage size and field structure** — Fixed-width vs. variable-length.
   Fixed vs. variable field boundaries (and by what mechanism).
6. **Implicit leading digit** — A Layout codec concern.
7. **Special values** — NaN, Inf, denormals, negative zero, NaR, and their
   encodings.

Properties 3 and 4 are recursive: they apply to each sub-Number independently.

## Format Catalog

How specific formats map into Number and Layout:

### Binary floating-point with base-2 exponent (fully covered)

| Format | Significand Number | Exponent Number | Value Sign | Layout Notes |
|--------|-------------------|-----------------|------------|--------------|
| IEEE binary32 | radix=2, dw=1, dc=24, Unsigned | radix=2, dw=1, dc=8, Unsigned (biased 127) | Explicit (0/1) | [S1][E8][M23], implicit leading digit |
| IEEE binary64 | radix=2, dw=1, dc=53, Unsigned | radix=2, dw=1, dc=11, Unsigned (biased 1023) | Explicit (0/1) | [S1][E11][M52], implicit leading digit |
| FFP (Amiga) | radix=2, dw=1, dc=24, Unsigned | radix=2, dw=1, dc=7, Unsigned (biased) | Explicit (0/1) | [M24][S1][E7] |
| MBF 32-bit | radix=2, dw=1, dc=24, Unsigned | radix=2, dw=1, dc=8, Unsigned (biased 129) | Explicit (0/1) | [E8][S1][M23] |
| MBF 40-bit | radix=2, dw=1, dc=32, Unsigned | radix=2, dw=1, dc=8, Unsigned (biased 129) | Explicit (0/1) | [E8][S1][M31] |
| PDP-10 | radix=2, dw=1, dc=27, Unsigned | radix=2, dw=1, dc=8, Unsigned (biased) | RadixComplement | No implicit bit |
| CDC 6600 | radix=2, dw=1, dc=48, Unsigned | radix=2, dw=1, dc=11, Unsigned (biased) | DiminishedRadixComplement | No implicit bit |
| rbj TC float32 | radix=2, dw=1, dc=24, Unsigned | radix=2, dw=1, dc=8, Unsigned (biased 127) | RadixComplement | Same Layout as IEEE binary32 |
| FP8 E4M3FNUZ | radix=2, dw=1, dc=4, Unsigned | radix=2, dw=1, dc=4, Unsigned (biased) | Explicit (0/1) | NaN at negative-zero pattern |
| VAX F/D/G/H | radix=2, dw=1, dc=varies, Unsigned | radix=2, dw=1, dc=varies, Unsigned (biased) | Explicit (0/1) | PDP-endian byte order |

### Binary floating-point with non-base-2 exponent (covered with exponent_base parameter)

| Format | Significand Number | Exponent Number | Exponent Base | Value Sign | Notes |
|--------|-------------------|-----------------|---------------|------------|-------|
| IBM HFP | radix=2, dw=1, dc=24, Unsigned | radix=2, dw=1, dc=7, Unsigned (biased 64) | 16 | Explicit (0/1) | 1 exponent step = 4 bit positions |
| Xerox Sigma | radix=2, dw=1, dc=24, Unsigned | radix=2, dw=1, dc=7, Unsigned (biased) | 16 | RadixComplement | Like IBM HFP with two's complement |
| Burroughs B5500 | radix=2, dw=1, dc=39, Unsigned | radix=2, dw=1, dc=6, Unsigned (biased) | 8 | Explicit | Base-8 exponent |

### BCD and decimal formats (covered)

| Format | Significand Number | Exponent Number | Value Sign | Layout Notes |
|--------|-------------------|-----------------|------------|--------------|
| COMP-3 | radix=10, dw=4, dc=varies, Unsigned | None (fixed-point) | Explicit (0xC/0xD/0xF) | Packed BCD nibbles, sign in last nibble |
| Ten's complement BCD | radix=10, dw=4, dc=varies, RadixComplement | None (fixed-point) | (implicit) | No sign field |
| Nine's complement BCD | radix=10, dw=4, dc=varies, DiminishedRadixComplement | None (fixed-point) | (implicit) | Negative zero exists |
| HP calculator | radix=10, dw=4, dc=14, Unsigned | radix=10, dw=4, dc=3, RadixComplement (thousand's complement) | Explicit (0/9) | BCD nibbles throughout |
| TI-89 | radix=10, dw=4, dc=14, Unsigned | radix=2, dw=1, dc=15, Unsigned (biased) | Explicit (1 bit) | Mixed: binary exponent + BCD mantissa |

### Variable-field formats (partially covered)

| Format | Number Structure | Layout Notes | Coverage |
|--------|-----------------|--------------|----------|
| Posit\<N, ES\> | radix=2, significand + exponent, RadixComplement | Regime run-length determines field boundaries | Number: covered. Layout: requires value-dependent field parser |
| Type I Unum | radix=2, significand + exponent | Explicit size fields in the word | Number: covered. Layout: requires self-describing field parser |

### Decimal floating-point (partially covered)

| Format | Number Structure | Layout Notes | Coverage |
|--------|-----------------|--------------|----------|
| IEEE Decimal32 DPD | radix=10, dw=4, dc=7, Unsigned; exponent radix=2 | DPD codec (3 digits → 10 bits), combination field | Number: covered. Layout: DPD codec needed; combination field is complex |
| IEEE Decimal64 BID | radix=10, dc=16, Unsigned; exponent radix=2 | Significand stored as binary integer | Number: covered. Layout: storage radix ≠ arithmetic radix |

### Formats at the boundary (significant gaps)

| Format | Issue |
|--------|-------|
| Zoned decimal | Sign embedded in zone nibble of last digit byte — not a dedicated field |
| Balanced ternary | Non-binary atoms. A trit is not 1, 4, or 8 bits natively |
| Negabinary | Radix = -2. Number definition needs to handle negative radix |
| String "-1.23e4" | Variable-length storage, delimiter characters as structural elements |
| Burroughs Medium Systems | Up to 100 digits, variable-length |

## Remaining Gaps

These are identified problems without proposed solutions. They are listed
here to be honest about the design's current boundaries, not to suggest
that solutions are imminent.

### Zoned decimal zone overpunch
The sign is embedded within an existing digit cell — the zone nibble of the
last byte. This is not a dedicated sign cell. It might fit "Explicit" with
an annotation that the sign shares a cell with the last digit. Or it might
be something the model cannot cleanly express.

### DPD combination field
Five bits that simultaneously encode exponent MSBs and the leading
significand digit. Multi-purpose bits. This is a Layout concern, but it
means a single field contributes to two different sub-Numbers. The
parallel-tree model assumes each Layout node maps to exactly one Number
node.

### BID storage/arithmetic radix mismatch
The significand is stored as a binary integer whose value represents a
decimal number. The storage radix is 2; the arithmetic radix is 10. The
current model does not distinguish these. It may need to.

### Negative radix
Negabinary (radix = -2) is mathematically well-defined. The Number
definition implicitly assumes positive radix in its current form.

### Non-binary atoms
Balanced ternary digits (trits) are not binary. A trit can be encoded in 2
bits, but then we are describing a binary encoding of ternary, not native
ternary. The digit_width concept assumes bits as the fundamental unit.

### Variable-length storage
String formats and some mainframe formats (Burroughs Medium Systems, up to
100 digits) have no fixed total storage size. If Layout requires a fixed
total width, these formats cannot be described.

### Multiple dynamic-field strategies
Posits use regime run-length. Type I Unums use explicit size fields. Tapered
floating-point (Morris) uses a self-describing exponent width. These are
three different mechanisms for variable field boundaries. It is not clear
whether a single Layout abstraction can accommodate all three without
becoming an ad-hoc union type.

### String delimiters
In string representations like "-1.23e4", the decimal point and exponent
indicator are structural elements that are neither digits, sign, nor
exponent. The current model has no concept of structural delimiters.

## Design Principles

1. **Problems first, solutions later.** This document describes the
   structure of the problem space. Implementation choices are deferred.

2. **Recursive composition over flat enumeration.** A format is not a bag of
   properties. It is a tree of sub-Numbers, each with its own properties.

3. **Number and Layout are independent.** Mathematical semantics and
   physical storage are orthogonal concerns. You can change one without
   changing the other.

4. **Sign methods are parameterized, not enumerated.** Two's complement and
   ten's complement are the same method (RadixComplement) applied to
   different radixes.

5. **Storage codecs belong in Layout.** Implicit leading digits and DPD are
   packing strategies, not mathematical properties of the number.

6. **Honesty about gaps.** The design does not cover everything. The gaps
   are documented. Claiming universality without evidence is worse than
   admitting limitations.
