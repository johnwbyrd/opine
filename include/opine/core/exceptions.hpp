#ifndef OPINE_CORE_EXCEPTIONS_HPP
#define OPINE_CORE_EXCEPTIONS_HPP

#include <concepts>

namespace opine {

// -----------------------------------------------------------------
// IEEE 754 §7 exception flags
// -----------------------------------------------------------------
// A bitmask. Every operation computes its flags unconditionally as
// a pure value; the Exceptions policy decides what happens to them
// (discarded, accumulated, or returned). Under Silent the computed
// value is dead and the optimizer removes it. Fits the test
// harness's uint8_t flags channel.
using flags_t = unsigned char;

inline constexpr flags_t FlagNone = 0x00;
inline constexpr flags_t FlagInvalid = 0x01;   // §7.2 invalid operation
inline constexpr flags_t FlagDivByZero = 0x02; // §7.3 division by zero
inline constexpr flags_t FlagOverflow = 0x04;  // §7.4 overflow
inline constexpr flags_t FlagUnderflow = 0x08; // §7.5 underflow
inline constexpr flags_t FlagInexact = 0x10;   // §7.6 inexact

// Accumulated flags for the StatusFlags policy: per-thread, sticky
// until cleared — IEEE 754 §7.1 default exception handling.
inline flags_t &statusFlags() {
  thread_local flags_t f = FlagNone;
  return f;
}
inline void clearStatusFlags() { statusFlags() = FlagNone; }

// Operation result under exceptions::ReturnStatus: every rounding
// operation returns {bits, flags} instead of bare bits.
template <typename T> struct WithStatus {
  typename T::storage_type bits;
  flags_t flags;
};

template <typename E>
concept ExceptionPolicy = requires {
  { E::has_status_flags } -> std::convertible_to<bool>;
  { E::has_traps } -> std::convertible_to<bool>;
};

namespace exceptions {

// Silently produce best-effort result. No side effects.
struct Silent {
  static constexpr bool has_status_flags = false;
  static constexpr bool has_traps = false;
};

// Set queryable status flags after operations.
struct StatusFlags {
  static constexpr bool has_status_flags = true;
  static constexpr bool has_traps = false;
};

// Return {result, status} pair from every operation.
struct ReturnStatus {
  static constexpr bool has_status_flags = false;
  static constexpr bool has_traps = false;
};

// Call a handler on exceptional conditions.
struct Trap {
  static constexpr bool has_status_flags = false;
  static constexpr bool has_traps = true;
};

using Default = Silent;

static_assert(ExceptionPolicy<Silent>);
static_assert(ExceptionPolicy<StatusFlags>);
static_assert(ExceptionPolicy<ReturnStatus>);
static_assert(ExceptionPolicy<Trap>);

} // namespace exceptions
} // namespace opine

#endif // OPINE_CORE_EXCEPTIONS_HPP
