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

### Berkeley TestFloat / SoftFloat (John Hauser)

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

SoftFloat itself is the reference IEEE 754 implementation. Hauser wrote
it specifically as a reference — not as a production library, but as the
thing production libraries are tested against. SoftFloat has its own
internal cross-check: the `testsoftfloat` program compares SoftFloat
against a separate, simpler, slower, independent software float
implementation. When SoftFloat and a hardware FPU disagree, the bug is
in the hardware — this has been demonstrated repeatedly at Intel, ARM,
and RISC-V vendors. SoftFloat is the ground truth for IEEE 754.

**Use for:** Validating that OPINE's `IEEE754Encoding` produces
bit-identical results to the IEEE 754 specification. Also used to
validate OPINE's own reference oracle (see below). This is OPINE's
credibility gate.

**Limitations:** IEEE 754 only. Cannot validate any non-IEEE encoding.

URL: http://www.jhauser.us/arithmetic/TestFloat.html
License: UC Berkeley open-source (BSD-style)

### IBM FPGen Test Vectors

Pre-generated IEEE 754 conformance vectors, organized by feature area
(addition, multiplication, rounding, special values, etc.). Available
on GitHub at sergev/ieee754-test-suite. Each vector specifies input bit
patterns, the operation, the expected output bit pattern, and the
expected exception flags.

**Use for:** A second independent source of IEEE 754 expected results,
used to cross-validate the oracle. Also useful as static regression
tests that can run in CI without building TestFloat.

**Limitations:** IEEE 754 only. The vectors use a non-standard hex
literal syntax that requires a parser.

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

### What is NOT Ground Truth

**LLVM APFloat is not a reference oracle.** APFloat is a production tool
for compiler constant folding. Known bugs relevant to OPINE include:

- Issue #55838: wrong denormal truncation
- Issue #60942: FP operations underspecified
- Issue #62479: constant folding uses host libm
- Issue #44218: x87 excess precision instability

These bugs are tolerable in APFloat's actual use case, where a wrong
rounding in the last bit of an intermediate constant-fold result rarely
produces a user-visible bug. They are not tolerable in a reference
oracle. APFloat is the thing you test against an oracle, not the oracle
itself.

## OPINE's Own Test Infrastructure

External test suites validate IEEE 754 conformance. They cannot validate
OPINE's non-IEEE configurations, which are the configurations that make
OPINE valuable. OPINE needs its own test infrastructure built around a
reference oracle.

### The Reference Oracle

The oracle has two parts, each with a different trust model and a
different authorship strategy.

#### Part 1: Exact Mathematical Result (MPFR — Do Not Write)

GNU MPFR (Multiple Precision Floating-Point Reliable) computes
floating-point operations to arbitrary precision with guaranteed correct
rounding. It is the standard reference for what the mathematical answer
actually is. MPFR has been developed since 2000, is required to build
GCC, and its correctness for basic arithmetic is not in question.

When OPINE needs to know the exact product of two FP8 mantissas, MPFR
computes it to 200 bits of precision — far more than enough to determine
the correctly-rounded result in any target format under any rounding
mode. This eliminates an entire class of oracle bugs (wrong intermediate
precision, lost carry bits, double rounding) by making the exact result
available with arbitrary headroom.

MPFR supports all four IEEE 754 rounding modes plus round-away-from-
zero. It handles signed zeros, infinities, and NaN. It is portable,
platform-independent (results do not depend on machine word size), and
LGPL-licensed.

OPINE should not reimplement arbitrary-precision arithmetic. MPFR exists,
is trusted, and does this job.

URL: https://www.mpfr.org/
License: LGPL
Dependency: GMP (GNU Multiple Precision Arithmetic Library)

#### Part 2: Policy Application (Write This)

MPFR gives the exact mathematical result. But MPFR does not know what
rbj's two's complement encoding is. It does not know that E4M3FNUZ uses
the negative-zero bit pattern as NaN. It does not know that Relaxed
encoding saturates on overflow instead of producing infinity. It does
not know that RbjTwosComplement packs the result as a two's complement
integer rather than sign-magnitude.

The policy application layer takes an exact result from MPFR and applies
OPINE's policies step by step to produce the correct output bit pattern
and exception flags. This is the part that must be written, because
nobody else has OPINE's policy system.

The policy application layer is:

- **Short.** Roughly 200 lines for all arithmetic operations.

- **Simple.** Each step is a direct translation of the encoding
  specification. No optimizations, no bit tricks. Each decision point
  is an `if` on an encoding parameter.

