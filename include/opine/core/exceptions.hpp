#ifndef OPINE_CORE_EXCEPTIONS_HPP
#define OPINE_CORE_EXCEPTIONS_HPP

#include <concepts>

namespace opine {

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
