# OPINE

Compile-time configurable floating-point arithmetic for C++20.

## What

OPINE is a header-only C++20 library that treats a floating-point type as
a composition of six orthogonal axes: what one value **is** (Number), how
it maps to bits (Layout), plus Rounding, Exceptions, Platform, and
(deferred) Box. Every operation is a template parameterized on the
resulting `Type`, so choosing IEEE 754 binary32, rbj's integer-ordered
two's-complement FP8, AMD's E4M3FNUZ, or a saturating FP16 with flushed
denormals is a compile-time decision that produces zero-overhead
specialized code.

The point isn't to be another arbitrary-precision or soft-float library;
it's to make format engineering — the choices IEEE 754 makes for you —
explicit, parametric, and testable. Every arithmetic operation in the
library is cross-checked against a MPFR-based reference oracle,
Berkeley SoftFloat, and Berkeley TestFloat, exhaustively at FP8 and
extensively at FP16 through FP128.

## Look

```cpp
#include <opine/opine.hpp>

using namespace opine;

// Predefined bundles cover every currently supported IEEE 754 width,
// plus bfloat16, FP8 (E5M2 / E4M3 / E4M3FNUZ), and x87 extended80.
using f32 = float32;                  // IEEE 754 binary32, RNDN
using fp8 = fp8_e4m3;                 // OCP MX FP8 E4M3

// rbj's integer-ordered two's-complement FP8: same IEEE-shaped layout,
// different Number. Comparison degenerates to signed-integer compare.
using rbj8 = RbjType<4, 3>;

// Or roll your own:
using saturating_fp16 = Type<
    numbers::GPUStyle<5, 10>,          // IEEE special values, no denormals
    layouts::IEEE<5, 10, true>,
    rounding::TowardZero
>;

// Operations take and return storage bits.
f32::storage_type a = /* ... */;
f32::storage_type b = /* ... */;
auto sum  = add<f32>(a, b);
auto prod = mul<f32>(a, b);
auto quot = div<f32>(a, b);
bool less = lt<f32>(a, b);

// Conversion spells BOTH Types at the call site, destination first
// (reads like a cast). The source can't be deduced from the argument:
// distinct Types share a storage width — fp8_e5m2 and fp8_e4m3 are
// both 8 bits. Rounding is the destination's Rounding axis.
auto h    = convert<f32, fp8>(a);         // FP8 → binary32, exact
auto back = convert<fp8, f32>(sum);       // binary32 → FP8, rounds

// Native bridges (bit_cast + convert) are the intended way to get
// values in and out.
auto third = fromNative<fp8>(1.0f / 3.0f);  // 0x2B: 0.34375
float f    = toFloat<fp8>(third);
```

For longer runnable programs — the rbj signed-integer-sort demo, a
cross-format quantization tour, the §7.4 overflow matrix, a
compile-time axis table across all predefined Types — see
[`examples/`](examples/README.md).

## Axes

`Type<Number, Layout, Rounding, Exceptions, Platform>` — Box is
defined in the design but not yet expressed in code; all values are
scalar today.

- **Number** — what one numeric value is. A primitive (`radix`,
  `digit_width`, `digit_count`, `sign_method`) or a composite
  (`FloatingPoint` today; `FixedPoint`, `SharedExponent`, and `Codebook`
  are cataloged, not implemented). This is where "IEEE has NaN at
  reserved exponent", "rbj has NaN at the trap value", and "E4M3FNUZ has
  no negative zero" live.
- **Layout** — how one Number maps to bits. Fixed-width today, with
  field offsets and `implicit_digit`. DPD / BID / posit-regime / variable
  layouts are in the design space, not the code.
- **Rounding** — `TowardZero`, `ToNearestTiesToEven` (default),
  `TowardPositive`, `TowardNegative`. `ToNearestTiesAway` and `ToOdd` are
  declared, not yet wired through the oracle.
- **Exceptions** — `Silent` (default), `StatusFlags`, `ReturnStatus`,
  `Trap`. The type-system machinery exists; the runtime plumbing lands
  in TDD step 12.
- **Platform** — identity + structural parameters (`machine_word_bits`,
  `type_policy`). Hardware capabilities are declared as booleans today
  and will migrate to template specializations when specialized backends
  land.