- **Obviously correct.** Correctness is verifiable by inspection. If
  there is any doubt about whether a step is right, the step is too
  clever.

- **Policy-parameterized.** Takes Format, Encoding, and Rounding as
  parameters and applies them explicitly. The same code handles IEEE
  754, rbj's two's complement, E4M3FNUZ, Relaxed, and any future
  encoding.

The policy application layer's structure for a single operation:

```
Given: exact result from MPFR (sign, exact mantissa, exact exponent)

1. Check for special-value result conditions:
   - Is the exact result zero? (which zero, per encoding)
   - Is the exact result too large to represent? (overflow)
   - Was the operation invalid? (0 × Inf, 0/0, Inf - Inf, etc.)

2. Handle overflow per Encoding:
   - ReservedExponent Inf → return infinity bit pattern
   - IntegerExtremes Inf → return max/min integer bit pattern
   - No Inf → saturate to largest finite value
   - Set overflow exception flag

3. Determine target mantissa width:
   - Is the result in the denormal range? (exponent < E_min)
   - If so, does Encoding flush denormals?
     - FlushToZero → return zero, set underflow flag
     - Full → compute right-shift amount for gradual underflow
   - If not, mantissa width is M bits (+ implicit bit if applicable)

4. Round the exact mantissa to target width:
   - Extract the mantissa bits that will be stored
   - Examine the remaining bits (guard, round, sticky)
   - Apply Rounding policy to determine whether to round up
   - If rounding up causes mantissa overflow (all 1s → carry),
     increment exponent and reset mantissa
   - If rounding caused the result to become exact, clear inexact flag
   - Otherwise, set inexact flag

5. Handle underflow (post-rounding):
   - If result is in denormal range and inexact, set underflow flag
   - If result rounded to zero, set underflow flag

6. Pack result per Encoding:
   - SignMagnitude: sign bit | biased exponent | stored mantissa
   - TwosComplement: pack as positive, then conditionally negate
   - OnesComplement: pack as positive, then conditionally complement

7. Return bit pattern and exception flags
```

Each numbered step is a few lines of code. The special-value handling
(step 1) is the most complex part because each encoding has different
special values, but even this is a straightforward switch on the
encoding parameters.

#### Composed Oracle Interface

The oracle composes Parts 1 and 2:

```cpp
struct OracleResult {
    uint64_t bits;            // the correct output bit pattern
    ExceptionFlags flags;     // which exceptions occurred
};

template <typename Format, typename Encoding, typename Rounding>
OracleResult oracle_add(uint64_t a_bits, uint64_t b_bits);

template <typename Format, typename Encoding, typename Rounding>
OracleResult oracle_multiply(uint64_t a_bits, uint64_t b_bits);

// etc. for subtract, divide, compare, negate, abs, conversions
```

Internally, each function:

1. Unpacks input bit patterns into MPFR values (respecting the Encoding
   to interpret the bit pattern correctly — sign-magnitude vs. two's
   complement, etc.)
2. Performs the operation in MPFR at high precision (e.g., 256 bits)
3. Passes the MPFR result to the policy application layer
4. Returns the resulting bit pattern and exception flags

### Validating the Oracle

The oracle must be trusted before it can validate anything else. Trust
is established by cross-validation against external references for IEEE
754 semantics, and by inspection and property testing for non-IEEE
semantics.

#### IEEE 754 Validation

**Against Berkeley SoftFloat:** Run the oracle in IEEE754Encoding mode
for every operation (add, subtract, multiply, divide, compare) and call
the corresponding SoftFloat function on the same inputs. Compare bit
patterns and exception flags. Any disagreement is a bug in the oracle's
policy application layer (Part 2), since Part 1 (MPFR) is trusted.

For FP8 and FP16, this comparison is exhaustive — every input pair. For
FP32, it uses TestFloat's test vector generation, which covers all
rounding modes, all special values, boundary cases, and random sampling.

**Against IBM FPGen vectors:** Run the oracle on every vector in the IBM
test suite. These are a second independent source of expected results.
Cross-validating against both SoftFloat and IBM vectors catches bugs
that either source alone might miss (e.g., a SoftFloat bug that the
oracle inadvertently reproduces, or an IBM vector that tests a case
SoftFloat's generator doesn't emphasize).

After this step, the oracle's IEEE 754 behavior is validated by two
independent external references.

#### Non-IEEE Validation

The oracle's policy application layer for non-IEEE encodings
(RbjTwosComplement, E4M3FNUZ, Relaxed, GPUStyle) cannot be validated
against external references, because no external reference implements
those encodings. Validation relies on:

**Code review:** The policy application layer is roughly 200 lines,
with each encoding decision point being a simple conditional. It is
short enough to verify by reading.

**Property-based tests:** For every encoding, verify that the oracle's
results satisfy the encoding's defining invariants:

- RbjTwosComplement: signed integer ordering equals float ordering
  for all non-NaN values (exhaustive for FP8)
- RbjTwosComplement: negation via two's complement integer negation
  produces the correct float negation for all values except the trap
  value (exhaustive for FP8)
- E4M3FNUZ: the negative-zero bit pattern (sign=1, exp=0, mant=0)
  is the only NaN; no other bit pattern is NaN (exhaustive for FP8)
- Relaxed: no bit pattern decodes to NaN or infinity (exhaustive for
  FP8)
- All encodings: `unpack(pack(unpack(x))) == unpack(x)` for all valid
  bit patterns (the oracle's unpack/pack is consistent with itself)

**Cross-encoding consistency:** For values representable in both IEEE
754 and another encoding, verify that the mathematical value of the
oracle's result is the same. The bit patterns will differ (different
sign conventions, different special-value encodings), but the real
number they represent must agree. This catches errors in the policy
application layer's packing logic without relying on an external
reference for the non-IEEE encoding.

**MPFR as backstop:** Because Part 1 (the exact result) is computed by
MPFR regardless of encoding, the only thing that can go wrong in the
oracle for a non-IEEE encoding is Part 2 (policy application). This
constrains the bug surface to 200 lines of simple conditional logic,
which is a tractable review target.

## The Test Harness: This Against That

Every test described in this document is an instance of the same
pattern: take two things that should agree, run them on the same
inputs, compare outputs. The oracle vs. OPINE is one instance. But
every validation task in the project has this shape:

- OPINE vs. oracle (correctness)
- OPINE `IEEE754Encoding` vs. SoftFloat (IEEE 754 conformance)
- OPINE scalar vs. OPINE SWAR (vectorization correctness)
- OPINE encoding A vs. OPINE encoding B on shared values
  (cross-encoding consistency)
- OPINE current commit vs. OPINE previous commit (regression)
- OPINE vs. compiler-rt `__addsf3` (compatibility)
- OPINE vs. hardware `float` on the host (sanity check)
- Oracle IEEE754 vs. SoftFloat (oracle validation)
- Oracle encoding A vs. Oracle encoding B on shared values
  (oracle self-consistency)

These are all the same harness with different parameters. The harness
should be written once and parameterized.

### Structure

```cpp
template <typename IterationStrategy,
          typename ImplA,
          typename ImplB,
          typename Comparator,
          typename Reporter>
TestResult test_against(
    IterationStrategy iter,
    ImplA impl_a,
    ImplB impl_b,
    Comparator cmp,
    Reporter report
);
```

**IterationStrategy** generates input bit patterns. It determines the
coverage model:

```cpp
// Every input pair for a given format. Feasible for FP8 (65,536
// pairs), slow but possible for FP16 (~4.3 billion pairs).
template <typename Format>
struct Exhaustive {
    // Iterates i from 0..2^N-1, j from 0..2^N-1
    // Yields (storage_type(i), storage_type(j))
};

// Specific input pairs chosen to exercise known-difficult cases.
// Zero boundaries, overflow boundaries, rounding boundaries,
// special value interactions, catastrophic cancellation.
template <typename Format, typename Encoding>
struct Targeted {
    // Yields pairs from a hand-curated list.
    // The list depends on the encoding (two's complement has
    // different difficult cases than sign-magnitude).
};

// Uniform random sampling over the input space. Used for FP32
// binary operations where exhaustive is infeasible.
template <typename Format>
struct Random {
    uint64_t seed;
    size_t count;
    // Yields (random_bits, random_bits) pairs
};

// Reads input pairs from TestFloat's testfloat_gen output format
// or IBM FPGen vector files.
struct ExternalVectors {
    std::istream& source;
    // Yields pairs as specified by the external source
};

// Unary variant: iterates single values, not pairs.
template <typename Format>
struct ExhaustiveUnary {
    // Iterates i from 0..2^N-1
    // Yields storage_type(i)
};
```

**ImplA and ImplB** are callables that take input bit patterns and
return output bit patterns (and optionally exception flags). They are
the two things being compared. Any callable with the right signature
works:

```cpp
// OPINE operation
auto opine_add = [](uint64_t a, uint64_t b) -> TestOutput {
    auto ua = unpack<Format, Encoding>(a);
    auto ub = unpack<Format, Encoding>(b);
    auto result = add<Format, Encoding, Rounding, Exceptions, Platform>(ua, ub);
    return { pack<Format, Encoding>(result), get_flags() };
};

// Oracle
auto oracle_add_fn = [](uint64_t a, uint64_t b) -> TestOutput {
    auto r = oracle_add<Format, Encoding, Rounding>(a, b);
    return { r.bits, r.flags };
};

// SoftFloat
auto softfloat_add = [](uint64_t a, uint64_t b) -> TestOutput {
    softfloat_exceptionFlags = 0;
    float16_t sa = { .v = (uint16_t)a };
    float16_t sb = { .v = (uint16_t)b };
    float16_t sr = f16_add(sa, sb);
    return { sr.v, softfloat_exceptionFlags };
};

// compiler-rt
auto compilerrt_add = [](uint64_t a, uint64_t b) -> TestOutput {
    float fa, fb;
    memcpy(&fa, &a, sizeof(float));
    memcpy(&fb, &b, sizeof(float));
    float fr = __addsf3(fa, fb);
    uint32_t result;
    memcpy(&result, &fr, sizeof(float));
    return { result, 0 };  // compiler-rt doesn't report flags
};

// Hardware float (host sanity check)
auto host_add = [](uint64_t a, uint64_t b) -> TestOutput {
    float fa, fb;
    memcpy(&fa, &a, sizeof(float));
    memcpy(&fb, &b, sizeof(float));
    float fr = fa + fb;
    uint32_t result;
    memcpy(&result, &fr, sizeof(float));
    return { result, 0 };
};
```

**Comparator** determines what "agree" means. This is the part that
varies in non-obvious ways:

```cpp
// Outputs must be identical bit patterns and identical flags.
// The common case for same-encoding comparisons.
struct BitExact {
    bool compare(TestOutput a, TestOutput b) {
        return a.bits == b.bits && a.flags == b.flags;
    }
};

// Outputs must be identical bit patterns; ignore flags.
// Used when one implementation doesn't report flags
// (e.g., compiler-rt, hardware float).
struct BitExactIgnoreFlags {
    bool compare(TestOutput a, TestOutput b) {
        return a.bits == b.bits;
    }
};

// Outputs, interpreted under possibly different encodings, must
// represent the same real number. Used for cross-encoding
// consistency tests. Requires decoding both bit patterns to a
// common representation (e.g., MPFR) and comparing values.
template <typename EncodingA, typename EncodingB>
struct SameRealValue {
    bool compare(TestOutput a, TestOutput b) {
        auto va = decode_to_mpfr<EncodingA>(a.bits);
        auto vb = decode_to_mpfr<EncodingB>(b.bits);
        return mpfr_equal_p(va, vb);
    }
};

// Outputs must differ by at most N ULPs in the target format.
// Used for approximate comparisons (e.g., testing a fast-path
// implementation against a correctly-rounded reference).
template <int MaxULPs>
struct WithinULPs {
    bool compare(TestOutput a, TestOutput b) {
        return ulp_distance(a.bits, b.bits) <= MaxULPs;
    }
};

// Outputs must agree, but known-legitimate divergences are
// accepted. For example, if implementation A flushes denormals
// and implementation B does not, the comparator accepts any
// disagreement where A produced zero and B produced a denormal.
template <typename DivergencePolicy>
struct BitExactWithExceptions {
    bool compare(TestOutput a, TestOutput b) {
        if (a.bits == b.bits && a.flags == b.flags) return true;
        return DivergencePolicy::is_acceptable(a, b);
    }
};
```

**Reporter** collects and presents failures. At minimum it records the
input bit patterns, the two outputs, and the decoded floating-point
values for human inspection:

```cpp
struct Failure {
    uint64_t input_a;
    uint64_t input_b;         // unused for unary ops
    TestOutput output_a;      // from ImplA
    TestOutput output_b;      // from ImplB
    // Optional: decoded values for human readability
    std::string decoded_a;    // e.g., "+1.5 × 2^3"
    std::string decoded_b;
};

struct TestResult {
    size_t total_cases;
    size_t failures;
    std::vector<Failure> first_n_failures;  // cap to avoid OOM
};
```

The reporter can print immediately (useful for interactive debugging),
collect for summary (useful for CI), or abort on first failure (useful
for bisecting).

### How Everything Else Becomes a Configuration

With this harness, every test in the project is a one-liner that
selects its parameters:

```cpp
// Oracle validation: oracle vs. SoftFloat, exhaustive FP8, bit-exact
test_against(
    Exhaustive<fp8_e5m2>{},
    oracle_add<fp8_e5m2, IEEE754, RoundTiesToEven>,
    softfloat_f16_add,   // SoftFloat doesn't have FP8; use FP16 + convert
    BitExact{},
    default_reporter
);

// Correctness: OPINE vs. oracle, exhaustive FP8, bit-exact
test_against(
    Exhaustive<fp8_e5m2>{},
    opine_add<fp8_e5m2, IEEE754, RoundTiesToEven>,
    oracle_add<fp8_e5m2, IEEE754, RoundTiesToEven>,
    BitExact{},
    default_reporter
);

// Cross-encoding: OPINE IEEE754 vs. OPINE RbjTC, shared values, same real value
test_against(
    SharedValues<fp8_e5m2, IEEE754, RbjTwosComplement>{},
    opine_add<fp8_e5m2, IEEE754, RoundTiesToEven>,
    opine_add<fp8_e5m2, RbjTwosComplement, RoundTiesToEven>,
    SameRealValue<IEEE754, RbjTwosComplement>{},
    default_reporter
);

// SWAR correctness: OPINE scalar vs. OPINE SWAR, exhaustive FP8, bit-exact
test_against(
    Exhaustive<fp8_e5m2>{},
    opine_add_scalar<fp8_e5m2, IEEE754, RoundTiesToEven>,
    opine_add_swar<fp8_e5m2, IEEE754, RoundTiesToEven>,
    BitExact{},
    default_reporter
);

// Regression: OPINE vs. saved golden outputs, targeted FP32, bit-exact
test_against(
    ExternalVectors{"golden/add_fp32_ieee754_rte.vectors"},
    opine_add<fp32, IEEE754, RoundTiesToEven>,
    golden_lookup,
    BitExact{},
    default_reporter
);

// Compatibility: OPINE vs. compiler-rt, targeted FP32, bit-exact ignoring flags
test_against(
    Targeted<fp32, IEEE754>{},
    opine_add<fp32, IEEE754, RoundTiesToEven>,
    compilerrt_addsf3,
    BitExactIgnoreFlags{},
    default_reporter
);
```

The targeted test case lists, the exhaustive iteration, the oracle,
the TestFloat shim, the property tests, the cross-encoding checks —
they are all instances of `test_against` with specific parameters. No
test requires custom logic beyond choosing its four parameters.

### Property Tests as Degenerate Cases

Property-based tests (commutativity, sign symmetry, monotonic ordering)
are instances of the same harness where ImplA and ImplB are both the
same implementation called with transformed inputs:

```cpp
// Commutativity: add(a,b) vs. add(b,a)
test_against(
    Exhaustive<fp8_e5m2>{},
    opine_add<fp8_e5m2, IEEE754, RoundTiesToEven>,
    [](uint64_t a, uint64_t b) {
        return opine_add<fp8_e5m2, IEEE754, RoundTiesToEven>(b, a);
    },
    BitExact{},
    default_reporter
);

// Monotonic ordering: compare(a,b) vs. integer compare(a,b)
test_against(
    Exhaustive<fp8_e5m2>{},
    opine_compare<fp8_e5m2, RbjTwosComplement>,
    [](uint64_t a, uint64_t b) -> TestOutput {
        int8_t sa = static_cast<int8_t>(a);
        int8_t sb = static_cast<int8_t>(b);
        // Map integer comparison to float comparison result
        return { .bits = (sa < sb) ? -1u : (sa > sb) ? 1u : 0u };
    },
    BitExact{},
    default_reporter
);
```

### What This Harness Enables Later

When BLAS operations are implemented, the same harness tests them:
OPINE GEMM vs. a naive triple-loop reference GEMM, exhaustive for
small matrices, random for larger ones, with a `WithinULPs<N>`
comparator (because GEMM accumulation order may differ).

When SWAR is implemented, SWAR vs. scalar is a `test_against` call
with `BitExact`. No new test infrastructure needed.

When a new encoding is added, its tests are a `test_against` call
with the oracle on one side and the new implementation on the other.
The oracle already handles the new encoding (if the policy application
layer is updated). The harness doesn't change.

When someone ports OPINE to a new platform (say, MSP430), the
correctness tests are the same `test_against` calls with a different
Platform policy. If the results differ from the oracle, the Platform
implementation has a bug. The test infrastructure doesn't care what
platform is being tested.

