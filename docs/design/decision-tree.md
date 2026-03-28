# Decision Tree: Identifying Any Numeric Format

A binary decision tree for identifying any numeric format ever built.
Each node is a yes/no question. Follow the branch that matches. The
path from root to leaf uniquely identifies the format family, and
often the exact format.

---

## Q1. Is each value self-contained?

Can you interpret one value without external context (no shared
exponent, no lookup table, no metadata beyond the bits themselves)?

**NO** → the value depends on context external to its own bits.
Go to Q2.

**YES** → each value stands alone.  Go to Q4.

---

## Q2. Is the bit pattern an index into a fixed value table?

**YES**: codebook / lookup-table formats.  The bits are not a
structured number --- they are an address into a table of precomputed
values.

- **NF4** (NormalFloat4, used in QLoRA/bitsandbytes): 4-bit index
  into 16 values chosen to approximate a normal distribution.
  Not decomposable into sign/exponent/significand.
- **Other codebook quantizations** (GPTQ palettes, learned
  codebooks): the table is trained, not standardized.  The bit
  pattern has no arithmetic structure.

→ **Leaf: Codebook format.**  Outside the sign/exponent/significand
framework.  Requires a value table as part of the format definition.

**NO** → the value has arithmetic structure but shares some parameter
with neighboring values.  Go to Q3.

---

## Q3. What context does the value depend on?

→ **Leaf: Block / shared-exponent format.**

- **MXFP4 / MXFP6 / MXFP8** (OCP Microscaling): a block of small
  floats (FP4 E2M1, FP6 E3M2, FP8 E4M3/E5M2) sharing a single
  scale factor (an FP8 E8M0 exponent).  Each element is a normal
  tiny float; the shared exponent shifts the whole block.
- **Block floating point (BFP)**: older term for the same idea.
  Common in DSP and some GPU intermediate representations.

Within each element, return to Q4 to classify the element format
itself.

---

## Q4. Is the arithmetic radix 2?

Does the format compute in binary?  This is the deepest implementation
split: binary arithmetic is shift-and-add, decimal arithmetic requires
BCD correction or dedicated hardware.

Careful: IBM HFP is radix 2 here (the significand multiplies as
binary).  BID is NOT radix 2 (the significand is a decimal value, even
though it is stored as a binary integer --- storage encoding is not
arithmetic radix).

**NO** → Go to Q5.

**YES** → binary format.  Go to Q7.

---

## Q5. Is the arithmetic radix 10?

**YES** → decimal format.  Go to Q14.

**NO** → exotic radix.  Go to Q6.

---

## Q6. What is the radix?

- **Radix 3**: balanced ternary.  Digits are {-1, 0, +1}.  Sign is
  inherent (no sign field).  Used in Setun computer (1958).
  Proposed FP: Ternary27 (5-trit exponent, 19-trit significand).
- **Radix -2**: negabinary.  Digits are {0, 1}, but the negative
  base encodes sign implicitly.  Used in zfp compression (Lawrence
  Livermore).
- **Radix 8 or 16 with non-binary digits**: no known real format.
  All known base-8 and base-16 formats use binary digits with
  a non-base-2 exponent (classified under radix 2, Q12).

→ **Leaf: Exotic-radix format.**

---

## Binary path

## Q7. Does the format store an exponent?

**NO** → Go to Q8.

**YES** → binary floating-point.  Go to Q9.

---

## Q8. Is there a fractional part?

**YES** → **Leaf: Fixed-point format.**
Q-format notation (Q15.16, Q1.31, etc.).  Radix point position is
a type-level constant.  Sign is typically two's complement.

