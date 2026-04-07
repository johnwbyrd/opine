# SoftFloat 3e: extF80 Non-Canonical Bugs Exist in BOTH Code Paths

**Date:** 2026-02-18
**SoftFloat version:** Release 3e, commit `a0c6494` (unpatched upstream merge base)
**TestFloat version:** With genCases J-bit patch (commit `c9309bb`)

## 1. Key Finding

The non-canonical extFloat80 encoding bugs are **not limited to the
FAST_INT64 code path**.  The non-FAST_INT64 (M-variant / multi-word)
code path also has bugs with non-canonical encodings — fewer than
FAST_INT64, but still significant.

Previous analysis (see `README.md`) assumed the M-variant was correct
based on the presence of `softfloat_normExtF80SigM()` calls in mul,
div, rem, and sqrt.  Empirical testing proves this assumption was wrong.

## 2. Empirical Test Results

Both configurations tested against the same TestFloat with the genCases
J-bit patch applied.  SoftFloat built from unpatched upstream (`a0c6494`).

### 2.1 Build Commands

**FAST_INT64 (Linux-x86_64-GCC):**
```bash
cd ~/git/berkeley-softfloat-3
git checkout a0c6494
cd build/Linux-x86_64-GCC && make clean && make
# Compiles with -DSOFTFLOAT_FAST_INT64
```

**Non-FAST_INT64 (Linux-386-GCC):**
```bash
cd ~/git/berkeley-softfloat-3/build/Linux-386-GCC && make clean && make
# Compiles WITHOUT -DSOFTFLOAT_FAST_INT64
```

**TestFloat (linked against each SoftFloat in turn):**
```bash
cd ~/git/berkeley-testfloat-3/build/Linux-x86_64-GCC
# For FAST_INT64:
make clean && make
# For non-FAST_INT64:
make clean && make PLATFORM=Linux-386-GCC
```

**Test invocation (for each operation):**
```bash
cd ~/git/berkeley-testfloat-3/build/Linux-x86_64-GCC
./testsoftfloat -level 1 <operation>
```

### 2.2 Results Table

| Operation              | FAST_INT64 errors | non-FAST_INT64 errors | Notes |
|------------------------|------------------:|----------------------:|-------|
| extF80_add             | 20                | **20**                | Both paths buggy |
| extF80_sub             | 20                | **20**                | Both paths buggy |
| extF80_mul             | 20                | 0                     | M-variant has normalization guard |
| extF80_div             | 0                 | 0                     | Both correct |
| extF80_rem             | 20                | **20**                | Both paths buggy (pseudo-infinity) |
| extF80_sqrt            | 1                 | 0                     | M-variant constructs canonical result |
| extF80_eq              | 20                | **20**                | Both paths buggy |
| extF80_le              | 20                | **20**                | Both paths buggy |
| extF80_lt              | 20                | **20**                | Both paths buggy |
| extF80_eq_signaling    | 20                | **20**                | Both paths buggy |
| extF80_le_quiet        | 20                | **20**                | Both paths buggy |
| extF80_lt_quiet        | 20                | **20**                | Both paths buggy |
| extF80_to_ui32         | 20                | **2**                 | M-variant: only pseudo-infinity |
| extF80_to_ui64         | 20                | **20**                | Both paths buggy |
| extF80_to_i32          | 20                | **2**                 | M-variant: only pseudo-infinity |
| extF80_to_i64          | 20                | **20**                | Both paths buggy |
| extF80_to_ui32_r_minMag| 20                | 0                     | M-variant correct |
| extF80_to_ui64_r_minMag| 20                | 0                     | M-variant correct |
| extF80_to_i32_r_minMag | 20                | 0                     | M-variant correct |
| extF80_to_i64_r_minMag | 20                | 0                     | M-variant correct |
| extF80_to_f16          | 20                | 0                     | M-variant correct |
| extF80_to_f32          | 20                | 0                     | M-variant correct |
| extF80_to_f64          | 20                | 0                     | M-variant correct |
| extF80_to_f128         | 20                | 0                     | M-variant correct |
| extF80_roundToInt      | 0                 | 0                     | Both correct |

**Summary counts:**

| | FAST_INT64 | non-FAST_INT64 |
|---|---|---|
| Operations with errors | 23 | **14** |
| Operations clean       | 2  | 11 |

### 2.3 Sample Error Output