The harness is the test infrastructure. Everything else is data.

## Exhaustive Testing for Small Formats

For FP8 formats (256 values), every unary operation can be tested
exhaustively (256 cases) and every binary operation can be tested
exhaustively (256 × 256 = 65,536 cases). This takes milliseconds.

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

## Targeted Test Cases

For formats too large for exhaustive testing, and as a complement to
exhaustive testing for small formats, targeted tests cover known-
difficult cases. These are organized by category.

**Zero boundary:** +0, -0 (if encoding has negative zero), smallest
positive normal, smallest positive denormal (if encoding has denormals),
smallest negative normal, smallest negative denormal.

**Overflow boundary:** Largest finite positive value, largest finite
negative value, the input pair whose exact result is the smallest value
that overflows, the input pair whose exact result is the largest value
that does not overflow.

**Underflow boundary:** Smallest normal value, largest subnormal value
(if encoding has denormals), the input pair whose exact result crosses
the normal/subnormal boundary, the input pair whose exact result
underflows to zero.

**Rounding boundary:** Values exactly halfway between two representable
values (tests ties-to-even vs. ties-away), values one ULP above and
below the halfway point, values where rounding causes mantissa overflow
(e.g., 1.111...1 rounds up to 10.000...0, requiring exponent increment).

**Special value interaction:** NaN + number, NaN + NaN, NaN + Inf
(for encodings with NaN), Inf + number, Inf + Inf, Inf - Inf (for
encodings with Inf), 0 × Inf, 0 / 0, Inf / Inf (invalid operations),
negative zero interactions: (-0) + (+0), (-0) × (+0), etc. (for
encodings with negative zero).

**Catastrophic cancellation:** Nearly-equal values whose subtraction
loses most significant bits, values where cancellation exposes guard
bit handling.

**Two's complement specific (RbjTwosComplement encoding):** Negation of
every value (verify round-trip, verify NaN trap value is detected),
carry propagation: values with all-zero mantissa (carry from mantissa
into exponent during negation), the most-negative value (NaN trap
value): verify it is not negated but trapped, comparison of all pairs
near zero crossing (verify monotonic integer ordering).

## Property-Based Tests

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
representable in both encodings and that the rounding mode and denormal
handling produce the same mathematical value. Where they diverge
legitimately — e.g., FlushToZero vs. Full denormals — the test applies
only to the non-divergent cases.)

## The Test Matrix

Each cell in the matrix is a test suite that must pass.

**Axes:**

Operation: pack/unpack, add, subtract, multiply, divide, compare,
negate, abs, format conversion.

Format: FP8 E5M2, FP8 E4M3, FP16, FP32.

Encoding: IEEE754, Relaxed, RbjTwosComplement, E4M3FNUZ, GPUStyle.

Rounding: TowardZero, ToNearestTiesToEven, ToNearestTiesAway,
TowardPositive, TowardNegative.

**Coverage by format size:**

FP8 × any encoding × any rounding: exhaustive (all input pairs against
the oracle).

FP16 × any encoding × any rounding: exhaustive for unary operations,
targeted + random for binary (with exhaustive binary runs in CI
nightly).

FP32 × IEEE754Encoding: validated against TestFloat, IBM vectors, and
the oracle (targeted + random).

FP32 × other encodings: targeted + random against the oracle.

**What is NOT in the test matrix:**

Elementary functions (exp, log, sin, cos): deferred until implemented.
Decimal conversion: deferred, low priority. BLAS operations: tested at
the BLAS level, not the float level. SWAR operations: tested by
verifying that SWAR results match scalar results for all inputs
(exhaustive for FP8).

## TDD Sequence for the Rewrite

The rewrite from the current policy structure to the five-axis
architecture is driven by tests. Each step has a clear pass/fail
criterion. Steps are ordered by dependency: later steps depend on
earlier steps being complete and passing.

### Step 1: Oracle Part 1 — MPFR Integration

Set up the MPFR dependency. Write thin wrappers that take two bit
patterns (in any OPINE format/encoding), decode them to MPFR values,
perform an operation at high precision (256 bits), and return the
exact MPFR result.

This step does not apply any OPINE policies to the result. It only
computes the exact mathematical answer.

Deliverable: `tests/oracle/mpfr_exact.hpp`

Pass criterion: For IEEE754Encoding FP32, verify that the MPFR exact
result, when manually rounded to FP32 precision, matches SoftFloat's
output for a sample of 10,000 random input pairs per operation. This
validates that the bit-pattern-to-MPFR decoding is correct.

