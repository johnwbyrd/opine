# The Menagerie: A Catalog of Numeric Formats

An encyclopedic reference for every numeric format discussed in the
opine design documents.  Each entry explains what the format is, where
it came from, how it works, and what makes it unusual.  No discussion
of how opine handles them --- just the formats themselves.

---

## IEEE 754 binary formats

### IEEE binary16 (half precision)

16 bits.  Layout: [S1][E5][M10].  Exponent bias 15.

Defined in IEEE 754-2008.  Originally a storage format --- most
hardware converts to binary32 for arithmetic.  ARM, x86 (F16C
extension), and NVIDIA GPUs now support native binary16 arithmetic.
Full IEEE special values: ±0, ±Inf, quiet/signaling NaN, denormals.

### IEEE binary32 (single precision)

32 bits.  Layout: [S1][E8][M23].  Exponent bias 127.

The most widely used floating-point format.  Defined in IEEE 754-1985.
23 stored significand bits + 1 implicit leading bit = 24 bits of
precision (~7.2 decimal digits).  Full IEEE special values.

### IEEE binary64 (double precision)

64 bits.  Layout: [S1][E11][M52].  Exponent bias 1023.

The default floating-point type in most programming languages.
52 stored significand bits + 1 implicit = 53 bits (~15.9 decimal
digits).  Full IEEE special values.

### x87 extended80

80 bits.  Layout: [S1][E15][I1][M63].  Exponent bias 16383.

Intel x87 FPU native format.  Unlike all other IEEE binary formats,
the leading significand bit is explicit (the "integer bit" at
position 63), not implicit.  This means the hardware stores 64
significand bits, not 63+1.  The x87 register file holds all values
in this format internally; narrower formats are converted on load
and store.  Full IEEE special values.  Not part of IEEE 754 proper
but follows the same conventions.

### IEEE binary128 (quadruple precision)

128 bits.  Layout: [S1][E15][M112].  Exponent bias 16383.

Defined in IEEE 754-2008.  112 stored + 1 implicit = 113 significand
bits (~34 decimal digits).  Rarely supported in hardware; usually
implemented in software.  Used in numerical analysis, computational
geometry, and as an intermediate accumulator format.

### IEEE binary256 (octuple precision)

256 bits.  Layout: [S1][E19][M236].  Exponent bias 262143.

Defined in IEEE 754-2008 for completeness.  No known hardware
implementation.  Sometimes used in arbitrary-precision libraries as
a fixed-width fast path.

---

## ML accelerator formats

### bfloat16 (Brain Float)

16 bits.  Layout: [S1][E8][M7].  Exponent bias 127.

Created by Google for TensorFlow (circa 2016).  The exponent is
identical to binary32 (8 bits, bias 127), so the representable range
is the same.  The significand is truncated from 23 bits to 7 (from
~7.2 to ~2.4 decimal digits).  This means binary32 values can be
converted to bfloat16 by simply truncating the low 16 bits of the
significand.  Supported on Google TPUs, Intel Nervana/Habana, AMD
Instinct, NVIDIA A100+, ARM Neoverse V2+.  Full IEEE special values.

### TensorFloat-32 (TF32)

19 bits.  Layout: [S1][E8][M10].  Exponent bias 127.

NVIDIA format (Ampere architecture, 2020).  Combines binary32's
exponent range with binary16's significand precision.  Used
internally by Tensor Cores --- input is binary32, but the Tensor Core
truncates each significand to 10 bits before multiplying, then
accumulates in binary32.  Not a storage format; exists only inside
the Tensor Core pipeline.

### FP8 E5M2

8 bits.  Layout: [S1][E5][M2].  Exponent bias 15.

Defined by the OCP (Open Compute Project) MX specification and
adopted in NVIDIA Hopper (H100), AMD MI300, Intel Gaudi2.  Same
exponent as binary16.  3 significand bits (2 stored + 1 implicit).
IEEE-like special values: NaN and Inf at reserved exponent.
Primarily used for ML inference forward pass.

### FP8 E4M3FN

8 bits.  Layout: [S1][E4][M3].  Exponent bias 7.

OCP MX specification.  Has NaN (all-ones exponent with non-zero
significand).  No infinity (all-ones exponent with zero significand
is a finite value, not Inf).  "FN" = Finite, NaN.  Primarily used
for ML training.

### FP8 E4M3FNUZ

8 bits.  Layout: [S1][E4][M3].  Exponent bias 8.

