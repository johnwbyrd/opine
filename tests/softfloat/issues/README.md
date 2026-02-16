# SoftFloat 3e: extFloat80 Unnormal Handling Bugs in Add/Sub

**Affected files:** `s_addMagsExtF80.c`, `s_subMagsExtF80.c`
**SoftFloat version:** Release 3e, commit `a0c6494`
**Related upstream issue:** [ucb-bar/berkeley-softfloat-3#37](https://github.com/ucb-bar/berkeley-softfloat-3/issues/37) (pseudo-denormals, a different but related encoding class)

## 1. Background

### 1.1 The extFloat80 Format

The x87 80-bit extended precision format (`extFloat80`) is unique among IEEE
754 formats: it stores an **explicit** integer bit (the "J-bit") at bit 63 of
the 64-bit significand.  All other IEEE formats (binary32, binary64, binary128)
use an **implicit** leading 1 for normal numbers.

```
  79   78        64  63  62                          0
 [sign][  exponent ][J ][        fraction            ]
   1      15 bits    1          63 bits
         <------- 64-bit significand field -------->
```

A **normal** extFloat80 value has: biased exponent in [1, 0x7FFE] and J = 1.

### 1.2 What Are Unnormals?

An **unnormal** is an extFloat80 bit pattern where the biased exponent is
non-zero (and not max) but J = 0.  The exponent says "normal" but the
significand says "not."  These are architecturally invalid ("unsupported") on
post-8087 x87 hardware --- the 80387 and later raise #IA (Invalid Arithmetic
Operand) when they encounter one.

There are three classes of non-standard extFloat80 encodings (all have J = 0):

