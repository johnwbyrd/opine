// rbj's differentiator: FP compare = signed-int compare.
//
// Fill an array of FP8 values, sort the storage bytes as *signed
// integers*, and print the result. For rbj the output is in float
// order — negatives, zeros, positives, in ascending magnitude on
// each side. The trick is the whole point of the two's-complement
// FP encoding: bit-pattern-as-int8_t IS numerically ordered.
//
// The same trick applied to IEEE-shaped FP8 gives the *wrong*
// order, because IEEE uses sign-magnitude: the negatives sort
// among themselves by ascending magnitude (i.e., reversed).
//
// No branches, no sign special-cases, no NaN-aware helpers — just
// std::sort on a byte array.

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>

#include <opine/opine.hpp>

template <typename T> static void demo(const char *label) {
  using namespace opine;
  using S = typename T::storage_type;

  // A varied sample: positives, negatives, zero, powers of two,
  // and a subnormal.
  std::array<S, 12> vals{
      fromNative<T>(1.0f),  fromNative<T>(-1.0f),   fromNative<T>(0.0f),
      fromNative<T>(0.5f),  fromNative<T>(-0.5f),   fromNative<T>(2.5f),
      fromNative<T>(-2.5f), fromNative<T>(0.125f),  fromNative<T>(-0.125f),
      fromNative<T>(4.0f),  fromNative<T>(-4.0f),   fromNative<T>(0.75f)};

  auto show = [&](const char *tag) {
    std::printf("  %-9s", tag);
    for (S v : vals)
      std::printf(" %7.3f", double(toFloat<T>(v)));
    std::printf("\n");
  };

  std::printf("\n== %s ==\n", label);
  show("before");

  // Sort the underlying bytes as signed int8_t. This is the whole
  // trick: reinterpret + std::sort. No knowledge of the FP format
  // is needed by the sorter.
  int8_t buf[vals.size()];
  std::memcpy(buf, vals.data(), sizeof(buf));
  std::sort(buf, buf + vals.size());
  std::memcpy(vals.data(), buf, sizeof(buf));

  show("after");
}

int main() {
  using namespace opine;
  std::printf("Sort a float array by std::sort'ing its storage as signed ints.\n");
  std::printf("(rbj: sorted correctly.  IEEE: negatives come out reversed.)\n");
  demo<RbjType<4, 3>>("rbj FP8 (RadixComplement)");
  demo<fp8_e4m3>("IEEE FP8 E4M3 (SignMagnitude)");
  return 0;
}