AMD variant.  "FNUZ" = Finite, NaN, Unsigned Zero.  NaN is encoded
as the bit pattern that would be negative zero (1 followed by all
zeros).  No negative zero, no infinity.  Denormals flush to zero.
Note the bias is 8 (not 7 as in E4M3FN) because the negative-zero
bit pattern is repurposed as NaN, shifting the exponent range by 1.

### FP4 E2M1

4 bits.  Layout: [S1][E2][M1].  Exponent bias 1.

Used in OCP MXFP4 blocks.  The smallest "real" float: 1 sign bit,
2 exponent bits, 1 significand bit (+ implicit leading bit = 2 bits
total significand).  Only 16 possible values.  No NaN, no Inf.
Always appears in blocks with a shared exponent (MXFP4).

### Pixar PXR24

24 bits.  Layout: [S1][E8][M15].  Exponent bias 127.

Pixar's format for OpenEXR image storage.  Same exponent as binary32;
significand truncated to 15 stored bits.  Used for visual effects
where binary16 lacks range and binary32 wastes storage.

### AMD fp24

24 bits.  Layout: [S1][E7][M16].  Exponent bias 63.

AMD GPU format used in some shader stages.  7-bit exponent (less
range than binary32), 16 stored significand bits.

---

## Block and shared-exponent formats

### MXFP4 / MXFP6 / MXFP8 (OCP Microscaling)

A block of small floats sharing a single scale factor.  Defined by
the Open Compute Project's Microscaling Formats specification (2023).

Each block contains 16 or 32 elements.  Each element is a tiny float
(FP4 E2M1, FP6 E2M3 or E3M2, or FP8 E4M3/E5M2).  The entire block
shares a single FP8 E8M0 exponent (8-bit exponent, no significand ---
pure power of two).

The shared exponent shifts the dynamic range of all elements in the
block simultaneously.  Individual elements provide local precision.
This is a form of block floating point, modernized for ML workloads.

### Block floating point (BFP)

The general concept of a group of values sharing a common exponent.
Used in DSP (TI TMS320 family), some GPU intermediate formats, and
audio codecs (AC-3/Dolby Digital uses BFP for spectral coefficients).
Each value in the block stores only a significand; the exponent is
stored once for the entire block.

### NF4 (NormalFloat4)

4 bits.  Lookup table format (NOT a structured float).

Used in QLoRA (Dettmers et al., 2023) for 4-bit weight quantization.
The 16 possible 4-bit patterns map to 16 specific values chosen to
be optimal quantization levels for normally-distributed weights.  The
values are not evenly spaced and do not follow any
sign/exponent/significand structure.  The mapping is a fixed lookup
table.  Used in the bitsandbytes library.

---

## Historical binary floating-point

### DEC PDP-10 (1966)

36 bits.  Layout: [S1][E8][M27].  Exponent bias 128.

Digital Equipment Corporation PDP-10 (also PDP-6, DECSYSTEM-20).
Two's complement of the entire word: negating a float means
two's-complementing the full 36 bits.  No implicit bit (the leading
significand bit is stored explicitly).  No NaN, no Inf, no denormals.

The key property: because the format uses two's complement with a
biased exponent, the mapping from bit pattern (interpreted as a
signed integer) to float value is monotonically increasing.  You can
compare two floats using integer comparison instructions.  This was
a hardware simplification --- the PDP-10 had no separate float
compare instruction.

### CDC 6600 (1964)

60 bits.  Layout: [S1][E11][M48].  Exponent bias 1024.

Control Data Corporation 6600, designed by Seymour Cray.  One's
complement of the entire word.  No implicit bit.  No NaN, no Inf,
no denormals.  Negative zero exists (the all-ones word), as with
all one's complement formats.

The CDC 6600 was the fastest computer in the world from 1964 to
1969.  Its 60-bit word was chosen to give enough precision for
nuclear weapons simulations at Los Alamos.

### Xerox Sigma (1966)

Approximately 48 bits.  Layout: [S1][E7][M...].  Exponent bias 64.

Xerox (originally SDS) Sigma series.  Two's complement sign
encoding, like PDP-10.  Base-16 exponent (one exponent step = 4 bit
positions in the significand), like IBM HFP.  Combines the two
unusual properties: integer-ordered comparison AND hexadecimal
exponent.  No implicit bit.  No NaN/Inf/denormals.

### Motorola FFP (Amiga, ~1985)

32 bits.  Layout: [M24][S1][E7].  Exponent bias 64.

