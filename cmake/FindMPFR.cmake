# FindMPFR.cmake — Find the GNU MPFR Library
#
# Provides:
#   MPFR::MPFR         - Imported target (links GMP::GMP automatically)
#   MPFR_FOUND         - True if found
#   MPFR_INCLUDE_DIR   - Header directory
#   MPFR_LIBRARY       - Library path
#   MPFR_VERSION       - Version string (if determinable)

find_package(GMP QUIET)

if(DEFINED ENV{MPFR_DIR})
    set(_MPFR_HINTS "$ENV{MPFR_DIR}")
endif()

find_path(MPFR_INCLUDE_DIR
    NAMES mpfr.h
    HINTS ${_MPFR_HINTS}
    PATH_SUFFIXES include
)

find_library(MPFR_LIBRARY
    NAMES mpfr libmpfr
    HINTS ${_MPFR_HINTS}
    PATH_SUFFIXES lib lib64
)

# Extract version from mpfr.h
if(MPFR_INCLUDE_DIR AND EXISTS "${MPFR_INCLUDE_DIR}/mpfr.h")
    file(STRINGS "${MPFR_INCLUDE_DIR}/mpfr.h" _mpfr_version_line
         REGEX "^#define MPFR_VERSION_STRING[ \t]+\"[0-9]+\\.[0-9]+\\.[0-9]+\"")
    if(_mpfr_version_line)
        string(REGEX REPLACE ".*\"([0-9]+\\.[0-9]+\\.[0-9]+)\".*" "\\1"
               MPFR_VERSION "${_mpfr_version_line}")
    endif()
endif()

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(MPFR
    REQUIRED_VARS MPFR_LIBRARY MPFR_INCLUDE_DIR GMP_FOUND
    VERSION_VAR MPFR_VERSION
)

if(MPFR_FOUND AND NOT TARGET MPFR::MPFR)
    add_library(MPFR::MPFR UNKNOWN IMPORTED)
    set_target_properties(MPFR::MPFR PROPERTIES
        IMPORTED_LOCATION "${MPFR_LIBRARY}"
        INTERFACE_INCLUDE_DIRECTORIES "${MPFR_INCLUDE_DIR}"
        INTERFACE_LINK_LIBRARIES GMP::GMP
    )
endif()

mark_as_advanced(MPFR_INCLUDE_DIR MPFR_LIBRARY)