**non-FAST_INT64 extF80_add (unnormals with J=0 produce wrong results):**
```
-5D0D.7FFFFFFC01FFFFFF  -403D.FFFFFFDFFEFFFFFE
    => -5D0E.BFFFFFFE00FFFFFF ....x  expected -5D0C.FFFFFFF803FFFFFF ....x
```

**non-FAST_INT64 extF80_eq (pseudo-infinity compared as zero; pseudo-denormal != equivalent unnormal):**
```
+0000.0000000000000000  +7FFF.0000000000000000  => 1 .....  expected 0 .....
+0000.0000000000000001  +0001.0000000000000001  => 0 .....  expected 1 .....
```

**non-FAST_INT64 extF80_rem (pseudo-infinity divisor triggers invalid instead of returning dividend):**
```
+0001.8000000000000000  -7FFF.0000000000000000
    => -7FFF.C000000000000000 v....  expected +0001.8000000000000000 .....
```

**non-FAST_INT64 extF80_to_ui32 (pseudo-infinity treated as zero instead of overflow):**
```
+7FFF.0000000000000000  => 00000000 .....  expected FFFFFFFF v....
-7FFF.0000000000000000  => 00000000 .....  expected 00000000 v....
```

**non-FAST_INT64 extF80_to_ui64 (unnormals produce wrong conversion results):**
```
-407F.0000000000000000
    => FFFFFFFFFFFFFFFF v....  expected 0000000000000000 .....
```

## 3. Analysis: Why the M-Variant Path Fails

### 3.1 Operations the M-variant gets RIGHT (that FAST_INT64 gets wrong)

These operations have normalization guards that correctly handle J=0 inputs:

- **mul** (`extF80M_mul.c`): Calls `softfloat_normExtF80SigM(&sigA)` and
  `softfloat_normExtF80SigM(&sigB)` for inputs with J=0.  Also checks
  `!sigA` / `!sigB` directly for the Inf*0 case (no `magBits` shortcut).
- **sqrt** (`extF80M_sqrt.c`): Calls `softfloat_normExtF80SigM(&rem64)`
  for J=0 inputs.  Constructs canonical infinity explicitly for Inf result.
- **_r_minMag conversions**: The M-variant implementations handle
  unnormals correctly through their normalization logic.
- **to_f16/f32/f64/f128 conversions**: The M-variant implementations
  normalize before converting.

### 3.2 Operations the M-variant gets WRONG

**add/sub** (`s_addExtF80M.c`):
- The M-variant has `if (!expB) { expB = 1; if (!expA) expA = 1; }` which
  handles pseudo-denormals (exp=0, J=1) by bumping exp to 1.
- But it does NOT normalize unnormals (exp>0, J=0).  The significand is
  used as-is with the original exponent, producing wrong results.
- The code reads `sigZ = aSPtr->signif` directly and uses it without
  checking or fixing the J-bit.

**rem** (`extF80M_rem.c`):
- Same pseudo-infinity bug as FAST_INT64 path.  When B has exp=0x7FFF
  and sig=0 (pseudo-infinity), the normalization guard at line 107 sees
  sig=0 and goes to `invalid` instead of recognizing it as infinity.

**All 6 comparisons** (`extF80M_eq.c`, `extF80M_le.c`, `extF80M_lt.c`, etc.):
- The M-variant comparisons use `softfloat_compareNonnormExtF80M()`.
- This function handles the pseudo-denormal case (exp=0, J=1) but
  appears to fail for other non-canonical patterns (pseudo-infinity
  treated as zero, unnormal exp/sig mismatch with equivalent values).

**to_ui32/i32** (2 errors each):
- Only fails for pseudo-infinity input (`+7FFF.0000000000000000`).
  The M-variant treats it as zero instead of overflow.

**to_ui64/i64** (20 errors):
- More general unnormal conversion failures.

### 3.3 Shared Root Causes

The bugs that appear in BOTH paths share two root causes:

1. **No normalization in add/sub**: Neither path normalizes unnormal
   inputs before performing addition/subtraction arithmetic.

2. **Pseudo-infinity not recognized**: In rem and some conversions,
   pseudo-infinity (`{0x7FFF, 0x0000000000000000}`) is treated as zero
   or invalid instead of as infinity.

3. **Comparison functions don't canonicalize**: Both paths compare
   non-canonical bit patterns directly, leading to wrong ordering
   and equality results.

