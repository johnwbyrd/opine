# Format Catalog

Every format is a (Number, Layout) pair at the element level.  Box
adds multiplicity.  The catalog below shows the Number decomposition
for each known format.

See `menagerie.md` for encyclopedic descriptions of each format.
See `decision-tree.md` to identify an unknown format.

## Binary floating-point with fixed fields

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

## Non-base-2 exponents

| Format | Sig radix | Sig dw | Sig digits | Exp radix | Exp dw | Exp digits | Exp base | Value sign | Exp sign |
|---|---|---|---|---|---|---|---|---|---|
| IBM HFP single | 2 | 1 | 24 | 2 | 1 | 7 | 16 | Explicit | Biased(64) |
| IBM HFP double | 2 | 1 | 56 | 2 | 1 | 7 | 16 | Explicit | Biased(64) |
| Xerox Sigma | 2 | 1 | 24 | 2 | 1 | 7 | 16 | RC(2) | Biased(64) |
| Burroughs B5500 | 2 | 1 | 39 | 2 | 1 | 6 | 8 | Explicit | Biased(32) |

## BCD and calculator formats

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
digit_width=1 for exponent.  This is only expressible because
significand and exponent are separate Numbers.

## IEEE 754 decimal floating point

| Format | Sig radix | Sig dw | Sig digits | Exp radix | Exp dw | Exp digits | Exp base | Value sign | Packing |
|---|---|---|---|---|---|---|---|---|---|
| Decimal32 DPD | 10 | 4 | 7 | 2 | 1 | 8 | 10 | Explicit | DPD |
| Decimal64 DPD | 10 | 4 | 16 | 2 | 1 | 10 | 10 | Explicit | DPD |
| Decimal64 BID | 10 | 4 | 16 | 2 | 1 | 10 | 10 | Explicit | BID |

DPD and BID are Layout packing codecs.  The Number is the same in
both encodings: 16 decimal digits (radix=10, digit_width=4).  DPD
compresses 3 BCD digits into 10 bits.  BID stores the entire
significand as a binary integer.  Same Number, different Layout.

## Block / microscaling formats

| Format | Composition | Element | Count | Shared exp | Exp base |
|---|---|---|---|---|---|
| MXFP4 | SharedExponent | FP4 E2M1 | 16 or 32 | FP8 E8M0 | 2 |
| MXFP6 | SharedExponent | FP6 E2M3/E3M2 | 16 or 32 | FP8 E8M0 | 2 |
| MXFP8 | SharedExponent | FP8 E4M3/E5M2 | 16 or 32 | FP8 E8M0 | 2 |
| NF4 | Codebook | — | 16 values | — | — |

MXFP elements are normal tiny floats; the shared exponent shifts
the whole block.  The block is the Number.  A vector of blocks is
Box applied to that Number.

NF4 is a 4-bit index into a 16-entry value table.  No
sign/exponent/significand structure.

## Variable-width field formats

| Format | Sig radix | Sig dw | Sig digits | Exp base | Value sign | Field boundaries |
|---|---|---|---|---|---|---|
| posit\<32,2\> | 2 | 1 | Variable | 2 | RC(2) | Dynamic (regime scan) |
| posit\<8,2\> | 2 | 1 | Variable | 2 | RC(2) | Dynamic (regime scan) |
| Tapered FP | 2 | 1 | Variable | 2 | Explicit | Dynamic (length prefix) |
| Type I Unum | 2 | 1 | Variable | 2 | Explicit | Dynamic (size fields) |

## String formats

| Format | Sig radix | Sig dw | Sig digits | Exp base | Value sign | Storage |
|---|---|---|---|---|---|---|
| "-1.23e4" | 10 | 8 | Variable | 10 | Explicit | Variable-length |
| "123.45" | 10 | 8 | Variable | — | Explicit | Variable-length |

## Fixed-point and integer

| Format | Sig radix | Sig dw | Sig digits | Value sign |
|---|---|---|---|---|
| Q15.16 | 2 | 1 | 32 | RC(2) |
| int32_t | 2 | 1 | 32 | RC(2) |
| uint8_t | 2 | 1 | 8 | Unsigned |

## Alternative number systems

| Format | Sig radix | Sig dw | Sig digits | Value sign |
|---|---|---|---|---|
| Balanced ternary | 3 | trit | Variable | Inherent |
| Negabinary | -2 | 1 | Variable | Inherent |
