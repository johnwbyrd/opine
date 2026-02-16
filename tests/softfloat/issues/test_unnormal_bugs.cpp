// test_unnormal_bugs.cpp — Demonstrate bugs in SoftFloat 3e extFloat80 add/sub
//
// Berkeley SoftFloat 3e has an inconsistency in its handling of extFloat80
// "unnormal" bit patterns (non-zero biased exponent, explicit J-bit = 0):
//
//   - extF80_mul and extF80_div normalize unnormals before operating.
//   - extF80_add and extF80_sub do NOT, producing wrong results.
//
// This program tests arithmetic identities and cross-operation consistency
// to expose the bugs.  Against unpatched SoftFloat, many tests fail.
// Against a correctly patched SoftFloat, all tests pass.
//
// Build from the opine build directory:
//
//   g++ -std=c++17 -O2 \
//     -I _deps/softfloat-src/source/include \
//     -I softfloat_platform \
//     -DSOFTFLOAT_FAST_INT64 -DLITTLEENDIAN \
//     ../tests/softfloat/issues/test_unnormal_bugs.cpp \
//     libsoftfloat.a -o test_unnormal_bugs
//
// Run:  ./test_unnormal_bugs

#include <cstdint>
#include <cstdio>
#include <cstring>

extern "C" {
#include "softfloat.h"
}

// ============================================================================
// Helpers
// ============================================================================

static extFloat80_t make(uint16_t signExp, uint64_t sig) {
    extFloat80_t r;
    r.signExp = signExp;
    r.signif = sig;
    return r;
}

static bool eq(extFloat80_t a, extFloat80_t b) {
    return a.signExp == b.signExp && a.signif == b.signif;
}

static bool isNaN80(extFloat80_t v) {
    int exp = v.signExp & 0x7FFF;
    if (exp != 0x7FFF) return false;
    // For extFloat80, NaN = max exp with: J=1 and frac!=0, or J=0 (pseudo-NaN)
    uint64_t jBit = v.signif & UINT64_C(0x8000000000000000);
    uint64_t frac = v.signif & UINT64_C(0x7FFFFFFFFFFFFFFF);
    if (jBit && frac == 0) return false; // that's infinity, not NaN
    if (v.signif == 0) return false;     // pseudo-infinity with frac=0
    return true;
}

static void dump(const char *label, extFloat80_t v) {
    std::printf("    %-24s signExp=0x%04X sig=0x%016llX", label,
                v.signExp, (unsigned long long)v.signif);
    int exp = v.signExp & 0x7FFF;
    int sign = v.signExp >> 15;
    int j = (v.signif >> 63) & 1;
    if (exp == 0x7FFF) {
        if (isNaN80(v))
            std::printf("  [NaN]");
        else
            std::printf("  [%sInf]", sign ? "-" : "+");
    } else {
        std::printf("  (sign=%d exp=0x%04X J=%d)", sign, exp, j);
        if (exp != 0 && j == 0) std::printf(" UNNORMAL");
    }
    std::printf("\n");
}

static double toDouble(extFloat80_t v) {
    float64_t f64 = extF80_to_f64(v);
    double d;
    std::memcpy(&d, &f64, sizeof(d));
    return d;
}

// ============================================================================
// Well-known bit patterns
// ============================================================================

static const extFloat80_t POS_ZERO = make(0x0000, 0x0000000000000000);
static const extFloat80_t NEG_ZERO = make(0x8000, 0x0000000000000000);
static const extFloat80_t POS_ONE  = make(0x3FFF, 0x8000000000000000);
static const extFloat80_t NEG_ONE  = make(0xBFFF, 0x8000000000000000);
static const extFloat80_t POS_TWO  = make(0x4000, 0x8000000000000000);

// ============================================================================
// Test infrastructure
// ============================================================================

static int g_pass = 0, g_fail = 0;

