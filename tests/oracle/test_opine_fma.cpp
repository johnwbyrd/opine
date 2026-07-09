// OPINE vs MPFR sweep for opine::fma across every Type — the first
// ternary operation. Coverage per Type is structural³ (every
// combination of the encoding's hot-spot values) plus width-tiered
// random triples. The oracle is mpfr_fma at 3p+32 bits — the
// intermediate precision below which double rounding stops being
// innocuous for a 2p-bit product plus a p-bit addend — followed by
// mpfrRoundToFormat, which adds no rounding of its own.
//
// A ReturnStatus case spot-checks the flags and the fusion itself:
// (1+ε)² − (1+2ε) must come out as exactly ε², which a rounded
// multiply cannot produce.

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include "harness/generic_binary_test.hpp"

using namespace opine;
using namespace opine::testing;

namespace {

template <typename T> constexpr int fmaRandomTriples() {
  constexpr int TotalBits = T::layout::total_bits;
  if constexpr (T::layout::exp_bits >= 24)
    return 2000;
  else if constexpr (T::layout::exp_bits >= 19)
    return 20000;
  else if constexpr (T::layout::exp_bits >= 15)
    return 50000;
  else if constexpr (TotalBits <= 8)
    return 200000;
  else
    return 100000;
}

template <typename T> void verifyFma() {
  using Storage = typename T::storage_type;
  constexpr int TotalBits = T::layout::total_bits;
  constexpr int HexWidth = (TotalBits + 3) / 4;

  OpineAdapter<T> Opine;
  MpfrAdapter<T> Mpfr;
  NanAwareBitExact<T> Cmp;

  auto structural = structuralValues<T>();
  // Wide formats: cube growth meets slow per-op cost; trim the list
  // (random triples keep hitting what the trim drops).
  if constexpr (T::layout::exp_bits >= 19)
    if (structural.size() > 24)
      structural.resize(24);

  TargetedTriples<Storage> Struct3{structural.data(), int(structural.size())};
  RandomTriples<Storage, TotalBits> Rnd{/*Seed=*/uint64_t(987654),
                                        fmaRandomTriples<T>()};
  auto Iter = combined(Struct3, Rnd);

  auto ImplA = [&](Storage X, Storage Y, Storage Z) {
    return Opine.dispatchTernary(Op::MulAdd, X, Y, Z);
  };
  auto ImplB = [&](Storage X, Storage Y, Storage Z) {
    return Mpfr.dispatchTernary(Op::MulAdd, X, Y, Z);
  };
  auto R = testAgainstTernary<Storage>("fma", HexWidth, Iter, ImplA, ImplB,
                                       Cmp);
  CHECK(R.Failed == 0);
}

} // namespace

TEST_CASE_TEMPLATE("fma: OPINE vs MPFR", T,
                   // FP8 (structural³ + random)
                   fp8_e5m2, fp8_e4m3, fp8_e4m3fnuz, RbjType<5, 2>,
                   RbjType<4, 3>, FastType<5, 2>, FastType<4, 3>,
                   // FP16 and up
                   bfloat16, float16, float32, float64, extFloat80,
                   float128) {
  verifyFma<T>();
}

TEST_CASE_TEMPLATE("fma: OPINE vs MPFR (binary256/1024)", T, float256,
                   float1024) {
  verifyFma<T>();
}

// Encoding × rounding sweep, including the modes with no direct
// MPFR analog.
TEST_CASE_TEMPLATE(
    "fma: OPINE vs MPFR, rounding sweep", T,
    IeeeR<5, 2, rounding::TowardZero>, IeeeR<5, 2, rounding::TowardPositive>,
    IeeeR<5, 2, rounding::TowardNegative>, IeeeR<4, 3, rounding::TowardZero>,
    IeeeR<4, 3, rounding::TowardPositive>,
    IeeeR<4, 3, rounding::TowardNegative>, FnuzR<rounding::TowardZero>,
    FnuzR<rounding::TowardPositive>, FnuzR<rounding::TowardNegative>,
    RbjR<5, 2, rounding::TowardZero>, RbjR<5, 2, rounding::TowardPositive>,
    RbjR<5, 2, rounding::TowardNegative>,
    IeeeR<5, 2, rounding::ToNearestTiesAway>,
    IeeeR<4, 3, rounding::ToNearestTiesAway>,
    FnuzR<rounding::ToNearestTiesAway>,
    RbjR<5, 2, rounding::ToNearestTiesAway>, IeeeR<5, 2, rounding::ToOdd>,
    IeeeR<4, 3, rounding::ToOdd>, FnuzR<rounding::ToOdd>,
    RbjR<5, 2, rounding::ToOdd>) {
  verifyFma<T>();
}

// -----------------------------------------------------------------
// Fusion + §7 flags through the ReturnStatus policy
// -----------------------------------------------------------------
TEST_CASE("fma: fusion and flags (ReturnStatus, binary32)") {
  using T = Type<numbers::IEEE754<8, 23>, layouts::IEEE<8, 23, true>,
                 rounding::Default, exceptions::ReturnStatus>;
  using S = typename T::storage_type;

  // The fused signature: (1+ε)² − (1+2ε) = ε² exactly (ε = 2^-23).
  // A rounded multiply drops the ε² term; fma must keep it.
  const S OnePlusEps = S(0x3F800001u);
  const S OnePlus2Eps = S(0x3F800002u);
  const S EpsSquared = S(0x28800000u); // 2^-46
  auto fused = opine::fma<T>(OnePlusEps, OnePlusEps,
                             opine::neg<T>(OnePlus2Eps));
  CHECK(fused.bits == EpsSquared);
  CHECK(fused.flags == FlagNone); // exact — single rounding, no dust

  // The unfused pipeline erases it: mul rounds (1+ε)² to 1+2ε.
  auto unfused = opine::add<T>(
      opine::mul<T>(OnePlusEps, OnePlusEps).bits,
      opine::neg<T>(OnePlus2Eps));
  CHECK(unfused.bits == S(0)); // +0: the term is gone

  // maxFinite × 2 − maxFinite = maxFinite, exactly: the huge
  // intermediate product never touches the format's range.
  const S Max = opine::detail::packMaxFinite<T>(false);
  auto big = opine::fma<T>(Max, fromNative<T>(2.0f).bits, opine::neg<T>(Max));
  CHECK(big.bits == Max);
  CHECK(big.flags == FlagNone);

  // Inf × 0 + 1 is invalid.
  const S PInf = opine::detail::packSpecial<T>(ValueCategory::Infinity, false);
  auto inv = opine::fma<T>(PInf, S(0), fromNative<T>(1.0f).bits);
  CHECK(inv.flags == FlagInvalid);
  CHECK(isNan<T>(inv.bits));

  // 2^-75 × 2^-75 + 0 = 2^-150: exactly half the smallest
  // subnormal, ties to even zero — underflow and inexact.
  const S TinyFactor = S(0x1A000000u); // 2^-75
  auto tiny = opine::fma<T>(TinyFactor, TinyFactor, S(0));
  CHECK(tiny.bits == S(0));
  CHECK(tiny.flags == (FlagUnderflow | FlagInexact));

  // Exact cancellation: 1 × 1 − 1 = +0 under round-to-nearest.
  auto zero = opine::fma<T>(fromNative<T>(1.0f).bits, fromNative<T>(1.0f).bits,
                            fromNative<T>(-1.0f).bits);
  CHECK(zero.bits == S(0));
  CHECK(zero.flags == FlagNone);
}