## 4. Implications for the Fix

### 4.1 Current Fix Coverage

The user's fix on SoftFloat master (commits `631bd61` + `1787949`) adds
`softfloat_canonicalizeExtF80()` to the FAST_INT64 code path only.  This
fixes all 23 failing operations in the FAST_INT64 path.

The non-FAST_INT64 path needs its own fixes:
- `s_addExtF80M.c`: Needs unnormal normalization
- `extF80M_rem.c`: Needs pseudo-infinity handling
- `s_compareNonnormExtF80M.c`: Needs better non-canonical handling
- `extF80M_to_ui32.c`, `extF80M_to_i32.c`: Pseudo-infinity case
- `extF80M_to_ui64.c`, `extF80M_to_i64.c`: Unnormal handling

### 4.2 TestFloat Coverage

The genCases J-bit patch (TestFloat PR #21) successfully catches bugs
in **both** configurations.  The test vector generator is configuration-
independent — the same non-canonical test vectors exercise whichever
SoftFloat library is linked.

## 5. Reproduction Steps

### 5.1 Prerequisites

```bash
# Clone the repositories side by side
git clone git@github.com:johnwbyrd/berkeley-softfloat-3.git ~/git/berkeley-softfloat-3
git clone git@github.com:johnwbyrd/berkeley-testfloat-3.git ~/git/berkeley-testfloat-3

# TestFloat must be on master (has genCases J-bit patch, commit c9309bb)
cd ~/git/berkeley-testfloat-3 && git checkout master

# SoftFloat must be on the unpatched upstream base to see bugs
cd ~/git/berkeley-softfloat-3 && git checkout a0c6494
```

### 5.2 Reproduce FAST_INT64 Failures

```bash
# Build SoftFloat with FAST_INT64
cd ~/git/berkeley-softfloat-3/build/Linux-x86_64-GCC
make clean && make

# Build TestFloat
cd ~/git/berkeley-testfloat-3/build/Linux-x86_64-GCC
make clean && make

# Run tests — these will show errors
./testsoftfloat -level 1 extF80_add     # 20 errors
./testsoftfloat -level 1 extF80_mul     # 20 errors
./testsoftfloat -level 1 extF80_eq      # 20 errors
./testsoftfloat -level 1 extF80_to_f32  # 20 errors
# ... (23 of 25 extF80 operations fail)
```

### 5.3 Reproduce non-FAST_INT64 Failures

```bash
# Build SoftFloat WITHOUT FAST_INT64
cd ~/git/berkeley-softfloat-3/build/Linux-386-GCC
make clean && make

# Rebuild TestFloat linked against non-FAST_INT64 SoftFloat
cd ~/git/berkeley-testfloat-3/build/Linux-x86_64-GCC
make clean && make PLATFORM=Linux-386-GCC

# Run tests — some will show errors, some will pass
./testsoftfloat -level 1 extF80_add     # 20 errors (SAME BUG)
./testsoftfloat -level 1 extF80_mul     # 0 errors  (M-variant is correct)
./testsoftfloat -level 1 extF80_eq      # 20 errors (SAME BUG)
./testsoftfloat -level 1 extF80_to_f32  # 0 errors  (M-variant is correct)
# ... (14 of 25 extF80 operations fail)
```

### 5.4 Verify FAST_INT64 Fix

```bash
# Switch SoftFloat to patched master
cd ~/git/berkeley-softfloat-3 && git checkout master
cd build/Linux-x86_64-GCC && make clean && make

# Rebuild TestFloat
cd ~/git/berkeley-testfloat-3/build/Linux-x86_64-GCC
make clean && make

# All extF80 operations should pass
for op in extF80_add extF80_sub extF80_mul extF80_div extF80_rem \
  extF80_sqrt extF80_eq extF80_le extF80_lt extF80_eq_signaling \
  extF80_le_quiet extF80_lt_quiet extF80_to_ui32 extF80_to_ui64 \
  extF80_to_i32 extF80_to_i64 extF80_to_ui32_r_minMag \
  extF80_to_ui64_r_minMag extF80_to_i32_r_minMag \
  extF80_to_i64_r_minMag extF80_to_f16 extF80_to_f32 \
  extF80_to_f64 extF80_to_f128 extF80_roundToInt; do
  echo "=== $op ===" && ./testsoftfloat -level 1 "$op" 2>&1 | tail -1
done
```
