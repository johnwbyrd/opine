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
exhaustive FP8 tests. It was discarded because the five-axis redesign
changes every template signature — incremental refactoring would cost
more than rewriting. Backwards compatibility is an anti-feature.

## Architecture: Five Axes + ComputeFormat

Read `docs/design/design.md` for the full spec. Summary:

```cpp
Float<Format, Encoding, Rounding, Exceptions, Platform>
```

**Axis 1 — Format** (bit geometry): Field widths and positions. Pure
structure. Says nothing about meaning. `IEEE_Layout<E, M>` convenience
alias for standard [S][E][M] ordering.

**Axis 2 — Encoding** (what bit patterns mean): Sign encoding
(sign-magnitude, two's complement, one's complement), implicit bit,
exponent bias, negative zero, NaN encoding, infinity encoding, denormal
mode. Sub-parameters interact; constraints enforced at compile time via
`ValidEncoding` concept. Predefined bundles: IEEE754, RbjTwosComplement,
PDP10, CDC6600, E4M3FNUZ, Relaxed, GPUStyle.

**Axis 3 — Rounding**: Guard bit count and rounding algorithm.
Independent of everything else. TowardZero (0 guard bits), ToNearestTiesToEven
(3: G,R,S), ToNearestTiesAway (3), TowardPositive (1), TowardNegative (1).

**Axis 4 — Exceptions**: What happens on errors. Silent (default),
StatusFlags, ReturnStatus, Trap. Determines the API surface of every
arithmetic function.

**Axis 5 — Platform**: Hardware capabilities. Type selection policy,
machine word width, instruction availability. Determines SWAR lane count
and implementation strategy as derived properties.

**ComputeFormat** (NOT a sixth axis): Parameter of operations, not values.
Specifies exponent bits, mantissa bits, guard bits for intermediate
computation. Usually derived from the Float type via DefaultComputeFormat.
Overridable for reduced-precision (fast 6502) or extended-precision
(Kahan intermediates) computation.

## TDD Sequence

Read `docs/design/tdd.md` for the full spec. The 12-step build order:

1. Oracle Part 1 — MPFR integration (exact mathematical results)
2. Oracle Part 2 — Policy application layer (~200 lines)
3. Oracle non-IEEE validation (property tests)
4. Format + Encoding + pack/unpack (the library core)
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

- `docs/design/design.md` — Five-axis architecture, ComputeFormat,
  operation signatures, encoding bundles, refactoring path (was redesign.md)
- `docs/design/tdd.md` — Oracle methodology, test harness, 12-step sequence
- `docs/design/twos-complement.md` — rbj's two's complement FP representation
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

## Enums (opine namespace)

```
SignEncoding:  SignMagnitude | TwosComplement | OnesComplement
NegativeZero:  Exists | DoesNotExist
NanEncoding:  ReservedExponent | TrapValue | NegativeZeroBitPattern | None
InfEncoding:  ReservedExponent | IntegerExtremes | None
DenormalMode:  Full | FlushToZero | FlushInputs | FlushBoth | None
AutoBias = -1  (sentinel: compute bias from exponent width)
```
