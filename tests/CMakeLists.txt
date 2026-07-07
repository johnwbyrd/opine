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
endif()
