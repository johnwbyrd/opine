function(add_opine_test name source)
    add_executable(${name} ${source})
    target_link_libraries(${name} PRIVATE opine)
    add_test(NAME ${name} COMMAND ${name})
endfunction()

# Compile-time verification of the type system
add_opine_test(test_types unit/test_types.cpp)

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
FetchContent_MakeAvailable(doctest)

# Oracle cross-validation tests (only if MPFR available)
if(OPINE_HAS_ORACLE)
    add_executable(test_oracle oracle/test_oracle.cpp)
    target_link_libraries(test_oracle PRIVATE opine softfloat MPFR::MPFR doctest_with_main)
    target_include_directories(test_oracle PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    add_test(NAME test_oracle COMMAND test_oracle)
endif()
