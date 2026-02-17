# SoftFloat 3e: extFloat80 Non-Canonical Input Handling Bugs

**Affected files:** `s_addMagsExtF80.c`, `s_subMagsExtF80.c`, `extF80_mul.c`,
`extF80_rem.c`, `extF80_sqrt.c`
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

### 1.2 Non-Canonical Encodings

The explicit J-bit creates encodings with no analog in other IEEE formats.
Four classes of non-canonical encoding exist:

| Class | Exponent | J | Significand | Description |
|---|---|---|---|---|
| **Unnormal** | 1 to 0x7FFE | 0 | any | Non-zero exp, J = 0. Primary focus of this report. |
| **Pseudo-denormal** | 0 | 1 | &ge; 0x8000000000000000 | J = 1 when exp = 0. See [issue #37](https://github.com/ucb-bar/berkeley-softfloat-3/issues/37). |
| **Pseudo-NaN** | 0x7FFF | 0 | &ne; 0 | Max exp, J = 0, non-zero fraction. Treated as NaN. |
| **Pseudo-infinity** | 0x7FFF | 0 | 0 | Max exp, J = 0, zero significand. Treated as &infin;. |

It is critical to note that, even though Intel 80387 and later trap on
unnormals, **unnormals are valid numbers**, both in theory and in practice on
some hardware.  Consider for example [the original Intel 8087 patent](https://patents.google.com/patent/USRE33629E),
which goes into some detail about unnormals and how the hardware handles them:
"The file format has a explicit leading bit in the significand and thus allows
unnormalized as well as normalized arithmetic."

On post-8087 hardware (80387+), all non-canonical encodings raise #IA
(Invalid Arithmetic Operand).  But a software implementation like SoftFloat
is free to interpret these bit patterns by their mathematical values, and
indeed SoftFloat's own normalization guards in `extF80_mul`, `extF80_div`,
etc. demonstrate intent to handle them.  The TestFloat reference
implementation (`slowfloat`) handles all non-canonical classes correctly.

### 1.3 Mathematical Values

**Unnormals:** For biased exponent E in [1, 0x7FFE] and 64-bit significand S:

```
value = (-1)^sign * 2^(E - 16383) * S / 2^63
```

When J = 0, the significand's integer part is 0, so the value is smaller than
a normal number with the same exponent would be.  When S = 0, the value is
exactly zero regardless of E (an "unnormal-zero").

**Pseudo-denormals:** Equivalent to a denormal with the same significand:

```
value = (-1)^sign * 2^(1 - 16383) * S / 2^63
```

The J-bit contributes 2^(-16382) to the value — the same as a normal number
with exponent 1.

**Pseudo-NaN:** Treated as NaN (not a number).

**Pseudo-infinity:** Treated as &plusmn;&infin;.


## 2. Why These Bugs Went Undetected

### 2.1 SoftFloat Documentation

SoftFloat's documentation (Section 4.4) explicitly states:

> The 80-bit extended format [...] functions are not guaranteed to operate as
> expected when inputs of type `extFloat80_t` are non-canonical.

This disclaimer was taken as a permanent design boundary rather than a gap to
be fixed.  Meanwhile, six of SoftFloat's eight extFloat80 operations include
normalization guards that partially handle non-canonical inputs — silently
masking the fact that add/sub were missing theirs.

### 2.2 TestFloat's J-Bit Forcing

Berkeley TestFloat 3e (`genCases_extF80.c`) generates test inputs for
extFloat80 operations.  In all eight test generators (four deterministic,
four random), a single line forces J = 1 for every non-zero exponent:

```c
if ( uiZ64 & 0x7FFF ) uiZ0 |= UINT64_C( 0x8000000000000000 );
```

The four random generators additionally mask off the J-bit before applying
the force:

```c
uiZ0 = ... & UINT64_C( 0x7FFFFFFFFFFFFFFF );
...
if ( uiZ64 & 0x7FFF ) uiZ0 |= UINT64_C( 0x8000000000000000 );
```

This makes it impossible for TestFloat to generate any unnormal, pseudo-NaN,
or pseudo-infinity test input.  Since the reference implementation
(`slowfloat`) handles all non-canonical classes correctly, the only thing
preventing TestFloat from catching these bugs was the test generator.

Both the bugs and the J-bit forcing trace to the very first commit
(Nov 28, 2014) of both repositories, by the same author (John R. Hauser).

### 2.3 TestFloat Fix

Adding a J-bit dimension to the test generator (`genCases_extF80.c`) — so
that each deterministic test case is generated in both canonical (J = 1 for
normals) and non-canonical (J = 0 for normals, J = 1 for subnormals)
variants — immediately reveals all bugs documented in this report.  The
random generators similarly have their J-bit masks and forcing lines removed
so they naturally produce both canonical and non-canonical bit patterns.

The patched TestFloat was used to produce the test results in Section 6.


## 3. The Bugs

### 3.1 Add/Sub: Missing Normalization Guard

**Files:** `s_addMagsExtF80.c`, `s_subMagsExtF80.c`

Six of the eight extFloat80 operations include a normalization guard like
this one from `extF80_mul.c` (lines 101-107):

```c
    if ( ! expA ) expA = 1;
    if ( ! (sigA & UINT64_C( 0x8000000000000000 )) ) {
        if ( ! sigA ) goto zero;
        normExpSig = softfloat_normSubnormalExtF80Sig( sigA );
        expA += normExpSig.exp;
        sigA = normExpSig.sig;
    }
```

This checks the J-bit.  If it's clear and sig is non-zero, it normalizes.
If sig is zero, the operand is treated as zero.

`softfloat_addMagsExtF80` and `softfloat_subMagsExtF80` have **no such
guard**.  They extract `sigA` and `sigB` and immediately proceed to exponent
comparison and arithmetic.

**The phantom J-bit:** In `s_addMagsExtF80.c`, the `shiftRight1` label
(line 135) unconditionally forces the J-bit:

```c
 shiftRight1:
    sig64Extra = softfloat_shortShiftRightJam64Extra( sigZ, sigZExtra, 1 );
    sigZ = sig64Extra.v | UINT64_C( 0x8000000000000000 );   // <-- forces J=1
    sigZExtra = sig64Extra.extra;
    ++expZ;
```

For normal inputs, this is a no-op (the J-bit is already set from the
addition of two J = 1 significands).  But for unnormal inputs where
the significand was zero or small, this manufactures a bit that wasn't in
either operand.

**Concrete example — zero-significand unnormal:**

```
Input:  {exp=0x3FFF, sig=0x0000000000000000}
Value:  2^(0x3FFF - 16383) * 0 / 2^63 = 0

  extF80_mul(x, 1.0)  =  +0.0                       CORRECT  (normalization guard)
  extF80_add(x, 0.0)  =  {0x4000, 0x8000...}        WRONG    (= 2.0)
  extF80_sub(x, 0.0)  =  {0x3F7F, 0x0000...}        WRONG    (unnormal output)
```

SoftFloat conjures 2.0 from `0 + 0`.

**Concrete example — near-one unnormal:**

```
Input:  {exp=0x3FFF, sig=0x7FFFFFFFFFFFFFFF}
Value:  2^0 * 0x7FFFFFFFFFFFFFFF / 2^63 ~ 1.0 - 2^{-63}

  extF80_mul(x, 1.0)  =  {0x3FFE, 0xFFFFFFFFFFFFFFFE}    CORRECT  (~ 0.9999...)
  extF80_add(x, 0.0)  =  {0x4000, 0xC000000000000000}    WRONG    (= 3.0)
```

**Concrete example — largest unnormal:**

```
Input:  {exp=0x7FFE, sig=0x7FFFFFFFFFFFFFFF}
Value:  finite (just below the maximum normal)

  extF80_mul(x, 1.0)  =  {0x7FFD, 0xFFFFFFFFFFFFFFFE}    CORRECT
  extF80_add(x, 0.0)  =  {0x7FFF, 0x8000000000000000}    WRONG    (= +Inf)
```

The last case is particularly dangerous: a finite value is promoted to
infinity.

### 3.2 Add/Sub: Subnormal Boundary Crossing

The unnormal `{exp=0x0001, J=0, sig=0x7FFFFFFFFFFFFFFF}` has the same
mathematical value as the subnormal `{exp=0x0000, sig=0x7FFFFFFFFFFFFFFF}`:

```
unnormal:  2^(1-16383) * 0x7FFFFFFFFFFFFFFF / 2^63
subnormal: 2^(1-16383) * 0x7FFFFFFFFFFFFFFF / 2^63    (exp=0 uses emin = 1-bias)
```

These are identical because extFloat80 subnormals (exp = 0) use the same
effective exponent as exp = 1.

A naive fix that copies the mul/div normalization guard verbatim would fail
here.  `softfloat_normSubnormalExtF80Sig(0x7FFFFFFFFFFFFFFF)` shifts left
by 1 (returning `{exp=-1, sig=0xFFFFFFFFFFFFFFFE}`), and `expA = 1 + (-1) = 0`.
The significand has been doubled, but the effective exponent hasn't changed
(exp = 0 and exp = 1 have the same weight), so the intermediate value is 2x
too large.

In `extF80_mul`, this over-shift is masked by `softfloat_roundPackToExtF80`
(line 127), which right-shifts by `1 - exp` when packing a subnormal result,
undoing the excess left-shift.  In `addMagsExtF80`, the subnormal-sum
normalization at lines 86-90 re-normalizes before `roundPack` sees it,
preventing the compensation.

```
Against unpatched SoftFloat (after naive fix):
  unnormal  + 0  = {signExp=0x0001, sig=0xFFFFFFFFFFFFFFFE}    WRONG (2x too large)
  subnormal + 0  = {signExp=0x0000, sig=0x7FFFFFFFFFFFFFFF}    CORRECT
```

The fix must limit the normalization shift so the exponent does not drop
below 1.  See Section 5.

### 3.3 Add/Sub: Pseudo-NaN Ordering Constraint

Unnormals with `exp = 0x7FFF` and `sig != 0` are pseudo-NaNs.  They must be
treated as NaN by the NaN-propagation code.

In `extF80_mul`, the NaN/Inf check happens at lines 84-98, *before* the
normalization guard at lines 101-107.  So pseudo-NaNs are caught early and
never normalized.

A fix that adds the normalization guard *before* the NaN check would convert
a pseudo-NaN into a regular number:

```
Input: {exp=0x7FFF, sig=0x4000000000000000}   (pseudo-NaN)
  normSubnormalExtF80Sig(0x4000...) -> shift left 1: sig=0x8000..., exp=-1
  expB = 0x7FFF + (-1) = 0x7FFE   <- no longer max exponent!
  NaN check at expB == 0x7FFF fails.
  Result: treated as a normal number, NaN silently disappears.
```

The normalization guard must exclude `exp = 0x7FFF` from normalization.

### 3.4 Add: Pseudo-Denormal Significand Overflow

A **pseudo-denormal** has `exp = 0` and `J = 1` (sig >= `0x8000000000000000`).
These are a distinct encoding class from unnormals (which have `exp > 0,
J = 0`), but they trigger a related bug in `s_addMagsExtF80.c`.

See: [ucb-bar/berkeley-softfloat-3#37](https://github.com/ucb-bar/berkeley-softfloat-3/issues/37)

When two pseudo-denormals with J = 1 are added, `sigA + sigB` can overflow
64 bits.  In the equal-exponent subnormal path (line 84 of
`s_addMagsExtF80.c`):

```c
    sigZ = sigA + sigB;       // <-- can overflow!
    sigZExtra = 0;
    if ( ! expA ) {
        normExpSig = softfloat_normSubnormalExtF80Sig( sigZ );
```

When `sigA = 0x8000000000000000` and `sigB = 0x8000000000000000`, the 64-bit
addition wraps to `sigZ = 0`.  The carry is silently lost.
`softfloat_normSubnormalExtF80Sig(0)` then produces undefined behavior.

```
a = {exp=0x0000, sig=0x8000000000000000}   (pseudo-denormal, value = 2^(-16382))
b = {exp=0x0000, sig=0x8000000000000000}   (same)

Expected:  a + b = 2^(-16381) = {exp=0x0002, sig=0x8000000000000000}
SoftFloat: sigZ = 0x8000... + 0x8000... = 0x0000... (carry lost!)
           Result: {0x0000, 0x0000000000000000} = +0.0   WRONG
```

The issue also affects the case from GitHub issue #37:
`0xFFFFFFFFFFFFFFFF + 0x0000000000000001 = carry + 0x0000...`.

Note: `s_subMagsExtF80.c` does not need this fix because subtraction of
same-sign same-exponent significands cannot overflow.

### 3.5 Mul: Infinity x Unnormal-Zero

**File:** `extF80_mul.c`
**Root cause:** The `magBits` zero-detection shortcut at lines 91 and 96.

When one operand is infinity, `extF80_mul` checks whether the other is zero
(since Inf x 0 is invalid).  It does so with:

```c
    if ( expA == 0x7FFF ) {
        ...
        magBits = expB | sigB;    // line 91
        goto infArg;
    }
    if ( expB == 0x7FFF ) {
        ...
        magBits = expA | sigA;    // line 96
        goto infArg;
    }
    ...
 infArg:
    if ( ! magBits ) {            // line 138: zero check
        softfloat_raiseFlags( softfloat_flag_invalid );
        ...                       // return NaN
    } else {
        ...                       // return Inf
    }
```

`magBits = expB | sigB` is a shortcut: for canonical encodings, an operand
is zero if and only if both `exp` and `sig` are zero.  But an unnormal-zero
has `exp != 0` and `sig = 0`, making its mathematical value zero while
`magBits != 0`.

```
Example 1:
  A = +7FFF.8000000000000000    (canonical +Inf)
  B = +0001.0000000000000000    (unnormal: exp=1, sig=0, value = 0)

  magBits = 0x0001 | 0 = 0x0001   (non-zero!)
  infArg: magBits != 0 -> return +Inf

  Expected: Inf x 0 = NaN (invalid)
  SoftFloat: Inf x 0 = +Inf   WRONG

Example 2:
  A = +7FFF.8000000000000000    (canonical +Inf)
  B = +3FFF.0000000000000000    (unnormal: exp=0x3FFF, sig=0, value = 0)

  magBits = 0x3FFF | 0 = 0x3FFF   (non-zero!)
  infArg: magBits != 0 -> return +Inf

  Expected: Inf x 0 = NaN (invalid)
  SoftFloat: Inf x 0 = +Inf   WRONG
```

Any unnormal-zero (sig = 0 with any non-zero exponent) triggers this bug.
The normalization guard at lines 101-114 would correctly identify `sigB = 0`
as zero and jump to the `zero` label — but the Inf/NaN path at lines 84-98
runs *before* normalization, so for `Inf x unnormal-zero` the normalization
guard is never reached.

The same pattern at line 96 (`magBits = expA | sigA`) causes the symmetric
case: `unnormal-zero x Inf` also incorrectly returns Inf.

**Why div is not affected:** `extF80_div` does not use the `magBits`
pattern.  For `Inf / x`, it unconditionally returns Inf (correct, since
`Inf / 0 = Inf`).  For `x / 0`, the normalization guard catches unnormal-
zeros via `!sigB` before the division-by-zero path.

### 3.6 Rem: Pseudo-Infinity Divisor

**File:** `extF80_rem.c`
**Root cause:** Order-of-operations conflict between infinity handling and
the normalization guard.

When B (the divisor) has `exp = 0x7FFF`, `extF80_rem` recognizes it as
infinity at lines 93-101.  The comment reads: "Argument b is an infinity.
Doubling `expB` is an easy way to ensure that `expDiff` later is less than
-1, which will result in returning a canonicalized version of argument a."
This sets `expB += expB` (= 0xFFFE) and falls through.

For a canonical infinity (`{0x7FFF, 0x8000000000000000}`), the normalization
guard at lines 105-110 is skipped because J = 1.  But for a pseudo-infinity
(`{0x7FFF, 0x0000000000000000}`), `sig = 0` and J = 0, so the guard fires:

```c
    if ( ! (sigB & UINT64_C( 0x8000000000000000 )) ) {    // J=0: true
        if ( ! sigB ) goto invalid;                        // sig=0: INVALID
```

The guard interprets `sig = 0` as "invalid operand" rather than recognizing
that the infinity handling at lines 93-101 has already determined B is
infinity.

```
A = 1.0 = {exp=0x3FFF, sig=0x8000000000000000}
B = pseudo-Inf = {exp=0x7FFF, sig=0x0000000000000000}

Expected:  rem(1.0, Inf) = 1.0
SoftFloat: rem(1.0, pseudo-Inf) = NaN (invalid)   WRONG
```

### 3.7 Sqrt: Non-Canonical Infinity Passthrough

**File:** `extF80_sqrt.c`
**Root cause:** The Inf/NaN check returns the input without canonicalization.

When `exp = 0x7FFF` and `sigA & 0x7FFFFFFFFFFFFFFF == 0`, the code treats
the input as infinity.  For positive infinity, line 81 returns `a` unchanged:

```c
    if ( expA == 0x7FFF ) {
        if ( sigA & UINT64_C( 0x7FFFFFFFFFFFFFFF ) ) {
            ...  // propagate NaN
        }
        if ( ! signA ) return a;    // line 81: return input as-is
        goto invalid;
    }
```

For canonical `+Inf` (`{0x7FFF, 0x8000000000000000}`), returning `a`
unchanged is correct.  For pseudo-infinity (`{0x7FFF, 0x0000000000000000}`),
it returns the non-canonical bit pattern.  The slowfloat reference
canonicalizes the result to `{0x7FFF, 0x8000000000000000}`.

```
A = pseudo-Inf = {exp=0x7FFF, sig=0x0000000000000000}

Expected:  sqrt(+Inf) = {0x7FFF, 0x8000000000000000}  (canonical +Inf)
SoftFloat: sqrt(+Inf) = {0x7FFF, 0x0000000000000000}  (pseudo-Inf passthrough)
```

This is the least severe of the bugs — the mathematical value is the same,
but the bit pattern is non-canonical.


## 4. Summary of Affected Operations

| Operation | Normalization guard | Inf/NaN path | Bugs |
|---|---|---|---|
| `extF80_add` | **Missing** | Correct | 3.1, 3.2, 3.3, 3.4 |
| `extF80_sub` | **Missing** | Correct | 3.1, 3.2, 3.3 |
| `extF80_mul` | Present (line 101) | **`magBits` shortcut** | 3.5 |
| `extF80_div` | Present (line 105) | Correct | — |
| `extF80_rem` | Present (line 104) | **Guard trips after Inf handling** | 3.6 |
| `extF80_sqrt` | Present (line 92) | **Passthrough without canonicalization** | 3.7 |
| `extF80_roundToInt` | Present | Correct | — |


## 5. Proposed Fixes for Add/Sub

These are the most impactful bugs: add and sub are the only operations
completely missing the normalization guard, causing wrong results for all
unnormal inputs (not just edge cases involving infinity).

### 5.1 Changes to `s_addMagsExtF80.c`

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

### 5.2 Changes to `s_subMagsExtF80.c`

The identical normalization guard is inserted at the same location (between
field extraction and `expDiff`).  One additional change: the variable
`normExpSig` must be declared, since the original `subMagsExtF80` does not
have one:

```c
    struct exp32_sig64 normExpSig;    /* ADD THIS to the variable declarations */
```

The guard code is identical to Section 5.1.

### 5.3 How the Fix Works

The guard has four cases for each operand:

1. **exp = 0 or exp = 0x7FFF:** Skip (subnormals and NaN/Inf are handled by
   existing code paths downstream).

2. **J = 1:** Skip (already a proper normal).

3. **J = 0, sig = 0:** Set `exp = 0`, converting the unnormal-zero to a
   proper `+0`/`-0`.  The existing zero-handling code takes over.

4. **J = 0, sig != 0:** Normalize via `softfloat_normSubnormalExtF80Sig()`.
   - If the resulting exponent is >= 1: apply normally (the value stays in
     the normal range).
   - If the resulting exponent would be < 1: the value is actually a
     subnormal.  Shift left by only `exp - 1` positions (not the full
     amount) and set `exp = 0`.  This preserves the value because exp = 0
     and exp = 1 share the same effective exponent weight (emin = 1 - bias),
     so a partial shift of `exp - 1` compensates exactly.

### 5.4 Pseudo-Denormal Carry Detection (Bug 3.4)

In the equal-exponent subnormal path (where `expA == 0`), add carry
detection before the call to `softfloat_normSubnormalExtF80Sig`:

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
produces the correct result: the right-shift captures the implicit carry as
the J-bit (via the OR with `0x8000000000000000`) and increments `expZ` to 2.

This fix is only needed in `s_addMagsExtF80.c`.


## 6. Verification

### 6.1 TestFloat Fix

The test generator `genCases_extF80.c` in Berkeley TestFloat 3e was patched
to add a J-bit dimension to the test generation sequence.  Each deterministic
test case is now generated in both canonical (J = 1 for normals) and
non-canonical (J = 0 for normals, J = 1 for subnormals) variants.  Random
generators have their J-bit masks and forcing lines removed.

This doubles the deterministic test count per operand.  For binary operations
(two operands), total test count increases 4x:

| Level | Before | After |
|---|---|---|
| Level 1 (binary op) | 46,464 | 185,856 |
| Level 2 (binary op) | ~59.6M | ~238M |

### 6.2 TestFloat Results (against unpatched SoftFloat)

Using the patched TestFloat with `testsoftfloat -level 1`:

| Operation | Result | Notes |
|---|---|---|
| `extF80_add` | **ERRORS FOUND** | Missing normalization guard (3.1-3.4) |
| `extF80_sub` | **ERRORS FOUND** | Missing normalization guard (3.1-3.3) |
| `extF80_mul` | **ERRORS FOUND** | Inf x unnormal-zero (3.5) |
| `extF80_div` | pass | |
| `extF80_rem` | **20 ERRORS** | Pseudo-infinity divisor (3.6) |
| `extF80_sqrt` | **1 ERROR per rounding mode** | Pseudo-infinity passthrough (3.7) |
| `extF80_roundToInt` | pass | |
| `f64_add` | pass | Other formats unaffected |

### 6.3 Targeted Test

`test_unnormal_bugs.cpp` checks 45 invariants covering the add/sub bugs
(Sections 3.1-3.4).  Build and run from the opine build directory:

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
| Patched (add/sub fix) | 45 | 0 |

### 6.4 MPFR Oracle Test

The OPINE MPFR oracle test (`test_mpfr_exact`) compares SoftFloat against
a high-precision MPFR oracle across all five IEEE formats and all four
arithmetic operations (add, sub, mul, div).

| SoftFloat | Format | add | sub | mul | div |
|---|---|---|---|---|---|
| Unpatched | extFloat80 | 2515 FAIL | 2455 FAIL | pass | pass |
| Patched | extFloat80 | **pass** | **pass** | pass | pass |

All other formats (float16, float32, float64, float128) pass in both
configurations.  The fix does not affect them.

Note: the MPFR oracle test focuses on finite arithmetic and does not test
mul with infinity operands, so the mul bug (3.5) is not caught by this test.
It is caught by the patched TestFloat (6.2).