The Amiga's ROM-based fast floating point library.  Non-standard
field ordering: the 24-bit significand (including implicit leading
bit) is at the MSB, followed by the 1-bit sign, with the 7-bit
exponent at the LSB.  No NaN, no Inf, no denormals.

The reversed field order meant Amiga FFP values could not be passed
to IEEE routines without conversion.  The Amiga later added separate
IEEE single and double libraries (mathieeesingbas.library and
mathieeedoubbas.library), but FFP remained in ROM for backward
compatibility.

### Microsoft Binary Format (MBF, ~1975)

MBF-32: 32 bits.  Layout: [E8][S1][M23].  Exponent bias 129.
MBF-40: 40 bits.  Layout: [E8][S1][M31].  Exponent bias 129.

Created by Monte Davidoff and Bill Gates for Altair BASIC (1975).
Used in MBASIC, GW-BASIC, Applesoft BASIC, Commodore BASIC, and
early versions of QuickBASIC.  Exponent is at the MSB (opposite of
IEEE).  Implicit leading bit.  No NaN, no Inf, no denormals.

The 40-bit variant exists because the 6502 port (Applesoft BASIC)
had extra ROM space.  The extra 8 bits go to the significand,
giving ~9.6 decimal digits of precision.

MBF was the dominant microcomputer float format until QuickBASIC 3.0
switched to IEEE 754 in 1988.

### IBM 1130 (1965)

32 bits.  Two's complement significand.

The IBM 1130 was a small scientific computer.  It used two's
complement encoding for the significand (not the whole word).
Details of the exponent encoding vary by documentation source.

---

## Integer-ordered floating point

The concept: encode floating-point values so that the bit pattern,
interpreted as a signed integer, preserves the numeric ordering.
This means integer comparison instructions give correct float
comparison results --- no separate float compare needed.

This property was first achieved accidentally by the DEC PDP-10
(1966) and Xerox Sigma (1966), both of which used two's complement
sign encoding with biased exponents.  It was later rediscovered and
formalized by Robert Bristow-Johnson (rbj) as a deliberate design
choice.

The key requirements: two's complement sign encoding (so negative
values have the same ordering as negative integers) and a biased
exponent (so larger exponents give larger bit patterns).  The bias
must be chosen so that the exponent field's zero point aligns with
the integer zero point.

For a 32-bit format with 8-bit exponent: same layout as IEEE binary32
[S1][E8][M23], but interpreted as two's complement.  Bias 128 (not
127).  NaN at the trap value (0x80000000, the most-negative integer).
+Inf at 0x7FFFFFFF, -Inf at 0x80000001.  No negative zero (two's
complement has only one zero).  Integer compare, integer add, and
integer subtract work directly on float values.

---

## IBM Hexadecimal floating point (HFP)

### IBM HFP single precision

32 bits.  Layout: [S1][E7][M24].  Exponent bias 64.

Introduced with the IBM System/360 (1964) and used on all subsequent
IBM mainframes (370, z/Architecture) for backward compatibility.  The
exponent base is 16, not 2: the value is (-1)^S × 0.M × 16^(E-64).

This means one exponent step shifts the significand by 4 bit positions,
not 1.  The significand is in fractional form (0.M), and there is no
implicit leading bit --- normalization requires only that the leading
hex digit be nonzero, which leaves up to 3 leading zero bits.  This
gives effectively 21-24 bits of precision, depending on the value
(compared to a consistent 24 for IEEE binary32).

No NaN, no Inf, no denormals.  Sign-magnitude.

### IBM HFP double precision

64 bits.  Layout: [S1][E7][M56].  Exponent bias 64.

Same exponent as single precision (7 bits, base 16, bias 64), with
a 56-bit significand.  Effectively 53-56 bits of precision.

### IBM HFP extended precision

128 bits.  Layout: [S1][E7][M112].  Exponent bias 64.

The widest HFP format.  Same 7-bit base-16 exponent.  Implemented
as a pair of double-precision values (high and low) with a shared
exponent.

IBM z/Architecture still supports HFP alongside IEEE 754 binary and
IEEE 754 decimal --- three complete floating-point instruction sets
in one processor.

---

## Burroughs formats

### Burroughs B5500 (1964)

48-bit word.  39-bit significand, 6-bit exponent.  Exponent bias 32.

The Burroughs B5500 used base-8 exponents: one exponent step = 3 bit
positions.  Unusual property: all integers are stored as
floating-point values with exponent 0.  There is no separate integer
format --- the hardware always uses float.

### Burroughs Medium Systems (B2500/B3500)

