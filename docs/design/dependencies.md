# External Dependencies and Build Infrastructure

## Problem

The TDD sequence (tdd.md steps 1-5) requires three external libraries
that the OPINE library itself does not depend on. These are test-only
dependencies:

1. **GNU MPFR + GMP** — arbitrary-precision arithmetic for the reference
   oracle (tdd.md step 1)
2. **Berkeley SoftFloat** — IEEE 754 reference implementation for oracle
   validation and cross-checking (tdd.md step 2)
3. **Berkeley TestFloat** — IEEE 754 conformance test generation and
   verification (tdd.md step 5)

The OPINE library remains header-only with zero external dependencies.
The test infrastructure is heavier than the library. This is appropriate.

## Dependency Characteristics

### MPFR + GMP

- **Source**: https://www.mpfr.org/ and https://gmplib.org/
- **License**: LGPL
- **Build system**: Autotools (./configure && make)
- **Size**: ~1.5M lines combined
- **System packages**: Available everywhere for development hosts.
  - Ubuntu: `apt install libmpfr-dev libgmp-dev`
  - macOS: `brew install mpfr`
  - Windows: vcpkg (`vcpkg install mpfr`), MSYS2, or manual build
- **CMake integration**: `find_package(MPFR)` / `find_package(GMP)` —
  no standard Find module in CMake, but simple to write (library + header
  search).
- **Building from source via FetchContent**: Not practical. Autotools
  build, large codebase, complex configuration. System package is the
  right answer.

### Berkeley SoftFloat 3e

- **Source**: http://www.jhauser.us/arithmetic/SoftFloat.html
- **GitHub mirror**: https://github.com/ucb-bar/berkeley-softfloat-3
- **License**: BSD
- **Build system**: Custom Makefile with platform-specific `COMPILE`
  target directories. No CMake. No pkg-config.
- **Size**: ~50 .c files, ~20 .h files. Small.
- **System packages**: Not typically packaged.
- **CMake integration**: We write a CMakeLists.txt wrapper that builds
  the library from source. Straightforward — it's a flat list of .c
  files compiled into a static library.

### Berkeley TestFloat 3e

- **Source**: http://www.jhauser.us/arithmetic/TestFloat.html
- **GitHub mirror**: https://github.com/ucb-bar/berkeley-testfloat-3
- **License**: BSD
- **Build system**: Same custom Makefile structure as SoftFloat.
  Depends on SoftFloat.
- **Size**: ~30 .c files. Small.
- **System packages**: Not typically packaged.
- **CMake integration**: Same approach as SoftFloat. Depends on the
  SoftFloat library being built first.

## Dependency Strategy

### MPFR + GMP: System Package, Optional

MPFR and GMP are found via `find_package`. If found, oracle tests are
built. If not found, oracle tests are skipped with a status message.

```cmake
find_package(MPFR)
find_package(GMP)

if(MPFR_FOUND AND GMP_FOUND)
    set(OPINE_HAS_ORACLE TRUE)
    message(STATUS "MPFR found: oracle tests enabled")
else()
    set(OPINE_HAS_ORACLE FALSE)
    message(STATUS "MPFR not found: oracle tests disabled")
endif()
```

This means:
- Linux/macOS developers install MPFR via package manager and get
  the full test suite.
- Windows developers get library tests + SoftFloat validation without
  installing anything extra.
- CI configures per-platform: Linux installs MPFR, Windows does not.

### SoftFloat + TestFloat: FetchContent, Always Available

SoftFloat and TestFloat are fetched and built from source at CMake
configure time. They are small, pure C, BSD-licensed, and have no
external dependencies. Building them is trivial.

```cmake
include(FetchContent)

FetchContent_Declare(
    softfloat
    GIT_REPOSITORY https://github.com/ucb-bar/berkeley-softfloat-3.git
    GIT_TAG        <pinned-commit-hash>
)

FetchContent_Declare(
    testfloat
    GIT_REPOSITORY https://github.com/ucb-bar/berkeley-testfloat-3.git
    GIT_TAG        <pinned-commit-hash>
)

FetchContent_MakeAvailable(softfloat testfloat)
```

Since SoftFloat and TestFloat use a custom Makefile (not CMake), we
provide our own CMakeLists.txt wrappers that:

1. List the .c source files
2. Set the correct include paths
3. Define the correct preprocessor macros (platform specialization)
4. Build a static library target

The wrappers live in `cmake/` in our repository:

```
cmake/
    FindMPFR.cmake          — Find module for MPFR
    FindGMP.cmake           — Find module for GMP
    softfloat.cmake         — FetchContent + CMake build wrapper
    testfloat.cmake         — FetchContent + CMake build wrapper
```

### SoftFloat CMake Wrapper Details

SoftFloat's source structure:

```
source/
    include/          — public headers (softfloat.h, softfloat_types.h)
    8086/             — x86 little-endian specialization
    8086-SSE/         — x86 SSE specialization
    ARM-VFPv2/        — ARM specialization
    <platform>/       — platform-specific source files
    *.c               — platform-independent source files
```

The Makefile selects a `SPECIALIZE_TYPE` (8086, 8086-SSE, ARM-VFPv2)
that determines which platform directory to include. For OPINE's
purposes, `8086-SSE` is correct for x86-64 hosts (Linux, macOS,
Windows) and `8086` for 32-bit x86. ARM hosts use `ARM-VFPv2`.

The CMake wrapper detects the host architecture and selects the
correct specialization directory. It then builds all platform-
independent .c files plus the selected platform .c files into a
static library.

