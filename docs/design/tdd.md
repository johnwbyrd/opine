# Testing Strategy and TDD Methodology

## Why TDD Applies Here

For most software, TDD is a workflow preference. For floating-point
arithmetic, it is nearly mandatory. The specification of every operation
on every input is mathematically determined — given a format, encoding,
rounding mode, and two input values, there is exactly one correct output
and exactly one correct set of exception flags. There is no ambiguity to
defer, no product decision to wait on, no user feedback to incorporate.
The test can be written before the implementation because the expected
answer is computable from first principles.

This property is unique to the problem domain and should be exploited
fully.

## External Test Infrastructure

Several established test suites exist for IEEE 754 floating-point. OPINE
should interface with them for credibility but cannot depend on them
exclusively, because they all assume IEEE 754 semantics and cannot
validate OPINE's non-IEEE configurations.

### Berkeley TestFloat (John Hauser)

The standard tool for verifying IEEE 754 conformance. TestFloat compares
a floating-point implementation against Berkeley SoftFloat, testing all
required operations, all rounding modes, and all special values including
subnormals.

TestFloat's architecture is modular. The `testfloat_gen` program
generates test cases (input bit patterns). A user-provided program
invokes the operation under test and writes results. The `testfloat_ver`
program checks the results against SoftFloat. OPINE needs only a thin
shim that reads TestFloat's input format, calls the OPINE operation with
IEEE 754 encoding, and writes the result.

TestFloat 3e (2018) supports binary16, binary32, binary64, and
extFloat80. The binary16 support is directly relevant for OPINE's FP16
formats.

**Use for:** Validating that OPINE's `IEEE754Encoding` produces
bit-identical results to the IEEE 754 specification for all tested
operations, rounding modes, and special values. This is OPINE's
credibility gate — if it cannot pass TestFloat in IEEE 754 mode, nothing
else it claims is trustworthy.

**Limitations:** TestFloat assumes sign-magnitude, reserved-exponent
NaN/Inf, negative zero, and IEEE 754 denormal semantics. It will report
failures for any OPINE configuration that intentionally departs from
IEEE 754.

URL: http://www.jhauser.us/arithmetic/TestFloat.html
License: UC Berkeley open-source (BSD-style)
Dependency: Requires Berkeley SoftFloat (same author, same license)

### IBM FPGen Test Vectors

Pre-generated IEEE 754 conformance vectors, organized by feature area
(addition, multiplication, rounding, special values, etc.). Available
on GitHub at sergev/ieee754-test-suite. Each vector specifies input bit
patterns, the operation, the expected output bit pattern, and the
expected exception flags.

**Use for:** Static regression tests for IEEE 754 mode. These vectors
complement TestFloat's dynamic testing by providing a fixed set of
known-correct results that can be checked in CI without building
TestFloat.

**Limitations:** Same as TestFloat — IEEE 754 only. The vectors use a
non-standard hex literal syntax that requires a parser.

URL: https://github.com/sergev/ieee754-test-suite

### Kahan's Paranoia

A diagnostic program (already in the OPINE repository at
`docs/reference/paranoia.c`) that probes properties of a floating-point
implementation: radix, precision, rounding behavior, guard digit
presence, underflow behavior, etc. Originally written in BASIC by
Kahan (1983), translated to Pascal by Wichmann, translated to C by
Gay and Sumner.

**Use for:** Sanity-checking OPINE's IEEE 754 mode. Paranoia reports
what it observes rather than pass/fail — it is a characterization tool,
not a conformance test. Useful as a smoke test and as documentation of
observed behavior.

**Limitations:** Not a pass/fail test suite. Assumes a single
floating-point type (typically `float` or `double`). Does not test
non-IEEE semantics.

### LLVM compiler-rt Builtin Tests

Unit tests for each compiler-rt builtin function (`__addsf3`,
`__mulsf3`, `__divsf3`, etc.). Located in the LLVM repository at
`compiler-rt/test/builtins/Unit/`. Each test exercises specific
input-output pairs.

**Use for:** Compatibility testing when OPINE provides drop-in
replacements for compiler-rt symbols. If OPINE is integrated into
LLVM-MOS as a replacement for compiler-rt's soft-float builtins, it
must produce identical results for the same inputs with the same
rounding mode. These tests verify that.