static void check(const char *testName, bool cond) {
    if (cond) {
        ++g_pass;
    } else {
        ++g_fail;
        std::printf("  FAIL: %s\n", testName);
    }
}

// ============================================================================
// BUG 1: Missing unnormal normalization in addMagsExtF80 / subMagsExtF80
//
// extF80_mul and extF80_div check the J-bit and call
// softfloat_normSubnormalExtF80Sig() to normalize unnormal inputs.
// addMagsExtF80 and subMagsExtF80 skip this, causing them to treat
// unnormals as if the J-bit were set, manufacturing value from nothing.
// ============================================================================

static void test_bug1_zero_significand_unnormal() {
    std::printf("\n--- Bug 1a: Zero-significand unnormal ---\n");
    std::printf("  Input: {exp=0x3FFF, sig=0x0000000000000000}\n");
    std::printf("  The significand is all zeros.  Mathematical value = 0.\n\n");

    // unnormal: biased exponent = 0x3FFF (2^0), but significand = 0
    // Mathematical value: 2^(0x3FFF - 16383) * 0 / 2^63 = 0
    extFloat80_t unnormal = make(0x3FFF, 0x0000000000000000);

    // Mul correctly identifies this as zero (it checks J-bit, finds sig=0,
    // and goes to its "zero" label).
    softfloat_exceptionFlags = 0;
    extFloat80_t mul_result = extF80_mul(unnormal, POS_ONE);
    dump("x * 1  =", mul_result);
    check("mul(unnormal, 1.0) == +0", eq(mul_result, POS_ZERO));

    // Add should give the same answer: 0 + 0 = 0.
    // BUG: unpatched SoftFloat returns 2.0 here.
    softfloat_exceptionFlags = 0;
    extFloat80_t add_result = extF80_add(unnormal, POS_ZERO);
    dump("x + 0  =", add_result);
    check("add(unnormal, 0) == +0", eq(add_result, POS_ZERO));

    // The identity: x + 0 should equal x * 1
    check("add(x, 0) == mul(x, 1)", eq(add_result, mul_result));

    // Sub: x - 0 should also be 0
    softfloat_exceptionFlags = 0;
    extFloat80_t sub_result = extF80_sub(unnormal, POS_ZERO);
    dump("x - 0  =", sub_result);
    check("sub(unnormal, 0) == +0", eq(sub_result, POS_ZERO));

    // Also: 0 + x should be 0, and 1.0 + x should be 1.0
    softfloat_exceptionFlags = 0;
    extFloat80_t zero_plus = extF80_add(POS_ZERO, unnormal);
    dump("0 + x  =", zero_plus);
    check("add(0, unnormal) == +0", eq(zero_plus, POS_ZERO));

    softfloat_exceptionFlags = 0;
    extFloat80_t one_plus = extF80_add(POS_ONE, unnormal);
    dump("1 + x  =", one_plus);
    check("add(1.0, unnormal) == 1.0", eq(one_plus, POS_ONE));
}

static void test_bug1_nonzero_significand_unnormal() {
    std::printf("\n--- Bug 1b: Non-zero-significand unnormal ---\n");
    std::printf("  Input: {exp=0x3FFF, sig=0x7FFFFFFFFFFFFFFF}  (J=0)\n");
    std::printf("  Mathematical value: 2^0 * 0x7FFF.../2^63 ~ 1.0 - 2^{-63}\n\n");

    // unnormal: exp=0x3FFF (2^0), J=0, frac=all-1s
    // value = 2^0 * (0 + 0x7FFFFFFFFFFFFFFF/2^63) ≈ 0.999999999...
    extFloat80_t unnormal = make(0x3FFF, 0x7FFFFFFFFFFFFFFF);

    softfloat_exceptionFlags = 0;
    extFloat80_t mul_result = extF80_mul(unnormal, POS_ONE);
    dump("x * 1  =", mul_result);

    softfloat_exceptionFlags = 0;
    extFloat80_t add_result = extF80_add(unnormal, POS_ZERO);
    dump("x + 0  =", add_result);

    // The identity: x + 0 must equal x * 1
    check("add(x, 0) == mul(x, 1)", eq(add_result, mul_result));

    // Sub: x - 0 must also equal x * 1
    softfloat_exceptionFlags = 0;
    extFloat80_t sub_result = extF80_sub(unnormal, POS_ZERO);
    dump("x - 0  =", sub_result);
    check("sub(x, 0) == mul(x, 1)", eq(sub_result, mul_result));
}