Variable-length decimal.  Up to 100 BCD mantissa digits.

The Burroughs Medium Systems were decimal machines designed for
business data processing.  Significand is BCD (digit_width=4).
Base-10 exponent.  The mantissa length is variable and can be very
large --- up to 100 decimal digits, far beyond any other hardware
float.

---

## BCD and packed decimal formats

### COMP-3 / packed decimal (IBM COBOL)

Variable size: (N+1)/2 bytes for N digits.  BCD nibbles with
trailing sign nibble.

The IBM mainframe standard for decimal arithmetic.  Each byte holds
two BCD digits (values 0--9 in each nibble), except the last byte,
which holds one digit and a sign nibble.  Sign values: 0xC =
positive, 0xD = negative, 0xF = unsigned.  Invalid nibble values
(0xA, 0xB, 0xE) are sometimes used as additional sign indicators.

No exponent.  The radix point position is external metadata,
specified by the COBOL PIC clause (e.g., PIC S9(5)V99 means 5
integer digits and 2 fractional digits, 4 bytes total).

Example: PIC S9(5) COMP-3 is 3 bytes (5 digit nibbles + 1 sign
nibble = 6 nibbles = 3 bytes).  The value -12345 is stored as
0x12 0x34 0x5D.

IBM z/Architecture has hardware instructions (AP, SP, MP, DP) that
operate directly on COMP-3 data in memory.

### Zoned decimal (COBOL DISPLAY)

N bytes for N digits.  One digit per byte.

Each byte has a "zone" nibble in the high 4 bits and a digit nibble
in the low 4 bits.  The zone is 0xF for EBCDIC (IBM mainframes) or
0x3 for ASCII (making the digit bytes look like ASCII '0' through
'9': 0x30--0x39).

The sign is encoded in the zone nibble of the last byte:
0xC = positive, 0xD = negative, 0xF = unsigned (EBCDIC).  This
means the last byte serves double duty as both a digit and a sign
indicator.  The digit value in the last byte is still valid; only
its zone nibble is overloaded.

Example (EBCDIC): the value -123 as PIC S9(3) is 0xF1 0xF2 0xD3.
The last byte 0xD3 means "digit 3, negative."

IBM z/Architecture has hardware PACK and UNPK instructions to
convert between zoned and packed decimal.

### Ten's complement BCD

BCD digits (digit_width=4).  No dedicated sign field.

The decimal analog of two's complement.  To negate: subtract each
digit from 9, then add 1.  The most significant digit determines
sign: 0--4 is positive, 5--9 is negative.  Example: 0x0123 = +123,
0x9877 = -123 (9877 + 0123 = 10000, which overflows to 0000).

Single representation of zero (no negative zero).  Used in some BCD
ALU designs and counter circuits.

### Nine's complement BCD

BCD digits (digit_width=4).  No dedicated sign field.

The decimal analog of one's complement.  To negate: subtract each
digit from 9 (no add-1 step).  Negative zero exists (all nines).
Example: 0x0123 = +123, 0x9876 = -123.

Simpler to compute than ten's complement (no carry propagation for
negation), but the two representations of zero complicate comparison.

### 8087 packed BCD

80 bits.  18 BCD digits + sign byte.

The Intel x87 FPU has FBLD (load packed BCD) and FBSTP (store packed
BCD) instructions that convert between 80-bit packed BCD and the
x87's internal 80-bit extended float format.  The packed BCD format
has 18 BCD digit pairs (9 bytes), a sign in the most significant
byte (0x00 = positive, 0x80 = negative), and unused bits.

This exists for COBOL compatibility on x86 --- the x87 can load a
COMP-3-like value, compute in binary floating-point, and store the
result back as packed BCD, all in hardware.

---

## Calculator formats

### HP calculator format

56 bits (7 bytes).  12 BCD significand digits + 3 BCD exponent digits
+ 2 sign nibbles.

Hewlett-Packard scientific calculators (HP-35, HP-65, HP-41C, etc.)
used a distinctive BCD floating-point format.  The value sign is an
explicit nibble: 0 = positive, 9 = negative (not 0xC/0xD as in
COMP-3).

The exponent uses thousand's complement: a 3-digit radix-10 field
where the sign is implicit (000--499 = positive, 500--999 =
negative).  This is radix complement applied to radix 10 on a
3-digit field --- the same mathematical operation as two's complement
on a binary field.

The significand and exponent have different sign methods within
one format.  No NaN, no Inf, no denormals.

