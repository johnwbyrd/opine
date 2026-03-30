# OPINE — Project Guide

## What This Is

OPINE (Optimized Policy-Instantiated Numeric Engine) is a C++20 header-only
library for compile-time configurable floating-point arithmetic. It targets
everything from 8-bit 6502 microprocessors to modern GPUs.

## Current State

**Clean rewrite in progress.** The previous implementation lives on the
`previous` git branch. This `main` branch is an orphan — fresh start,
no history from the old code.

The old code had ~865 lines of headers with working pack/unpack, two
rounding policies, denormal policies, and type selection. All passing
exhaustive FP8 tests. It was discarded because the six-axis redesign
changes every template signature — incremental refactoring would cost
more than rewriting. Backwards compatibility is an anti-feature.

## Architecture: Six Axes + ComputeFormat

Read `docs/design/design.md` for the full spec. Summary:

```cpp
Type<Number, Box, Layout, Rounding, Exceptions, Platform>
```

**Axis 1 — Number** (what one value is): Primitive or composite.
A primitive Number is a digit sequence with radix, digit_width,
digit_count, and sign_method. A composite Number assembles
sub-Numbers: FloatingPoint (significand + exponent),
FixedPoint (significand + radix position), SharedExponent
(MXFP blocks), Codebook (NF4 lookup tables). The significand
and exponent are separate Numbers with independent properties.

**Axis 2 — Box** (how many, arranged how): Scalar (`Box<>`),
vector (`Box<8>`), matrix (`Box<4,4>`), cube (`Box<4,4,4>`).
Carries both logical dimensions and physical memory arrangement
(stride, row-major/column-major, SWAR packing, SIMD mapping).
Orthogonal to Number — any Number can fill any Box.

**Axis 3 — Layout** (how one Number maps to storage): Field
positions, byte ordering, packing codecs (DPD, BID, implicit
leading digit). `total_size` is compile-time for fixed-width
formats, Variable for strings. Fixed-width formats pay nothing
for the existence of variable-width formats.

**Axis 4 — Rounding**: Guard digit count and rounding algorithm.
Independent of everything else. TowardZero, ToNearestTiesToEven,
ToNearestTiesAway, TowardPositive, TowardNegative, ToOdd.

**Axis 5 — Exceptions**: What happens on errors. Silent (default),
StatusFlags, ReturnStatus, Trap. Determines the API surface of every
arithmetic function.

**Axis 6 — Platform**: Target hardware identity for template
specialization. Structural parameters (word width, register depth)
for algorithmic decisions. Hardware capabilities expressed through
template specializations, not boolean flags. Generic software
implementations for everything; platforms provide specializations
the compiler prefers via partial matching.

**ComputeFormat** (NOT an axis): Parameter of operations, not values.
Works in digits, not bits. Specifies exponent digits, significand
digits, guard digits for intermediate computation. Usually derived
from the Type via defaults. Overridable for reduced-precision (fast
6502) or extended-precision (Kahan intermediates) computation.

## TDD Sequence

Read `docs/design/tdd.md` for the full spec. The 12-step build order:

1. Oracle Part 1 — MPFR integration (exact mathematical results)
2. Oracle Part 2 — Policy application layer (~200 lines)
3. Oracle non-IEEE validation (property tests)
4. Number + Layout + pack/unpack (the library core)
5. TestFloat shim (IEEE 754 conformance)
6. Comparison
7. Negate, abs
8. Addition
9. Multiplication
10. Subtraction and division
11. Format conversion
12. Exception flags

Steps 1-3 build the test oracle. Steps 4-12 build the library against it.
Every step has exhaustive FP8 testing (256 values, 65536 pairs).

## Key Design Docs

- `docs/design/design.md` — Six-axis architecture, ComputeFormat,
  operation pipeline, predefined bundles
- `docs/design/catalog.md` — Number decomposition for every known format
- `docs/design/menagerie.md` — Encyclopedic catalog of numeric formats
- `docs/design/decision-tree.md` — Decision tree to identify any format
- `docs/design/problems.md` — Failures of the previous five-axis design
- `docs/design/tdd.md` — Oracle methodology, test harness, 12-step sequence
- `docs/design/twos-complement.md` — Integer-ordered two's complement FP
- `docs/design/bits.md` — Guard/Round/Sticky bit mechanics
- `docs/design/research.md` — Distilled guidance from 10 foundational papers

## Directory Structure

```
include/opine/          — Header-only library
tests/oracle/           — MPFR-based reference oracle (test-only dependency)
tests/unit/             — Unit tests
docs/design/            — Living design specifications
docs/reference/         — Reference code and papers
```

## Build

```bash
cmake -B build -S . -DCMAKE_BUILD_TYPE=Release
cmake --build build
cd build && ctest --output-on-failure
```

C++20 required. Clang 18+ preferred (_BitInt support). GCC 13+ and MSVC
also supported with fallback types.

## Coding Conventions

- `.clang-format`: LLVM style, LF line endings
- `.clang-tidy`: LLVM + misc checks, CamelCase naming
- All configuration via compile-time templates. Zero runtime overhead.
- `static_assert` for compile-time validation of policy constraints.
- Exhaustive testing for FP8 formats (256 values / 65536 pairs).

## Number Properties (opine namespace)

```
sign_method:   Explicit | RadixComplement | DiminishedRadixComplement |
               Inherent | Unsigned
radix:         int (2, 3, 10, -2, ...)
digit_width:   int (1, 4, 8 bits per digit)
digit_count:   int | Variable
```

## Floating-Point Properties

```
exponent_base:   int (2, 8, 10, 16)
implicit_digit:  bool
negative_zero:   Exists | DoesNotExist
nan_encoding:    ReservedExponent | TrapValue | NegativeZeroBitPattern | None
inf_encoding:    ReservedExponent | IntegerExtremes | None
denormal_mode:   Full | FlushToZero | FlushInputs | FlushBoth | None
```