### Step 2: Oracle Part 2 — Policy Application Layer

Write the policy application layer. This takes an exact MPFR result
and applies Format, Encoding, and Rounding policies to produce a bit
pattern and exception flags.

Deliverable: `tests/oracle/policy_apply.hpp`

Pass criterion: The composed oracle (Part 1 + Part 2), running in
IEEE754Encoding mode, agrees with Berkeley SoftFloat on every test
vector from TestFloat for add, subtract, multiply, divide, and compare,
for FP16 and FP32, for all four IEEE rounding modes. Additionally,
agrees with every applicable vector in the IBM FPGen test suite.

This is the critical gate. Nothing proceeds until the oracle matches
two independent external references for IEEE 754 semantics.

### Step 3: Oracle Non-IEEE Validation

Run property-based tests on the oracle's non-IEEE encodings.

Deliverable: `tests/oracle/test_oracle_properties.cpp`

Pass criterion: For RbjTwosComplement, monotonic ordering holds
exhaustively for FP8. For E4M3FNUZ, exactly one bit pattern is NaN
(exhaustive FP8). For Relaxed, no bit pattern is NaN or Inf (exhaustive
FP8). Cross-encoding consistency holds for all FP8 values representable
in both IEEE754 and each non-IEEE encoding.

### Step 4: Format + Encoding + Pack/Unpack

Implement the new five-axis types: `Format` (geometry only), `Encoding`
(what bit patterns mean), and `pack`/`unpack` parameterized on both.
Tests are written before implementation.

Deliverable: New `format.hpp`, `encoding.hpp`, `pack_unpack.hpp`.

Tests written first: Exhaustive FP8 round-trip
(`pack(unpack(x)) == x`) for IEEE754Encoding, RelaxedEncoding, and
RbjTwosComplement. For RbjTwosComplement, additionally verify monotonic
ordering (signed integer comparison equals float comparison for all
non-NaN FP8 values).

Pass criterion: All FP8 round-trip and ordering tests pass. This is a
superset of the current test suite's coverage.

### Step 5: TestFloat Shim

Write the TestFloat integration shim for OPINE's IEEE754Encoding.

Deliverable: `tests/testfloat/opine_testfloat_shim.cpp`

Pass criterion: OPINE passes TestFloat for pack/unpack, compare, and
any other operations implemented so far, for FP16 and FP32, all
rounding modes.

### Step 6: Comparison

Implement comparison. This is deliberately placed before addition and
multiplication because it is simpler and because the two's complement
encoding's comparison behavior (reduce to signed integer compare) is
a load-bearing property that must be verified before building arithmetic
on top of it.

Tests written first: Exhaustive FP8 comparison (all 65,536 pairs) for
each encoding, against the oracle. For RbjTwosComplement, additionally
verify that the comparison result equals the signed integer comparison
result (exhaustive FP8).

Deliverable: `operations/compare.hpp`, `tests/unit/test_compare.cpp`

Pass criterion: Exhaustive FP8 tests pass for all encodings. TestFloat
FP16/FP32 comparison tests pass for IEEE754Encoding.

### Step 7: Negate, Abs

Implement negate and absolute value. Critical for two's complement
(negate must handle the trap value; abs must detect it).

Tests written first: Exhaustive FP8 for all encodings against the
oracle. For RbjTwosComplement, verify trap value detection and carry
propagation for every value with an all-zero mantissa field.

Deliverable: `operations/negate_abs.hpp`, `tests/unit/test_negate_abs.cpp`

Pass criterion: Exhaustive FP8 tests pass for all encodings.

### Step 8: Addition

Tests written first: Exhaustive FP8 addition (65,536 pairs × each
encoding × each rounding mode) against the oracle. Then implement
addition.

Deliverable: `operations/add.hpp`, `tests/unit/test_add.cpp`

Pass criterion: Exhaustive FP8 tests pass. TestFloat FP16/FP32
addition tests pass for IEEE754Encoding.

### Step 9: Multiplication

Same structure as Step 8.

Deliverable: `operations/multiply.hpp`, `tests/unit/test_multiply.cpp`

Pass criterion: Exhaustive FP8 tests pass. TestFloat FP16/FP32
multiply tests pass for IEEE754Encoding.

### Step 10: Subtraction and Division

Same structure. Subtraction may share most of its implementation with
addition (effective operation determined by sign combination). Division
is the most complex arithmetic operation and will likely require the
most targeted test cases for FP16/FP32 boundary behavior.

### Step 11: Format Conversion