**Limitations:** Not exhaustive — tests are hand-selected edge cases and
representative values, not systematic coverage. Designed for binary32
and binary64 only. Assumes IEEE 754 semantics.

URL: https://github.com/llvm/llvm-project/tree/main/compiler-rt/test/builtins/Unit

### Fred Tydeman's FPCE Test Suite

A comprehensive test suite for C compiler floating-point conformance,
including decimal-binary conversion accuracy, math library accuracy,
and compiler optimization correctness. Tydeman's testing has found
hundreds of flaws in FPUs from Intel, AMD, Motorola, and others.

**Use for:** If OPINE ever provides elementary functions (exp, log, sin,
cos) or decimal conversion, Tydeman's tests are the standard for
verifying accuracy. Not immediately relevant for arithmetic operations.

**Limitations:** Commercial license (not free). C-compiler-oriented,
not library-oriented. Tests the entire toolchain, not just the
arithmetic.

URL: http://www.tybor.com/

## OPINE's Own Test Infrastructure

External test suites validate IEEE 754 conformance. They cannot validate
OPINE's non-IEEE configurations, which are the configurations that make
OPINE valuable. OPINE needs its own test infrastructure built around a
reference oracle.

### The Reference Oracle

The core of OPINE's test infrastructure is a brute-force reference
implementation that computes the mathematically exact result of any
floating-point operation and then applies the specified policy to
produce the correctly rounded, policy-compliant output.

The oracle is:

- **Slow.** It uses arbitrary-precision or sufficiently-wide integer
  arithmetic internally. Speed is irrelevant — it runs once per test
  case, not in production.

- **Simple.** Each step is a direct translation of the specification.
  No optimizations, no clever bit tricks, no SWAR. Readability and
  obviousness are the only design criteria.

- **Obviously correct.** The oracle's correctness should be verifiable
  by inspection. If there is any doubt about whether the oracle is
  right, the oracle is too complicated.

- **Policy-parameterized.** The oracle takes Format, Encoding, and
  Rounding as parameters and applies them step by step. The same oracle
  function handles IEEE 754, rbj's two's complement, E4M3FNUZ, and any
  other configuration.

The oracle's interface:

```cpp
struct OracleResult {
    storage_type value;       // the correct output bit pattern
    ExceptionFlags flags;     // which exceptions occurred
};

template <typename Format, typename Encoding, typename Rounding>
OracleResult oracle_multiply(storage_type a, storage_type b);

template <typename Format, typename Encoding, typename Rounding>
OracleResult oracle_add(storage_type a, storage_type b);

// etc. for subtract, divide, compare, negate, abs, conversions
```

The oracle's internal structure for a multiply:

```
1. Unpack a: extract sign, exponent, mantissa (applying Encoding rules)
2. Unpack b: same
3. Check for special-value inputs (NaN, Inf, zero) per Encoding rules
4. Compute exact product:
   a. Result sign = a.sign XOR b.sign
   b. Result exponent = a.exponent + b.exponent - bias
   c. Result mantissa = a.mantissa * b.mantissa (exact, using wide integers)
5. Normalize: shift mantissa so implicit bit is in the correct position
6. Apply rounding: examine guard bits, apply Rounding policy
7. Handle overflow: if exponent > max, apply Encoding's overflow behavior
   (infinity, saturate, or NaN, depending on encoding and exception policy)
8. Handle underflow: if exponent < min, apply Encoding's denormal mode
   (gradual underflow, flush to zero, etc.)
9. Pack result: assemble bit pattern per Encoding rules
10. Return result and exception flags
```

Each step is a few lines of code. The entire oracle for one operation is
perhaps 50-80 lines. The total oracle covering all four arithmetic
operations, comparison, negate, abs, and format conversion is 200-400
lines.

### The Bootstrap Chain

The oracle must be trusted before it can validate anything else. Trust
is established by cross-validation against external test infrastructure.

**Step 1:** Run the oracle in IEEE754Encoding mode against TestFloat's
expected results for FP32. If the oracle disagrees with TestFloat, the
oracle has a bug. Fix the oracle until it agrees on every test vector.

