# Summary of Goldberg's "What Every Computer Scientist Should Know About Floating-Point Arithmetic"

## Relevance to Fast Math Library Design

This 1991 paper (reprinted by Sun Microsystems) is a foundational reference for understanding the tradeoffs involved in IEEE 754 conformance. Here are the key points relevant to designing a math library that must decide which IEEE 754 features to support and how strictly:

---

## 1. Rounding and Guard Digits

**Key finding:** A single guard digit is sufficient to guarantee that addition and subtraction are accurate (relative error bounded by 2ε), but this is *not* the same as exact rounding.

**Exactly rounded operations** (computing as if with infinite precision, then rounding to nearest representable float) provide stronger guarantees than merely using guard digits. The IEEE standard requires exactly rounded results for basic operations (+, -, ×, ÷, √).

**Implication for fast math:** If you relax exact rounding to "1-2 ulp accuracy," you lose:
- Bit-identical results across platforms
- The ability to use certain algorithms that depend on exact rounding (e.g., Dekker's splitting algorithm for extended precision arithmetic)
- Theorem 7's guarantee that `(m ÷ n) × n = m` for certain cases

---

## 2. Round-to-Even vs. Other Rounding Modes

**Key finding:** Round-to-even (banker's rounding) prevents systematic drift in iterated calculations. Theorem 5 shows that with round-to-even, the sequence `x₀, x₁ = (x₀ - y) + y, x₂ = (x₁ - y) + y, ...` either stabilizes at x or oscillates between two values. With round-up, values can drift unboundedly.

**Implication for fast math:** If you allow non-default rounding modes or inconsistent rounding, long-running numerical algorithms may accumulate bias.

---

## 3. Special Values: NaN, Infinity, Denormals

### NaNs
- Allow computations to continue when mathematically undefined operations occur (0/0, √(-1), ∞-∞)
- Propagate through calculations, carrying diagnostic information in the significand
- Essential for robust algorithms like zero-finders that may probe outside a function's domain

### Infinities
- Safer than saturating to the largest representable number
- Example given: computing √(x² + y²) where x² overflows. With infinity arithmetic, the result is ∞ (safe). With saturation to max float, you get a wildly wrong finite answer.

### Denormalized Numbers (Subnormals)
- Provide "gradual underflow" — the gap between zero and the smallest positive number is the same as the gap between adjacent small numbers
- Without denormals, the property `x - y = 0 ⟺ x = y` fails
- **Performance cost noted:** "Programs that frequently underflow often run noticeably slower on hardware that uses software traps" for denormals

**Implication for fast math:** 
- Flushing denormals to zero (FTZ) breaks the `x - y = 0 ⟺ x = y` invariant
- Disabling NaN/infinity propagation breaks algorithms that rely on them for robustness
- However, handling these special cases has real performance costs

---

## 4. Exception Flags and Traps

IEEE 754 defines five exception flags: invalid, overflow, underflow, division by zero, inexact.

**Key insight:** Flags (sticky bits that record exceptions) are often more useful than traps (which interrupt execution). The paper gives an example of computing `arccos(-1)` via `2·arctan(√((1-x)/(1+x)))` — this correctly produces π, but sets the divide-by-zero flag spuriously. A well-written library function can save/restore flags to hide such internal artifacts.

**Implication for fast math:** If your library doesn't preserve flag semantics correctly, user code that checks flags for diagnostic purposes will get spurious results.

---

## 5. Extended Precision and the "Double Rounding" Problem

The x87 FPU's 80-bit extended precision creates problems: when a calculation is done in 80-bit precision and then rounded to 64-bit double, you can get "double rounding" errors where the result differs from what you'd get with native 64-bit arithmetic.

**Implication for fast math:** Intermediate precision affects final results. If your library uses higher internal precision for speed or accuracy, document this behavior — it may cause reproducibility issues.

---

## 6. Compiler Optimizations That Break IEEE Semantics

The paper identifies several "safe-looking" optimizations that are **not** valid for floating-point:

| Optimization | Why it's invalid |
|--------------|------------------|
| `x + 0 → x` | Fails when x is -0 (should give +0 in default rounding) |
| `x × 0 → 0` | Fails when x is NaN or ∞ |
| `x - x → 0` | Fails when x is NaN or ∞ |
| `x / x → 1` | Fails when x is 0, NaN, or ∞ |
| Reassociation `(a + b) + c → a + (b + c)` | Changes rounding behavior |

**Implication for fast math:** A "fast math" mode that enables these optimizations will produce different (sometimes wrong) results for edge cases. Users must understand this tradeoff.

---

## 7. Binary-Decimal Conversion

**Theorem 15:** Converting a 32-bit float to 8 decimal digits and back does not guarantee round-trip accuracy. You need 9 decimal digits for float, 17 for double.

**Implication for fast math:** If your library includes formatting/parsing routines, cutting corners on decimal conversion precision will cause data corruption on round-trips through text.

---

## Summary Table for Library Designers

| IEEE 754 Feature | Conformance Cost | Non-Conformance Risk |
|------------------|------------------|----------------------|
| Exact rounding on +,-,×,÷,√ | Hardware cost, potential software fallback | Breaks algorithms using Dekker splitting, loses cross-platform reproducibility |
| Round-to-even default | Negligible | Systematic drift in long calculations |
| NaN propagation | Branch/special-case handling | Silent wrong answers, broken zero-finders |
| Infinity arithmetic | Branch/special-case handling | Wrong finite answers on overflow |
| Denormal support | Significant performance cost on some hardware | `x-y=0` no longer implies `x=y` |
| Exception flags | State management overhead | Spurious diagnostics, broken user flag-checking code |
| Correct 0 and ∞ signed behavior | Edge-case logic | `1/(1/x) ≠ x` for x = ±∞, sign errors in limit calculations |

---

## Bottom Line

Goldberg's paper argues that IEEE 754's "complexity" exists for good reasons — each feature solves real numerical problems. A fast math library that relaxes these guarantees should:

1. **Document precisely** which guarantees are relaxed
2. **Provide modes** for users who need strict conformance
3. **Understand the algorithms** that will break under relaxed semantics (extended precision arithmetic, certain iterative methods, functions that rely on infinity/NaN propagation)

---

# Summary of Dawson's "Comparing Floating Point Numbers, 2012 Edition"

## Relevance to Fast Math Library Design

This practical blog post by Bruce Dawson (a game developer at Valve/Google) addresses how to test whether two floating-point numbers are "close enough" to be considered equal. This is directly relevant to math library design because it affects how you **validate** your library's outputs and what **accuracy guarantees** you can meaningfully offer.

---

## 1. The Problem with Exact Equality

Floating-point calculations accumulate rounding errors, so testing `f1 == f2` is usually inappropriate. Dawson demonstrates with a simple example:

```c
float f = 0.1f;
float sum = 0;
for (int i = 0; i < 10; ++i) sum += f;
float product = f * 10;
```

Results:
- `sum` = 1.000000119209290 (accumulated rounding errors)
- `product` = 1.000000000000000 (two rounding errors that happened to cancel)
- `f * 10` (in double) = 1.000000014901161 (exact result of 10 × float(0.1))

**Key insight:** Even "correct" results aren't identical, and sometimes getting the mathematically exact answer (1.0) happens by accident (canceling errors), not design.

---

## 2. Three Comparison Methods

### Absolute Epsilon
```c
bool isEqual = fabs(f1 - f2) <= epsilon;
```

**Problem:** A fixed epsilon (like `FLT_EPSILON ≈ 1.19e-7`) is:
- Too large for numbers near zero
- Meaningless for numbers > 2.0 (where adjacent floats are already farther apart than `FLT_EPSILON`)
- For numbers > 16,777,216, adjacent floats differ by more than 1.0

### Relative Epsilon
```c
float diff = fabs(f1 - f2);
float largest = max(fabs(f1), fabs(f2));
bool isEqual = diff <= largest * maxRelDiff;
```

**Better:** Scales with magnitude. But still problematic near zero (relative error becomes meaningless when comparing to zero).

### ULP-Based Comparison (Dawson's Preferred Method)

**Key insight:** Due to IEEE 754's design, adjacent floats have adjacent integer representations (when viewed as sign-magnitude integers). Therefore:

```c
int ulpsDiff = abs(IsInt(f1) - IsInt(f2));
bool isEqual = ulpsDiff <= maxUlps;
```

This directly measures "how many representable floats apart" two values are.

**Advantages:**
- Automatically scales with magnitude
- Directly meaningful: "these values differ by N representable numbers"
- Works uniformly across the float range

**Caveats:**
- Breaks down across zero (positive and negative floats have integer representations that are far apart)
- Requires special handling for +0 vs -0
- NaN handling required

---

## 3. Catastrophic Cancellation Example

Dawson shows a subtle case: `sin(float(pi))` returns ≈ -8.74e-8, which looks like a huge error compared to the "correct" answer of 0.

**But this is actually correct!** The sin function is computing `sin(float(pi))`, not `sin(π)`. Since `float(pi) ≈ π - 8.74e-8`, and `sin(π - θ) ≈ -θ` for small θ, the result is:

| Value | Result |
|-------|--------|
| `float(pi)` | 3.1415927410125732 |
| `sin(float(pi))` | -0.0000000874227800 |
| `float(pi) + sin(float(pi))` | 3.1415926535897966 (closer to true π!) |

**Implication for library testing:** What looks like large error may be correct behavior given the input. You must understand what you're actually computing.

---

## 4. Practical Recommendations

Dawson's decision tree:

| Comparing Against | Method |
|-------------------|--------|
| Zero | Absolute epsilon based on input magnitudes and algorithm analysis |
| Non-zero known value | Relative epsilon or ULP-based |
| Two arbitrary numbers | Combine absolute and relative (handle near-zero separately) |

**The combined approach:**
```c
bool AlmostEqualRelativeAndAbs(float A, float B,
                                float maxDiff,
                                float maxRelDiff = FLT_EPSILON) {
    float diff = fabs(A - B);
    if (diff <= maxDiff)          // Absolute check for near-zero
        return true;
    float largest = max(fabs(A), fabs(B));
    if (diff <= largest * maxRelDiff)  // Relative check
        return true;
    return false;
}
```

---

## 5. Implications for Fast Math Library Design

### For Testing Your Library

1. **Don't use naive equality tests.** Even correct implementations will fail exact equality checks.

2. **ULP-based accuracy is the right metric.** Specify accuracy as "within N ulps" rather than absolute error bounds. This is how libm implementations are typically specified (e.g., "correctly rounded" = 0.5 ulp, "faithfully rounded" = < 1 ulp).

3. **Test against higher precision, not mathematical ideals.** Compare `float sin(float x)` against `(float)((double)sin((double)x))`, not against infinite-precision sin(x).

4. **Understand your inputs.** `sin(float_pi)` computing the error in `float_pi` is a feature, not a bug.

### For Documenting Accuracy Guarantees

1. **Specify error in ulps.** "Maximum error: 2 ulps" is more meaningful than "maximum error: 1e-6."

2. **Handle edge cases explicitly.** Document behavior at:
   - Zero crossings
   - Subnormal inputs/outputs  
   - Overflow/underflow boundaries
   - Special values (NaN, ±∞)

3. **Distinguish argument reduction error from function error.** For periodic functions like sin/cos, most "error" comes from imprecise π, not from the core algorithm.

### For Choosing Accuracy/Speed Tradeoffs

The key question: **How many ulps of error will your users tolerate?**

- Graphics/games: Often 4-16 ulps acceptable
- Scientific computing: Typically demand < 1 ulp (faithfully rounded)
- Financial: May require exact decimal arithmetic (not IEEE 754 at all)

A "fast math" library can legitimately offer weaker guarantees (say, 4 ulps instead of 0.5 ulps) if this enables significant speedups, but this must be clearly documented and the testing infrastructure must measure ulp error, not just "close enough."

---

## Summary Table

| Concept | Implication for Library Design |
|---------|-------------------------------|
| ULP as error metric | Specify and test accuracy in ulps, not absolute error |
| Relative vs absolute epsilon | Near-zero behavior needs special attention in testing |
| Catastrophic cancellation | Understand what your function actually computes given representable inputs |
| `FLT_EPSILON` limitations | Don't use fixed epsilon for validation; scale with magnitude |
| +0 vs -0 | Comparison code must handle signed zero correctly |

---

# Summary of Gay's "Correctly Rounded Binary-Decimal and Decimal-Binary Conversions" (1990)

## Relevance to Fast Math Library Design

This is a foundational paper by David M. Gay (AT&T Bell Labs) that establishes practical algorithms for converting between decimal strings and binary floating-point with **correct rounding**. The implementations (`strtod` and `dtoa`) became the de facto standard used in most C libraries.

---

## 1. The Core Problem

When converting between decimal and binary floating-point:
- Most decimal numbers (like 0.1) cannot be exactly represented in binary
- Most binary floats cannot be exactly represented in finite decimal digits
- **Correctly rounded** means finding the nearest representable number, with ties broken appropriately (round-to-even for IEEE)

Two directions:
1. **Decimal → Binary** (`strtod`): Given "1.23e-20", find the nearest IEEE double
2. **Binary → Decimal** (`dtoa`): Given a double, produce a decimal string that round-trips correctly

---

## 2. Decimal-to-Binary Conversion

### Fast Path (Typical Cases)
When the decimal number has few digits and a moderate exponent, exact conversion is possible with a single floating-point operation:

- If `d = (integer) × 10^k` where both the integer and `10^k` are exactly representable as floats, one multiply or divide suffices
- For IEEE double: integers up to 2^53, powers of 10 up to 10^22

**Key insight:** Most real-world inputs fall into this fast path.

### Slow Path (Hard Cases)
When the fast path doesn't apply:
1. Compute an initial approximation b^(1) using floating-point
2. Use arbitrary-precision integer arithmetic to check if b^(1) is correctly rounded
3. If not, compute a correction δ (in ulps) and iterate
4. **Guaranteed to converge in at most 3 iterations** (usually ≤2)

The paper shows how to formulate the "is this correctly rounded?" test as integer arithmetic:
```
2M(d - b) ≤ M × β^(e-p+1)
```
where M is chosen to make all quantities integers.

---

## 3. Binary-to-Decimal Conversion

### The Steele-White Problem
Given a binary float b, find the **shortest** decimal string that rounds back to b. This is important for:
- Human readability (printing 0.1 instead of 0.10000000149011612)
- Round-trip correctness
- Data interchange (AMPL modeling language was Gay's motivation)

### Algorithm Structure
Maintain the invariant:
```
b = (b^(j) / S) × 10^(k-j) + Σ d_i × 10^(k-i)
```
Generate digits by:
```
d_j = floor(b^(j) / S)
b^(j+1) = 10 × (b^(j) mod S)
```

### Fast Path
When the number of output digits n is small (e.g., n ≤ 14 for double):
- Use floating-point arithmetic with error tracking
- Check if accumulated error allows confident rounding decision
- If uncertain, fall back to exact integer arithmetic

### Slow Path
For "hard cases" (borderline rounding, many digits needed):
- Use arbitrary-precision integer arithmetic
- Required for guaranteed correctness

---

## 4. Performance Results (1990 Hardware)

Gay measured on SGI (IEEE), Amdahl (IBM mainframe), and VAX:

### Decimal-to-Binary (strtod)

| Case | strtod | Library atof |
|------|--------|--------------|
| Typical (1.23e+20) | 11-16 µs | 15-24 µs |
| Hard (1.23e-30) | 101-380 µs | 20-413 µs |

**Finding:** For typical cases, correctly-rounded conversion is **as fast or faster** than the (incorrect) library routines. Hard cases are 5-10× slower but rare.

### Binary-to-Decimal (dtoa)

| Case | dtoa (6 digits) | ecvt | sprintf |
|------|-----------------|------|---------|
| Typical | 20-33 µs | 10-21 µs | 37-78 µs |
| Shortest string | 109-1565 µs | N/A | N/A |

**Finding:** Computing shortest strings is expensive but dtoa for fixed digits is competitive with (less accurate) library routines.

---

## 5. Key Implications for Fast Math Library Design

### What "Fast" Means for Conversion

1. **The common case is already fast.** Most real-world decimal inputs convert with 1-6 floating-point operations. Optimizing the rare hard cases provides diminishing returns.

2. **Correctness has modest cost.** Gay's conclusion: "correctly rounded conversions should be the default" because the cost is small for typical inputs.

3. **The hard cases are unavoidable for correctness.** Any implementation claiming correct rounding must handle arbitrary-precision arithmetic for borderline cases.

### Design Tradeoffs

| Feature | Benefit | Cost |
|---------|---------|------|
| Correct rounding | Bit-identical results, round-trip safety | Arbitrary-precision fallback code |
| Shortest-string output | Human readability, compact storage | 3-10× slower than fixed-digit output |
| IEEE inexact flag checking | Can avoid some slow paths | Platform-dependent, flag manipulation overhead |

### Recommendations from Gay

1. **Default to correct rounding.** "The principle of least surprise suggests that correctly rounded conversions should be the default."

2. **Provide fast approximate alternatives.** "There are many situations where precise conversions are not needed and where trading speed for accuracy is desirable."

3. **Optimize the fast path aggressively.** The algorithm structure allows the common case to use pure floating-point arithmetic.

---

## 6. Specific Implementation Details

### Magic Numbers for IEEE Double
- Integers exactly representable: up to 2^53 (9,007,199,254,740,992)
- Powers of 10 exactly representable: 10^0 through 10^22
- Round-trip requires 17 decimal digits (15 for float)

### Initial Approximation Quality
Gay achieves initial guesses within 6.01 ulps using:
- At most 6 floating-point operations for IEEE
- At most 4 for IBM mainframe
- At most 3 for VAX

This guarantees convergence in ≤3 correction iterations.

### Integer Arithmetic Optimization
Keep separate track of powers of 2 and powers of 5 to minimize big-integer sizes. The scaling factors M, S, b^(0) can often be smaller than naïve formulas suggest.

---

## 7. Relevance to Modern Libraries

Gay's `dtoa.c` became the basis for:
- glibc's printf/scanf
- Python's float parsing
- JavaScript's number conversion
- Many other language runtimes

The algorithms remain the state of the art. Modern improvements focus on:
- SIMD acceleration of the fast path
- Better branch prediction for case dispatch
- Memory allocation optimization for big integers

---

## Summary Table

| Aspect | Recommendation for Fast Math Library |
|--------|-------------------------------------|
| Default behavior | Correctly rounded (Gay's algorithms) |
| Fast path | Pure floating-point for typical inputs |
| Slow path | Arbitrary-precision integer fallback |
| Shortest-string printing | Optional; significantly slower |
| Approximate mode | Provide as explicit opt-in for speed-critical code |
| Testing | Use Schryer-style boundary cases to verify correctness |

---

# Summary of "FP8 Formats for Deep Learning" (NVIDIA/Arm/Intel, 2022)

## Relevance to Fast Math Library Design

This paper is a joint proposal from NVIDIA, Arm, and Intel for an 8-bit floating-point format specifically designed for deep learning workloads. It demonstrates a case study in **intentionally deviating from IEEE 754** for domain-specific performance gains, while trying to minimize the breakage of software assumptions.

---

## 1. The Two FP8 Encodings

The paper proposes two complementary 8-bit formats:

| Property | E4M3 | E5M2 |
|----------|------|------|
| Exponent bits | 4 | 5 |
| Mantissa bits | 3 | 2 |
| Exponent bias | 7 | 15 |
| Dynamic range (binades) | 18 | 32 |
| Max representable value | 448 | 57,344 |
| Min normal | 2⁻⁶ | 2⁻¹⁴ |
| Min subnormal | 2⁻⁹ | 2⁻¹⁶ |
| Infinities | **Not represented** | Yes (IEEE-style) |
| NaN encodings | **Only 1 pattern** | 3 patterns (IEEE-style) |

### Recommended Usage
- **E4M3**: Weights and activations (forward pass) — needs more precision
- **E5M2**: Gradients (backward pass) — needs more dynamic range

---

## 2. Key Design Decisions

### Deviations from IEEE 754

**E4M3 deviates significantly:**
- No infinities represented
- Only one NaN encoding (S.1111.111₂)
- Reclaimed bit patterns used for normal values

**Rationale:** The narrow 4-bit exponent gives only 17 binades of dynamic range. By eliminating infinities and most NaN encodings, they gain one extra binade (18 total) and 7 additional representable magnitudes (256, 288, 320, 352, 384, 416, 448).

**E5M2 follows IEEE conventions:**
- Infinities and multiple NaN encodings preserved
- Can be viewed as "IEEE FP16 with fewer mantissa bits"
- Straightforward conversion to/from FP16

### What They Preserved (and Why)

| IEEE Property | Preserved? | Rationale |
|---------------|------------|-----------|
| Sign-magnitude representation | Yes | Enables integer comparison/sorting of floats |
| Symmetric ±0 | Yes | Breaking symmetry would invalidate many algorithms |
| Symmetric ±NaN | Yes | Same reason |
| Subnormal numbers | Yes | Gradual underflow important for DL gradients |
| Exponent bias convention | Yes | Per-tensor scaling in software is more flexible |

---

## 3. The Scaling Factor Approach

A critical design choice: **per-tensor scaling factors in software** rather than programmable exponent bias in hardware.

### Why Not Programmable Bias?
- Programmable bias only allows powers of 2 as scaling
- Per-tensor software scaling allows any real value
- Some networks require different scaling for different tensors of the same type
- More flexibility for future algorithms

### How Scaling Works
1. Before converting FP16/FP32 → FP8: multiply by scaling factor
2. Values that overflow are **saturated** (not wrapped or made infinite)
3. After FP8 → FP16/FP32: multiply by inverse scaling factor
4. For matrix multiply: unscaling is amortized over many multiply-accumulates

### Key Finding on Scaling
The paper shows that some networks (e.g., GPT-3 with residual connections in FP8) cannot work with a single global exponent bias. Per-tensor calibration is necessary:

| Configuration | Perplexity (GPT-3 1.3B) |
|---------------|-------------------------|
| BF16 baseline | 10.19 |
| E4M3 GEMM-only, bias=7 | 10.29 (matches) |
| E4M3 GEMM+residuals, bias=7 | 12.59 (fails) |
| E4M3 GEMM+residuals, per-tensor scaling | 10.44 (matches) |

---

## 4. Empirical Results

### Training Accuracy (FP8 vs FP16/BF16 Baseline)

**Image Classification (ImageNet):**

| Model | Baseline Top-1 | FP8 Top-1 |
|-------|----------------|-----------|
| ResNet-50 | 76.71 | 76.76 |
| ResNet-101 | 77.51 | 77.48 |
| DenseNet-169 | 76.97 | 76.83 |
| DeiT (Transformer) | 80.08 | 80.02 |
| MobileNet v2 | 71.65 | 71.04 (gap) |

**Language Models (Perplexity, lower is better):**

| Model | Baseline | FP8 |
|-------|----------|-----|
| GPT-3 126M | 19.14 | 19.24 |
| GPT-3 1.3B | 10.62 | 10.66 |
| GPT-3 22B | 7.21 | 7.24 |
| GPT-3 175B | 6.65 | 6.68 |

**Key finding:** FP8 training matches 16-bit baselines within run-to-run variation, using **identical hyperparameters**.

### Post-Training Quantization (FP8 vs INT8)

| Model | FP16 | INT8 | E4M3 |
|-------|------|------|------|
| BERT Base (F1) | 88.19 | 76.89 | **88.09** |
| BERT Large (F1) | 90.87 | 89.65 | **90.94** |
| GPT-3 6.7B (perplexity) | 8.51 | 10.29 | **8.41** |

**Key finding:** FP8 post-training quantization significantly outperforms INT8, especially for models that resist fixed-point quantization (like BERT Base).

---

## 5. Implications for Fast Math Library Design

### When to Deviate from IEEE 754

The paper provides a template for principled deviation:

1. **Identify domain constraints:** DL doesn't need infinities (overflows are saturated), rarely encounters NaN (and one encoding suffices for debugging)

2. **Quantify the benefit:** Gain 7 additional representable values and one extra binade — significant for a format with only 18 binades total

3. **Preserve essential properties:** Sign-magnitude symmetry enables integer comparison; breaking this would invalidate too much software

4. **Document clearly:** The paper explicitly states what's IEEE-compliant and what isn't

### Per-Format Tradeoffs

| Format Choice | Precision | Range | Use Case |
|---------------|-----------|-------|----------|
| E5M2 (IEEE-like) | Lower (2-bit mantissa) | Higher (32 binades) | Gradients, values with large dynamic range |
| E4M3 (non-IEEE) | Higher (3-bit mantissa) | Lower (18 binades) | Weights, activations, inference |

### Overflow Handling Without Infinities

The E4M3 approach: **saturate to max representable value** (448) instead of producing infinity.

- Saturation is often the right behavior for DL (gradient clipping is common anyway)
- Avoids NaN propagation from ∞ - ∞ or 0 × ∞
- Requires software to handle "loss scaling" overflow detection differently than FP16 AMP

### Type Conversion Semantics

When converting wider formats to E4M3:
- Both infinity and NaN in the source → NaN in E4M3
- This preserves the ability to detect overflow via NaN in mixed-precision training
- "Non-saturating mode" mentioned as optional for strict overflow handling

---

## 6. Summary Table for Library Designers

| Design Decision | FP8 Paper's Choice | Rationale |
|-----------------|-------------------|-----------|
| Multiple formats | Yes (E4M3 + E5M2) | Different precision/range needs for forward vs backward |
| IEEE special values | Partial (E5M2 yes, E4M3 minimal) | Trade special values for range when range is scarce |
| Programmable bias | No (software scaling instead) | More flexibility, any scaling factor possible |
| Overflow handling | Saturation | Better for DL than infinity propagation |
| Subnormals | Preserved | Gradual underflow matters for small gradients |
| Sign symmetry | Preserved | Too much software depends on it |

---

## Key Takeaway

This paper demonstrates that **domain-specific floating-point formats can succeed** if:

1. The deviations are justified by measurable benefits (extra range worth more than infinities for DL)
2. Critical software invariants are preserved (sign-magnitude comparison)
3. The use case is understood (DL tolerates saturation, needs per-tensor scaling)
4. Comprehensive empirical validation is provided (CNNs, RNNs, Transformers up to 175B parameters)

For a general-purpose fast math library, this suggests offering **format-specific code paths** when the target domain (like ML inference) can benefit from relaxed IEEE semantics.

---

# Summary of "Mixed Precision Training" (Baidu/NVIDIA, ICLR 2018)

## Relevance to Fast Math Library Design

This influential paper established the methodology for training neural networks in half-precision (FP16) while maintaining FP32-level accuracy. It provides a practical template for **how to successfully use reduced precision** in computation-intensive applications, with specific techniques to handle the reduced dynamic range of FP16.

---

## 1. The Core Problem

FP16 offers significant benefits:
- 2× memory reduction
- 2-8× faster arithmetic on modern GPUs (Tensor Cores)
- Reduced memory bandwidth pressure

But FP16 has critical limitations:
- **Narrower dynamic range:** Exponent range [−14, 15] vs FP32's [−126, 127]
- **Less precision:** 10-bit mantissa vs 23-bit
- **Smallest positive value:** 2⁻²⁴ (with subnormals) vs 2⁻¹⁴⁹ for FP32

Training with naive FP16 causes accuracy loss because:
1. Small gradient values become zero
2. Weight updates get lost when added to much larger weights

---

## 2. The Three Key Techniques

### Technique 1: FP32 Master Weights

**Problem:** Weight updates (gradient × learning rate) can be too small to affect FP16 weights.

Two failure modes:
1. **Update too small to represent:** Values < 2⁻²⁴ become zero in FP16
2. **Update swamped by weight magnitude:** If weight is 2048× larger than update, addition loses the update entirely (right-shift beyond mantissa bits)

**Solution:** Keep an FP32 "master copy" of weights:
```
FP16_weights = round_to_fp16(FP32_master_weights)
FP16_gradients = backward_pass(FP16_weights, FP16_activations)
FP32_master_weights -= learning_rate * FP32(FP16_gradients)
```

**Memory impact:** Weights storage increases 50% (FP16 + FP32 copies), but overall memory usage still roughly halved because activations dominate (and remain FP16).

### Technique 2: Loss Scaling

**Problem:** Gradient values cluster at small magnitudes. The paper shows a histogram where 67% of activation gradients are zero and most non-zero values have exponents < −24 (below FP16's representable range).

**Solution:** Scale the loss before backpropagation:
```
scaled_loss = loss * scale_factor  # e.g., scale_factor = 8 to 32K
gradients = backprop(scaled_loss)   # All gradients scaled by same factor
unscaled_gradients = gradients / scale_factor  # Before weight update
```

**Key insight:** By chain rule, scaling the loss scales all gradients uniformly. This shifts the gradient distribution into FP16's representable range.

**Choosing the scale factor:**
- Empirically: Try powers of 2 from 8 to 32K
- Analytically: Ensure `max_gradient × scale_factor < 65504` (FP16 max)
- Dynamically: Increase until overflow detected, then back off

**Overflow handling:** If gradients overflow to infinity/NaN, skip that weight update and reduce scale factor. This is safe because overflows are easily detected.

### Technique 3: FP32 Accumulation

**Problem:** Accumulating many FP16 products (as in matrix multiply or dot products) loses precision.

**Solution:** Accumulate partial products in FP32, then convert result to FP16:
```
FP32 accumulator = 0
for i in range(N):
    accumulator += FP16_a[i] * FP16_b[i]  # Products accumulated in FP32
FP16_result = round_to_fp16(accumulator)
```

**Hardware support:** NVIDIA Tensor Cores implement this directly (FP16 inputs, FP32 accumulator, FP16 or FP32 output).

**Where FP32 accumulation is needed:**
- Matrix multiplications (convolutions, fully-connected layers)
- Large reductions (batch normalization statistics, softmax denominators)

**Where it's not needed:**
- Point-wise operations (ReLU, element-wise multiply) — memory-bound anyway

---

## 3. Empirical Results

### Image Classification (ImageNet)

| Model | FP32 Baseline | Mixed Precision |
|-------|---------------|-----------------|
| AlexNet | 56.77% | 56.93% |
| VGG-D | 65.40% | 65.43% |
| GoogLeNet | 68.33% | 68.43% |
| Inception v3 | 73.85% | 74.13% |
| ResNet-50 | 75.92% | 76.04% |

**No loss scaling required** for these networks.

### Object Detection (Pascal VOC mAP)

| Model | FP32 | MP (no scaling) | MP (with scaling) |
|-------|------|-----------------|-------------------|
| Faster R-CNN | 69.1% | 68.6% | 69.7% |
| Multibox SSD | 76.9% | **diverges** | 77.1% |

**SSD requires loss scaling** (factor of 8) — without it, training diverges completely.

### Speech Recognition (Character Error Rate)

| Dataset | FP32 | Mixed Precision |
|---------|------|-----------------|
| English | 2.20% | 1.99% |
| Mandarin | 15.82% | 15.01% |

MP actually **outperformed** FP32 — authors suggest FP16 may act as regularizer.

### Language Modeling (bigLSTM, Perplexity)

- Without loss scaling: Diverges after 300K iterations
- With loss scaling (128×): Matches FP32 baseline

### Machine Translation

- Matches FP32 within run-to-run variation
- Loss scaling helps but isn't strictly required

### GANs (DCGAN)

- Qualitatively comparable outputs
- No loss scaling required

---

## 4. Implications for Fast Math Library Design

### Precision Requirements by Operation Type

| Operation Type | FP16 Input OK? | FP32 Accumulation Needed? | Notes |
|----------------|----------------|---------------------------|-------|
| Matrix multiply | Yes | **Yes** | Tensor Core pattern |
| Convolution | Yes | **Yes** | Same as matmul |
| Batch norm stats | Yes | **Yes** | Large reductions |
| Softmax | Yes | **Yes** | Exp and sum |
| ReLU, tanh, etc. | Yes | No | Memory-bound |
| Element-wise ops | Yes | No | Memory-bound |
| Weight storage | FP16 for forward/backward | N/A | Master copy in FP32 |
| Gradient storage | FP16 | N/A | May need scaling |

### Library Design Recommendations

1. **Provide mixed-precision matrix multiply:**
   - FP16 inputs, FP32 accumulator, configurable output precision
   - This is the pattern hardware accelerators optimize for

2. **Support loss scaling infrastructure:**
   - Scale gradients before backprop
   - Detect overflow (check for inf/NaN in gradients)
   - Unscale before weight update

3. **Reduction operations need FP32:**
   - Sum, mean, variance computations
   - Softmax normalization
   - Even if inputs/outputs are FP16

4. **Don't force uniform precision:**
   - Different tensors have different precision needs
   - Gradients often need more dynamic range than activations
   - Master weights must be FP32

### The Dynamic Range Problem

The paper's gradient histogram is key: most gradient values are tiny (exponents < −24), but they matter for training. Loss scaling effectively implements **software-controlled exponent bias** — exactly what the FP8 paper later formalized.

| Without Scaling | With 8× Scaling |
|-----------------|-----------------|
| Values in [2⁻²⁷, 2⁻²⁴) → zero | Values in [2⁻²⁷, 2⁻²⁴) → representable |
| Training diverges | Training succeeds |

---

## 5. Summary Table for Library Designers

| Technique | What It Solves | Implementation |
|-----------|---------------|----------------|
| FP32 master weights | Updates lost in FP16 addition | Keep shadow FP32 copy for optimizer |
| Loss scaling | Small gradients become zero | Multiply loss by constant before backprop |
| FP32 accumulation | Dot product precision loss | Accumulate in FP32, store in FP16 |
| Overflow detection | Scaling too aggressive | Check for inf/NaN, skip update, reduce scale |

---

## Key Takeaway

Mixed precision training works because:

1. **Forward/backward passes tolerate FP16** — the noise is acceptable and may even regularize
2. **Accumulation needs FP32** — critical for numerical stability in reductions
3. **Weight updates need FP32** — small updates matter over many iterations
4. **Dynamic range can be software-managed** — loss scaling shifts values into representable range

For a math library: provide the **building blocks** (FP16 storage, FP32 accumulation, configurable output precision) and let the framework handle scaling and master weight management.

---

# Summary of "RLibm-Prog: Progressive Polynomial Approximations for Fast Correctly Rounded Math Libraries" (Rutgers, 2022)

## Relevance to Fast Math Library Design

This paper presents a novel approach to generating polynomial approximations for elementary functions (sin, cos, exp, log, etc.) that are provably correctly rounded for all inputs across multiple floating-point formats. The key innovation is "progressive polynomials" that produce correct results for smaller formats (bfloat16, tensorfloat32) using only the first few terms, with additional terms needed only for larger formats (float32).

---

## 1. The Core Problem

**Traditional approach (minimax):** Find a polynomial that minimizes the maximum error relative to the true mathematical value of f(x). This requires high-degree polynomials to get within 0.5 ulp.

**The Table Maker's Dilemma:** For some inputs, the true value falls almost exactly on the boundary between two representable floats. Determining the correct rounding requires computing with potentially unbounded precision.

**Double rounding errors:** Libraries like CR-LIBM that produce correctly rounded results for double precision can produce wrong results when repurposed for float32, because rounding twice (double → float → correctly rounded float) can give different results than rounding once.

---

## 2. The RLibm Insight: Approximate the Rounded Result, Not the Real Value

Instead of minimizing error relative to f(x), RLibm finds polynomials where P(x) falls within the **rounding interval** — the range of real values that all round to the same floating-point result.

**Example:** If f(x) = 1.500000001 and the adjacent floats are 1.5 and 1.5000001, any polynomial output in [1.5, 1.50000005) rounds correctly to 1.5.

**Key benefit:** This provides much more freedom for the polynomial, allowing lower-degree approximations that are both faster and correct.

---

## 3. Progressive Polynomials

The paper's main contribution: a single polynomial where:

- First 2-3 terms → correctly rounded for bfloat16
- First 3-4 terms → correctly rounded for tensorfloat32
- All 5-7 terms → correctly rounded for float32

**Example for exp(x):**
```
P(x) = C₁ + C₂x + C₃x² + C₄x³ + C₅x⁴ + C₆x⁵ + C₇x⁶
       └─────────────┘
       bfloat16 (4 terms)
       └─────────────────────┘
       tensorfloat32 (5 terms)
       └─────────────────────────────────────┘
       float32 (7 terms)
```

**Why this works:** Lower-precision formats have larger rounding intervals (adjacent values are farther apart), so fewer polynomial terms suffice.

---

## 4. The Algorithm: Linear Programming with Randomized Constraint Solving

### Formulation as Linear Program

For each input x and target format T, create constraint:
```
l_T(x) ≤ C₁ + C₂x + C₃x² + ... + Cₖx^(k-1) ≤ h_T(x)
```
where [l_T(x), h_T(x)] is the rounding interval.

### The Scale Problem

For float32, there are ~2³² inputs. For progressive polynomials across multiple formats, there are billions of constraints. Standard LP solvers cannot handle this.

### Key Observation: Low-Dimensional LP

The LP has only k variables (polynomial coefficients, typically 5-7), but billions of constraints. Prior work shows that if the system is "full-rank" (k linearly independent constraints exist), only O(k²) constraints determine the solution.

### Randomized Algorithm

1. Sample 6k² constraints randomly
2. Solve this small LP exactly
3. Check how many total constraints are violated
4. If > 1/3k violated: discard sample, resample
5. If ≤ 1/3k violated: double the weight of violated constraints, repeat

**Convergence:** Proven to find the solution in O(k log n) iterations in expectation when a solution exists.

---

## 5. Results

### Correctness Comparison

| Function | RLibm-Prog | glibc double | Intel double | CR-LIBM |
|----------|------------|--------------|--------------|---------|
| ln(x) FP32 all modes | ✓ | ✗ | ✗ | ✗ |
| exp(x) FP32 all modes | ✓ | ✗ | ✗ | ✓ |
| log₂(x) FP32 all modes | ✓ | ✓ | ✓ | ✓ |
| sinh(x) FP32 all modes | ✓ | ✗ | ✗ | ✗ |

**Key finding:** Mainstream libraries (glibc, Intel) do not produce correctly rounded results for many functions. Even CR-LIBM fails for some float32 cases due to double rounding.

### Performance (Speedup over other libraries)

| Library | bfloat16 | tensorfloat32 | float32 |
|---------|----------|---------------|---------|
| vs glibc double | 42% faster | 29% faster | 20% faster |
| vs Intel double | 74% faster | 64% faster | 49% faster |
| vs CR-LIBM | 123% faster | 105% faster | 85% faster |
| vs RLibm-All | 25% faster | 16% faster | 5% faster |

### Memory Usage

| Function | RLibm-All | RLibm-Prog | Reduction |
|----------|-----------|------------|-----------|
| ln(x) | 24,576 B | 360 B | 68× |
| log₂(x) | 6,144 B | 40 B | 154× |
| exp(x) | 10,240 B | 160 B | 64× |
| Average | 7,667 B | 123 B | 62× |

RLibm-All required large lookup tables for piecewise polynomials. RLibm-Prog's single progressive polynomials eliminate this.

### Surprising Results

For bfloat16 (7 mantissa bits):
- ln(x), log₂(x), log₁₀(x) need only 1 term (a constant!) for correct rounding
- This is because bfloat16's rounding intervals are so wide

---

## 6. Implications for Fast Math Library Design

### Correct Rounding is Achievable and Fast

The paper demonstrates that correctly rounded elementary functions can be:
- Faster than mainstream libraries that don't guarantee correctness
- Smaller (no large lookup tables)
- Universal (same code works for all rounding modes)

### The Round-to-Odd Trick

To produce correct results for all 5 IEEE rounding modes with a single polynomial:
1. Generate polynomial for (n+2)-bit precision with round-to-odd mode
2. This preserves enough information to correctly round to any mode for n-bit precision

### Progressive Performance is Practical

For ML workloads using bfloat16 or tensorfloat32:
- Use only the first few terms of the polynomial
- Get 25-60% speedup over full polynomial evaluation
- Still guarantee correct rounding

### Adoption

Three polynomial approximations from RLibm-Prog (ln, log₂, log₁₀) have been incorporated into LLVM's math library, demonstrating that this approach is practical for production use.

---

## 7. Summary Table for Library Designers

| Design Choice | RLibm-Prog Approach | Benefit |
|---------------|---------------------|---------|
| Approximation target | Correctly rounded result | More freedom, lower degree |
| Multi-format support | Progressive polynomials | Single implementation, progressive speedup |
| Rounding mode support | Round-to-odd at (n+2) bits | All 5 modes from one polynomial |
| Constraint solving | Randomized LP in low dimensions | Handles billions of constraints |
| Storage | Single polynomial (no piecewise) | 62× smaller than prior work |

---

## Key Takeaway

RLibm-Prog proves that correct rounding and high performance are not mutually exclusive. By approximating the rounded result rather than the mathematical value, and by exploiting the structure of low-dimensional linear programs, it's possible to generate elementary function implementations that are simultaneously:

- Correctly rounded for all inputs
- Faster than mainstream libraries
- Compact (no large lookup tables)
- Progressive (faster for smaller formats)

This approach has been validated by adoption into LLVM's production math library.

---

# Summary of "The Pitfalls of Verifying Floating-Point Computations" (Monniaux, 2008)

## Relevance to Fast Math Library Design

This paper is a comprehensive catalog of the ways floating-point computations can behave unexpectedly in real systems. It's essential reading for anyone designing math libraries because it documents the gap between "what IEEE 754 says" and "what actually happens" when code runs through compilers on real hardware.

---

## 1. Core Thesis: IEEE 754 Compliance Is Not Enough

The paper systematically debunks common myths:

| Myth | Reality |
|------|---------|
| "float maps to IEEE single, double to IEEE double" | On x87, computations happen in 80-bit extended precision |
| "Arithmetic is deterministic" | Same source code can give different results depending on register allocation |
| "If x < 1 tests true, x < 1 stays true" | Value may be in extended-precision register during test, then spilled to memory and rounded |
| "Same source + same platform = same results" | Different compilers, optimization levels, or even adding printf() can change results |

**Key insight:** "It is the environment the programmer or user of the system sees that conforms or fails to conform to this standard. Hardware components that require software support to conform shall not be said to conform apart from such software."

---

## 2. The x87 Extended Precision Problem

### The Architecture

x87 floating-point unit has 80-bit registers (64-bit mantissa, 15-bit exponent). Operations are performed in this extended format, then rounded when stored to memory.

### Concrete Examples of Breakage

**Example 1: Overflow depends on optimization**
```c
double v = 1E308;
double x = (v * v) / v;
printf("%g\n", x);
```
- Without optimization: prints `+∞` (v*v overflows in double)
- With optimization: prints `10308` (computation stays in 80-bit registers, no overflow)

**Example 2: Inlining changes semantics**
```c
static inline double f(double x) { return x/1E308; }
double square(double x) { return x*x; }
int main(void) { printf("%g\n", f(square(1E308))); }
```
- Without optimization (no inlining): prints `+∞`
- With optimization (inlined): prints `10308`

**Example 3: A value can be both zero and non-zero**
```c
double x = 0x1p-1022, y = 0x1p100, z;
z = x / y;
if (z != 0) {
    do_nothing(&z);  // forces spill to memory
    assert(z != 0);  // FAILS!
}
```
- `z` is non-zero in 80-bit register (passes first test)
- After spill to 64-bit memory, `z` becomes zero (assertion fails)

### Double Rounding

When a value is rounded twice (80-bit → 64-bit → 32-bit), the result can differ from single rounding (80-bit → 32-bit):

```
Real value: exactly halfway between two doubles
First round to 80-bit: rounds to nearest (say, up)
Second round to 64-bit: rounds up again (because now > halfway)
Direct round to 64-bit: would have rounded down (tie-breaking rule)
```

---

## 3. Other Architecture Issues

### PowerPC Fused Multiply-Add

```c
double dotProduct(double a1, double b1, double a2, double b2) {
    return a1*b1 + a2*b2;
}
```
- With optimization: uses FMA instruction (intermediate result not rounded)
- Without optimization: separate multiply and add (intermediate rounded)
- **Different numerical results**

### Flush-to-Zero Modes

Many processors have modes that flush subnormal numbers to zero for performance. This changes semantics:
- `x - y == 0` no longer implies `x == y`
- Gradual underflow properties lost

---

## 4. Transcendental Function Inconsistencies

### No Standard Behavior

IEEE 754 specifies +, -, ×, ÷, √ but **not** sin, cos, exp, log, etc.

### Concrete Discrepancies

**Example: sin(14885392687)**
| Platform | Result |
|----------|--------|
| Pentium 4 x87 | 1.671 × 10⁻¹⁰ |
| GNU libc on x86_64 | 1.4798 × 10⁻¹⁰ |

That's an **11.5% difference** for the same mathematical function!

**Cause:** Different precision for π in argument reduction (66-bit vs 256-bit approximations).

### Processor Generation Differences

Intel documentation notes transcendental function precision improved between i486 and Pentium:
- i486/i387: "worst-case error is typically 3 or 3.5 ulps, but sometimes as large as 4.5 ulps"
- Pentium and later: less than 1 ulp claimed

**Implication:** Same source code, same compiler, different processor → different results.

---

## 5. Compiler Optimization Hazards

### Unsafe Optimizations That Compilers Perform

| Optimization | Problem |
|--------------|---------|
| Associativity: `(a+b)+c` → `a+(b+c)` | Floating-point not associative |
| `x + 0` → `x` | Wrong for x = -0 |
| `x * 0` → `0` | Wrong for x = NaN or ∞ |
| `x - x` → `0` | Wrong for x = NaN or ∞ |
| Assume no NaN/∞ | Enables "simplifications" that break edge cases |
| Vectorization | Requires reordering operations |

### The min() Problem

Four "equivalent" ways to compute min(x, y):
```c
x < y ? x : y
x <= y ? x : y
x > y ? y : x
x >= y ? y : x
```

All four give different results for:
- x = +0, y = -0
- x = NaN, y = 1

Some compilers (Intel icc by default, gcc with -ffast-math) compile all four to the same instruction, breaking IEEE semantics.

### Compiler Flags to Watch

| Compiler | Safe Flag | Unsafe Flag (often default!) |
|----------|-----------|------------------------------|
| gcc | `-frounding-math -fsignaling-nans` | `-ffast-math` |
| icc | `-fp-model precise` | (default is relaxed) |
| MSVC | `/fp:precise` | `/fp:fast` |

---

## 6. Library Bugs in Non-Default Modes

The paper documents bugs when rounding mode ≠ round-to-nearest:

- **FreeBSD 4.4:** `printf()` produces garbage output for large values in round-to-+∞ mode
- **GNU libc 2.3.3 on x86_64:** `pow()` segfaults, `exp(1)` ≈ 2⁵⁰², `exp(1.46)` < 0 in round-to-+∞

**Implication for library designers:** Test all rounding modes, not just round-to-nearest.

---

## 7. Implications for Static Analysis and Verification

### Sound Analysis Requirements

To prove properties about floating-point programs, must account for:

1. **Extended precision temporaries** on x87
2. **Fused multiply-add** on PowerPC
3. **Compiler optimizations** that reorder or simplify
4. **Library function variability**
5. **Rounding mode changes**

### The Error Bound Formula

For sound analysis, use:
```
|x - r(x)| ≤ max(εrel·|x|, εabs)
```
or the coarser:
```
|x - r(x)| ≤ εrel·|x| + εabs
```

The absolute error term εabs is necessary because of subnormal numbers (pure relative error doesn't work near zero).

---

## 8. Summary Table for Library Designers

| Issue | Impact | Mitigation |
|-------|--------|------------|
| x87 extended precision | Results depend on register allocation | Use SSE, or `-ffloat-store`, or 80-bit throughout |
| Double rounding | Different results than direct rounding | Avoid intermediate precision > target |
| FMA instructions | Changes intermediate rounding | `-mno-fused-madd` or design algorithms assuming FMA |
| Compiler "optimizations" | Breaks IEEE semantics | `-fp-model precise`, avoid `-ffast-math` |
| Transcendental variability | Platform-dependent results | Use correctly-rounded library (MPFR, RLibm) |
| Non-default rounding bugs | Libraries may crash or give wrong results | Test all rounding modes |
| Flush-to-zero | Breaks gradual underflow | Disable FTZ or document its use |

---

## Key Takeaway

**For a math library to be reliable, it must be tested against the actual execution environment, not just the IEEE 754 specification.** The combination of:
- Hardware quirks (x87 extended precision, FMA)
- Compiler transformations (optimization, inlining, register allocation)
- Library implementations (different π approximations, bugs in non-default modes)

...means that "IEEE 754 compliant" is necessary but nowhere near sufficient for predictable behavior. Library designers must either:
1. Control the entire stack (compiler flags, target hardware, link with known-good dependencies), or
2. Design algorithms robust to these variations, or
3. Document precisely which configurations are supported

---

# Summary of "An Accurate Elementary Mathematical Library for the IEEE Floating Point Standard" (Gal & Bachelis, 1991)

## Relevance to Fast Math Library Design

This paper introduces the **"accurate tables method"** — a technique for implementing elementary functions (sin, cos, exp, log, etc.) that achieves both high performance and near-perfect accuracy (99.7-99.9% correctly rounded results). The key innovation is using "perturbed" table points that are chosen so their function values are nearly exactly representable in floating-point.

---

## 1. The Core Problem

Standard table-based methods use equally-spaced points:
```
x_i = i/256,  f(x_i) stored in table
```

**Problem:** f(x_i) is almost never exactly representable in floating-point. The rounding error when storing table values limits achievable accuracy to ~53 bits (double precision), regardless of how accurate the polynomial approximation is.

---

## 2. The Accurate Tables Method

### Key Insight: Perturb Table Points

Instead of equally-spaced points, use:
```
x_i = i/256 + ε_i    (where ε_i is a small perturbation)
```

Choose ε_i such that f(x_i), when written as an infinite binary fraction, has **K zeros (or ones) after bit 53**:
```
f(x_i) = 1.XXXX...XXX 000000000000 XXX...
         └─52 bits──┘ └──K zeros──┘
```

**Result:** The table value effectively has 52+K bits of precision, even though only 53 bits are stored. With K=11 or 12, the table contributes error < 2⁻⁶⁴.

### Finding Good Perturbations

For each function and interval, search for perturbations ε_i where:
1. f(x_i) has K zeros after bit 53
2. |ε_i| < 1/million (perturbation is tiny)

This requires extended-precision arithmetic during table generation (one-time offline cost).

---

## 3. Algorithm Structure

### Step 1: Range Reduction
Reduce argument to a small interval where polynomial approximation is efficient.

**Example for exp(x):**
```
exp(x) = 2^n × exp(y)    where y = x - n×ln(2), |y| < 0.35
```

### Step 2: Table Lookup
Find nearest perturbed table point x_i:
```
i = INT(y × 512)
z = y - x_i        (small: |z| < 1/1024)
```

### Step 3: Polynomial Approximation
Compute correction term using low-degree polynomial:
```
exp(y) = f_i × exp(z) ≈ f_i × (1 + p(z))
```

Where p(z) is a minimax polynomial of degree 4-5 on the small interval.

### Step 4: Combine Results
```
result = f_i + f_i × p(z)
```

Since f_i has implicit extra precision, and p(z) is computed accurately on a small interval, the combined error is dominated by floating-point arithmetic error (~2⁻⁶² relative error).

---

## 4. Function-Specific Details

### Exponential (exp)

**Table:** 355 entries (i = -177 to 177)
- x_i = i/512 + ε_i
- f_i = exp(x_i) with 12 implicit zeros after bit 53

**Range reduction:** x = n×ln(2) + y, compute ln(2) as sum of two values to avoid precision loss in n×ln(2).

**Polynomial:** Degree 4, error < 6.5×10⁻¹⁹

**Result:** 99.8% correctly rounded

### Logarithm (ln)

**Table:** 192 triplets (x_i, ln(x_i), 1/x_i)
- Both ln(x_i) and 1/x_i have implicit zeros after bit 53

**Formula:**
```
ln(y) = ln(x_i) + ln(1 + z)    where z = (y - x_i)/x_i
```

**Polynomial:** Degree 6 for ln(1+z), error < 3.1×10⁻²¹

**Optimization:** Uses "adaptation of coefficients" method to reduce 6 multiplications + 5 additions to 5 multiplications + 5 additions.

**Result:** 99.9% correctly rounded

### Sine/Cosine

**Table:** 186 pairs (sin(x_i), cos(x_i))
- Both values have 11 implicit zeros after bit 53

**Range reduction:** Critical challenge — requires 103+ bits of π to maintain accuracy for large arguments.

**Hard case analysis:** Found worst argument in [0, 2²⁷]:
```
x_hard = 294600672 × π/2
```
Contains 30 zeros after bit 53, requiring 83+54 = 137 bits of π for full accuracy.

**Formula:**
```
sin(y) = sin(x_i)×cos(z) + cos(x_i)×sin(z)
```

**Result:** 99.9% correctly rounded

### Tangent/Cotangent

**Table:** Store tan(x_i) and 1/tan(x_i), both with 11 implicit zeros

**Formula:**
```
tan(y) = (tan(x_i) + tan(z)) / (1 - tan(x_i)×tan(z))
```

**Cotangent near zero:** Special handling using Newton iteration for 1/tan(z).

**Result:** 99.7% correctly rounded

### Arctangent

**Unique approach:** Instead of storing function values, store polynomial coefficients C_i0, C_i1, ..., C_i5 that are "accurate" (have implicit zeros):
```
arctan(x) ≈ C_i0 + C_i1×(x-x_i) + ... + C_i5×(x-x_i)⁵
```

The x_i are chosen so that:
- C_i0 = arctan(x_i) has 12 zeros after bit 53
- C_i1 has 7 zeros after bit 53

**Result:** 99.9% correctly rounded

---

## 5. Achieving 100% Correct Rounding

The paper describes a two-phase approach:

**Phase 1:** Normal computation produces result with ~62 bits of accuracy.

**Detection:** Check if last 9 bits of intermediate result match "dangerous" patterns (100000000 or 011111111).

**Phase 2 (if dangerous):** Recompute with 137-bit precision.

**Analysis:** 
- Probability of dangerous pattern: ~2⁻⁸ ≈ 0.3%
- Probability that 137 bits is insufficient: ~2⁻⁸⁴ per input
- With ~2⁶⁴ possible inputs: probability any fails ≈ 2⁻²⁰ < 1 in million

The authors conjecture this achieves 100% correct rounding but acknowledge proving it is "formidable."

---

## 6. Accuracy Results

| Function | % Correctly Rounded | Max Error (ulps) |
|----------|---------------------|------------------|
| exp | 99.8% | 0.504 |
| ln | 99.9% | 0.520 |
| log₁₀ | 99.9% | 0.514 |
| sin | 99.9% | 0.515 |
| cos | 99.9% | 0.509 |
| tan | 99.7% | 0.539 |
| cotan | 99.8% | 0.543 |
| arctan | 99.9% | 0.521 |
| sinh | 99.8% | 0.507 |
| cosh | 99.8% | 0.505 |

All tested across 300,000 random arguments in all four rounding modes.

---

## 7. Performance Optimizations

### Polynomial Evaluation

**Standard Horner:** n multiplications + (n-1) additions

**Adaptation of coefficients:** Can reduce multiplications. Example for degree-6 polynomial:
```
p = z² × ((B6×z² + A)×(z² + B×z + C) + D) + z
```
Reduces from 6 mult + 5 add to 5 mult + 5 add.

### Range Reduction for π

**Fast path:** |x| < π/4, no reduction needed

**Medium path:** π/4 < |x| ≤ π, simple subtraction

**Slow path:** |x| > π, use 103 bits of π, extra bits if needed

The slow path is rare for typical inputs, so average performance is good.

---

## 8. Implications for Library Design

### Table Generation (Offline)
1. For each table point, search for perturbation ε that makes f(x+ε) nearly representable
2. Requires extended precision arithmetic
3. One-time cost, results stored in library

### Runtime Algorithm
1. Range reduction (function-specific)
2. Table lookup (single memory access)
3. Low-degree polynomial (4-6 terms typical)
4. Combine with table value

### Memory vs Speed Tradeoff

| Interval spacing | Table size | Polynomial degree |
|------------------|------------|-------------------|
| 1/128 | Smaller | Higher (slower) |
| 1/256 | Medium | Medium |
| 1/512 | Larger | Lower (faster) |

The paper uses 1/256 or 1/512 as a practical compromise.

### Monotonicity Preservation

The method naturally preserves monotonicity within each interval (polynomial is monotonic on small interval). At interval boundaries, small perturbations to polynomial coefficients ensure global monotonicity.

---

## Key Takeaway

The accurate tables method achieves **both** high performance and near-perfect accuracy by:
1. Choosing table points where function values are nearly exactly representable
2. Using low-degree polynomials on small intervals
3. Combining these with extended-precision-like accuracy from "implicit zeros"

This approach was ahead of its time — many ideas reappear in modern correctly-rounded libraries like CR-LIBM and RLIBM. The 99.7-99.9% correct rounding achieved in 1991 is remarkable given the constraints of the era.

---

# Summary of "How Java's Floating-Point Hurts Everyone Everywhere" (Kahan & Darcy, 1998)

## 1. What This Paper Is

This is a polemic by William Kahan (the "father of IEEE 754") arguing that Java's floating-point design decisions are fundamentally broken. It's less a technical paper and more a detailed critique with concrete examples of why Java's choices hurt numerical programmers.

---

## 2. The Five Gratuitous Mistakes

Kahan identifies five fundamental errors in Java's floating-point design:

### 1. Linguistically Legislated Exact Reproducibility

**Java's claim:** "Write once, run anywhere" — identical floating-point results on all platforms.

**Why it's wrong:**
- Different platforms have different optimal algorithms (e.g., matrix multiply blocking for cache)
- Enforcing bit-identical results forces suboptimal code everywhere
- Matrix multiplication example: optimal blocking on UltraSPARC runs 10× slower on Pentium Pro than Pentium-optimized code
- The order of floating-point operations affects results slightly, but this is acceptable for most applications

**Kahan's view:** Reproducibility is sometimes needed by some programmers. Predictability within controllable limits is needed by all programmers all the time. Java conflates these.

### 2. Wrong Choice for Mixed-Precision Evaluation

**The problem:** When you write `float a, b, c; ... a*b + c`, should intermediate results be computed in float precision or higher?

**Java's choice:** Strict float precision for float operands, strict double for double.

**Why it's wrong:** The old K&R C approach (evaluate everything in the widest available precision, then round to target) is safer:
- Extra precision in intermediates reduces roundoff accumulation
- Hardware often has wider registers anyway (x87 has 80-bit)
- Forcing narrow precision wastes the hardware's capabilities

**Example - angle at the eye:**
```
Computing angle = atan2(|u×v|, u·v) for vectors u, v
```
In strict float precision, this fails catastrophically for nearly-parallel vectors. In extended precision, it works fine.

### 3. Infinities and NaNs Without Flags or Traps

**The problem:** Java adopted IEEE 754's special values (∞, NaN) but not the flags and traps that make them safe.

**Why flags matter:**
- NaN can propagate through a computation, then disappear: `(NaN < 7)` becomes `false`
- Without flags, there's no trace that something went wrong
- Programmers must either pre-test every operation (slow, error-prone) or hope nothing bad happens

**The Ariane 5 disaster:** Kahan uses this as an example. An integer overflow trap (in Ada) triggered a diagnostic dump into memory being used by guidance software. If IEEE 754 defaults had been followed (flag raised, computation continues with defined result), the rocket would have flown normally.

**Kahan's point:** "A trap too often catches creatures it was not set to catch."

### 4. No Access to Hardware Capabilities

**What's missing:**
- Rounding mode control (round toward +∞, -∞, 0)
- Extended precision (80-bit on x87)
- Exception flags

**Why it matters:**
- Directed rounding is essential for interval arithmetic and debugging
- Extended precision dramatically improves accuracy of many algorithms
- Over 95% of floating-point hardware supports these features; Java ignores them

### 5. No Operator Overloading

**The problem:** Without operator overloading, you can't write `a + b` for complex numbers, matrices, intervals, etc.

**Why it matters:**
- Complex arithmetic is essential for many applications
- Writing `Complex.add(a, Complex.multiply(b, c))` instead of `a + b*c` is error-prone and unreadable
- Multiple competing Complex classes will inevitably exist, breaking "write once, run anywhere"

---

## 3. Misconceptions Kahan Addresses

### "Catastrophic Cancellation"

**Common belief:** Subtracting nearly-equal numbers always causes catastrophic error.

**Reality:** Cancellation reveals pre-existing error; it doesn't create it. The subtraction itself is exact (or nearly so). The problem is that the operands were already contaminated.

**Correct view:** Computation is a web. Errors introduced anywhere propagate. Blaming cancellation is like blaming the messenger.

### "Precision Should Match Data Accuracy"

**The wrong rule of thumb:** "Arithmetic should be barely more precise than the data and desired result."

**Why it's wrong:**
- Precision (format resolution) ≠ Accuracy (correctness)
- Extra precision in intermediate computations reduces error accumulation
- Some algorithms can extract more accuracy than their arithmetic precision suggests
- The rule dates from slide-rule era and was never reliable

**Kahan's four rules of thumb instead:**
1. Use the widest precision available for intermediate calculations
2. Round only the final result to the target precision
3. Avoid subtracting nearly-equal quantities when possible (reformulate)
4. When you must subtract nearly-equal quantities, compute them to extra precision first

---

## 4. Extended Precision: Why It Matters

**The case for 80-bit extended (x87):**
- Iterative refinement for linear systems: accuracy improves spectacularly (far more than the extra 11 bits suggest)
- Evaluating polynomials: extended intermediates prevent catastrophic roundoff
- Complex arithmetic: branch cuts handled correctly
- Debugging: compare extended-precision results to narrow-precision to find instabilities

**Java's crime:** Forbidding use of extended precision even when hardware provides it.

**Example - Cantilever calculation:** A structural engineering formula that works perfectly in extended precision produces nonsense in strict double precision for certain (valid) inputs.

---

## 5. The "Two Cruel Delusions"

1. **"Write Once, Run Anywhere"** — Impossible to guarantee due to inevitable platform divergence, JVM implementation differences, and the impossibility of enforcing standards without authoritarian control.

2. **"Exact Reproducibility of All Floating-Point Results"** — Unenforceable and counterproductive. Sacrifices performance and accuracy for a guarantee that can't actually be kept.

---

## 6. Kahan's Recommendations

1. **Adopt old K&R C semantics:** Evaluate in widest available precision, round only on assignment.

2. **Provide access to IEEE 754 features:** Flags, traps, rounding modes, extended precision.

3. **Add operator overloading:** At least enough for Complex, Interval, and similar mathematical types.

4. **Distinguish reproducibility from predictability:** Let programmers choose when they need bit-identical results (rare) vs. when they need results within predictable error bounds (common).

5. **Stop pretending Java floats are portable:** They're not, and the pretense causes real harm.

---

## Key Takeaway

Kahan's critique, while aimed at Java, applies to any system that:
- Forces narrow precision when wider is available
- Hides IEEE 754 features (flags, rounding modes) from programmers
- Prioritizes superficial "portability" over numerical quality
- Treats floating-point as a solved problem that programmers needn't understand

A well-designed math library should:
- Exploit extended precision when available
- Provide access to rounding mode control
- Allow programmers to query exception flags
- Not sacrifice accuracy for reproducibility unless explicitly requested

---

# Summary of "RLIBM-32: High Performance Correctly Rounded Math Libraries for 32-bit Floating Point Representations" (Lim & Nagarakatte, PLDI 2021)

## 1. What This Paper Is

This paper extends the RLibm approach (which we covered in the earlier RLibm-Prog paper) to handle 32-bit floating-point types. The challenge: 32-bit float has ~4 billion possible inputs, far too many for the linear programming approach that worked for 16-bit types.

---

## 2. The Core Problem

**Goal:** Generate polynomial approximations for elementary functions (sin, cos, exp, log, etc.) that produce **correctly rounded results for all inputs** in 32-bit float.

**Challenge:** The original RLibm approach creates one LP constraint per input. With 4 billion float inputs, that's 4 billion constraints — far beyond any LP solver's capacity.

**Prior approach limitations:**
- Minimax polynomials minimize error relative to the *mathematical* function, not the *rounded* result
- Even with tiny approximation error, numerical errors in range reduction and output compensation can cause wrong rounding
- CR-LIBM and other correctly-rounded libraries are significantly slower than mainstream (incorrect) libraries

---

## 3. Key Innovation: Approximate the Rounded Result, Not the Real Value

Same core insight as RLibm-Prog, but scaled up:

**Traditional:** Find P(x) that minimizes |P(x) - f(x)|

**RLibm:** Find P(x) such that P(x) falls within the **rounding interval** — the range of values that all round to the same correctly-rounded result

This provides much more freedom, enabling lower-degree polynomials.

---

## 4. Techniques for Scaling to 32-bit

### Counterexample-Guided Polynomial Generation

Instead of creating constraints for all 4 billion inputs:

```
1. Sample a subset of inputs (e.g., 10,000)
2. Generate LP constraints for sampled inputs
3. Solve LP to get polynomial P
4. Test P against ALL inputs
5. If any fail, add failing inputs to sample and repeat
6. Stop when P works for all inputs
```

**Key insight:** Most inputs have "easy" rounding intervals. Only a small fraction have tight constraints that determine the polynomial. The counterexample loop finds these critical inputs.

### Piecewise Polynomials with Bit-Pattern Domain Splitting

When a single polynomial can't cover the whole domain:

1. Split the reduced domain into sub-domains
2. Use bits from the input's binary representation to select sub-domain
3. Generate separate polynomial for each sub-domain

**Example for sinpi(x):**
- After range reduction, reduced input R ∈ [0, 1/512]
- Split into 32 sub-domains using 5 bits of R's double representation
- Each sub-domain gets its own polynomial
- Selection is just bit extraction — very fast

**Performance benefit:** More sub-domains → lower-degree polynomials → faster evaluation. Sweet spot around 2⁸ sub-domains gives ~1.2× speedup.

### Handling Range Reduction with Multiple Functions

Some functions require multiple elementary functions after range reduction:

**Example:** sinpi(x) needs both sinpi(R) and cospi(R):
```
sinpi(x) = sinpi(N/512)×cospi(R) + cospi(N/512)×sinpi(R)
```

**Problem:** Need to deduce valid intervals for *both* sinpi(R) and cospi(R) such that the combined output compensation produces correct results.

**Solution:** Simultaneously expand/contract the intervals for both functions, checking that output compensation stays within the original rounding interval.

### Avoiding Cancellation in Output Compensation

**Problem with cospi:** Standard identity cos(a+b) = cos(a)cos(b) - sin(a)sin(b) involves subtraction, risking cancellation.

**Solution:** Transform to cos(a-b) = cos(a)cos(b) + sin(a)sin(b), which only uses addition:
```
Instead of: L' = N/512 + Q
Use:        L' = N'/512 - R  (where N' = N+1, R = 1/512 - Q)
```

This makes output compensation monotonic and avoids cancellation errors.

---

## 5. Results

### Correctness

| Library | Correctly rounded for ALL inputs? |
|---------|-----------------------------------|
| RLibm-32 | **Yes** |
| glibc libm | No (millions of wrong results) |
| Intel libm | No |
| CR-LIBM | Yes (but slower) |
| MetaLibm | No |

### Performance (Float Functions)

| Comparison | RLibm-32 Speedup |
|------------|------------------|
| vs glibc float | 1.1× faster |
| vs glibc double | 1.2× faster |
| vs Intel float | 1.5× faster |
| vs Intel double | 1.6× faster |
| vs CR-LIBM | 2.0× faster |
| vs MetaLibm float | 2.5× faster |

**Key result:** RLibm-32 is both **correct** (unlike glibc/Intel) and **fast** (unlike CR-LIBM).

### Posit32 Functions

First-ever correctly rounded elementary functions for 32-bit posits. 1.1-1.4× faster than repurposed double libraries.

### Vectorization

With Intel's vectorizing compiler, RLibm-32 is only 5-10% slower than Intel's vectorized (but incorrect) code.

---

## 6. Algorithm Overview

```
GeneratePolynomial(f, T, H):
  // T = target type (float), H = higher precision (double)
  
  1. For each input x in T:
     - Compute correctly rounded result y = RN(f(x), T) using oracle
     - Compute rounding interval [l, h] in H where all values round to y
  
  2. Apply range reduction:
     - Transform x → reduced input r
     - Transform rounding interval → reduced interval for r
     - If multiple functions needed (e.g., sin and cos), 
       deduce valid intervals for each
  
  3. If multiple inputs map to same r:
     - Intersect their reduced intervals
  
  4. Split domain into sub-domains (if needed)
  
  5. For each sub-domain:
     - Sample inputs
     - Create LP constraints from sampled reduced intervals
     - Solve LP for polynomial coefficients
     - Test polynomial on ALL inputs in sub-domain
     - Add counterexamples to sample if any fail
     - Repeat until polynomial correct for all inputs
  
  6. Store piecewise polynomial coefficients in table
```

---

## 7. Functions Implemented

**For 32-bit float:**
- ln, log₂, log₁₀
- exp, exp2, exp10
- sinh, cosh
- sinpi, cospi

**For 32-bit posit:**
- ln, log₂, log₁₀
- exp, exp2, exp10
- sinh, cosh

---

## 8. Implications for Library Design

### What RLibm-32 Demonstrates

1. **Correct rounding doesn't require sacrificing performance** — can be faster than incorrect libraries

2. **Piecewise polynomials are practical** — bit-pattern domain selection is essentially free

3. **LP-based generation works at scale** — counterexample-guided approach handles billions of inputs

4. **Range reduction design matters** — must avoid cancellation in output compensation

### Limitations Acknowledged

- Extending to double precision is an open problem (validation of all 2⁶⁴ inputs infeasible)
- Some functions (sin, cos with argument reduction involving π) need higher-precision range reduction
- Currently covers subset of elementary functions

### Future Directions

- More elementary functions for 32-bit
- Extension to double precision (with partial verification)
- Goal: standards should mandate correctly rounded results

---

## Key Takeaway

RLibm-32 proves that the excuse "correctly rounded libraries are too slow" is no longer valid. By approximating the rounded result rather than the real value, and using clever techniques to scale to 32-bit inputs, they achieve both correctness AND performance — faster than Intel's libm while being correct for every single input.