Conversion between formats (FP8 to FP16, FP16 to FP8, etc.) with
correct rounding and overflow/underflow handling per encoding policy.

Tests written first: Exhaustive FP8-to-FP16 and FP16-to-FP8 against
the oracle. Round-trip property for values representable in both
formats.

Pass criterion: Exhaustive conversion tests pass. Cross-format
round-trip verified.

### Step 12: Exception Flags

Implement StatusFlags and ReturnStatus exception policies. Verify
that every operation sets the correct flags for every input.

Tests written first: Extend all existing exhaustive FP8 tests to also
verify exception flags against the oracle (which already computes
them).

Pass criterion: Exhaustive FP8 tests verify both result values AND
exception flags.

## CI Integration

### Fast suite (every commit)

Exhaustive FP8 round-trip for all encodings. Exhaustive FP8 arithmetic
(add, multiply) for IEEE754Encoding with TowardZero and
ToNearestTiesToEven. Targeted FP16 and FP32 tests (edge cases, boundary
conditions). Property-based tests (commutativity, sign symmetry,
monotonicity). Compile-time tests (`static_assert` in constexpr
context).

Expected runtime: under 60 seconds.

### Full suite (nightly)

Everything in the fast suite. Exhaustive FP8 arithmetic for ALL
encoding × rounding combinations. Exhaustive FP16 unary operations for
all encodings. TestFloat FP16 and FP32 for IEEE754Encoding, all
rounding modes. IBM FPGen vector validation. Random sampling of FP32
binary operations (millions of pairs) against the oracle.

Expected runtime: 1-4 hours.

### Extended suite (weekly or pre-release)

Everything in the full suite. Exhaustive FP16 binary operations for
IEEE754Encoding (hours per operation). Paranoia diagnostic run.
Cross-encoding consistency checks. Oracle self-validation (re-verify
oracle against SoftFloat and IBM vectors).

Expected runtime: 8-24 hours.

## Build Dependencies for Testing

The test infrastructure introduces dependencies that the OPINE library
itself does not have. These are test-only dependencies.

**MPFR + GMP:** Required for the reference oracle. Available on every
platform OPINE targets for development (Linux, macOS, Windows via
MSYS2). Not required on the target platforms where OPINE runs (6502,
Cortex-M0, RV32IM) — the oracle runs on the development host, not the
target.

**Berkeley SoftFloat:** Required for oracle validation and TestFloat
integration. C source, BSD-licensed, compiles with any ISO C compiler.
Linked into the test executables, not the OPINE library.

**Berkeley TestFloat:** Required for IEEE 754 conformance testing. Same
build requirements as SoftFloat. The `testfloat_gen` and `testfloat_ver`
programs are standalone executables.

None of these dependencies affect the OPINE library itself, which
remains a header-only C++ library with no external dependencies. The
test infrastructure is heavier than the library. This is appropriate —
the tests are more important than the library.

## What This Testing Strategy Proves

When someone asks "does OPINE correctly implement E4M3FNUZ with
round-toward-zero?", the answer is: every one of the 65,536 possible
input pairs for each binary operation has been tested against a
reference oracle. The oracle computes the mathematically exact result
using MPFR (trusted, external, arbitrary-precision) and applies the
E4M3FNUZ encoding policy step by step using a 200-line policy
application layer that has been validated against Berkeley SoftFloat
and IBM FPGen vectors for IEEE 754 semantics, and against property-
based invariants for non-IEEE semantics.

When someone asks "is OPINE's IEEE 754 mode actually IEEE 754
compliant?", the answer is: it passes Berkeley TestFloat for all
implemented operations, all rounding modes, for FP16 and FP32. These
are the same tests used to validate hardware FPUs.

When someone asks "how do I know the two's complement encoding is
correct?", the answer is: the monotonic ordering property (signed
integer comparison equals float comparison) has been verified
exhaustively for all FP8 values. Every arithmetic operation has been
verified against the reference oracle for all 65,536 input pairs.
The oracle's exact-result computation is MPFR (trusted); the oracle's
policy application has been verified against SoftFloat for IEEE 754
and against structural invariants for two's complement.

When someone asks "why should I trust the oracle?", the answer is:
Part 1 is MPFR, which is used by GCC and has been the arbitrary-
precision reference for 25 years. Part 2 is 200 lines of conditional
logic that has been cross-validated against two independent IEEE 754
references (SoftFloat and IBM FPGen). The bug surface is small,
inspectable, and independently verified.

These are not claims. They are CI jobs.