static void test_bug1_identity_violations() {
    std::printf("\n--- Bug 1c: Systematic identity violations ---\n");
    std::printf("  For each unnormal x, check x+0 == x*1 and x-0 == x*1.\n\n");

    // Several unnormals at different exponent values, all with J=0
    struct { const char *name; uint16_t signExp; uint64_t sig; } cases[] = {
        {"exp=0x0001, sig=0x4000...", 0x0001, UINT64_C(0x4000000000000000)},
        {"exp=0x0002, sig=0x4000...", 0x0002, UINT64_C(0x4000000000000000)},
        {"exp=0x0010, sig=0x7FFF...", 0x0010, UINT64_C(0x7FFFFFFFFFFFFFFF)},
        {"exp=0x3FFE, sig=0x0000...1",0x3FFE, UINT64_C(0x0000000000000001)},
        {"exp=0x3FFF, sig=0x4000...", 0x3FFF, UINT64_C(0x4000000000000000)},
        {"exp=0x4000, sig=0x7FFF...", 0x4000, UINT64_C(0x7FFFFFFFFFFFFFFF)},
        {"exp=0x7FFE, sig=0x7FFF...", 0x7FFE, UINT64_C(0x7FFFFFFFFFFFFFFF)},
    };

    for (auto &c : cases) {
        extFloat80_t x = make(c.signExp, c.sig);

        softfloat_exceptionFlags = 0;
        extFloat80_t mul_ref = extF80_mul(x, POS_ONE);

        softfloat_exceptionFlags = 0;
        extFloat80_t add_result = extF80_add(x, POS_ZERO);

        softfloat_exceptionFlags = 0;
        extFloat80_t sub_result = extF80_sub(x, POS_ZERO);

        char buf[128];
        std::snprintf(buf, sizeof(buf), "x+0 == x*1 for %s", c.name);
        if (!eq(add_result, mul_ref)) {
            std::printf("    %s:\n", c.name);
            dump("x * 1 =", mul_ref);
            dump("x + 0 =", add_result);
        }
        check(buf, eq(add_result, mul_ref));

        std::snprintf(buf, sizeof(buf), "x-0 == x*1 for %s", c.name);
        check(buf, eq(sub_result, mul_ref));
    }
}

// ============================================================================
// BUG 2: Subnormal boundary crossing during unnormal normalization
//
// When an unnormal has a small exponent (e.g., exp=1), naive normalization
// via softfloat_normSubnormalExtF80Sig() shifts the significand left and
// decrements the exponent.  But if this pushes the exponent to 0, the
// exponent's effective weight doesn't change (both exp=0 and exp=1 map to
// emin = 2^(1-bias)), so the left-shift doubles the value without
// compensation.
//
// extF80_mul has this same over-shift, but roundPackToExtF80 compensates
// (line 165: shiftRightJam64Extra by 1-exp).  In addMagsExtF80, the
// subnormal-sum path at lines 86-90 re-normalizes before roundPack,
// preventing the compensation.
// ============================================================================

