# Actionable Design Guidance for OPINE

*Distilled from 10 foundational papers on floating-point arithmetic*

---

## 1. Policy Architecture: What to Make Configurable

The papers collectively identify **seven orthogonal dimensions** where applications legitimately need different behavior. OPINE's policy system should expose all of these:

### Rounding Mode Policy
| Option | Use Case | Paper Source |
|--------|----------|--------------|
| Round-to-nearest-even | Default; prevents drift in iterated calculations | Goldberg |
| Round-toward-zero | Truncation semantics; some hardware default | Goldberg |
| Round-toward-±∞ | Interval arithmetic, conservative bounds | Kahan |
| Stochastic rounding | ML training (emerging technique) | — |

**Action**: Implement all four IEEE modes plus stochastic. Round-to-even should be default because Goldberg proves other modes cause unbounded drift in sequences like `x₁ = (x₀ - y) + y`.

### Special Value Policy
| Value | Keep It When... | Drop It When... | Paper Source |
|-------|-----------------|-----------------|--------------|
| NaN | Need robust zero-finders, error propagation | Pure forward computation, ML inference | Goldberg, FP8 |
| ±Infinity | Need safe overflow behavior | Can saturate instead (DL), memory-constrained | Goldberg, FP8 |
| Denormals | Need `x - y = 0 ⟺ x = y` invariant | Performance-critical, FTZ acceptable | Goldberg, Monniaux |
| Signed zero | Need correct limit behavior at branch cuts | Don't care about `1/+0` vs `1/-0` | Goldberg |

**Action**: Make each special value independently toggleable. The FP8 paper shows E4M3 drops infinities and most NaNs to gain range—this is a valid tradeoff OPINE should support.

### Precision Policy
The papers identify distinct precision needs by operation type:

| Operation | Minimum Precision | Paper Source |
|-----------|-------------------|--------------|
| Storage | Target format (fp8, fp16, fp32) | Mixed Precision |
| Accumulation | FP32 minimum for sums/dot products | Mixed Precision |
| Intermediates | Wider is better (Kahan: use widest available) | Kahan, Monniaux |
| Master weights | FP32 (for ML training) | Mixed Precision |

**Action**: OPINE should support mixed-precision patterns where storage type ≠ computation type. The `Accumulator<Storage, Compute>` pattern from the Mixed Precision paper is essential.

---

## 2. Accuracy Specification: How to Define "Correct"

### Use ULPs, Not Absolute Error

Dawson's paper is definitive: **specify and test accuracy in ULPs** (units in last place), not absolute error.

| Accuracy Level | ULP Bound | Typical Use Case |
|----------------|-----------|------------------|
| Correctly rounded | 0.5 ulp | Scientific computing, reproducibility |
| Faithfully rounded | < 1 ulp | High-quality libm |
| Fast | 1-4 ulps | Most applications |
| Approximate | 4-16 ulps | Graphics, games, ML inference |

**Action**: Every OPINE function should document its ULP bound as a compile-time constant. Users select accuracy tier via policy.

### The RLibm Insight: Approximate the Rounded Result

RLibm-Prog and RLibm-32 prove that **approximating the correctly-rounded result** (not the mathematical value) enables:
- Lower-degree polynomials (faster)
- Correct rounding for all inputs
- Progressive evaluation (fewer terms for lower precision)

**Action**: For elementary functions, consider RLibm-style polynomial generation. Three RLibm polynomials are already in LLVM's libm—these are production-proven.

---

## 3. Implementation Strategy: What Code to Generate

### The Two-Path Pattern

Multiple papers (Gay, Gal, RLibm) converge on the same structure:

```
Fast path: Pure floating-point, handles 95%+ of inputs
Slow path: Extended precision or arbitrary precision, handles edge cases
```

Gay shows decimal conversion: fast path is as fast as incorrect libraries; slow path is 5-10× slower but rare.

**Action**: OPINE's implementation policies should support:
- `FastOnly`: Skip slow path, accept occasional wrong rounding
- `Correct`: Include slow path for guaranteed correctness
- `Progressive`: Use fast path for low precision, slow path for high precision

### Platform-Specific Hazards

Monniaux catalogs what breaks IEEE semantics in practice:

| Hazard | Platforms Affected | Mitigation |
|--------|-------------------|------------|
| x87 extended precision | x86 32-bit | Use SSE, or `-ffloat-store` |
| Double rounding | x87, any wider intermediate | Avoid intermediate precision > target |
| FMA changes rounding | PowerPC, modern x86 | Design assuming FMA or explicitly disable |
| Compiler "optimizations" | All | Use `-fp-model precise`, avoid `-ffast-math` |