Full architecture is in [`docs/design/design.md`](docs/design/design.md);
the format catalog is in [`docs/design/catalog.md`](docs/design/catalog.md);
the identification decision tree is in
[`docs/design/decision-tree.md`](docs/design/decision-tree.md).

## What works

| Operation | FP8 (7 variants) | bfloat16 / FP16 / FP32 / FP64 | extFloat80 | float128 |
|-----------|:---:|:---:|:---:|:---:|
| pack / unpack | ✓ exhaustive | ✓ | ✓ (canonicalizes non-canonical x87) | ✓ |
| eq / lt / le  | ✓ exhaustive | ✓ | ✓ | ✓ |
| neg / abs     | ✓ exhaustive | ✓ | ✓ | ✓ |
| add           | ✓ exhaustive | ✓ | ✓ | ✓ |
| sub           | ✓ exhaustive | ✓ | ✓ | ✓ |
| mul           | ✓ exhaustive | ✓ | ✓ | — |
| div           | ✓ exhaustive | ✓ | — | — |
| convert       | ✓ exhaustive (all 49 encoding pairs) | ✓ | ✓ sampled | ✓ sampled |

"Exhaustive" at FP8 means all 65,536 input pairs cross-checked against
the MPFR oracle for every encoding. Wider formats run structural +
per-binade stratified + random against the oracle via
[`GenericBinaryFpTest<T>`](tests/harness/generic_binary_test.hpp);
adding a new format or op is one line each. `mul_supported<T>` and
`div_supported<T>` gate the 128-bit working-type limit at compile time —
extFloat80 division and float128 mul/div need a multi-word scheme
(follow-up).

`convert<Dst, Src>` works between **any** two supported Types — every
width pair fits the 128-bit working type, so the table has no gaps.
NaN converts to the destination's canonical quiet NaN (payloads are
not propagated); Inf into a format with no Inf encoding saturates to
max finite; −0 into a format without −0 is +0. Where
`exact_conversion<Src, Dst>` holds (e.g. every FP8 → FP16, FP16 →
FP32, bfloat16 → FP32, extFloat80 → float128), round-tripping is the
identity on every non-NaN bit pattern — verified exhaustively.
Chained conversions may double-round versus a direct one; convert
directly.

**Not implemented:** exception flags (step 12), float↔integer and
string conversion, elementary functions, and the Box axis.
`DiminishedRadixComplement` value_sign (CDC 6600) parses but has no
arithmetic pipeline yet.

**Encodings supported end-to-end:** Explicit + ReservedExponent (IEEE
754), Explicit + NegativeZeroBitPattern (E4M3FNUZ), Explicit + None
(saturating), Explicit-J-bit (x87 extended80), RadixComplement +
IntegerExtremes (rbj / PDP-10), with input- and output-denormal flushing
via `FlushInputs` / `FlushToZero` / `FlushBoth`.

## Correctness

The library's testing story is what makes non-IEEE encodings tractable
to build. Three independent references:

- **MPFR oracle.** [`tests/harness/impl_mpfr.hpp`](tests/harness/impl_mpfr.hpp)
  decodes any bit pattern under any `Number`+`Layout` to an exact
  256-bit MPFR value, performs the exact arithmetic, and re-encodes
  using the Type's `Rounding` policy — including the IEEE 754 §7.4
  overflow rule (Inf only when the mode carries the magnitude upward)
  and §6.3 signed-zero rules. The re-encode branches on every
  `nan_encoding` / `inf_encoding` / `value_sign` / `denormal_mode`
  combination the axes admit.
- **Berkeley SoftFloat.** Fetched, patched, and linked into
  [`test_oracle.cpp`](tests/oracle/test_oracle.cpp) as an independent
  IEEE 754 reference for float16 / float32 / float64 / extFloat80 /
  float128.
- **Berkeley TestFloat.** The `genCases` generator drives
  [`test_opine_testfloat.cpp`](tests/testfloat/test_opine_testfloat.cpp)
  across FP16 and FP32 for add / sub / mul / div under all four
  supported rounding modes, plus eq / lt / le — the same conformance
  battery hardware FPU vendors run.