**Step 2:** Run the oracle in IEEE754Encoding mode against the IBM FPGen
vectors. These are a second independent source of expected results. Any
disagreement is a bug in the oracle.

**Step 3:** Run the oracle exhaustively for FP8 and FP16 in
IEEE754Encoding mode against Berkeley SoftFloat (called directly, not
through TestFloat). For FP8, this is 256 × 256 = 65,536 pairs per
operation. For FP16, this is ~4 billion pairs — feasible in hours for
a single operation.

At this point, the oracle is validated for IEEE 754 semantics by three
independent external references. It is now trusted.

**Step 4:** The oracle's handling of non-IEEE encodings (Relaxed,
RbjTwosComplement, E4M3FNUZ) cannot be validated against external
references, because no external reference implements those encodings.
Instead, these are validated by:

- Code review: the oracle's policy application logic is simple enough
  to verify by inspection.
- Property-based tests: for every encoding, verify that the oracle's
  results satisfy the encoding's invariants (e.g., for RbjTwosComplement,
  verify that the output bit pattern's signed integer ordering is
  consistent with the mathematical value ordering).
- Cross-encoding consistency: for inputs and outputs that are
  representable in both IEEE 754 and another encoding, verify that the
  mathematical value of the result is the same (only the bit pattern
  encoding differs).

### Exhaustive Testing for Small Formats

For FP8 formats (256 values), every unary operation can be tested
exhaustively (256 cases) and every binary operation can be tested
exhaustively (65,536 cases). This takes milliseconds.

For FP16 formats (65,536 values), every unary operation can be tested
exhaustively (65,536 cases, instant) and every binary operation can be
tested exhaustively (~4.3 billion cases, hours on a modern machine).

For FP32 formats (~4 billion values), unary operations are exhaustively
testable (hours). Binary operations are not (136 years). FP32 binary
operations are tested with targeted vectors (edge cases, boundary
conditions, random sampling) and validated against TestFloat.

The exhaustive property for small formats is OPINE's strongest testing
claim. No competing soft-float library tests exhaustively for FP8
because no competing library has FP8.

### Targeted Test Cases

For formats too large for exhaustive testing, and as a complement to
exhaustive testing for small formats, targeted tests cover known-
difficult cases. These are organized by category:

**Zero boundary:**
- +0, -0 (if encoding has negative zero)
- Smallest positive normal
- Smallest positive denormal (if encoding has denormals)
- Smallest negative normal
- Smallest negative denormal

**Overflow boundary:**
- Largest finite positive value
- Largest finite negative value
- The input pair whose exact result is the smallest value that overflows
- The input pair whose exact result is the largest value that does not
  overflow

**Underflow boundary:**
- Smallest normal value
- Largest subnormal value (if encoding has denormals)
- The input pair whose exact result crosses the normal/subnormal boundary
- The input pair whose exact result underflows to zero

**Rounding boundary:**
- Values exactly halfway between two representable values (tests
  ties-to-even vs. ties-away)
- Values one ULP above and below the halfway point
- Values where rounding causes mantissa overflow (e.g., 1.111...1
  rounds up to 10.000...0, requiring exponent increment)

**Special value interaction:**
- NaN + number, NaN + NaN, NaN + Inf (for encodings with NaN)
- Inf + number, Inf + Inf, Inf - Inf (for encodings with Inf)
- 0 × Inf, 0 / 0, Inf / Inf (invalid operations)
- Negative zero interactions: (-0) + (+0), (-0) × (+0), etc.
  (for encodings with negative zero)

**Catastrophic cancellation:**
- Nearly-equal values whose subtraction loses most significant bits
- Values where cancellation exposes guard bit handling

**Two's complement specific (for RbjTwosComplement encoding):**
- Negation of every value (verify round-trip, verify NaN trap value)
- Carry propagation: values with all-zero mantissa (carry from mantissa
  into exponent during negation)
- The most-negative value (NaN trap value): verify it is detected and
  not negated
- Comparison of all pairs near zero crossing (verify monotonic integer
  ordering)

### Property-Based Tests

In addition to testing specific input-output pairs, property-based
tests verify invariants that must hold across all inputs.

**Round-trip property:**
```
For all valid bit patterns x:
    pack(unpack(x)) == x
```

