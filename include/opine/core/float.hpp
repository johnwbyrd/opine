#ifndef OPINE_CORE_FLOAT_HPP
#define OPINE_CORE_FLOAT_HPP

#include "opine/core/compute_format.hpp"
#include "opine/core/encoding.hpp"
#include "opine/core/exceptions.hpp"
#include "opine/core/format.hpp"
#include "opine/core/platform.hpp"
#include "opine/core/rounding.hpp"

namespace opine {

template <typename Fmt, typename Enc = encodings::IEEE754,
          typename Rnd = rounding::Default, typename Exc = exceptions::Default,
          typename Plat = platforms::Default>
  requires ValidEncoding<Enc> && RoundingPolicy<Rnd> && ExceptionPolicy<Exc> &&
           PlatformPolicy<Plat>
struct Float {
  using format = Fmt;
  using encoding = Enc;
  using rounding = Rnd;
  using exceptions = Exc;
  using platform = Plat;

  using compute_format = DefaultComputeFormat<Fmt, Enc, Rnd>;

  // SWAR lane count: how many of these fit in a machine word
  static constexpr int swar_lanes =
      Plat::machine_word_bits / Fmt::total_bits;

  // Exponent bias: resolved from AutoBias or explicit value
  static constexpr int exponent_bias = [] {
    if constexpr (Enc::exponent_bias != AutoBias) {
      return Enc::exponent_bias;
    } else if constexpr (Enc::sign_encoding == SignEncoding::TwosComplement) {
      return 1 << (Fmt::exp_bits - 1); // 2^(E-1)
    } else {
      return (1 << (Fmt::exp_bits - 1)) - 1; // 2^(E-1) - 1
    }
  }();
};

// --- Convenience aliases ---

// Standard IEEE 754 formats
template <int ExpBits, int MantBits>
using IEEE754Float =
    Float<IEEE_Layout<ExpBits, MantBits>, encodings::IEEE754>;

using float16 = IEEE754Float<5, 10>;
using float32 = IEEE754Float<8, 23>;
using float64 = IEEE754Float<11, 52>;

// ML formats
using bfloat16 = Float<IEEE_Layout<8, 7>, encodings::IEEE754>;
using fp8_e5m2 = Float<IEEE_Layout<5, 2>, encodings::IEEE754>;
using fp8_e4m3 = Float<IEEE_Layout<4, 3>, encodings::IEEE754>;
using fp8_e4m3fnuz = Float<IEEE_Layout<4, 3>, encodings::E4M3FNUZ>;

// rbj's two's complement formats
template <int ExpBits, int MantBits>
using RbjFloat =
    Float<IEEE_Layout<ExpBits, MantBits>, encodings::RbjTwosComplement>;

// Fast approximate math (no NaN, no Inf, flush denormals, truncation)
template <int ExpBits, int MantBits>
using FastFloat = Float<IEEE_Layout<ExpBits, MantBits>, encodings::Relaxed,
                        rounding::TowardZero, exceptions::Silent>;

} // namespace opine

#endif // OPINE_CORE_FLOAT_HPP
