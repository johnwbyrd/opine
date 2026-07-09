#ifndef OPINE_TESTS_HARNESS_WIDE_FORMATS_HPP
#define OPINE_TESTS_HARNESS_WIDE_FORMATS_HPP

// binary256/512/1024 test gating.
//
// The arithmetic pipeline is width-agnostic (multi-limb digit
// geometry), but Type::storage_type is still the scalar bits_t<k>,
// which exists past 128 bits only on Clang (_BitInt). Until Layout
// grows a multi-word storage_type, the wide-format test cases
// compile on the Clang lane only. Wrap them in
// OPINE_TEST_HAS_WIDE_STORAGE so the gate reads as what it is — a
// storage limitation, not an arithmetic one.

#if defined(__clang__)
#define OPINE_TEST_HAS_WIDE_STORAGE 1
#else
#define OPINE_TEST_HAS_WIDE_STORAGE 0
#endif

#endif // OPINE_TESTS_HARNESS_WIDE_FORMATS_HPP
