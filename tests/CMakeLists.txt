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

# Oracle tests (only if MPFR available)
if(OPINE_HAS_ORACLE)
    add_executable(test_mpfr_exact oracle/test_mpfr_exact.cpp)
    target_link_libraries(test_mpfr_exact PRIVATE opine softfloat MPFR::MPFR)
    target_include_directories(test_mpfr_exact PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    add_test(NAME test_mpfr_exact COMMAND test_mpfr_exact)
endif()