**Action**: OPINE should detect and document which platform hazards apply. Consider generating compiler pragmas or intrinsics to ensure intended behavior.

### Assembly vs. Portable Code

Gal's accurate tables method achieves 99.7-99.9% correct rounding with:
- Table lookup (single memory access)
- Low-degree polynomial (4-6 terms)
- Extended-precision combination

**Action**: OPINE's progressive optimization story should be:
1. Start with portable C++ (works everywhere)
2. Add lookup tables (better accuracy, moderate code size)
3. Add platform assembly (best performance on specific targets)

---

## 4. Testing Infrastructure: How to Validate

### Exhaustive Testing is Feasible for Small Formats

RLibm-32 tests all 2³² float inputs. For OPINE's 8-bit and 16-bit formats, exhaustive testing is trivial:
- FP8: 256 values, instant
- FP16: 65,536 values, milliseconds
- FP32: 4 billion values, hours (but doable)

**Action**: Exhaustively test all 8-bit and 16-bit format behaviors. For 32-bit, use exhaustive testing for at least one platform as ground truth.

### Test Against Higher Precision, Not Math

Dawson's key insight: test `float sin(float x)` against `(float)((double)sin((double)x))`, not against infinite-precision sin(x).

**Action**: OPINE's test oracle should be the next-higher-precision format, not arbitrary-precision math (except for generating ground truth tables).

### Test All Rounding Modes

Monniaux documents that glibc's `pow()` **segfaults** and `exp(1)` returns 2⁵⁰² in round-toward-+∞ mode.

**Action**: Test every function in all four rounding modes, not just round-to-nearest. Most libraries fail this.

### Edge Cases to Cover

From Dawson and Goldberg, the critical edge cases:

| Category | Specific Cases |
|----------|----------------|
| Zero boundary | +0, -0, smallest positive, smallest negative |
| Overflow boundary | Largest finite, overflow threshold |
| Underflow boundary | Smallest normal, largest subnormal, underflow threshold |
| Special values | ±∞, NaN (quiet and signaling), all NaN payloads |
| Catastrophic cancellation | Nearly-equal operands that cancel |

---

## 5. Format Support: What to Implement

### Standard Formats (must have)

| Format | Bits | Use Case | Paper Source |
|--------|------|----------|--------------|
| E5M2 | 8 | ML gradients (needs range) | FP8 |
| E4M3 | 8 | ML weights/activations (needs precision) | FP8 |
| Binary16 | 16 | ML inference, graphics | IEEE 754 |
| BFloat16 | 16 | ML training | Mixed Precision |
| Binary32 | 32 | General purpose | IEEE 754 |
| Binary64 | 64 | Scientific computing | IEEE 754 |

### Custom Format Support

The FP8 paper shows that domain-specific formats can outperform standard ones. OPINE's format descriptor system should support:
- Arbitrary exponent/mantissa split
- Configurable bias
- Presence/absence of implicit bit
- Presence/absence of special value encodings

**Action**: The format descriptor you have is on the right track. Ensure it can express E4M3's "no infinities, one NaN" encoding.

---

## 6. Conversion Operations: Critical Details

### Decimal-Binary Conversion

Gay's paper establishes the requirements:

| Requirement | Float | Double |
|-------------|-------|--------|
| Round-trip digits | 9 | 17 |
| Exactly representable integers | Up to 2²⁴ | Up to 2⁵³ |
| Exactly representable powers of 10 | 10⁰ to 10¹⁰ | 10⁰ to 10²² |

**Action**: If OPINE includes string conversion, implement Gay's algorithm or document that it's approximate.

### Format-to-Format Conversion

The FP8 and Mixed Precision papers establish conversion semantics:

| Source | Destination | Handling |
|--------|-------------|----------|
| Wider → Narrower | Normal | Round according to policy |
| Wider → Narrower | Overflow | Saturate or infinity (policy) |
| Wider → Narrower | Underflow | Denormal or flush-to-zero (policy) |
| ∞ or NaN → E4M3 | — | Both become NaN (E4M3 has no ∞) |

**Action**: Conversion between OPINE formats should respect both source and destination policies. Document the interaction.

---

## 7. Documentation Requirements

### What Users Need to Know

From Kahan's critique and Monniaux's catalog, users need:

1. **Exactly which IEEE 754 features are relaxed** and why
2. **ULP bounds for every operation** (not just "approximate")
3. **Platform-specific behavior** (what changes on x87 vs SSE vs ARM)
4. **Which algorithms break** under relaxed semantics (e.g., Dekker splitting requires exact rounding)

