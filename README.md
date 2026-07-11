# OPINE

One library that speaks every floating-point format — correctly,
provably, and at any size from 8 bits to 1024. Floating point is a
set of opinions; OPINE lets you hold your own.

## Why this exists

For decades, "floating point" meant two types: `float` and `double`.
That world is gone. Machine learning brought FP8 (in at least three
incompatible flavors), bfloat16, and FP16. Old hardware left us x87's
80-bit format and stranger things. Researchers keep proposing new
encodings. And when you need more precision than `double`, you're
suddenly shopping for a whole different category of library.

Each of these formats is its own little island. They disagree about
where the bits go, what NaN looks like, whether negative zero exists,
what happens on overflow. Converting between them is ad-hoc. Testing
against them is worse — most software support for the newer formats
ships with a shrug where the verification should be.

Here's the thing those formats have in common: every one of them is
a **bundle of opinions**. How much precision is worth paying for.
Whether NaN deserves bit patterns. What should happen when a result
doesn't fit. Whether the tiny values near zero earn their hardware
cost. IEEE 754 is a superb, battle-tested set of answers — but it is
a set of answers, chosen in committee, not a law of nature. The name
is the thesis: OPINE hands the opinions back to you.

Concretely, it's a single, header-only C++20 library where **every
format is the same machinery with different settings**. IEEE binary32
and a two's-complement FP8 and a 1024-bit float with 997 bits of
precision are all just parameter choices, fed through one arithmetic
pipeline that has been verified against independent references —
exhaustively, for every possible input, wherever the format is small
enough to make that possible. And the choices genuinely are yours,
including the deliberately relaxed ones: a float with no NaN, no
infinity, truncation instead of rounding, and small values flushed
to zero is a legitimate engineering tradeoff — OPINE will execute it
faithfully and tell you exactly what it costs versus the careful
answer.

That combination is the point. Libraries that are *fast* for one
format exist. Libraries that are *big* exist. What hasn't existed is
one place where all of these formats are:

- **Complete** -- arithmetic, comparison, conversion, printing,
  parsing, exception flags — the whole IEEE 754 operation set
- **Demonstrably correct**, AND
- **FAST**, with the lion's share of the computation and
  error-checking, happening at compile time.

Now you get all that side by side, under a single API.

## What you can do with it

- **Run the same algorithm at any precision.** Write it once, then
  instantiate at FP8, binary32, binary64, binary256, binary1024.
  Watch exactly where your algorithm stops working — or starts.
- **Convert anything to anything.** `convert<Dst, Src>(x)` works
  between any two supported formats, with correct rounding and
  sensible handling of the awkward cases (NaN into a format with no
  NaN, infinity into a format that saturates instead).
- **Study quantization honestly.** Simulate FP8 or your own custom
  format *bit-exactly* — the values your model will actually see —
  instead of approximating with `float` and hoping.
- **Prototype hardware formats before the hardware exists.** A new
  12-bit float is one `using` declaration, and every operation,
  conversion, and test in the library immediately works on it.
- **Pick your tradeoffs — including the "sloppy" ones.** Truncate
  instead of rounding. Flush denormals. Drop NaN and infinity
  entirely and saturate. If your workload never sees those cases,
  that's not wrong, it's a decision — and because the strict and
  relaxed formats live in one library, you can measure exactly what
  the shortcut costs on your own data.
- **Print and parse without losing anything.** `toString` produces
  correctly rounded decimals at any width (all 300 digits of a
  binary1024 value, if you ask); `fromString` parses back with
  correct rounding in the format's own rounding mode.
- **Catch the events IEEE 754 says you should be able to catch.**
  Overflow, underflow, division by zero, invalid operations, and
  inexact results are reported through a policy you pick: silently
  discarded, accumulated in sticky flags, or returned with every
  result.

## Thirty seconds of code