Tested exhaustively for FP8 and FP16. For FP32, tested with random
sampling.

**Monotonic ordering (TwosComplement encoding only):**
```
For all valid values a, b (excluding NaN):
    float_value(a) < float_value(b)  iff  signed_integer(a) < signed_integer(b)
```

Tested exhaustively for FP8. This is the defining property of the
two's complement representation.

**Nextafter is increment (TwosComplement encoding only):**
```
For all valid values x (excluding NaN, +Inf):
    nextafter(x, +Inf) == x + 1    (as signed integer)
```

Tested exhaustively for FP8.

**Commutativity:**
```
For all values a, b:
    add(a, b) == add(b, a)
    multiply(a, b) == multiply(b, a)
```

Tested exhaustively for FP8. Violations indicate a bug in operand
handling, not a rounding issue.

**Sign symmetry:**
```
For all values a, b:
    negate(multiply(a, b)) == multiply(negate(a), b)
    negate(multiply(a, b)) == multiply(a, negate(b))
```

Tested exhaustively for FP8. May not hold exactly at extreme values
for some encodings (e.g., if saturation clips asymmetrically).

**Conversion round-trip:**
```
For all values x representable in both format A and format B:
    convert_AtoB(convert_BtoA(x)) == x
```

Tested exhaustively for FP8-to-FP16 and FP16-to-FP8 (for values
representable in FP8).

**Cross-encoding consistency:**
```
For all values a, b representable in both encoding E1 and E2:
    real_value(operate_E1(a, b)) == real_value(operate_E2(a, b))
```

The mathematical result must be the same regardless of encoding. Only
the bit-pattern encoding differs. (This assumes the result is
representable in both encodings; if not, the rounding/overflow
behavior may differ legitimately.)

## The Test Matrix

Each cell in the matrix is a test suite that must pass:

**Axes:**
- Operation: pack/unpack, add, subtract, multiply, divide, compare,
  negate, abs, format conversion
- Format: FP8 E5M2, FP8 E4M3, FP16, FP32
- Encoding: IEEE754, Relaxed, RbjTwosComplement, E4M3FNUZ, GPUStyle
- Rounding: TowardZero, ToNearestTiesToEven, ToNearestTiesAway,
  TowardPositive, TowardNegative

**Coverage:**
- FP8 × any encoding × any rounding: exhaustive (all input pairs)
- FP16 × any encoding × any rounding: exhaustive for unary, targeted
  + random for binary (with exhaustive runs in CI nightly)
- FP32 × IEEE754Encoding: validated against TestFloat and IBM vectors
- FP32 × other encodings: targeted + random against oracle

**What is NOT in the test matrix:**
- Elementary functions (exp, log, sin, cos): deferred until implemented
- Decimal conversion: deferred, low priority
- BLAS operations: tested at the BLAS level, not the float level
- SWAR operations: tested by verifying that SWAR results match scalar
  results for all inputs (exhaustive for FP8)

## TDD Sequence for the Rewrite

The rewrite from the current policy structure to the five-axis
architecture is driven by tests. Each step has a clear pass/fail
criterion.

### Step 1: Reference Oracle

Write the brute-force reference oracle. Validate it against TestFloat
and IBM vectors for IEEE754Encoding at FP32. This is the foundation.
Nothing else proceeds until the oracle is trusted.

Deliverable: `tests/oracle/oracle.hpp` — a single header containing
the reference implementation for all operations.

Pass criterion: Oracle agrees with TestFloat on every FP32 test vector
for add, subtract, multiply, divide, and compare, for all four IEEE
rounding modes.

### Step 2: Format + Encoding + Pack/Unpack

Implement the new `Format` (geometry only), `Encoding` (what bit
patterns mean), and `pack`/`unpack` parameterized on both.

Deliverable: New `format.hpp`, `encoding.hpp`, `pack_unpack.hpp`.

Pass criterion: Exhaustive FP8 round-trip (`pack(unpack(x)) == x`)
passes for `IEEE754Encoding`, `RelaxedEncoding`, and
`RbjTwosComplement`. This is a superset of the current test suite's
coverage.

### Step 3: TestFloat Shim