### The FP8 Paper as Documentation Model

The FP8 paper is exemplary documentation for a non-IEEE format:
- Explicit comparison table vs IEEE
- Rationale for every deviation
- Empirical validation that deviations don't break target use cases

**Action**: For every OPINE policy combination that deviates from IEEE 754, document it like the FP8 paper does.

---

## 8. Architectural Principles

### From the Papers: What Works

| Principle | Source | OPINE Application |
|-----------|--------|-------------------|
| Separate format from behavior | All | Policy-based design ✓ |
| Compile-time configuration | Kahan, performance papers | Template policies ✓ |
| Mixed precision patterns | Mixed Precision | Storage type ≠ compute type |
| Progressive refinement | RLibm-Prog | Fewer polynomial terms for lower precision |
| Platform abstraction | Monniaux | Implementation policy hides hardware quirks |

### The Kahan Rules (for intermediate precision)

1. Use the widest precision available for intermediate calculations
2. Round only the final result to the target precision
3. Avoid subtracting nearly-equal quantities when possible
4. When you must subtract nearly-equal quantities, compute them to extra precision first

**Action**: OPINE should make it easy to follow these rules (wide intermediates) and possible to violate them (when performance demands it).

---

## 9. Priorities for OPINE Development

Based on the papers, here's a prioritized roadmap:

### Phase 1: Foundation (Current)
- ✅ Format descriptors
- ✅ Pack/unpack
- ✅ Type selection policies
- ⬜ Basic arithmetic (+, -, ×, ÷) with rounding policies
- ⬜ Comparison operations (with proper NaN/signed-zero handling)

### Phase 2: Correctness Infrastructure
- ⬜ Exhaustive test suite for 8-bit and 16-bit formats
- ⬜ ULP error measurement utilities
- ⬜ Test all rounding modes
- ⬜ Edge case coverage (the tables above)

### Phase 3: Elementary Functions
- ⬜ exp, log (most commonly needed)
- ⬜ sin, cos (with proper argument reduction—this is hard)
- ⬜ Consider RLibm-style polynomial generation

### Phase 4: Platform Optimization
- ⬜ x86 SSE/AVX paths
- ⬜ ARM NEON paths
- ⬜ Detect and handle platform hazards (x87, FMA)

### Phase 5: ML-Specific Features
- ⬜ Loss scaling utilities
- ⬜ FP32 accumulation patterns
- ⬜ Stochastic rounding

---

## 10. Key Quotes to Remember

> "IEEE 754's 'complexity' exists for good reasons — each feature solves real numerical problems." —Goldberg

> "Correctly rounded conversions should be the default." —Gay

> "The excuse 'correctly rounded libraries are too slow' is no longer valid." —RLibm-32

> "Reproducibility is sometimes needed by some programmers. Predictability within controllable limits is needed by all programmers all the time." —Kahan

> "For a math library to be reliable, it must be tested against the actual execution environment, not just the IEEE 754 specification." —Monniaux

---

## Summary: The Three Big Ideas

1. **Make tradeoffs explicit**: Every IEEE 754 feature has a cost and a benefit. OPINE's value is letting users choose which costs to pay.

2. **Measure in ULPs**: Accuracy guarantees must be quantified in ULPs, tested exhaustively for small formats, and documented per-function.

3. **Correct rounding is achievable**: RLibm proves you can have both correctness and speed. But you can also legitimately choose to trade correctness for more speed—just document the tradeoff.

---

## Source Papers

Full summaries available in [docs/reference/papers/summaries.md](../reference/papers/summaries.md):

1. Goldberg (1991) — "What Every Computer Scientist Should Know About Floating-Point Arithmetic"
2. Dawson (2012) — "Comparing Floating Point Numbers"
3. Gay (1990) — "Correctly Rounded Binary-Decimal and Decimal-Binary Conversions"
4. FP8 Paper (NVIDIA/Arm/Intel 2022) — "FP8 Formats for Deep Learning"
5. Mixed Precision Training (Baidu/NVIDIA 2018)
6. RLibm-Prog (Rutgers 2022) — Progressive polynomial approximations
7. Monniaux (2008) — "The Pitfalls of Verifying Floating-Point Computations"
8. Gal & Bachelis (1991) — "An Accurate Elementary Mathematical Library"
9. Kahan & Darcy (1998) — "How Java's Floating-Point Hurts Everyone Everywhere"
10. RLibm-32 (PLDI 2021) — Correctly rounded 32-bit math libraries