### TI-89 calculator format

80 bits (10 bytes).  14 BCD significand digits + 15-bit binary
exponent + 1-bit sign.

Texas Instruments TI-89 (and TI-92, Voyage 200).  A mixed-radix
format: the significand is BCD (digit_width=4, radix=10), but the
exponent is a 15-bit binary integer (digit_width=1, radix=2) with
bias 0x4000.  The exponent base is 10 (it represents powers of 10)
despite being stored in binary.

The sign is a single bit --- unusual for a BCD format, where the
sign is typically a nibble.  This means the sign field is narrower
than the significand's digit width.

### Casio and Sharp calculators

Various BCD formats.  Generally similar in structure to HP: BCD
significand, BCD exponent, explicit sign nibbles.  Details vary by
model.  Fewer digits than HP in entry-level models; more in
graphing calculators.

---

## IEEE 754 decimal floating point

### Decimal32 (DPD encoding)

32 bits.  7 significand digits + 8-bit exponent + sign.

The significand is logically 7 decimal digits.  Physically, the
leading digit and 2 exponent MSBs are jointly encoded in a 5-bit
"combination field" --- the same 5 bits mean different things
depending on their value.  The remaining 6 digits are stored as
two DPD (Densely Packed Decimal) groups of 3 digits in 10 bits each
(20 bits total, vs 24 bits for raw BCD).

DPD is a compression codec: it packs 3 decimal digits (0--999) into
10 bits (2^10 = 1024 > 1000).  The decode table is defined in IEEE
754-2008.  Full IEEE special values.

### Decimal64 (DPD encoding)

64 bits.  16 significand digits + 10-bit exponent + sign.

Same structure as Decimal32, scaled up.  5 groups of 3 DPD-encoded
digits (50 bits) + 1 leading digit in the combination field.
Implemented in hardware on IBM z/Architecture (z9 and later) and
IBM POWER6+.

### Decimal128 (DPD encoding)

128 bits.  34 significand digits + 14-bit exponent + sign.

The largest IEEE 754 decimal format.  11 DPD groups + 1 leading
digit.  Implemented in hardware on IBM z/Architecture.

### Decimal32/64/128 (BID encoding)

Same logical formats as DPD, but the significand is stored as a
binary integer.  Decimal64 BID stores the 16-digit decimal
significand as a binary integer in 50 bits (enough to hold 10^16 -
1 = 9999999999999999).

BID is used by Intel's decimal floating-point library (libdfp) and
is the default encoding on x86.  DPD is used by IBM hardware.  The
two encodings are interconvertible and represent the same values ---
they differ only in the physical storage of the significand.

The BID encoding means the storage radix (binary) differs from the
arithmetic radix (decimal).  Unpacking requires a binary-to-decimal
conversion.

---

## Variable-field formats

### Posits (Gustafson, 2015)

Fixed total width, variable field boundaries.  Common sizes:
posit\<8,0\>, posit\<16,1\>, posit\<32,2\>, posit\<64,3\>.

A posit\<n, es\> has n total bits and a maximum of es exponent
supplement bits.  The fields are:

- **Sign**: 1 bit (MSB).  Negative values are two's complement of
  the whole word.
- **Regime**: a run of identical bits terminated by the opposite bit
  (or end of word).  Length is value-dependent (1 to n-1 bits).  A
  run of k ones encodes regime value k-1.  A run of k zeros encodes
  regime value -k.
- **Exponent**: the next es bits (or fewer, if the regime consumed
  most of the word).
- **Fraction**: whatever bits remain.  May be zero bits.

The effective exponent is: useed^regime × 2^exponent, where
useed = 2^(2^es).