Write the TestFloat integration shim for OPINE's IEEE754Encoding.

Deliverable: `tests/testfloat/opine_testfloat_shim.cpp`

Pass criterion: OPINE passes TestFloat for pack/unpack, compare, and
any other operations implemented so far, for FP16 and FP32, all
rounding modes.

### Step 4: Addition

Write exhaustive FP8 addition tests (65,536 pairs × each encoding ×
each rounding mode) against the oracle. Then implement addition.

Deliverable: `operations/add.hpp`, `tests/unit/test_add.cpp`

Pass criterion: Exhaustive FP8 tests pass. TestFloat FP16/FP32
addition tests pass for IEEE754Encoding.

### Step 5: Multiplication

Same structure as Step 4. Exhaustive FP8 tests first, then
implementation.

Deliverable: `operations/multiply.hpp`, `tests/unit/test_multiply.cpp`

Pass criterion: Exhaustive FP8 tests pass. TestFloat FP16/FP32
multiply tests pass for IEEE754Encoding.

### Step 6: Subtraction and Division

Same structure. Subtraction may share most of its implementation with
addition (effective operation determined by sign combination).

### Step 7: Comparison, Negate, Abs

These are simpler operations but critical for the two's complement
encoding, where comparison must reduce to signed integer comparison
and negate must handle the trap value.

Pass criterion: Exhaustive FP8 tests pass for all encodings.
For RbjTwosComplement, the monotonic ordering property is verified
exhaustively.

### Step 8: Format Conversion

Conversion between formats (FP8 to FP16, FP16 to FP8, etc.) with
correct rounding and overflow/underflow handling per encoding policy.

Pass criterion: Exhaustive FP8-to-FP16 and FP16-to-FP8 conversion
tests pass. Round-trip property verified for values representable in
both formats.

### Step 9: Exception Flags

Implement StatusFlags and ReturnStatus exception policies. Verify
that every operation sets the correct flags for every input.

Pass criterion: Exhaustive FP8 tests verify both result values AND
exception flags against the oracle.

## CI Integration

### Fast suite (every commit)

- Exhaustive FP8 round-trip for all encodings
- Exhaustive FP8 arithmetic (add, multiply) for IEEE754Encoding with
  TowardZero and ToNearestTiesToEven
- Targeted FP16 and FP32 tests (edge cases, boundary conditions)
- Property-based tests (commutativity, sign symmetry, monotonicity)
- Compile-time tests (`static_assert` in constexpr context)

Expected runtime: under 60 seconds.

### Full suite (nightly)

- Everything in the fast suite
- Exhaustive FP8 arithmetic for ALL encoding × rounding combinations
- Exhaustive FP16 unary operations for all encodings
- TestFloat FP16 and FP32 for IEEE754Encoding, all rounding modes
- IBM FPGen vector validation
- Random sampling of FP32 binary operations (millions of pairs)

Expected runtime: 1-4 hours.

### Extended suite (weekly or pre-release)

- Everything in the full suite
- Exhaustive FP16 binary operations for IEEE754Encoding (hours per
  operation)
- Paranoia diagnostic run
- Cross-encoding consistency checks

Expected runtime: 8-24 hours.

## What This Testing Strategy Proves

When someone asks "does OPINE correctly implement E4M3FNUZ with
round-toward-zero?", the answer is: every one of the 65,536 possible
input pairs for each binary operation has been tested against a
reference oracle that computes the mathematically exact result and
applies the specified policy step by step. The oracle itself has been
validated against three independent external sources (TestFloat, IBM
FPGen, Berkeley SoftFloat) for IEEE 754 semantics.

When someone asks "is OPINE's IEEE 754 mode actually IEEE 754
compliant?", the answer is: it passes Berkeley TestFloat for all
implemented operations, all rounding modes, for FP16 and FP32. These
are the same tests used to validate hardware FPUs.

When someone asks "how do I know the two's complement encoding is
correct?", the answer is: the monotonic ordering property (signed
integer comparison equals float comparison) has been verified
exhaustively for all FP8 values. Every arithmetic operation has been
verified against the reference oracle for all 65,536 input pairs.
Carry propagation during negation has been verified for every value
with an all-zero mantissa field.

These are not claims. They are CI jobs.