```cpp
#include <opine/opine.hpp>
using namespace opine;

// Pick formats. These are all ordinary types, resolved at compile time.
using f32 = float32;      // IEEE 754 binary32 — the familiar `float`
using fp8 = fp8_e4m3;     // 8-bit float used in ML inference
using f1k = float1024;    // IEEE binary1024: 997 bits of precision

// Values live as bit patterns; fromNative gets you in from a float.
auto a = fromNative<f32>(1.5f);
auto b = fromNative<f32>(2.25f);

// The full operation set, all correctly rounded:
auto sum  = add<f32>(a, b);
auto prod = mul<f32>(a, b);
auto root = sqrt<f32>(sum);
auto fused = fma<f32>(a, b, sum);     // one rounding, genuinely fused
bool less  = lt<f32>(a, b);

// Convert between formats — destination first, like a cast:
auto tiny = convert<fp8, f32>(sum);   // squeeze into 8 bits, rounds
auto wide = convert<f1k, f32>(sum);   // exact: every f32 fits

// And back out to something you can print:
float f = toFloat<f32>(sum);          // 3.75
auto s  = toString<fp8>(tiny);        // "3.75" — it happens to fit!
auto pi = fromString<f1k>("3.14159265358979323846264338327950");
```

If you want the step-by-step version of everything above — including
how to add OPINE to your build in the first place — start with the
**[tutorial](docs/design/tutorial.md)**.

## Try it in two minutes