| Class | Exponent | Description |
|---|---|---|
| **Unnormal** | 1 to 0x7FFE | Non-zero exp, J = 0. The focus of this report. |
| **Pseudo-denormal** | 0 | exp = 0 with sig >= 0x8000000000000000. Covered by [issue #37](https://github.com/ucb-bar/berkeley-softfloat-3/issues/37). |
| **Pseudo-NaN/Inf** | 0x7FFF | Max exp, J = 0. Must be treated as NaN. |

### 1.3 Mathematical Value of an Unnormal

The mathematical value is unambiguous.  For biased exponent E in [1, 0x7FFE]
and 64-bit significand S:

```
value = (-1)^sign * 2^(E - bias) * S / 2^63
```

When J = 0, the significand's integer part is 0, so the value is smaller than
a normal number with the same exponent would be.  When S = 0, the value is
exactly zero regardless of E.


## 2. The Bug

### 2.1 Core Issue: Missing Normalization Guard

Six of the eight extFloat80 arithmetic operations in SoftFloat explicitly
check the J-bit and normalize unnormal inputs before operating:

| Operation | File | Normalizes unnormals? |
|---|---|---|
| `extF80_mul` | `extF80_mul.c:101-114` | **Yes** |
| `extF80_div` | `extF80_div.c:105-122` | **Yes** |
| `extF80_rem` | `extF80_rem.c:105-120` | **Yes** |
| `extF80_sqrt` | `extF80_sqrt.c:93-98` | **Yes** |
| `extF80_roundToInt` | `extF80_roundToInt.c:67-76` | **Yes** |
| `softfloat_addMagsExtF80` | `s_addMagsExtF80.c` | **No** |
| `softfloat_subMagsExtF80` | `s_subMagsExtF80.c` | **No** |

The normalization guard in `extF80_mul` (lines 101-107) is representative:

```c
    if ( ! expA ) expA = 1;
    if ( ! (sigA & UINT64_C( 0x8000000000000000 )) ) {
        if ( ! sigA ) goto zero;
        normExpSig = softfloat_normSubnormalExtF80Sig( sigA );
        expA += normExpSig.exp;
        sigA = normExpSig.sig;
    }
```

This checks the J-bit (`0x8000000000000000`).  If it's clear and sig is
non-zero, it left-shifts the significand until the leading bit is set and
adjusts the exponent to compensate.  If sig is zero, the operand is treated
as zero.

`softfloat_addMagsExtF80` and `softfloat_subMagsExtF80` have **no such
guard**.  They extract `sigA` and `sigB` and immediately proceed to exponent
comparison and arithmetic.

### 2.2 The Mechanism: Phantom J-Bit Injection

In `s_addMagsExtF80.c`, when two same-exponent operands are added (or when
one operand is much larger), the code falls through to the `shiftRight1`
label (line 135):

```c
 shiftRight1:
    sig64Extra = softfloat_shortShiftRightJam64Extra( sigZ, sigZExtra, 1 );
    sigZ = sig64Extra.v | UINT64_C( 0x8000000000000000 );   // <-- forces J=1
    sigZExtra = sig64Extra.extra;
    ++expZ;
```

The OR with `0x8000000000000000` **unconditionally forces the J-bit** in the
result.  For normal inputs (where sigZ already has the J-bit set from the
addition of two J=1 significands), this is a no-op.  But for unnormal inputs
where sigA or sigB had J = 0, this manufactures a bit that wasn't in either
operand.

### 2.3 Concrete Example

Consider `0 + unnormal{exp=0x3FFF, sig=0x0000000000000000}`:

The unnormal has a significand of all zeros.  Its mathematical value is:

```
2^(0x3FFF - 16383) * 0 / 2^63 = 0
```

**Expected result:** 0 + 0 = 0.

**What SoftFloat computes:**

1. `expA = 0, sigA = 0` (positive zero)
2. `expB = 0x3FFF, sigB = 0` (unnormal)
3. `expDiff = 0 - 0x3FFF < 0`, enter the `expDiff < 0` branch
4. `expZ = 0x3FFF` (takes the larger exponent)
5. `sigA` gets right-shifted by `0x3FFF` positions, becoming 0
6. `sigZ = sigA + sigB = 0 + 0 = 0`
7. `sigZ & 0x8000000000000000` is false, so fall through to `shiftRight1`
8. At `shiftRight1`: `sigZ = 0 | 0x8000000000000000 = 0x8000000000000000`
9. `expZ = 0x3FFF + 1 = 0x4000`
10. Result: `{exp=0x4000, sig=0x8000000000000000}` = **2.0**

SoftFloat conjures 2.0 from `0 + 0`.

Meanwhile, `extF80_mul(unnormal, 1.0)` correctly returns `+0` because mul
checks the J-bit, finds sig = 0, and jumps to its `zero` label.


## 3. Test Cases

The test program `test_unnormal_bugs.cpp` checks 40 invariants across three
bug categories.  Against unpatched SoftFloat, 15 fail.  All pass with the
proposed fix.

### 3.1 Bug 1: Identity Violations (x + 0 != x * 1)

For any value x, the additive identity (x + 0) and the multiplicative identity
(x * 1) must produce the same result.  Since `extF80_mul` normalizes unnormals
and `extF80_add` does not, they disagree:

```
Unnormal: {exp=0x3FFF, sig=0x0000000000000000}   (value = 0)
  x * 1  = {signExp=0x0000, sig=0x0000000000000000}    =  0.0   CORRECT
  x + 0  = {signExp=0x4000, sig=0x8000000000000000}    =  2.0   WRONG
  x - 0  = {signExp=0x3F7F, sig=0x0000000000000000}    UNNORMAL WRONG

Unnormal: {exp=0x3FFF, sig=0x7FFFFFFFFFFFFFFF}   (value ~ 1.0 - 2^-63)
  x * 1  = {signExp=0x3FFE, sig=0xFFFFFFFFFFFFFFFE}    ~ 0.9999  CORRECT
  x + 0  = {signExp=0x4000, sig=0xC000000000000000}    = 3.0     WRONG

Unnormal: {exp=0x7FFE, sig=0x7FFFFFFFFFFFFFFF}   (largest unnormal)
  x * 1  = {signExp=0x7FFD, sig=0xFFFFFFFFFFFFFFFE}    CORRECT
  x + 0  = {signExp=0x7FFF, sig=0x8000000000000000}    = +Inf    WRONG
```

The last case is particularly dangerous: `extF80_add` promotes a finite
value to infinity.

### 3.2 Bug 2: Subnormal Boundary Crossing

The unnormal `{exp=0x0001, J=0, sig=0x7FFFFFFFFFFFFFFF}` has the same
mathematical value as the proper subnormal `{exp=0x0000, sig=0x7FFFFFFFFFFFFFFF}`:

```
unnormal:  2^(1-16383) * 0x7FFFFFFFFFFFFFFF / 2^63
subnormal: 2^(1-16383) * 0x7FFFFFFFFFFFFFFF / 2^63    (exp=0 uses emin = 1-bias)
```

These are identical because extFloat80 subnormals (exp = 0) use the same
effective exponent as exp = 1.

A naive fix that adds the mul/div normalization guard verbatim would fail here.
`softfloat_normSubnormalExtF80Sig(0x7FFFFFFFFFFFFFFF)` shifts left by 1
(returning `{exp=-1, sig=0xFFFFFFFFFFFFFFFE}`), and `expA = 1 + (-1) = 0`.
The significand has been doubled, but the effective exponent hasn't changed
(exp = 0 and exp = 1 have the same weight), so the intermediate value is 2x
too large.

In `extF80_mul`, this over-shift is masked by `softfloat_roundPackToExtF80`
(line 165), which right-shifts by `1 - exp` when packing a subnormal result,
undoing the excess left-shift.  In `addMagsExtF80`, the subnormal-sum
normalization at lines 86-90 re-normalizes before `roundPack` sees it,
preventing the compensation.

```
Against unpatched SoftFloat (after naive fix):
  unnormal  + 0  = {signExp=0x0001, sig=0xFFFFFFFFFFFFFFFE}    WRONG (2x too large)
  subnormal + 0  = {signExp=0x0000, sig=0x7FFFFFFFFFFFFFFF}    CORRECT
```

The fix must limit the normalization shift so the exponent does not drop below
1.  See Section 4.

### 3.3 Bug 3: Pseudo-NaN Normalization

Unnormals with `exp = 0x7FFF` are pseudo-NaNs (or pseudo-infinities if
`frac = 0`).  They must be treated as NaN by the NaN-propagation code.

In `extF80_mul`, the NaN/Inf check happens at lines 84-98, *before* the
normalization guard at lines 101-107.  So pseudo-NaNs are caught early and
never normalized.

A fix that adds the normalization guard before the NaN check would convert a
pseudo-NaN into a regular number:

```
Input: {exp=0x7FFF, sig=0x4000000000000000}   (pseudo-NaN)
  normSubnormalExtF80Sig(0x4000...) → shift left 1: sig=0x8000..., exp=-1
  expB = 0x7FFF + (-1) = 0x7FFE   ← no longer max exponent!
  NaN check at expB == 0x7FFF fails.
  Result: treated as a normal number, NaN silently disappears.
```

The fix must exclude `exp = 0x7FFF` from normalization.

### 3.4 Bug 4: Pseudo-Denormal Significand Overflow

A **pseudo-denormal** has `exp = 0` and `J = 1` (sig >= `0x8000000000000000`).
These are a different encoding class from unnormals (which have `exp > 0, J = 0`),
but they trigger a related bug in `s_addMagsExtF80.c`.

See: [ucb-bar/berkeley-softfloat-3#37](https://github.com/ucb-bar/berkeley-softfloat-3/issues/37)

When two pseudo-denormals with `J = 1` are added, `sigA + sigB` can overflow
64 bits.  In the equal-exponent subnormal path (lines 84-91 of the original
`s_addMagsExtF80.c`):

```c
    sigZ = sigA + sigB;       // <-- can overflow!
    sigZExtra = 0;
    if ( ! expA ) {
        normExpSig = softfloat_normSubnormalExtF80Sig( sigZ );
        expZ = normExpSig.exp + 1;
        sigZ = normExpSig.sig;
        goto roundAndPack;
    }
```

When `sigA = 0x8000000000000000` and `sigB = 0x8000000000000000`, the 64-bit
addition wraps to `sigZ = 0`.  The carry is silently lost.
`softfloat_normSubnormalExtF80Sig(0)` then produces undefined behavior (or
returns a garbage result), and the output is wrong.

**Concrete example:**

```
a = {exp=0x0000, sig=0x8000000000000000}   (pseudo-denormal, value = 2^(-16382))
b = {exp=0x0000, sig=0x8000000000000000}   (same)

Expected: a + b = 2^(-16381) = {exp=0x0002, sig=0x8000000000000000}

What unpatched SoftFloat computes:
  sigZ = 0x8000... + 0x8000... = 0x0000... (carry lost!)
  normSubnormalExtF80Sig(0) → garbage
  Result: {signExp=0x0000, sig=0x0000000000000000} = +0.0   WRONG
```

The issue also affects the case from GitHub issue #37:
`0xFFFFFFFFFFFFFFFF + 0x0000000000000001 = carry + 0x0000...`.

The fix detects the carry (via `sigZ < sigA`) and treats the result as having
effective `exp = 1`, falling through to the `shiftRight1` label which correctly
captures the carry bit.  See Section 4.4.

Note: `s_subMagsExtF80.c` does not need this fix because subtraction of
same-sign same-exponent significands cannot overflow.


## 4. Proposed Fix

### 4.1 Changes to `s_addMagsExtF80.c`

Insert the following normalization guard between field extraction (line 71)
and the `expDiff` computation (line 74).  No other changes are needed; the
variable `normExpSig` is already declared at line 60.

```c
    /*------------------------------------------------------------------------
    | Normalize unnormals (non-zero exponent but J-bit clear), matching the
    | guard that extF80_mul and extF80_div already apply.
    *------------------------------------------------------------------------*/
    if ( expA && (expA != 0x7FFF)
            && ! (sigA & UINT64_C( 0x8000000000000000 )) ) {
        if ( ! sigA ) { expA = 0; }
        else {
            normExpSig = softfloat_normSubnormalExtF80Sig( sigA );
            if ( expA + normExpSig.exp >= 1 ) {
                expA += normExpSig.exp;
                sigA = normExpSig.sig;
            } else {
                if ( expA > 1 ) sigA <<= (expA - 1);
                expA = 0;
            }
        }
    }
    if ( expB && (expB != 0x7FFF)
            && ! (sigB & UINT64_C( 0x8000000000000000 )) ) {
        if ( ! sigB ) { expB = 0; }
        else {
            normExpSig = softfloat_normSubnormalExtF80Sig( sigB );
            if ( expB + normExpSig.exp >= 1 ) {
                expB += normExpSig.exp;
                sigB = normExpSig.sig;
            } else {
                if ( expB > 1 ) sigB <<= (expB - 1);
                expB = 0;
            }
        }
    }
```

### 4.2 Changes to `s_subMagsExtF80.c`

The identical normalization guard is inserted at the same location (between
field extraction and `expDiff`).  One additional change: the variable
`normExpSig` must be declared, since the original `subMagsExtF80` does not
have one:

```c
    struct exp32_sig64 normExpSig;    /* ADD THIS to the variable declarations */
```

The guard code is identical to Section 4.1.

### 4.3 How the Fix Works

The guard has four cases for each operand:

1. **exp = 0 or exp = 0x7FFF:** Skip (subnormals and NaN/Inf are handled by
   existing code paths downstream).

2. **J = 1:** Skip (already a proper normal).

3. **J = 0, sig = 0:** Set `exp = 0`, converting the unnormal to a proper
   `+0`/`-0`.  The existing zero-handling code takes over.

4. **J = 0, sig != 0:** Normalize via `softfloat_normSubnormalExtF80Sig()`.
   - If the resulting exponent is >= 1: apply normally (the value stays in
     the normal range).
   - If the resulting exponent would be < 1: the value is actually a
     subnormal.  Shift left by only `exp - 1` positions (not the full
     amount) and set `exp = 0`.  This preserves the value because exp = 0
     and exp = 1 share the same effective exponent weight (emin = 1 - bias),
     so a partial shift of `exp - 1` compensates exactly.

### 4.4 Changes to `s_addMagsExtF80.c` for Pseudo-Denormal Carry (Bug 4)

In the equal-exponent subnormal path (where `expA == 0`), add carry detection
before the call to `softfloat_normSubnormalExtF80Sig`:

```c
        sigZ = sigA + sigB;
        sigZExtra = 0;
        if ( ! expA ) {
            if ( sigZ < sigA ) {
                /*------------------------------------------------------------
                | Significand addition carried (e.g., two pseudo-denormals
                | with J=1).  Treat as effective exp=1 and fall through to
                | shiftRight1, which captures the carry.
                *------------------------------------------------------------*/
                expZ = 1;
                goto shiftRight1;
            }
            normExpSig = softfloat_normSubnormalExtF80Sig( sigZ );
            expZ = normExpSig.exp + 1;
            sigZ = normExpSig.sig;
            goto roundAndPack;
        }
```

The carry detection uses `sigZ < sigA`, which is true if and only if the
64-bit addition wrapped.  When a carry occurs, the true 65-bit sum is
`(1 << 64) | sigZ`.  Setting `expZ = 1` and falling through to `shiftRight1`
produces the correct result: `shiftRight1` right-shifts by 1 (capturing the
implicit carry as the J-bit via the OR with `0x8000000000000000`) and
increments `expZ` to 2.

This fix is only needed in `s_addMagsExtF80.c`.  Subtraction of same-sign
values cannot produce a carry.


## 5. Verification

### 5.1 Targeted Test

`test_unnormal_bugs.cpp` checks 45 invariants covering all four bug
categories.  Build and run from the opine build directory:

```
g++ -std=c++17 -O2 \
  -I _deps/softfloat-src/source/include \
  -I softfloat_platform \
  -DSOFTFLOAT_FAST_INT64 -DLITTLEENDIAN \
  ../tests/softfloat/issues/test_unnormal_bugs.cpp \
  libsoftfloat.a -o test_unnormal_bugs

./test_unnormal_bugs
```

| SoftFloat | Passed | Failed |
|---|---|---|
| Unpatched | 26 | 19 |
| Patched | 45 | 0 |

### 5.2 Full Oracle Test

The OPINE MPFR oracle test (`test_mpfr_exact`) compares SoftFloat against
a high-precision MPFR oracle across all five IEEE formats and all four
arithmetic operations (add, sub, mul, div).  It tests both targeted
interesting-value pairs and random bit patterns.

| SoftFloat | Format | add | sub | mul | div |
|---|---|---|---|---|---|
| Unpatched | extFloat80 | 2515 FAIL | 2455 FAIL | pass | pass |
| Patched | extFloat80 | **pass** | **pass** | pass | pass |

All other formats (float16, float32, float64, float128) pass in both
configurations.  The fix does not affect them.