static void test_bug2_subnormal_boundary() {
    std::printf("\n--- Bug 2: Subnormal boundary crossing (exp=1, J=0) ---\n");

    // Pseudo-denormal: exp=1, J=0, frac=all-1s
    // Mathematical value: 2^(1-16383) * 0x7FFFFFFFFFFFFFFF/2^63
    // This is the SAME value as the proper subnormal {exp=0, sig=0x7FFF...}:
    //   subnormal: 2^(1-16383) * 0x7FFFFFFFFFFFFFFF/2^63
    // Because exp=0 and exp=1 share the same effective exponent emin=2^(1-bias).
    extFloat80_t unnormal   = make(0x0001, 0x7FFFFFFFFFFFFFFF);
    extFloat80_t subnormal  = make(0x0000, 0x7FFFFFFFFFFFFFFF);

    std::printf("  unnormal:  {exp=0x0001, sig=0x7FFFFFFFFFFFFFFF} (J=0)\n");
    std::printf("  subnormal: {exp=0x0000, sig=0x7FFFFFFFFFFFFFFF}\n");
    std::printf("  These represent the same mathematical value.\n\n");

    // Mul correctly handles this (normalization over-shifts, but
    // roundPackToExtF80 compensates during subnormal packing).
    softfloat_exceptionFlags = 0;
    extFloat80_t mul_unnorm = extF80_mul(unnormal, POS_ONE);
    softfloat_exceptionFlags = 0;
    extFloat80_t mul_subnorm = extF80_mul(subnormal, POS_ONE);
    dump("unnormal  * 1 =", mul_unnorm);
    dump("subnormal * 1 =", mul_subnorm);
    check("mul: unnormal*1 == subnormal*1", eq(mul_unnorm, mul_subnorm));

    // Add: the bug manifests here because addMagsExtF80's internal
    // subnormal handling re-normalizes, compounding the over-shift.
    softfloat_exceptionFlags = 0;
    extFloat80_t add_unnorm = extF80_add(unnormal, POS_ZERO);
    softfloat_exceptionFlags = 0;
    extFloat80_t add_subnorm = extF80_add(subnormal, POS_ZERO);
    dump("unnormal  + 0 =", add_unnorm);
    dump("subnormal + 0 =", add_subnorm);
    check("add: unnormal+0 == subnormal+0", eq(add_unnorm, add_subnorm));

    // Cross-check: add result should match mul result
    check("add(unnormal,0) == mul(unnormal,1)", eq(add_unnorm, mul_unnorm));

    // Also test with a non-trivial addition
    softfloat_exceptionFlags = 0;
    extFloat80_t add_unnorm2 = extF80_add(unnormal, subnormal);
    softfloat_exceptionFlags = 0;
    extFloat80_t add_subnorm2 = extF80_add(subnormal, subnormal);
    dump("unnormal  + subnormal =", add_unnorm2);
    dump("subnormal + subnormal =", add_subnorm2);
    check("unnormal+subnormal == subnormal+subnormal", eq(add_unnorm2, add_subnorm2));
}

// ============================================================================
// BUG 3: Pseudo-NaN normalization
//
// An unnormal with exp=0x7FFF is a "pseudo-NaN" (or "pseudo-infinity" if
// frac=0).  These must be treated as NaN by the NaN-handling code.
//
// A naive normalization guard that fires before the NaN check would convert
// a pseudo-NaN into a regular number, causing it to silently participate
// in arithmetic instead of propagating NaN.
// ============================================================================