Key preprocessor definitions:
- `SOFTFLOAT_ROUND_ODD` — enable round-to-odd support
- `INLINE_LEVEL=5` — inline everything (matches the Makefile default)
- `SOFTFLOAT_FAST_INT64` — use 64-bit integer fast paths (on 64-bit
  hosts)

### TestFloat CMake Wrapper Details

TestFloat depends on SoftFloat and has a similar structure. It
produces two executables:

- `testfloat_gen` — generates test vectors
- `testfloat_ver` — verifies results against SoftFloat
- `testfloat` — combined generate-and-verify

For OPINE's use, we primarily need the `testfloat_gen` and
`testfloat_ver` programs, or we can link TestFloat as a library and
call its functions directly from our test harness.

The simpler integration is to build TestFloat's verification logic
as a static library and link it into our test executables, rather
than spawning external processes.

## CMake Structure

```
CMakeLists.txt                     — top-level: library + tests
cmake/
    FindMPFR.cmake                 — find system MPFR
    FindGMP.cmake                  — find system GMP
    softfloat.cmake                — FetchContent + build wrapper
    testfloat.cmake                — FetchContent + build wrapper
tests/
    CMakeLists.txt                 — test targets
    unit/
        test_types.cpp             — compile-time type system tests
    oracle/                        — MPFR-based oracle (conditional)
    softfloat/                     — SoftFloat validation tests
    testfloat/                     — TestFloat integration
```

The top-level CMakeLists.txt includes the dependency modules:

```cmake
list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

# Always available
include(softfloat)
include(testfloat)

# Optional
find_package(MPFR)
find_package(GMP)
```

The tests/CMakeLists.txt conditionally adds test targets:

```cmake
# Always built
add_opine_test(test_types unit/test_types.cpp)

# SoftFloat validation (always built)
add_opine_test(test_softfloat_validation softfloat/test_validation.cpp)
target_link_libraries(test_softfloat_validation PRIVATE softfloat)

# Oracle tests (only if MPFR available)
if(OPINE_HAS_ORACLE)
    add_opine_test(test_oracle oracle/test_oracle.cpp)
    target_link_libraries(test_oracle PRIVATE ${MPFR_LIBRARIES} ${GMP_LIBRARIES})
    target_include_directories(test_oracle PRIVATE ${MPFR_INCLUDE_DIRS})
endif()
```

## GitHub Actions CI

```yaml
jobs:
  build-and-test:
    strategy:
      matrix:
        include:
          # Full suite: oracle + SoftFloat + TestFloat
          - os: ubuntu-latest
            compiler: clang
            install: "sudo apt-get install -y clang-18 libmpfr-dev libgmp-dev"
          - os: ubuntu-latest
            compiler: gcc
            install: "sudo apt-get install -y g++-13 libmpfr-dev libgmp-dev"
          - os: macos-latest
            compiler: clang
            install: "brew install mpfr"

          # Library tests + SoftFloat (no oracle)
          - os: windows-latest
            compiler: msvc
            install: ""
          - os: windows-latest
            compiler: clang
            install: "choco install llvm ninja -y"

    steps:
      - uses: actions/checkout@v4
      - run: ${{ matrix.install }}
      - run: cmake -B build -S . -DCMAKE_BUILD_TYPE=Release
      - run: cmake --build build --config Release
      - run: ctest --test-dir build --output-on-failure -C Release
```

On Windows, the oracle tests are simply not built (MPFR not found),
but SoftFloat-based validation still runs, providing IEEE 754
conformance checking.

## Local Development

### Linux / macOS (full suite)

```bash
# One-time setup
sudo apt install libmpfr-dev libgmp-dev   # or: brew install mpfr

# Build and test
cmake -B build -S .
cmake --build build
ctest --test-dir build --output-on-failure
```

### Windows (library + SoftFloat)

```bash
cmake -B build -S .
cmake --build build --config Release
ctest --test-dir build --output-on-failure -C Release
```

Oracle tests are skipped automatically. No manual configuration
needed.

### Local CI via `gh act`

```bash
gh act -j build-and-test
```

Runs the full GitHub Actions matrix locally. Requires Docker.
The Linux containers will have MPFR available (installed in the
workflow). Windows jobs can be tested with `gh act` on Windows
runners or skipped.

## Implementation Order

1. Write `cmake/FindMPFR.cmake` and `cmake/FindGMP.cmake`.
2. Write `cmake/softfloat.cmake` — FetchContent + CMake build wrapper.
   Verify SoftFloat builds on Linux, macOS, and Windows.
3. Write `cmake/testfloat.cmake` — FetchContent + CMake build wrapper.
   Verify TestFloat builds.
4. Write a minimal SoftFloat smoke test: call `f16_add` on a known
   input pair, verify the expected output. This proves the dependency
   chain works end-to-end.
5. Update `tests/CMakeLists.txt` with conditional oracle targets.
6. Update `.github/workflows/ci.yml` with the new matrix.

After this, the build infrastructure supports tdd.md steps 1-5.
The oracle (steps 1-3) can be written with MPFR available. The
SoftFloat validation (steps 2, 5) works everywhere.

## What This Does Not Cover

- The oracle implementation itself (tdd.md steps 1-3). That is
  the next phase after the build infrastructure is in place.
- The TestFloat shim that bridges OPINE's API to TestFloat's
  input/output format (tdd.md step 5). That comes after pack/unpack
  is implemented.
- IBM FPGen test vector integration. Those are static files, not
  a build dependency. They can be added to `tests/vectors/` when
  needed.
