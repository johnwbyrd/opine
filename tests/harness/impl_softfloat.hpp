#ifndef OPINE_TESTS_HARNESS_IMPL_SOFTFLOAT_HPP
#define OPINE_TESTS_HARNESS_IMPL_SOFTFLOAT_HPP

// SoftFloat adapter: one implementation among equals.
//
// Provides SoftFloatAdapter<FloatType> satisfying the adapter interface:
//   dispatch(Op, BitsType, BitsType)              -> TestOutput<BitsType>
//   dispatchUnary(Op, BitsType)                   -> TestOutput<BitsType>
//   dispatchTernary(Op, BitsType, BitsType, BitsType) -> TestOutput<BitsType>
//
// Specialized for float16, float32, float64, extFloat80, float128.
// Note: extFloat80 has no mulAdd in SoftFloat.

#include <bit>
#include <cstdint>

#include "harness/ops.hpp"
#include "opine/opine.hpp"

extern "C" {
#include "softfloat.h"
}

namespace opine::testing {

template <typename FloatType> struct SoftFloatAdapter;

// ===================================================================
// float16
// ===================================================================

template <> struct SoftFloatAdapter<opine::float16> {
  using Fmt = opine::float16::layout;
  using BitsType = opine::float16::storage_type;
  static constexpr BitsType SignBit = BitsType{1} << Fmt::sign_offset;

  static constexpr const char *name() { return "SoftFloat"; }

  TestOutput<BitsType> dispatch(Op O, BitsType A, BitsType B) const {
    softfloat_exceptionFlags = 0;
    float16_t Sa{static_cast<uint16_t>(A)}, Sb{static_cast<uint16_t>(B)};
    switch (O) {
    case Op::Add: { auto R = f16_add(Sa, Sb); return {BitsType(R.v), 0}; }
    case Op::Sub: { auto R = f16_sub(Sa, Sb); return {BitsType(R.v), 0}; }
    case Op::Mul: { auto R = f16_mul(Sa, Sb); return {BitsType(R.v), 0}; }
    case Op::Div: { auto R = f16_div(Sa, Sb); return {BitsType(R.v), 0}; }
    case Op::Rem: { auto R = f16_rem(Sa, Sb); return {BitsType(R.v), 0}; }
    case Op::Eq:  return {BitsType(f16_eq(Sa, Sb) ? 1 : 0), 0};
    case Op::Lt:  return {BitsType(f16_lt_quiet(Sa, Sb) ? 1 : 0), 0};
    case Op::Le:  return {BitsType(f16_le_quiet(Sa, Sb) ? 1 : 0), 0};
    default: return {0, 0};
    }
  }

  TestOutput<BitsType> dispatchUnary(Op O, BitsType A) const {
    softfloat_exceptionFlags = 0;
    float16_t Sa{static_cast<uint16_t>(A)};
    switch (O) {
    case Op::Sqrt: { auto R = f16_sqrt(Sa); return {BitsType(R.v), 0}; }
    case Op::Neg: return {BitsType(A ^ SignBit), 0};
    case Op::Abs: return {BitsType(A & ~SignBit), 0};
    default: return {0, 0};
    }
  }

  TestOutput<BitsType> dispatchTernary(Op O, BitsType A, BitsType B,
                                       BitsType C) const {
    softfloat_exceptionFlags = 0;
    float16_t Sa{static_cast<uint16_t>(A)}, Sb{static_cast<uint16_t>(B)},
        Sc{static_cast<uint16_t>(C)};
    switch (O) {
    case Op::MulAdd: {
      auto R = f16_mulAdd(Sa, Sb, Sc);
      return {BitsType(R.v), 0};
    }
    default: return {0, 0};
    }
  }
};

// ===================================================================
// float32
// ===================================================================

template <> struct SoftFloatAdapter<opine::float32> {
  using Fmt = opine::float32::layout;
  using BitsType = opine::float32::storage_type;
  static constexpr BitsType SignBit = BitsType{1} << Fmt::sign_offset;

  static constexpr const char *name() { return "SoftFloat"; }

  TestOutput<BitsType> dispatch(Op O, BitsType A, BitsType B) const {
    softfloat_exceptionFlags = 0;
    float32_t Sa{static_cast<uint32_t>(A)}, Sb{static_cast<uint32_t>(B)};
    switch (O) {
    case Op::Add: { auto R = f32_add(Sa, Sb); return {BitsType(R.v), 0}; }
    case Op::Sub: { auto R = f32_sub(Sa, Sb); return {BitsType(R.v), 0}; }
    case Op::Mul: { auto R = f32_mul(Sa, Sb); return {BitsType(R.v), 0}; }
    case Op::Div: { auto R = f32_div(Sa, Sb); return {BitsType(R.v), 0}; }
    case Op::Rem: { auto R = f32_rem(Sa, Sb); return {BitsType(R.v), 0}; }
    case Op::Eq:  return {BitsType(f32_eq(Sa, Sb) ? 1 : 0), 0};
    case Op::Lt:  return {BitsType(f32_lt_quiet(Sa, Sb) ? 1 : 0), 0};
    case Op::Le:  return {BitsType(f32_le_quiet(Sa, Sb) ? 1 : 0), 0};
    default: return {0, 0};
    }
  }

  TestOutput<BitsType> dispatchUnary(Op O, BitsType A) const {
    softfloat_exceptionFlags = 0;
    float32_t Sa{static_cast<uint32_t>(A)};
    switch (O) {
    case Op::Sqrt: { auto R = f32_sqrt(Sa); return {BitsType(R.v), 0}; }
    case Op::Neg: return {A ^ SignBit, 0};
    case Op::Abs: return {A & ~SignBit, 0};
    default: return {0, 0};
    }
  }

  TestOutput<BitsType> dispatchTernary(Op O, BitsType A, BitsType B,
                                       BitsType C) const {
    softfloat_exceptionFlags = 0;
    float32_t Sa{static_cast<uint32_t>(A)}, Sb{static_cast<uint32_t>(B)},
        Sc{static_cast<uint32_t>(C)};
    switch (O) {
    case Op::MulAdd: {
      auto R = f32_mulAdd(Sa, Sb, Sc);
      return {BitsType(R.v), 0};
    }
    default: return {0, 0};
    }
  }
};

// ===================================================================
// float64
// ===================================================================

template <> struct SoftFloatAdapter<opine::float64> {
  using Fmt = opine::float64::layout;
  using BitsType = opine::float64::storage_type;
  static constexpr BitsType SignBit = BitsType{1} << Fmt::sign_offset;

  static constexpr const char *name() { return "SoftFloat"; }

  TestOutput<BitsType> dispatch(Op O, BitsType A, BitsType B) const {
    softfloat_exceptionFlags = 0;
    float64_t Sa{static_cast<uint64_t>(A)}, Sb{static_cast<uint64_t>(B)};
    switch (O) {
    case Op::Add: { auto R = f64_add(Sa, Sb); return {BitsType(R.v), 0}; }
    case Op::Sub: { auto R = f64_sub(Sa, Sb); return {BitsType(R.v), 0}; }
    case Op::Mul: { auto R = f64_mul(Sa, Sb); return {BitsType(R.v), 0}; }
    case Op::Div: { auto R = f64_div(Sa, Sb); return {BitsType(R.v), 0}; }
    case Op::Rem: { auto R = f64_rem(Sa, Sb); return {BitsType(R.v), 0}; }
    case Op::Eq:  return {BitsType(f64_eq(Sa, Sb) ? 1 : 0), 0};
    case Op::Lt:  return {BitsType(f64_lt_quiet(Sa, Sb) ? 1 : 0), 0};
    case Op::Le:  return {BitsType(f64_le_quiet(Sa, Sb) ? 1 : 0), 0};
    default: return {0, 0};
    }
  }

  TestOutput<BitsType> dispatchUnary(Op O, BitsType A) const {
    softfloat_exceptionFlags = 0;
    float64_t Sa{static_cast<uint64_t>(A)};
    switch (O) {
    case Op::Sqrt: { auto R = f64_sqrt(Sa); return {BitsType(R.v), 0}; }
    case Op::Neg: return {A ^ SignBit, 0};
    case Op::Abs: return {A & ~SignBit, 0};
    default: return {0, 0};
    }
  }

  TestOutput<BitsType> dispatchTernary(Op O, BitsType A, BitsType B,
                                       BitsType C) const {
    softfloat_exceptionFlags = 0;
    float64_t Sa{static_cast<uint64_t>(A)}, Sb{static_cast<uint64_t>(B)},
        Sc{static_cast<uint64_t>(C)};
    switch (O) {
    case Op::MulAdd: {
      auto R = f64_mulAdd(Sa, Sb, Sc);
      return {BitsType(R.v), 0};
    }
    default: return {0, 0};
    }
  }
};

// ===================================================================
// extFloat80
// ===================================================================
// Note: SoftFloat has no extF80_mulAdd, so dispatchTernary is unsupported.

template <> struct SoftFloatAdapter<opine::extFloat80> {
  using Fmt = opine::extFloat80::layout;
  using BitsType = opine::extFloat80::storage_type;
  static constexpr BitsType SignBit = BitsType{1} << Fmt::sign_offset;

  static constexpr const char *name() { return "SoftFloat"; }

  static extFloat80_t toSF(BitsType V) {
    extFloat80_t S;
    S.signif = static_cast<uint64_t>(V);
    S.signExp = static_cast<uint16_t>(V >> 64);
    return S;
  }

  static BitsType fromSF(extFloat80_t S) {
    return (BitsType(S.signExp) << 64) | BitsType(S.signif);
  }

  // The pinned SoftFloat fork patches the extF80 arithmetic paths to
  // handle non-canonical encodings (see tests/softfloat/issues/), but
  // the comparison functions (extF80_eq / lt / le) still compare raw
  // fields, so an unnormal-zero compares unequal to +0 and a
  // pseudo-infinity compares unequal to canonical Inf. Comparisons
  // depend only on mathematical value, so rewrite each operand as its
  // canonical equal-valued encoding before comparing: unnormals
  // renormalize (or become subnormals), unnormal-zeros become true
  // zeros, pseudo-denormals become exp=1 normals, pseudo-infinities
  // become canonical infinities. Pseudo-NaNs stay as-is — the NaN
  // check inside the comparison functions already treats them as NaN.
  static BitsType canonicalizeForCompare(BitsType V) {
    uint64_t Sig = static_cast<uint64_t>(V);
    unsigned SignExp = static_cast<unsigned>(static_cast<uint16_t>(V >> 64));
    unsigned Exp = SignExp & 0x7FFFU;
    const bool JBit = (Sig >> 63) != 0;

    if (Exp == 0x7FFFU) {
      if (!JBit && Sig == 0) // pseudo-infinity
        Sig = UINT64_C(0x8000000000000000);
    } else if (Exp == 0) {
      if (JBit) // pseudo-denormal: same value as the exp=1 normal
        Exp = 1;
    } else if (!JBit) { // unnormal
      if (Sig == 0) {
        Exp = 0;
      } else {
        const unsigned Shift = unsigned(std::countl_zero(Sig));
        if (Exp > Shift) {
          Exp -= Shift;
          Sig <<= Shift;
        } else {
          // Value lands in the subnormal range; exp=0 and exp=1 share
          // the same weight, so shift by exp-1 and clear the exponent.
          Sig <<= (Exp - 1);
          Exp = 0;
        }
      }
    }
    SignExp = (SignExp & 0x8000U) | Exp;
    return (BitsType(SignExp) << 64) | BitsType(Sig);
  }

  TestOutput<BitsType> dispatch(Op O, BitsType A, BitsType B) const {
    softfloat_exceptionFlags = 0;
    if (O == Op::Eq || O == Op::Lt || O == Op::Le) {
      A = canonicalizeForCompare(A);
      B = canonicalizeForCompare(B);
    }
    extFloat80_t Sa = toSF(A), Sb = toSF(B);
    switch (O) {
    case Op::Add: return {fromSF(extF80_add(Sa, Sb)), 0};
    case Op::Sub: return {fromSF(extF80_sub(Sa, Sb)), 0};
    case Op::Mul: return {fromSF(extF80_mul(Sa, Sb)), 0};
    case Op::Div: return {fromSF(extF80_div(Sa, Sb)), 0};
    case Op::Rem: return {fromSF(extF80_rem(Sa, Sb)), 0};
    case Op::Eq:  return {BitsType(extF80_eq(Sa, Sb) ? 1 : 0), 0};
    case Op::Lt:  return {BitsType(extF80_lt_quiet(Sa, Sb) ? 1 : 0), 0};
    case Op::Le:  return {BitsType(extF80_le_quiet(Sa, Sb) ? 1 : 0), 0};
    default: return {0, 0};
    }
  }

  TestOutput<BitsType> dispatchUnary(Op O, BitsType A) const {
    softfloat_exceptionFlags = 0;
    extFloat80_t Sa = toSF(A);
    switch (O) {
    case Op::Sqrt: return {fromSF(extF80_sqrt(Sa)), 0};
    case Op::Neg: return {A ^ SignBit, 0};
    case Op::Abs: return {A & ~SignBit, 0};
    default: return {0, 0};
    }
  }
};

// ===================================================================
// float128
// ===================================================================

template <> struct SoftFloatAdapter<opine::float128> {
  using Fmt = opine::float128::layout;
  using BitsType = opine::float128::storage_type;
  static constexpr BitsType SignBit = BitsType{1} << Fmt::sign_offset;

  static constexpr const char *name() { return "SoftFloat"; }

  static float128_t toSF(BitsType V) {
    float128_t S;
    S.v[0] = static_cast<uint64_t>(V);
    S.v[1] = static_cast<uint64_t>(V >> 64);
    return S;
  }

  static BitsType fromSF(float128_t S) {
    return (BitsType(S.v[1]) << 64) | BitsType(S.v[0]);
  }

  TestOutput<BitsType> dispatch(Op O, BitsType A, BitsType B) const {
    softfloat_exceptionFlags = 0;
    float128_t Sa = toSF(A), Sb = toSF(B);
    switch (O) {
    case Op::Add: return {fromSF(f128_add(Sa, Sb)), 0};
    case Op::Sub: return {fromSF(f128_sub(Sa, Sb)), 0};
    case Op::Mul: return {fromSF(f128_mul(Sa, Sb)), 0};
    case Op::Div: return {fromSF(f128_div(Sa, Sb)), 0};
    case Op::Rem: return {fromSF(f128_rem(Sa, Sb)), 0};
    case Op::Eq:  return {BitsType(f128_eq(Sa, Sb) ? 1 : 0), 0};
    case Op::Lt:  return {BitsType(f128_lt_quiet(Sa, Sb) ? 1 : 0), 0};
    case Op::Le:  return {BitsType(f128_le_quiet(Sa, Sb) ? 1 : 0), 0};
    default: return {0, 0};
    }
  }

  TestOutput<BitsType> dispatchUnary(Op O, BitsType A) const {
    softfloat_exceptionFlags = 0;
    float128_t Sa = toSF(A);
    switch (O) {
    case Op::Sqrt: return {fromSF(f128_sqrt(Sa)), 0};
    case Op::Neg: return {A ^ SignBit, 0};
    case Op::Abs: return {A & ~SignBit, 0};
    default: return {0, 0};
    }
  }

  TestOutput<BitsType> dispatchTernary(Op O, BitsType A, BitsType B,
                                       BitsType C) const {
    softfloat_exceptionFlags = 0;
    float128_t Sa = toSF(A), Sb = toSF(B), Sc = toSF(C);
    switch (O) {
    case Op::MulAdd: return {fromSF(f128_mulAdd(Sa, Sb, Sc)), 0};
    default: return {0, 0};
    }
  }
};

} // namespace opine::testing

#endif // OPINE_TESTS_HARNESS_IMPL_SOFTFLOAT_HPP
