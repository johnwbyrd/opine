function(add_opine_test name source)
    add_executable(${name} ${source})
    target_link_libraries(${name} PRIVATE opine)
    add_test(NAME ${name} COMMAND ${name})
endfunction()

# Compile-time verification of the type system
add_opine_test(test_types unit/test_types.cpp)

# Exhaustive FP8 pack/unpack round-trip
add_opine_test(test_pack_unpack unit/test_pack_unpack.cpp)

# SoftFloat smoke test (always built)
add_executable(test_softfloat_smoke softfloat/test_softfloat_smoke.cpp)
target_link_libraries(test_softfloat_smoke PRIVATE softfloat)
add_test(NAME test_softfloat_smoke COMMAND test_softfloat_smoke)

# doctest (test-only dependency)
include(FetchContent)
FetchContent_Declare(doctest
    GIT_REPOSITORY https://github.com/doctest/doctest.git
    GIT_TAG v2.4.11
)
# doctest v2.4.11 declares cmake_minimum_required(VERSION 3.0), which
# CMake 4 rejects outright; treat it as 3.5 so configuration succeeds.
set(CMAKE_POLICY_VERSION_MINIMUM 3.5)
FetchContent_MakeAvailable(doctest)
unset(CMAKE_POLICY_VERSION_MINIMUM)

# DigitVector primitives: exhaustive small-limb sweeps and wide-width
# invariants everywhere; randomized differential vs _BitInt on Clang.
add_executable(test_digits unit/test_digits.cpp)
target_link_libraries(test_digits PRIVATE opine doctest_with_main)
add_test(NAME test_digits COMMAND test_digits)

# Oracle cross-validation tests (only if MPFR available)
if(OPINE_HAS_ORACLE)
    add_executable(test_oracle oracle/test_oracle.cpp)
    target_link_libraries(test_oracle PRIVATE opine softfloat MPFR::MPFR doctest_with_main)
    target_include_directories(test_oracle PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    add_test(NAME test_oracle COMMAND test_oracle)

    # Oracle rounding-mode dispatch (hand-worked expected results
    # for four rounding modes on chosen tie / near-tie values)
    add_executable(test_oracle_rounding oracle/test_oracle_rounding.cpp)
    target_link_libraries(test_oracle_rounding PRIVATE opine MPFR::MPFR doctest_with_main)
    target_include_directories(test_oracle_rounding PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    add_test(NAME test_oracle_rounding COMMAND test_oracle_rounding)

    # Non-IEEE oracle validation (property tests + round-trip)
    add_executable(test_oracle_nonstd oracle/test_oracle_nonstd.cpp)
    target_link_libraries(test_oracle_nonstd PRIVATE opine MPFR::MPFR doctest_with_main)
    target_include_directories(test_oracle_nonstd PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    add_test(NAME test_oracle_nonstd COMMAND test_oracle_nonstd)

    # OPINE compare vs MPFR oracle (exhaustive FP8)
    add_executable(test_opine_compare oracle/test_opine_compare.cpp)
    target_link_libraries(test_opine_compare PRIVATE opine MPFR::MPFR doctest_with_main)
    target_include_directories(test_opine_compare PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    add_test(NAME test_opine_compare COMMAND test_opine_compare)

    # OPINE neg/abs vs MPFR oracle (exhaustive FP8)
    add_executable(test_opine_neg_abs oracle/test_opine_neg_abs.cpp)
    target_link_libraries(test_opine_neg_abs PRIVATE opine MPFR::MPFR doctest_with_main)
    target_include_directories(test_opine_neg_abs PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    add_test(NAME test_opine_neg_abs COMMAND test_opine_neg_abs)

    # OPINE add vs MPFR oracle (exhaustive FP8)
    add_executable(test_opine_add oracle/test_opine_add.cpp)
    target_link_libraries(test_opine_add PRIVATE opine MPFR::MPFR doctest_with_main)
    target_include_directories(test_opine_add PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    add_test(NAME test_opine_add COMMAND test_opine_add)

    # OPINE mul vs MPFR oracle (exhaustive FP8)
    add_executable(test_opine_mul oracle/test_opine_mul.cpp)
    target_link_libraries(test_opine_mul PRIVATE opine MPFR::MPFR doctest_with_main)
    target_include_directories(test_opine_mul PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    add_test(NAME test_opine_mul COMMAND test_opine_mul)

    # OPINE sub vs MPFR oracle (exhaustive FP8)
    add_executable(test_opine_sub oracle/test_opine_sub.cpp)
    target_link_libraries(test_opine_sub PRIVATE opine MPFR::MPFR doctest_with_main)
    target_include_directories(test_opine_sub PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    add_test(NAME test_opine_sub COMMAND test_opine_sub)

    # OPINE div vs MPFR oracle (exhaustive FP8)
    add_executable(test_opine_div oracle/test_opine_div.cpp)
    target_link_libraries(test_opine_div PRIVATE opine MPFR::MPFR doctest_with_main)
    target_include_directories(test_opine_div PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    add_test(NAME test_opine_div COMMAND test_opine_div)

    # OPINE convert vs MPFR oracle (exhaustive FP8 pairs + sampled
    # wide formats + round-trip theorems)
    add_executable(test_opine_convert oracle/test_opine_convert.cpp)
    target_link_libraries(test_opine_convert PRIVATE opine MPFR::MPFR doctest_with_main)
    target_include_directories(test_opine_convert PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    add_test(NAME test_opine_convert COMMAND test_opine_convert)

    # OPINE exception flags vs MPFR oracle (TDD step 12): exhaustive
    # FP8 pairs comparing result bits AND IEEE 754 flags
    add_executable(test_opine_flags oracle/test_opine_flags.cpp)
    target_link_libraries(test_opine_flags PRIVATE opine MPFR::MPFR doctest_with_main)
    target_include_directories(test_opine_flags PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    add_test(NAME test_opine_flags COMMAND test_opine_flags)

    # String conversion vs MPFR (digit-exact toString, bit-exact
    # fromString, round-trip theorems incl. binary1024, hex floats)
    add_executable(test_opine_string oracle/test_opine_string.cpp)
    target_link_libraries(test_opine_string PRIVATE opine MPFR::MPFR doctest_with_main)
    target_include_directories(test_opine_string PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    add_test(NAME test_opine_string COMMAND test_opine_string)

    # §5 operations: remaining predicates, classification, min/max
    # family, nextUp/nextDown, copySign (exhaustive FP8 + f64 libm)
    add_executable(test_opine_ops oracle/test_opine_ops.cpp)
    target_link_libraries(test_opine_ops PRIVATE opine MPFR::MPFR doctest_with_main)
    target_include_directories(test_opine_ops PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    add_test(NAME test_opine_ops COMMAND test_opine_ops)
endif()

# TestFloat conformance shim (TDD step 5): OPINE vs SoftFloat over
# Berkeley TestFloat's generated cases. No MPFR needed.
add_executable(test_opine_testfloat testfloat/test_opine_testfloat.cpp)
target_link_libraries(test_opine_testfloat
    PRIVATE opine softfloat testfloat_lib doctest_with_main)
target_compile_definitions(test_opine_testfloat PRIVATE FLOAT16)
add_test(NAME test_opine_testfloat COMMAND test_opine_testfloat)
