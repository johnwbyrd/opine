# OPINE

**O**ptimized **P**olicy-**I**nstantiated **N**umeric **E**ngine

A C++20 header-only library for compile-time configurable floating-point arithmetic that works everywhere—from 1 MHz 6502s to modern GPUs.

[![](https://utfs.io/f/nGnSqDveMsqxmeqXINjRJyeGzNLsqiK3drCnoHp7jO1acDgB)](https://www.youtube.com/watch?v=j95kNwZw8YY)

## Key Features

- **Zero Runtime Overhead**: All configuration decisions made at compile time
- **Universal Scalability**: Same design works on 8-bit microprocessors and ML accelerators
- **Format Flexibility**: Support for IEEE 754, ML formats (fp8, fp16), and custom formats
- **Policy-Based Design**: Choose exact semantics for rounding, special values, and error handling
- **Progressive Optimization**: Start with working code, optimize hot paths with assembly later
- **Cross-Compiler Support**: Works with Clang (with `_BitInt`), GCC, and MSVC

## Why OPINE?

Standard floating-point libraries assume IEEE 754 compliance, hardware support, and one-size-fits-all semantics. None of these hold universally. OPINE separates *what* (format and behavior) from *how* (implementation), generating exactly the code you need through compile-time policies.

## Philosophy: Compose Any Behavior You Need

**OPINE is a composition system, not a fixed library.** We provide common, well-tested policy combinations (like IEEE 754 binary32), but the architecture allows you to compose any combination of policies that makes sense for your use case.

Every aspect of floating-point arithmetic that varies across applications is expressed as an independent policy dimension:

- **Precision**: How many bits? (8-bit FP8, 16-bit, 32-bit, arbitrary custom formats)
- **Rounding**: Which direction? (nearest-even, toward-zero, toward-±∞, stochastic, custom)
- **Special values**: What exists? (NaN? Infinity? Denormals? All? None? Mix?)
- **Error handling**: How to fail? (silent, saturate, trap, compile error)
- **Implementation**: What code? (portable C++, inline assembly, ROM calls, hardware intrinsics)
- **Type selection**: What integer widths? (exact `_BitInt`, least-width, fastest)

**These dimensions are orthogonal by design.** You can combine any rounding policy with any special value handling with any error handling strategy. Want round-to-zero with no denormals but with NaN? Compose it. Want IEEE 754 except flush denormals to zero? Compose it. Want to match specific GPU hardware behavior? Compose the policies that match.

### The Core Principle

**Your floating-point needs are unique. OPINE lets you express them precisely.**

We provide proven policy combinations (IEEE 754 formats, common ML formats), but you're not limited to these. Compose policies freely to match your hardware, your accuracy requirements, your performance constraints, or your compatibility needs.

When you ask "how does OPINE handle X?", the answer is: "however the policy you compose says to handle it." OPINE provides the composition mechanism and the policy building blocks—you decide the behavior.

## Current Status

### Implemented

- **Type Selection System**: Three policies (ExactWidth, LeastWidth, Fastest) with `_BitInt` support
- **Format Descriptors**: Arbitrary bit layouts with padding support
- **Pack/Unpack Operations**: Bidirectional conversion with implicit bit handling
- **Standard Formats**: fp8_e5m2, fp8_e4m3, fp16_e5m10, fp32_e8m23, fp64_e11m52
- **Cross-Platform**: Linux, macOS, Windows with GCC, Clang, and MSVC
- **Comprehensive Tests**: Exhaustive testing for 8-bit formats

### Planned

- Arithmetic operations (add, subtract, multiply, divide)
- Rounding policies (ToNearest, TowardZero, TowardPositive, TowardNegative)
- Special value handling (NaN, Infinity, denormals)
- Conversion between formats
- Platform-specific optimizations (assembly, ROM calls, hardware instructions)
- Microscaling formats (MXFP8, MXFP6, MXFP4)

## Building and Testing

```bash
# Configure with CMake
cmake -B build -S . -DCMAKE_BUILD_TYPE=Release

# Build
cmake --build build

# Run tests
cd build && ctest --output-on-failure
```

### Compiler Support

- **Clang 18+**: Full support including `_BitInt` for exact width types
- **GCC 13+**: Fallback to `uint_fast` types (no `_BitInt` support yet)
- **MSVC**: Fallback to `uint_fast` types

For best code generation (exact bit widths), use Clang.

## Documentation

Design documentation is available:

- **[Design Document](docs/design/design.md)** - Full motivation, examples, and architecture (1680 lines)
- **[Type Selection](docs/design/type_selection.md)** - How the type policy system works
- **[Pack/Unpack System](docs/design/pack_unpack.md)** - Format conversion and representation
- **[Guard/Round/Sticky Bits](docs/design/bits.md)** - Rounding implementation details

## Project Structure

```
opine/
├── include/opine/           # Header-only library
│   ├── core/               # Core types and format descriptors
│   ├── operations/         # Pack/unpack and arithmetic operations
│   └── policies/           # Type selection and rounding policies
├── tests/                  # Unit tests
├── examples/               # Usage examples
└── docs/design/            # Design documentation
```

## Implementation Notes

All policy decisions are made at compile time through template instantiation. Zero runtime overhead. Zero interference between different policy compositions used in the same program.

## License

MIT License - See [LICENSE](LICENSE) for details.

## Contributing

OPINE is in active development. The core infrastructure (types, formats, pack/unpack) is stable. Arithmetic operations are next.

Contributions welcome! Please see design docs for architectural principles.

# Further reading

### Foundational References

- [What Every Computer Scientist Should Know About Floating-Point Arithmetic](https://docs.oracle.com/cd/E19957-01/806-3568/ncg_goldberg.html) — Goldberg's essential introduction to floating-point representation and error analysis
- [How Java's Floating-Point Hurts Everyone Everywhere](https://www.cs.berkeley.edu/~wkahan/JAVAhurt.pdf) — Kahan & Darcy on portability pitfalls and IEEE 754 compliance issues
- [The Pitfalls of Verifying Floating-Point Computations](https://arxiv.org/abs/cs/0701192) — Monniaux on how compilers break floating-point semantics

### Correctly Rounded Math Libraries

- [RLIBM-32: High Performance Correctly Rounded Math Libraries for 32-bit Floating Point Representations](https://arxiv.org/abs/2104.04043) — Lim & Nagarakatte on polynomial approximations for correctly rounded libm
- [One Polynomial Approximation to Produce Correctly Rounded Results of an Elementary Function for Multiple Representations and Rounding Modes](https://arxiv.org/abs/2111.12852) — POPL 2022 Distinguished Paper extending RLIBM to multiple formats
- [An Accurate Elementary Mathematical Library for the IEEE Floating Point Standard](https://dl.acm.org/doi/10.1145/103147.103151) — Gal's "accurate tables method" for last-bit accuracy
- [Correctly Rounded Binary-Decimal and Decimal-Binary Conversions](https://ampl.com/_archive/first-website/REFS/rounding.pdf) — Gay's classic on efficient decimal/binary conversion

### ML & Low-Precision Formats

- [FP8 Formats for Deep Learning](https://arxiv.org/abs/2209.05433) — Micikevicius et al. defining E4M3 and E5M2 formats for training and inference
- [Mixed Precision Training](https://arxiv.org/abs/1710.03740) — Foundational techniques for training with half-precision floats

### Testing & Comparison

- [Comparing Floating Point Numbers, 2012 Edition](https://randomascii.wordpress.com/2012/02/25/comparing-floating-point-numbers-2012-edition/) — Dawson's practical guide to epsilon and ULP-based comparisons
- [PARANOIA - Kahan's Floating Point Test Program](https://people.math.sc.edu/Burkardt/c_src/paranoia/paranoia.html) — Classic diagnostic for floating-point implementation quirks
- [Floating-Point Arithmetic Test Programs](https://www.vinc17.net/research/fptest.en.html) — Lefèvre's collection of IEEE 754 compliance tests

