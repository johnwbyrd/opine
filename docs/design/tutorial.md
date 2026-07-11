# OPINE Tutorial

From zero to your own custom floating-point format, one small step at
a time. No floating-point expertise assumed — where a concept
matters, we explain it on the spot.

## 1. What you need

- A C++20 compiler: **Clang 18 or newer**, or **GCC 13 or newer**.
  (MSVC is not supported yet.)
- CMake 3.20+ if you build with CMake (recommended but optional —
  OPINE is header-only).
- That's it. MPFR/GMP are only needed to run OPINE's own test suite,
  never to use the library.

## 2. Adding OPINE to your project

Pick whichever of these matches how your project is set up.

**Option A — CMake subdirectory** (you vendored or submoduled the
repo):

```cmake
add_subdirectory(third_party/opine)
target_link_libraries(my_app PRIVATE opine)
```

**Option B — CMake FetchContent** (no vendoring):

```cmake
include(FetchContent)
FetchContent_Declare(opine
  GIT_REPOSITORY https://github.com/johnwbyrd/opine.git
  GIT_TAG        main)
FetchContent_MakeAvailable(opine)
target_link_libraries(my_app PRIVATE opine)
```

Note: building OPINE's target this way also configures its tests and
examples. If you want just the headers, Option C is leaner.

**Option C — no build system at all.** OPINE is headers. Copy (or
point at) the `include/` directory:

```bash
clang++ -std=c++20 -I path/to/opine/include my_app.cpp -o my_app
```

Everything below works identically with all three options.

## 3. Your first program

```cpp
// hello_opine.cpp
#include <cstdio>
#include <opine/opine.hpp>

int main() {
  using namespace opine;

  // float32 is IEEE 754 binary32 — the same format as a native
  // `float`, with the default round-to-nearest behavior.
  using f32 = float32;

  auto x = fromNative<f32>(1.5f);
  auto y = fromNative<f32>(2.25f);
  auto z = add<f32>(x, y);

  std::printf("1.5 + 2.25 = %g\n", double(toFloat<f32>(z)));
  return 0;
}
```

```bash
clang++ -std=c++20 -I path/to/opine/include hello_opine.cpp -o hello
./hello
# 1.5 + 2.25 = 3.75
```

Three functions did all the work:

- `fromNative<T>(value)` — turn a native `float`/`double` into T's
  bit pattern.
- `add<T>(a, b)` — do arithmetic *in format T*.
- `toFloat<T>(bits)` — turn T's bit pattern back into a native
  `float` you can print.

## 4. The one idea you have to internalize

OPINE operations don't take and return "numbers" — they take and
return **bit patterns**, of type `T::storage_type`. For `float32`
that's a 32-bit unsigned value; for an 8-bit format it's an 8-bit
value; for float1024 it's an array of words. `auto` handles it, as in
the example above.

Why bits instead of a nice wrapped number class? Because the format
you're computing in usually *doesn't exist* on your CPU. There is no
native FP8 `+` for the compiler to fall back on — and more
importantly, no way for one to sneak in. When every operation is an
explicit `add<T>`, you always know exactly which format's arithmetic
happened. Nothing implicit, nothing accidental.

Two practical consequences:

- Don't use `+`, `*`, `<` on storage values. It's either a compile
  error or — worse — integer arithmetic on the raw bits. Use
  `add<T>`, `mul<T>`, `lt<T>`.
- Two different formats can share a storage width (`fp8_e5m2` and
  `fp8_e4m3` are both 8 bits), so the compiler can't always stop you
  from feeding one format's bits to another format's function. Keep
  a type alias per format and be tidy about which bits are which.

## 5. Getting values in and out

```cpp
using namespace opine;
using fp8 = fp8_e4m3;   // an 8-bit float: 1 sign, 4 exponent, 3 mantissa bits

// In: from a native float (rounds to what fp8 can hold).
auto third = fromNative<fp8>(1.0f / 3.0f);

// Out: back to native (exact — fp8 values all fit in a float).
float f = toFloat<fp8>(third);          // 0.34375, the nearest fp8 value

// Out, without going through native at all: a correctly rounded
// decimal string. Works at ANY width, even where no native type
// could hold the value.
std::string s = toString<fp8>(third);      // "0.344"
std::string x = toString<fp8>(third, 5);   // "0.34375" — the exact value

// In, from text — also correctly rounded:
auto pi = fromString<fp8>("3.14159");      // nearest fp8: 3.25
```

