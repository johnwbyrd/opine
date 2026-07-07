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

// All-ones mask of the low `width` bits of Storage. Safe when width
// equals Storage's full value width — where the naive
// (Storage{1} << width) - 1 would shift by the type's width (UB; a
// hard error for _BitInt in constant expressions). Note sizeof is no
// substitute for value width: _BitInt(80) has sizeof 16 but 80 value
// bits. The double shift wraps to zero at full width, giving the
// correct all-ones result.
template <typename Storage> constexpr Storage maskLow(int width) {
  Storage top = Storage{1} << (width - 1);
  return Storage(Storage(top << 1) - Storage{1});
}

} // namespace opine

#endif // OPINE_CORE_BITS_HPP