**NO** → **Leaf: Integer.**
int8/16/32/64 (two's complement), uint8/16/32/64 (unsigned).
Degenerate fixed-point with radix position 0.

---

## Q9. Are the field boundaries the same for every value?

**NO** → variable-field format.  Go to Q10.

**YES** → fixed-field binary floating-point.  Go to Q11.

---

## Q10. How are the field boundaries determined?

**Unary regime run** → **Leaf: Posit.**
posit\<n, es\>.  Sign is two's complement of whole word.  Regime is
a run of identical bits terminated by the opposite bit.  Remaining
bits split between exponent supplement (es bits) and fraction.
Single NaR (Not-a-Real) at the trap value.  No infinity, no
negative zero.  Rounding precision is value-dependent.

**Explicit size fields** → **Leaf: Type I Unum.**
Both exponent and fraction widths are variable.  Binary size fields
stored in the word specify the widths.

**Encoded exponent length** → **Leaf: Tapered floating point**
(Morris 1971).  The exponent width is itself stored in the word.
Precursor to posits.

---

## Q11. Is the exponent base 2?

**NO** → Go to Q12.

**YES** → base-2 exponent.  Go to Q13.

---

## Q12. What is the exponent base?

**Base 16**:

- Is the sign two's complement?
  - **YES** → **Leaf: Xerox Sigma.**  [S1][E7][M24], base-16
    exponent, two's complement sign, bias 64.
  - **NO** (sign-magnitude) → **Leaf: IBM HFP.**
    Single [S1][E7][M24], double [S1][E7][M56], extended
    [S1][E7][M112].  Bias 64.  No implicit bit (fractional form
    0.M).  No NaN/Inf/denormals.  Also BRLESC: 68-bit word with
    3-bit tag, 56-bit mantissa, 8-bit base-16 exponent.

**Base 8** → **Leaf: Burroughs B5500.**  48-bit word, 39-bit
mantissa, 6-bit exponent, bias 32.

---

## Q13. Is the value's sign explicit (a dedicated field)?

**NO** → complement-based sign.  Go to Q14.

**YES** → Go to Q16.

---

## Q14. Is it two's complement or one's complement?

**Two's complement**:

- Is there an implicit leading bit?
  - **YES** → **Leaf: Integer-ordered float.**
    Same layout as IEEE [S1][E8][M23] but two's complement of
    whole word.  Bias 128.  NaN at trap value (0x80000000).  Integer
    compare gives correct float ordering.
  - **NO** → **Leaf: PDP-10 / IBM 1130.**
    PDP-10: [S1][E8][M27], 36 bits, bias 128, no implicit bit,
    no NaN/Inf.  Integer comparison gives correct ordering.
    IBM 1130: 32-bit, two's complement significand.

**One's complement** → **Leaf: CDC 6600.**
[S1][E11][M48], 60 bits, bias 1024.  Negative zero exists.  No
implicit bit.  No NaN/Inf/denormals.

---

## Q15. Does the format have NaN and/or Inf?

**NO** → no special values.  Go to Q16.

**YES** → has special values.  Go to Q17.

---

## Q16. What is the field order?

**[M][S][E]** (mantissa first) → **Leaf: Motorola FFP (Amiga).**
[M24][S1][E7], 32 bits.  Implicit leading bit.  Bias 64.  No
NaN/Inf/denormals.  ROM-based fast floating point.

**[E][S][M]** (exponent first) → **Leaf: Microsoft Binary Format.**
MBF-32: [E8][S1][M23], 32 bits, bias 129.
MBF-40: [E8][S1][M31], 40 bits, bias 129.
No NaN/Inf/denormals.  Used in MBASIC, GW-BASIC, Applesoft BASIC,
Commodore BASIC.

**[S][E][M]** (IEEE order, but no special values) → historical
or custom formats.  Identify by width and bias.

---

## Q17. Is the format wider than 8 bits?

**NO** → 8-bit ML float.  Go to Q18.

**YES** → wider than 8 bits with special values.  Go to Q19.

---

## Q18. What is the exponent width?

**5 bits (E5M2)** → **Leaf: FP8 E5M2.**
[S1][E5][M2], 8 bits.  IEEE-like special values (NaN, Inf).
Bias 15.  Implicit leading bit.  Primarily for ML inference.

**4 bits (E4M3)** → Does it have NaN at the negative-zero pattern?

- **YES** → **Leaf: FP8 E4M3FNUZ.**
  No negative zero, no Inf.  NaN is the bit pattern that would be
  negative zero.  Bias 8.  "FNUZ" = Finite, No Unsigned Zero.
  Flush denormals to zero.
- **NO** → **Leaf: FP8 E4M3FN.**
  Has NaN (reserved exponent).  No Inf.  Bias 7.

---

## Q19. What is the total width?

At this point we have: binary, base-2 exponent, explicit sign,
fixed fields, has NaN/Inf, wider than 8 bits.  This is the IEEE 754
binary family.  Identify by total width:

- **16 bits, 5-bit exponent** → **Leaf: IEEE binary16** (half).
  [S1][E5][M10], bias 15.
- **16 bits, 8-bit exponent** → **Leaf: bfloat16.**  [S1][E8][M7],
  bias 127.  Same exponent range as binary32, truncated significand.
  ML accelerator format (Google/TensorFlow).
- **19 bits** → **Leaf: TensorFloat-32 (TF32).**  [S1][E8][M10],
  bias 127.  NVIDIA Tensor Core format.  8-bit exponent (binary32
  range), 10-bit significand (binary16 precision).
- **24 bits** → **Leaf: Pixar PXR24 or AMD fp24.**
- **32 bits** → **Leaf: IEEE binary32** (float).  [S1][E8][M23],
  bias 127.
- **64 bits** → **Leaf: IEEE binary64** (double).  [S1][E11][M52],
  bias 1023.
- **80 bits** → **Leaf: x87 extended80.**  [S1][E15][M64], bias
  16383.  No implicit bit (explicit integer bit).  Intel x87 FPU
  native format.
- **128 bits** → **Leaf: IEEE binary128** (quad).  [S1][E15][M112],
  bias 16383.
- **256 bits** → **Leaf: IEEE binary256** (octuple).  Defined in
  IEEE 754-2008, rarely implemented.

---

## Decimal path

## Q20. Does the format store an exponent?

**NO** → decimal fixed-point.  Go to Q21.

**YES** → decimal floating-point.  Go to Q24.

---

## Q21. What is the digit width?

**4 bits (BCD nibble)** → Go to Q22.

**8 bits (byte)** → Go to Q23.

---

## Q22. Is there a dedicated sign field?

**YES (trailing nibble, 0xC/0xD/0xF)** → **Leaf: COMP-3 /
packed decimal.**  N digits at 2 per byte, plus trailing sign
nibble.  Size determined by COBOL PIC clause.  IBM mainframe
standard.

**YES (MSB, sign in its own byte)** → **Leaf: 8087 packed BCD.**
80 bits, 18 BCD digits, sign in MSB.  Used by x87 FBLD/FBSTP
instructions for COBOL compatibility.

**NO (sign implicit in digit values)** →

- MSD >= 5 means negative → **Leaf: Ten's complement BCD.**
  Decimal analog of two's complement.
- All-nines is negative zero → **Leaf: Nine's complement BCD.**
  Decimal analog of one's complement.

---

## Q23. Is the sign in the zone nibble of the last byte?

**YES** → **Leaf: Zoned decimal (COBOL DISPLAY).**
One digit per byte.  High nibble is zone (0xF for EBCDIC, 0x3 for
ASCII).  Last byte's zone encodes sign.  The last byte serves
double duty as both digit and sign.

**NO** → **Leaf: String integer.**  ASCII/EBCDIC digit characters,
sign is a leading or trailing +/- character.

---

## Q24. Is the total storage size fixed?

**NO** → variable-length decimal float.  Go to Q25.

**YES** → fixed-size decimal floating-point.  Go to Q26.

---

## Q25. Is it a human-readable string?

**YES** → **Leaf: Decimal string format.**
"-1.23e4", "123.45", etc.  Characters for sign, digits, decimal
point, and exponent indicator.  Variable-length.  Structural
delimiters (., e, E) are neither digits nor sign nor exponent ---
they are parsing markers.

**NO** → **Leaf: Burroughs Medium Systems.**
Up to 100 decimal mantissa digits.  BCD.  Base-10 exponent.
Variable-length within bounded container.

---

## Q26. Is the significand stored as BCD nibbles?

**NO** → **Leaf: IEEE 754 BID (Binary Integer Decimal).**
Decimal32/64/128.  The significand is a binary integer whose value
represents a decimal number.  Storage radix is 2, arithmetic radix
is 10.  Base-10 exponent.  IEEE special values.

**YES** → BCD-significand decimal float.  Go to Q27.

---

## Q27. Is it an IEEE 754 standard format?

**YES** → **Leaf: IEEE 754 DPD (Densely Packed Decimal).**
Decimal32/64/128.  Significand digits are logically BCD but
physically compressed: 3 digits in 10 bits (vs 12 for raw BCD).
5-bit combination field simultaneously encodes exponent MSBs and
leading significand digit.  IEEE special values.

**NO** → calculator or mainframe BCD float.  Go to Q28.

---

## Q28. Is the exponent also BCD?

**YES** → both significand and exponent are BCD.

- Exponent sign is radix complement (thousand's complement)?
  - **YES** → **Leaf: HP calculator format.**
    56 bits.  12 BCD significand digits.  3 BCD exponent digits.
    Value sign: explicit nibble (0=positive, 9=negative).  Exponent
    sign: thousand's complement (radix complement, radix 10).
    Significand and exponent have different sign methods.
  - **NO** → other BCD calculator formats (Casio, Sharp, etc.)

**NO** → mixed encoding (BCD significand, binary exponent).

→ **Leaf: TI-89 calculator format.**
80 bits.  14 BCD significand digits (digit_width=4).  15-bit binary
exponent (digit_width=1), biased (0x4000).  Base-10 exponent.
1-bit sign field (sub-nibble-width --- unusual for a BCD format).
Significand and exponent have different radix AND different digit
width.

---

## What the tree reveals

The tree has 28 nodes and about 40 leaves.  Every known numeric
format reaches a leaf.  The maximum depth is 9 questions; most
formats are identified in 5--7.

This tree identifies a single numeric format --- what one value IS.
It does not address how many values you have or how they are
arranged.  That is the concern of Box, which is orthogonal: any
format identified by this tree can fill any Box.  A Box contains 1
(scalar), N×N (square), or N×N×N (cube) numbers.  The element
format and the collection structure are independent choices.

- **Q1** (self-contained?) separates codebook and block formats from
  structured numbers.  These need external context (a value table or
  a shared exponent) that the number itself does not carry.

- **Q4** (radix) is the deepest arithmetic split.  Everything below
  it forks into parallel binary and decimal subtrees with similar
  shape.

- **Q9** (fixed fields?) is where posits and tapered FP diverge from
  everything else.  A sharp, clean edge.

- Questions about digit width and sign method are asked independently
  of the significand and exponent in the decimal subtree (Q21 vs
  exponent encoding in Q28; sign method in Q22 vs exponent sign in
  Q28).  This is what motivates treating the significand and exponent
  as independent Number objects with their own properties.

- DPD, BID, and the implicit leading digit (appearing in leaf
  descriptions throughout the tree) are Layout concerns, not Number
  concerns.  They are storage transformations over the same logical
  digits.