Special values: zero is all-zeros.  NaR (Not-a-Real) is 1 followed
by all zeros (the most-negative two's complement value).  No
infinity.  No negative zero.

The key property: dynamic precision.  Values near 1.0 have short
regimes and many fraction bits (high precision).  Values near the
extremes have long regimes and few or no fraction bits (wide range,
low precision).  This is an automatic trade-off, not a bug.

Posit arithmetic is binary floating-point arithmetic --- after
unpacking, the compute step is identical to IEEE.  The regime is a
storage encoding, not an arithmetic difference.

### Tapered floating point (Morris, 1971)

Variable total width or fixed width with variable field allocation.

Robert Morris (Bell Labs) proposed this in 1971: encode the exponent
width in the number itself, so extreme values get more exponent bits
at the expense of significand bits.  The concept is the intellectual
ancestor of posits.  Morris's specific encoding uses a unary-coded
length prefix for the exponent, similar to but not identical to
Gustafson's regime encoding.

### Type I Unums (Gustafson, 2012)

Variable field widths with explicit size fields.

Gustafson's original "Universal Numbers" (before posits).  Both the
exponent and fraction widths are variable, and their widths are
encoded as explicit binary integers stored within the word.  This
makes the word self-describing but costs bits for the size fields.
Superseded by posits, which achieve variable fields without
dedicating bits to explicit size metadata.

---

## Fixed-point formats

### Q-format (Qm.n)

Fixed total width.  m integer bits + n fractional bits.

Standard DSP notation.  Q15.16 means 15 integer bits + 16 fractional
bits = 31 data bits + 1 sign bit = 32 bits total.  Sign is two's
complement.  The radix point position is a property of the type, not
stored in the value.

Common in audio processing, control systems, and any application
where floating-point hardware is unavailable or too expensive.  ARM
Cortex-M0 (no FPU) uses Q-format arithmetic extensively.

### Integers

Degenerate fixed-point with no fractional bits.

- **int8/16/32/64**: two's complement.  -2^(n-1) to 2^(n-1)-1.
- **uint8/16/32/64**: unsigned.  0 to 2^n - 1.

Included because they are a natural endpoint of the fixed-point
spectrum and share the same sign methods (RadixComplement, Unsigned).

---

## String and character formats

### Decimal string ("-1.23e4")

Variable-length.  One ASCII (or EBCDIC or UTF-8) character per
element.

A human-readable representation of a decimal number.  Contains four
kinds of characters:

- **Digit characters**: '0' through '9' (ASCII 0x30--0x39).
- **Sign characters**: '+' or '-', typically leading.
- **Decimal point**: '.'.
- **Exponent indicator**: 'e' or 'E', followed by an optional sign
  and exponent digits.

The decimal point and exponent indicator are structural delimiters
--- they are neither digits nor sign nor exponent.  They tell the
parser where one field ends and another begins.  No other numeric
format has delimiter characters.

Multiple representations of the same value are possible: "1.0e2",
"100", "100.0", "+1.00e+02" all represent 100.

Used everywhere: JSON, CSV, XML, printf, scanf, user input, logging.
The single most common numeric format in the world by volume of data,
and the least efficient by bits per digit of precision.

---

## Alternative number systems

### Balanced ternary

Trits (ternary digits), each with value {-1, 0, +1}.

Implemented in real hardware: the Setun computer (Moscow State
University, 1958, approximately 50 built).  Place values are powers
of 3: 1, 3, 9, 27, 81, ...  The value -5 is written T11 (T
represents -1): -9 + 3 + 1 = -5.

Sign is inherent --- determined by the most significant non-zero
trit.  No sign field needed.  To negate, swap T and 1 in every
position.

A trit is not directly representable in binary.  On binary hardware,
each trit requires 2 bits (wasting one of four states), or trits can
be encoded in groups.

A proposed floating-point format: Ternary27 (27 trits: 1 padding
trit + 5-trit exponent + 21-trit significand).

### Negabinary (base -2)

Binary digits {0, 1}, base -2.

Place values alternate in sign: 1, -2, 4, -8, 16, -32, 64, ...
The value -3 is 1101: 1×(-8) + 1×4 + 0×(-2) + 1×1 = -3.  Both
positive and negative integers have natural representations using
only {0, 1} --- no sign field or complementation needed.

Used in zfp floating-point compression (Lawrence Livermore National
Laboratory) because small-magnitude values have many leading zeros
regardless of sign, which aids compression.

---

## Exotic and tagged formats

### BRLESC (Ballistic Research Laboratory)

68-bit word.  3-bit tag + 56-bit significand + 8-bit exponent.
Base-16 exponent.

The 3-bit tag is a type discriminant: it tells the hardware what
kind of value the word contains (floating-point, integer, address,
instruction, etc.).  The tag is not part of the numeric value --- it
is metadata that selects the interpretation.  This is a hardware
union/tagged-union: the same 68-bit word can be a float, an integer,
or something else entirely, and the tag says which.

### MX / Microscaling block formats

See "Block and shared-exponent formats" above.  The MX specification
(OCP, 2023) defines a family of block formats where elements share a
scale factor.  Each element is a tiny float (FP4, FP6, or FP8); the
shared exponent is FP8 E8M0 (a pure power of two with no
significand).

Not a single-value format --- it is a composition of a block of
values with shared metadata.