Backing these up:

- **Property tests** for non-IEEE encodings
  ([`test_oracle_nonstd.cpp`](tests/oracle/test_oracle_nonstd.cpp)):
  rbj's monotonic-ordering law (signed-int compare equals FP compare),
  E4M3FNUZ's single-NaN invariant, Relaxed's no-NaN / no-Inf invariant,
  and exhaustive decode → encode → decode round-trips.
- **Rounding-mode tests** ([`test_oracle_rounding.cpp`](tests/oracle/test_oracle_rounding.cpp))
  verify the oracle's four rounding modes on hand-worked tie and 3/4
  values, positive and negative — the case that isolates the
  `abs`-mode-swap for directed rounding.
- **Generic width-scaling framework** ([`generic_binary_test.hpp`](tests/harness/generic_binary_test.hpp)):
  every binary op runs the same
  structural + stratified + random battery, sized to the format's
  `total_bits`. Adding an op is one adapter case; adding a Type is
  one line in the test's `TEST_CASE_TEMPLATE`.

Test-suite methodology is documented in
[`docs/design/tdd.md`](docs/design/tdd.md). 13 test binaries, all
passing on Ubuntu clang-18, Ubuntu gcc-13, and macOS AppleClang.

## Build

```bash
cmake -B build -S . -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

Set `-DOPINE_REQUIRE_ORACLE=ON` to fail configuration when MPFR is
absent; CI uses this to prevent silent oracle skips.

**Requirements:** C++20. Clang 18+ (uses `_BitInt(N)` for exact-width
storage) or GCC 13+ (falls back to `__int128`, capped at 128 bits).
MSVC is not supported. MPFR + GMP are needed for the oracle tests
(`apt install libmpfr-dev libgmp-dev` on Debian/Ubuntu, `brew install
mpfr gmp` on macOS). SoftFloat and TestFloat are fetched from source
by CMake.

## Layout

```
include/opine/          — Header-only library
  core/
    number.hpp          — Axis 1: what one value is (Primitive, FloatingPoint)
    layout.hpp          — Axis 3: how one Number maps to bits
    type.hpp            — Type<...> template + predefined bundles
    rounding.hpp        — Rounding policies
    exceptions.hpp      — Exception policies
    platform.hpp        — Platform identity + structural parameters
    bits.hpp            — bits_t<N> + width-safe maskLow
    compute_format.hpp  — Operation-level precision parameter
    pack_unpack.hpp     — bits ↔ canonical UnpackedFloat
    round_pack.hpp      — Shared pipeline: prologue + roundAndPack epilogue
    compare.hpp         — eq / lt / le
    neg_abs.hpp         — neg / abs
    add.hpp / sub.hpp / mul.hpp / div.hpp
    convert.hpp         — convert<Dst, Src> + native float/double bridges
    arith_detail.hpp    — Shared G/R/S + overflow-mode helpers
tests/
  oracle/               — MPFR + SoftFloat cross-validation
  testfloat/            — TestFloat conformance shim
  harness/              — Test framework: adapters, iterators, generic runner
docs/
  design/               — Six-axis architecture, TDD methodology, format catalog
  reference/            — Papers (IEEE 754-2019 summary), reference C sources
examples/               — Eight self-contained showcase programs
```

## Design docs

- [`design.md`](docs/design/design.md) — the six axes
- [`problems.md`](docs/design/problems.md) — what the previous five-axis
  design couldn't express and why
- [`tdd.md`](docs/design/tdd.md) — testing methodology, oracle design,
  the 12-step build sequence
- [`catalog.md`](docs/design/catalog.md) — Number decomposition for
  every known format
- [`menagerie.md`](docs/design/menagerie.md) — encyclopedic catalog of
  numeric formats
- [`decision-tree.md`](docs/design/decision-tree.md) — a decision tree
  that identifies any format
- [`twos-complement.md`](docs/design/twos-complement.md) — rbj's
  integer-ordered representation
- [`research.md`](docs/design/research.md) — guidance distilled from
  10 foundational floating-point papers
- [`bits.md`](docs/design/bits.md) — guard / round / sticky mechanics

## License

See [LICENSE](LICENSE).