```bash
git clone https://github.com/johnwbyrd/opine.git
cd opine
cmake -B build -S . -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

That builds the tests and thirteen small example programs. A good tour:

- [`01_hello`](examples/01_hello.cpp) — the basic vocabulary.
- [`03_rbj_sort`](examples/03_rbj_sort.cpp) — a party trick with a
  point: a two's-complement float format whose values sort correctly
  when you treat the bytes as plain signed integers. `std::sort` on
  `int8_t`, floating-point order comes out.
- [`05_quantize`](examples/05_quantize.cpp) — what really happens to
  your values in FP8, format by format.
- [`11_fma_fusion`](examples/11_fma_fusion.cpp) — why fused
  multiply-add exists, in two acts: the answer a rounded multiply
  erases and `fma` keeps, then the one-liner that recovers a
  multiply's exact rounding error.
- [`13_exact_decimal`](examples/13_exact_decimal.cpp) — what "0.1"
  really stores at each width (every digit exact), and sqrt(2)
  computed and printed from float32 up to binary1024.
- [`09_rump`](examples/09_rump.cpp) — a famous polynomial that gives
  confidently wrong answers in `double`, the *wrong sign* in
  float128, and only comes right at float256. Precision matters, and
  now you can turn the knob.
- [`10_pi_bbp`](examples/10_pi_bbp.cpp) — π computed at every
  precision from float32 through float1024. Same code, six widths,
  300 correct digits at the top.

The full list is in [`examples/README.md`](examples/README.md).

## How it works, briefly

OPINE's core idea: a floating-point type isn't one thing, it's a
bundle of independent decisions. The library makes each decision a
separate template parameter:

```
Type<Number, Layout, Rounding, Exceptions, Platform>
```

- **Number** — what a value *means*: how many digits of precision,
  how the sign works, whether NaN / infinity / negative zero exist
  and how they're encoded.
- **Layout** — where the bits physically go.
- **Rounding** — what happens to digits that don't fit. All six
  classic modes, including round-to-odd (the one you want for
  intermediate results that will be rounded again).
- **Exceptions** — how overflow, underflow, and friends are
  reported: silently, via sticky flags, or attached to each result.
- **Platform** — machine-specific tuning knobs.

Every operation is one shared pipeline — unpack the bits, compute
exactly, round once, pack the bits — specialized at compile time for
the exact `Type` you chose. There is no runtime dispatch, no
allocation, and no format-specific code to get out of sync: an
oddball format is the *same code* as binary64 with different
parameters. That's also why there is no size ceiling — arithmetic
runs on arrays of machine words sized to the job, so binary1024
works on any supported compiler.

The deeper story is in [`docs/design/design.md`](docs/design/design.md).

## Can you trust the math?

This is the part of the project we're proudest of. Floating-point
code is notoriously easy to get subtly wrong, so OPINE treats
verification as a first-class feature:

- **An independent oracle.** Every operation is cross-checked against
  a reference built on [MPFR](https://www.mpfr.org/) (the
  arbitrary-precision library trusted throughout the numerical
  community). The oracle decodes bit patterns, computes the exact
  answer at hundreds of bits, and rounds it back — through completely
  different code than the library itself.
- **Exhaustive where exhaustive is possible.** For 8-bit formats,
  the tests don't sample — they check *every possible pair of
  inputs* (all 65,536 combinations) for every operation, every
  encoding, and every rounding mode, bit for bit, flags included.
- **Sampled hard where it isn't.** Wider formats run structured
  sweeps: every boundary value, every exponent range, plus large
  random batteries — up through binary1024.
- **Second and third opinions.** IEEE formats are additionally
  checked against Berkeley SoftFloat, and run through Berkeley
  TestFloat — the same conformance suite hardware FPU vendors use.

The references have caught real bugs in the library during
development, and the library has caught subtle bugs in the test
oracle. That cross-pressure is exactly the design. The methodology is
documented in [`docs/design/tdd.md`](docs/design/tdd.md).

## Is it fast?

It's honest. The abstraction itself is free — everything is resolved
at compile time, and a `Type` is exactly as expensive as the code it
generates. The arithmetic kernels currently favor being *obviously
correct* over being clever: division and square root, for example,
use simple digit-at-a-time algorithms. The architecture reserves a
specific place (the Platform axis) for optimized backends — with the
hard rule that a fast path must produce bit-identical results to the
reference path, enforced by the same test battery. Correct first,
fast where it counts, never fast-but-different.

## What's implemented today

| | FP8 (7 encodings) | bfloat16 / FP16 / FP32 / FP64 | x87 80-bit | float128 | float256/512/1024 |
|---|:---:|:---:|:---:|:---:|:---:|
| add, sub, mul, div | ✓ exhaustive | ✓ | ✓ | ✓ | ✓ |
| sqrt, fma | ✓ exhaustive | ✓ | ✓ | ✓ | ✓ |
| comparisons, classify, min/max, nextUp/nextDown | ✓ exhaustive | ✓ | ✓ | ✓ | ✓ |
| convert (any → any) | ✓ exhaustive | ✓ | ✓ | ✓ | ✓ |
| toString / fromString | ✓ exhaustive | ✓ | ✓ | ✓ | ✓ |
| exception flags | ✓ exhaustive | ✓ | ✓ | ✓ | ✓ |

("Exhaustive" means every possible input, or input pair, verified
against the oracle.)

Supported encodings, end to end: IEEE 754 (binary16 through
binary1024, plus bfloat16 and FP8 E5M2/E4M3), E4M3FNUZ (the
no-negative-zero FP8 used by AMD and others), x87 extended 80-bit
(including its non-canonical patterns), saturating no-NaN formats
with flushed denormals (GPU-style), and rbj's integer-sortable
two's-complement encoding. All six rounding modes. Three exception
policies.

**Not yet:** float↔integer conversion, elementary functions (sin,
exp, log), decimal radix, posits, and vector/SIMD packaging. The
architecture has a place for each; see the
[design docs](docs/design/) for the roadmap thinking.

## Requirements

- **C++20**, header-only. No dependencies to *use* the library.
- **Clang 18+ or GCC 13+** (MSVC is not supported yet).
- MPFR and GMP only if you want to run the oracle tests
  (`apt install libmpfr-dev libgmp-dev` or `brew install mpfr gmp`).

```bash
cmake -B build -S . -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

CI runs the full suite — twenty test binaries — on Ubuntu
clang-18, Ubuntu gcc-13, and macOS Apple clang, on every push.

## Learning more

- **[Tutorial](docs/design/tutorial.md)** — from zero to your own
  custom format, step by step. Start here.
- [`examples/`](examples/README.md) — thirteen small, commented programs.
- [`docs/design/design.md`](docs/design/design.md) — the full
  architecture: the axes, the pipeline, and the reasoning.
- [`docs/design/tdd.md`](docs/design/tdd.md) — the verification
  methodology.
- [`docs/design/catalog.md`](docs/design/catalog.md) and
  [`menagerie.md`](docs/design/menagerie.md) — every floating-point
  format we could find, decomposed into the axes.
- [`docs/design/twos-complement.md`](docs/design/twos-complement.md)
  — the integer-sortable encoding, and why it's interesting.

## License

See [LICENSE](LICENSE).
