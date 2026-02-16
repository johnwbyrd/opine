#ifndef OPINE_CORE_BITS_HPP
#define OPINE_CORE_BITS_HPP

// bits_t<N>: a fixed-width bit container parameterized on width.
//
// Not an integer semantically — a bag of bits. Supports shift, mask,
// OR, AND, comparison. The underlying type is _BitInt(N) on Clang,
// with a fallback to standard types / __int128 on GCC.

#include <cstdint>

namespace opine {

#if defined(__clang__)

template <int N>
using bits_t = unsigned _BitInt(N);

#elif defined(__SIZEOF_INT128__)

// GCC C++ mode: no _BitInt. Map to the smallest standard unsigned
// type that fits N bits. Limited to N <= 128.
namespace detail {

template <int N>
struct BitsStorage {
  static_assert(N > 0 && N <= 128,
                "GCC fallback limited to 128 bits; use Clang for wider types");
};

template <int N>
  requires(N > 0 && N <= 8)
struct BitsStorage<N> {
  using type = uint8_t;
};

template <int N>
  requires(N > 8 && N <= 16)
struct BitsStorage<N> {
  using type = uint16_t;
};

template <int N>
  requires(N > 16 && N <= 32)
struct BitsStorage<N> {
  using type = uint32_t;
};

template <int N>
  requires(N > 32 && N <= 64)
struct BitsStorage<N> {
  using type = uint64_t;
};

template <int N>
  requires(N > 64 && N <= 128)
struct BitsStorage<N> {
  using type = unsigned __int128;
};

} // namespace detail

template <int N>
using bits_t = typename detail::BitsStorage<N>::type;

#else
#error "Requires Clang (_BitInt) or GCC (__int128)"
#endif

} // namespace opine

#endif // OPINE_CORE_BITS_HPP
