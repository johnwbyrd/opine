# FindGMP.cmake — Find the GNU Multiple Precision Arithmetic Library
#
# Provides:
#   GMP::GMP          - Imported target
#   GMP_FOUND         - True if found
#   GMP_INCLUDE_DIR   - Header directory
#   GMP_LIBRARY       - Library path
#   GMP_VERSION       - Version string (if determinable)

if(DEFINED ENV{GMP_DIR})
    set(_GMP_HINTS "$ENV{GMP_DIR}")
endif()

find_path(GMP_INCLUDE_DIR
    NAMES gmp.h
    HINTS ${_GMP_HINTS}
    PATH_SUFFIXES include
)

find_library(GMP_LIBRARY
    NAMES gmp libgmp
    HINTS ${_GMP_HINTS}
    PATH_SUFFIXES lib lib64
)

# Extract version from gmp.h
if(GMP_INCLUDE_DIR AND EXISTS "${GMP_INCLUDE_DIR}/gmp.h")
    file(STRINGS "${GMP_INCLUDE_DIR}/gmp.h" _gmp_major
         REGEX "^#define __GNU_MP_VERSION[ \t]+[0-9]+")
    file(STRINGS "${GMP_INCLUDE_DIR}/gmp.h" _gmp_minor
         REGEX "^#define __GNU_MP_VERSION_MINOR[ \t]+[0-9]+")
    file(STRINGS "${GMP_INCLUDE_DIR}/gmp.h" _gmp_patch
         REGEX "^#define __GNU_MP_VERSION_PATCHLEVEL[ \t]+[0-9]+")
    if(_gmp_major AND _gmp_minor AND _gmp_patch)
        string(REGEX REPLACE ".*[ \t]([0-9]+)$" "\\1" _gmp_major "${_gmp_major}")
        string(REGEX REPLACE ".*[ \t]([0-9]+)$" "\\1" _gmp_minor "${_gmp_minor}")
        string(REGEX REPLACE ".*[ \t]([0-9]+)$" "\\1" _gmp_patch "${_gmp_patch}")
        set(GMP_VERSION "${_gmp_major}.${_gmp_minor}.${_gmp_patch}")
    endif()
endif()

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(GMP
    REQUIRED_VARS GMP_LIBRARY GMP_INCLUDE_DIR
    VERSION_VAR GMP_VERSION
)

if(GMP_FOUND AND NOT TARGET GMP::GMP)
    add_library(GMP::GMP UNKNOWN IMPORTED)
    set_target_properties(GMP::GMP PROPERTIES
        IMPORTED_LOCATION "${GMP_LIBRARY}"
        INTERFACE_INCLUDE_DIRECTORIES "${GMP_INCLUDE_DIR}"
    )
endif()

mark_as_advanced(GMP_INCLUDE_DIR GMP_LIBRARY)