`toString<T>(bits)` defaults to just enough digits to uniquely
identify the value (parse it back and you get the same bits); ask
for more digits and you get more — every one of them correct. This
is how the examples print 300 accurate digits of a binary1024 π.

## 6. The operation set

Everything is a free function templated on the format:

```cpp
// Arithmetic — every result correctly rounded:
add<T>(a, b);  sub<T>(a, b);  mul<T>(a, b);  div<T>(a, b);
sqrt<T>(a);
fma<T>(a, b, c);   // a*b + c with ONE rounding — a real fused multiply-add

// Comparison (NaN-aware, like native floats):
eq<T>(a, b);  ne<T>(a, b);  lt<T>(a, b);  le<T>(a, b);
gt<T>(a, b);  ge<T>(a, b);  unordered<T>(a, b);

// Classification:
isNan<T>(a);  isInfinite<T>(a);  isZero<T>(a);
isFinite<T>(a);  isNormal<T>(a);  isSubnormal<T>(a);  isSignMinus<T>(a);

// Selection and neighbors:
minimum<T>(a, b);  maximum<T>(a, b);          // NaN wins
minimumNumber<T>(a, b);  maximumNumber<T>(a, b); // the number wins
nextUp<T>(a);  nextDown<T>(a);                // adjacent representable values

// Sign surgery (never rounds, never signals):
neg<T>(a);  abs<T>(a);  copySign<T>(x, y);
```

A taste of why `fma` is special:

```cpp
using f32 = float32;
auto a = fromNative<f32>(1.0f + 1.19209290e-7f);   // 1 + ε

// (1+ε)² − (1+2ε) should be ε² — a very tiny, very real number.
auto b   = neg<f32>(fromNative<f32>(1.0f + 2.38418579e-7f));
auto two = mul<f32>(a, a);                 // rounds: the ε² term is GONE
std::printf("%s\n", toString<f32>(add<f32>(two, b)).c_str());
// "0"
std::printf("%s\n", toString<f32>(fma<f32>(a, a, b)).c_str());
// "1.42108547e-14" — exactly ε²
```

The unfused version loses the answer entirely; `fma` keeps it. If
you've ever wondered why numerical code cares so much about fused
multiply-add — that's why, in two lines.

## 7. Converting between formats

`convert<Destination, Source>(bits)` — both formats spelled at the
call site, destination first, so it reads like a cast:

```cpp
using f32 = float32;
using fp8 = fp8_e4m3;

auto precise = fromNative<f32>(0.30078125f);
auto squeezed = convert<fp8, f32>(precise);   // rounds into 8 bits
auto back     = convert<f32, fp8>(squeezed);  // exact: fp8 ⊂ f32

std::printf("%g -> %s\n",
            double(toFloat<f32>(precise)),
            toString<fp8>(squeezed, 4).c_str());  // 0.300781 -> 0.3125
```

That two-line round-trip is the honest way to answer "what will my
data look like in FP8?" — you get the exact value the narrow format
stores, not an approximation of it.

Two rules worth remembering:

- **Why spell the Source?** Because different formats share storage
  widths; the compiler cannot tell `fp8_e5m2` bits from `fp8_e4m3`
  bits on its own.
- **Convert directly**, not through a chain. `f64 → f32 → fp8` can
  round twice and land one bit off from `f64 → fp8`. Direct
  conversions are always single-rounded and correct.

## 8. Choosing how rounding works

Rounding is a template parameter of the format. The default is what
you expect (round to nearest, ties to even — same as your CPU).
Changing it is one line:

```cpp
// binary32, but rounding everything toward zero:
using f32_trunc = Type<numbers::IEEE754<8, 23>,
                       layouts::IEEE<8, 23, true>,
                       rounding::TowardZero>;
```

Available modes: `ToNearestTiesToEven` (default),
`ToNearestTiesAway`, `TowardZero`, `TowardPositive`,
`TowardNegative`, and `ToOdd`. The last one is the specialist's tool:
round-to-odd lets you compute an intermediate result at high
precision and round it *again* to a narrower format without the
"double rounding" error that would otherwise creep in.

The same value under different modes — see
[`examples/04_rounding_modes.cpp`](../../examples/04_rounding_modes.cpp)
for a runnable version.

## 9. Catching overflow and friends

IEEE 754 says operations should *tell you* when something noteworthy
happened: overflow, underflow, division by zero, invalid input,
inexact result. How OPINE reports these is — you guessed it — a
template parameter:

```cpp
// Policy 1 (default): Silent. Results are still correct; events
// are simply not reported. Zero overhead.

// Policy 2: StatusFlags. Events accumulate in a per-thread flag set:
using f32_flags = Type<numbers::IEEE754<8, 23>, layouts::IEEE<8, 23, true>,
                       rounding::Default, exceptions::StatusFlags>;

clearStatusFlags();
auto big = fromNative<f32_flags>(3e38f);
add<f32_flags>(big, big);                       // overflows to +inf
if (statusFlags() & FlagOverflow)
  std::puts("overflowed somewhere in that block");

// Policy 3: ReturnStatus. Every operation returns {bits, flags}:
using f32_status = Type<numbers::IEEE754<8, 23>, layouts::IEEE<8, 23, true>,
                        rounding::Default, exceptions::ReturnStatus>;

auto r = div<f32_status>(fromNative<f32_status>(1.0f).bits,
                         fromNative<f32_status>(0.0f).bits);
if (r.flags & FlagDivByZero)
  std::printf("1/0 = %s, and the operation said so\n",
              toString<f32_status>(r.bits).c_str());   // "inf"
```

The flags are `FlagInvalid`, `FlagDivByZero`, `FlagOverflow`,
`FlagUnderflow`, and `FlagInexact`. Under `ReturnStatus`, note that
value-producing calls return a small struct — hence the `.bits` in
the example.

## 10. Rolling your own format

This is where OPINE stops being a library and starts being a
laboratory. Suppose FP8 is too coarse for your data but FP16 is
wastefully wide. Define a 12-bit float:

```cpp
// 12 bits: 1 sign + 5 exponent + 6 mantissa.
using MyFp12 = Type<numbers::IEEE754<5, 6>, layouts::IEEE<5, 6, true>>;

static_assert(MyFp12::layout::total_bits == 12);

auto x = fromNative<MyFp12>(3.14159f);
std::printf("pi in 12 bits: %s\n", toString<MyFp12>(x, 6).c_str());
// pi in 12 bits: 3.15625
```

Every operation, every conversion, string I/O, exception flags, the
whole test methodology — all of it works on `MyFp12` immediately,
because it's the same pipeline the standard formats use. The
compiler generates code specialized for exactly these widths; there
is no runtime penalty for being nonstandard.

The `IEEE754<E, M>` recipe covers "an IEEE-style float with E
exponent bits and M mantissa bits." Beyond that live the more
opinionated Numbers: `numbers::GPUStyle` (IEEE specials but flushed
denormals), `numbers::Relaxed` (no NaN, no infinity, saturating —
the "my data never goes there, give me the speed" position),
`numbers::E4M3FNUZ` (the no-negative-zero FP8), and
`numbers::RbjTwosComplement` (the integer-sortable encoding). Each
is a different set of answers to the same questions — that's the
whole design.
[`examples/07_custom_format.cpp`](../../examples/07_custom_format.cpp)
compares a custom format against its standard neighbors.

## 11. Going wide

Nothing changes when the format outgrows the machine:

```cpp
using f1k = float1024;    // 997 bits of precision

auto a = fromString<f1k>("1e-200");
auto b = fromString<f1k>("1");
auto sum = add<f1k>(a, b);

// All of 1 + 1e-200 survives — try THAT in a double:
std::printf("%s\n", toString<f1k>(sum, 210).c_str());
// 1.00000000000000000000000000000000000000000000000000000000000...0001
```

`float128`, `float256`, `float512`, and `float1024` are predefined.
Storage becomes an array of words behind the scenes; your code
doesn't change at all.

## 12. Pitfalls checklist

- **Don't do native arithmetic on storage bits.** `a + b` on storage
  values is not float addition. Use `add<T>`.
- **Keep bits with their format.** Same-width formats interchange
  silently if you're careless; the compiler can't always catch it.
- **Spell both formats in `convert<Dst, Src>`** — and convert
  directly rather than chaining.
- **`ReturnStatus` changes return types** from bare bits to
  `{bits, flags}` structs. If you switch a format's exception policy,
  expect `.bits` to appear in your code.
- **NaN payloads are not preserved** across operations or
  conversions; you get the format's canonical NaN.
- **Clang 18+ / GCC 13+ only** for now.

## 13. Where to go next

- The thirteen [example programs](../../examples/README.md) — each is a
  single short file demonstrating one idea, in roughly increasing
  order of sophistication.
- [`design.md`](design.md) — the architecture: what the axes are and
  why the pipeline is shaped the way it is.
- [`tdd.md`](tdd.md) — how the library is verified, if you want to
  see what "exhaustively tested against an oracle" actually means.
- [`catalog.md`](catalog.md) — decompositions of every format we
  know of into OPINE's axes, including many not yet implemented.
