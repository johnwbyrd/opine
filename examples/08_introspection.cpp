// Compile-time introspection.
//
// The six axes are ordinary template parameters, so every axis
// property is a compile-time constant. Print a table for the
// interesting Types; every field in it is `constexpr`.

#include <cstdio>

#include <opine/opine.hpp>

template <typename T> constexpr const char *nanName() {
  using namespace opine;
  switch (T::number::nan_encoding) {
  case NanEncoding::ReservedExponent: return "ResExp";
  case NanEncoding::TrapValue: return "Trap";
  case NanEncoding::NegativeZeroBitPattern: return "NegZero";
  case NanEncoding::None: return "none";
  }
  return "?";
}

template <typename T> constexpr const char *infName() {
  using namespace opine;
  switch (T::number::inf_encoding) {
  case InfEncoding::ReservedExponent: return "ResExp";
  case InfEncoding::IntegerExtremes: return "IntExt";
  case InfEncoding::None: return "none";
  }
  return "?";
}

template <typename T> constexpr const char *signName() {
  using namespace opine;
  switch (T::number::value_sign) {
  case SignMethod::Explicit: return "SignMag";
  case SignMethod::RadixComplement: return "2sComp";
  case SignMethod::DiminishedRadixComplement: return "1sComp";
  case SignMethod::Inherent: return "Inherent";
  case SignMethod::Unsigned: return "Unsigned";
  }
  return "?";
}

template <typename T> constexpr const char *denormName() {
  using namespace opine;
  switch (T::number::denormal_mode) {
  case DenormalMode::Full: return "Full";
  case DenormalMode::FlushToZero: return "FTZ";
  case DenormalMode::FlushInputs: return "FTI";
  case DenormalMode::FlushBoth: return "FTZ+FTI";
  case DenormalMode::None: return "none";
  }
  return "?";
}

template <typename T> static void row(const char *name) {
  std::printf("  %-14s  %3d  %2d  %3d  %5d  %-8s %-8s %-8s %s\n", name,
              T::layout::total_bits, T::layout::exp_bits,
              T::number::significand::digit_count,
              T::number::exponent_bias,
              signName<T>(), nanName<T>(), infName<T>(), denormName<T>());
}

int main() {
  using namespace opine;

  std::printf("Every column below is a compile-time constant.\n\n");
  std::printf("  %-14s  %3s  %2s  %3s  %5s  %-8s %-8s %-8s %s\n",
              "Type", "tot", "E", "sig", "bias", "sign", "nan", "inf", "denorm");
  std::printf("  %-14s  %3s  %2s  %3s  %5s  %-8s %-8s %-8s %s\n",
              "----", "---", "-", "---", "----", "----", "---", "---", "------");

  row<fp8_e5m2>("fp8_e5m2");
  row<fp8_e4m3>("fp8_e4m3");
  row<fp8_e4m3fnuz>("fp8_e4m3fnuz");
  row<RbjType<4, 3>>("RbjType<4,3>");
  row<FastType<4, 3>>("FastType<4,3>");
  row<bfloat16>("bfloat16");
  row<float16>("float16");
  row<float32>("float32");
  row<float64>("float64");
  row<extFloat80>("extFloat80");
  row<float128>("float128");
  return 0;
}