static void test_bug3_pseudo_nan() {
    std::printf("\n--- Bug 3: Pseudo-NaN handling (exp=0x7FFF, J=0) ---\n");

    // Pseudo-NaN: max exponent, J=0, non-zero significand
    extFloat80_t pnan = make(0x7FFF, 0x4000000000000000);
    std::printf("  Input: {exp=0x7FFF, sig=0x4000000000000000}  (J=0, pseudo-NaN)\n\n");

    dump("pseudo-NaN", pnan);

    // Any arithmetic with NaN should produce NaN
    softfloat_exceptionFlags = 0;
    extFloat80_t add_result = extF80_add(POS_ONE, pnan);
    dump("1.0 + pseudo-NaN =", add_result);
    check("add(1.0, pseudo-NaN) is NaN", isNaN80(add_result));

    softfloat_exceptionFlags = 0;
    extFloat80_t sub_result = extF80_sub(POS_ONE, pnan);
    dump("1.0 - pseudo-NaN =", sub_result);
    check("sub(1.0, pseudo-NaN) is NaN", isNaN80(sub_result));

    // Mul should also give NaN (it does, even unpatched, because it
    // checks exp=0x7FFF before normalization)
    softfloat_exceptionFlags = 0;
    extFloat80_t mul_result = extF80_mul(POS_ONE, pnan);
    dump("1.0 * pseudo-NaN =", mul_result);
    check("mul(1.0, pseudo-NaN) is NaN", isNaN80(mul_result));

    // Test several pseudo-NaN patterns
    struct { const char *name; uint64_t sig; } pnan_cases[] = {
        {"sig=0x0000000000000001", UINT64_C(0x0000000000000001)},
        {"sig=0x4000000000000000", UINT64_C(0x4000000000000000)},
        {"sig=0x7FFFFFFFFFFFFFFF", UINT64_C(0x7FFFFFFFFFFFFFFF)},
    };

    for (auto &c : pnan_cases) {
        extFloat80_t p = make(0x7FFF, c.sig);
        softfloat_exceptionFlags = 0;
        extFloat80_t r = extF80_add(POS_ONE, p);
        char buf[128];
        std::snprintf(buf, sizeof(buf), "add(1.0, pseudo-NaN{%s}) is NaN", c.name);
        check(buf, isNaN80(r));
    }
}

// ============================================================================
// BUG 4: Pseudo-denormal significand overflow (GitHub issue #37)
//
// A pseudo-denormal has exp=0 and J=1 (sig >= 0x8000000000000000).
// These are distinct from unnormals (exp>0, J=0).
//
// When two pseudo-denormals with J=1 are added, sigA + sigB can overflow
// 64 bits.  Line 116 of s_addMagsExtF80.c does `sigZ = sigA + sigB` with
// no carry detection.  The carry is silently lost, and then
// softfloat_normSubnormalExtF80Sig receives the truncated sum (often 0).
//
// See: https://github.com/ucb-bar/berkeley-softfloat-3/issues/37
// ============================================================================

static void test_bug4_pseudo_denormal_overflow() {
    std::printf("\n--- Bug 4: Pseudo-denormal overflow (issue #37) ---\n\n");

    // Two pseudo-denormals whose significand sum overflows 64 bits.
    // Each has exp=0, J=1, so they're valid pseudo-denormals.
    // Their effective exponent is emin = 1-bias (same as exp=1).

    struct {
        const char *name;
        uint64_t sigA, sigB;
    } cases[] = {
        // 0x8000... + 0x8000... = carry + 0x0000... (simplest overflow)
        {"0x8000...+0x8000...", 0x8000000000000000, 0x8000000000000000},
        // 0xFFFF... + 0x0000...1 = carry + 0x0000... (issue #37's case)
        {"0xFFFF...+0x0000...1", 0xFFFFFFFFFFFFFFFF, 0x0000000000000001},
        // 0xC000... + 0x4000... = carry + 0x0000... (different split)
        {"0xC000...+0x4000...", 0xC000000000000000, 0x4000000000000000},
        // 0xFFFF... + 0xFFFF... = carry + 0xFFFF...E (overflow with residue)
        {"0xFFFF...+0xFFFF...", 0xFFFFFFFFFFFFFFFF, 0xFFFFFFFFFFFFFFFF},
    };

    for (auto &c : cases) {
        extFloat80_t a = make(0x0000, c.sigA);
        extFloat80_t b = make(0x0000, c.sigB);

        // Compute canonical result: normalize each via mul, then add
        softfloat_exceptionFlags = 0;
        extFloat80_t ca = extF80_mul(a, POS_ONE);
        softfloat_exceptionFlags = 0;
        extFloat80_t cb = extF80_mul(b, POS_ONE);
        softfloat_exceptionFlags = 0;
        extFloat80_t canonical = extF80_add(ca, cb);

        // Direct add (the potentially buggy path)
        softfloat_exceptionFlags = 0;
        extFloat80_t direct = extF80_add(a, b);

        char buf[128];
        std::snprintf(buf, sizeof(buf), "add: %s", c.name);
        if (!eq(direct, canonical)) {
            std::printf("  %s:\n", c.name);
            dump("direct", direct);
            dump("canonical", canonical);
        }
        check(buf, eq(direct, canonical));
    }

    // Also verify a pseudo-denormal that doesn't overflow (J=1 + J=0)
    {
        extFloat80_t a = make(0x0000, 0x8000000000000000);
        extFloat80_t b = make(0x0000, 0x0000000000000001);
        softfloat_exceptionFlags = 0;
        extFloat80_t ca = extF80_mul(a, POS_ONE);
        softfloat_exceptionFlags = 0;
        extFloat80_t cb = extF80_mul(b, POS_ONE);
        softfloat_exceptionFlags = 0;
        extFloat80_t canonical = extF80_add(ca, cb);
        softfloat_exceptionFlags = 0;
        extFloat80_t direct = extF80_add(a, b);
        check("add: 0x8000...+0x0000...1 (no overflow)", eq(direct, canonical));
    }
}

