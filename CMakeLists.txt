cmake_minimum_required(VERSION 3.20)
project(opine VERSION 0.1.0 LANGUAGES CXX C)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Header-only library
add_library(opine INTERFACE)
target_include_directories(opine INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

# External dependencies (test-only)
list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

# SoftFloat + TestFloat: always available (fetched from source)
include(softfloat)
include(testfloat)

# MPFR + GMP: optional system packages. CI sets OPINE_REQUIRE_ORACLE
# so a runner with a missing MPFR fails loudly instead of silently
# skipping the oracle tests.
option(OPINE_REQUIRE_ORACLE "Fail configuration if MPFR is unavailable" OFF)
find_package(MPFR QUIET)

if(MPFR_FOUND)
    set(OPINE_HAS_ORACLE TRUE)
    message(STATUS "MPFR ${MPFR_VERSION} found: oracle tests enabled")
else()
    set(OPINE_HAS_ORACLE FALSE)
    if(OPINE_REQUIRE_ORACLE)
        message(FATAL_ERROR "OPINE_REQUIRE_ORACLE=ON but MPFR was not found "
                            "(install libmpfr-dev/libgmp-dev or set MPFR_DIR/GMP_DIR)")
    endif()
    message(STATUS "MPFR not found: oracle tests disabled")
endif()

# Tests
enable_testing()
add_subdirectory(tests)