// ============================================================================
// Bonus: Show that mul/div DO handle unnormals correctly (reference behavior)
// ============================================================================

static void test_mul_div_correct() {
    std::printf("\n--- Reference: mul/div handle unnormals correctly ---\n\n");

    extFloat80_t unnormals[] = {
        make(0x3FFF, 0x0000000000000000),  // sig=0
        make(0x3FFF, 0x7FFFFFFFFFFFFFFF),  // J=0, frac=all-1s
        make(0x3FFF, 0x4000000000000000),  // J=0, frac=1 bit
        make(0x0001, 0x7FFFFFFFFFFFFFFF),  // pseudo-denormal
    };
    const char *names[] = {
        "{exp=bias, sig=0}",
        "{exp=bias, sig=0x7FFF...}",
        "{exp=bias, sig=0x4000...}",
        "{exp=1, sig=0x7FFF...}",
    };

    for (int i = 0; i < 4; ++i) {
        extFloat80_t x = unnormals[i];
        // x * 1 should be the canonical form of x
        softfloat_exceptionFlags = 0;
        extFloat80_t identity = extF80_mul(x, POS_ONE);
        // (x * 1) * 1 should be unchanged (idempotent)
        softfloat_exceptionFlags = 0;
        extFloat80_t twice = extF80_mul(identity, POS_ONE);

        char buf[128];
        std::snprintf(buf, sizeof(buf), "mul idempotent for %s", names[i]);
        check(buf, eq(identity, twice));

        // Also check div: x / 1 should equal x * 1
        softfloat_exceptionFlags = 0;
        extFloat80_t div_result = extF80_div(x, POS_ONE);
        std::snprintf(buf, sizeof(buf), "div(x,1) == mul(x,1) for %s", names[i]);
        check(buf, eq(div_result, identity));
    }
}

// ============================================================================
// Main
// ============================================================================

int main() {
    softfloat_roundingMode   = softfloat_round_near_even;
    softfloat_detectTininess = softfloat_tininess_afterRounding;

    std::printf("SoftFloat extFloat80 unnormal handling test\n");
    std::printf("============================================\n");

    test_bug1_zero_significand_unnormal();
    test_bug1_nonzero_significand_unnormal();
    test_bug1_identity_violations();
    test_bug2_subnormal_boundary();
    test_bug3_pseudo_nan();
    test_bug4_pseudo_denormal_overflow();
    test_mul_div_correct();

    std::printf("\n============================================\n");
    std::printf("Results: %d passed, %d failed\n", g_pass, g_fail);
    if (g_fail > 0) {
        std::printf("FAILED\n");
        return 1;
    }
    std::printf("PASSED\n");
    return 0;
}
