# FindLibHIDAPI.cmake
# Once done this will define
#
#  LIBHIDAPI_FOUND - System has libserialport
#  LIBHIDAPI_INCLUDE_DIR - The libserialport include directory
#  LIBHIDAPI_LIBRARY - The libraries needed to use libserialport
#  LIBHIDAPI_DEFINITIONS - Compiler switches required for using libserialport

# FreeBSD
FIND_PATH(LIBHIDAPI_INCLUDE_DIR NAMES hidapi.h
        HINTS
        /usr
        /usr/local
        /opt
        PATH_SUFFIXES hidapi
        )

find_library(LIBHIDAPI_LIBRARY NAMES libhidapi.a
        HINTS
        /usr
        /usr/local
        /opt)

include(FindPackageHandleStandardArgs)
FIND_PACKAGE_HANDLE_STANDARD_ARGS(LibHIDAPI DEFAULT_MSG LIBHIDAPI_LIBRARY LIBHIDAPI_INCLUDE_DIR)

mark_as_advanced(LIBHIDAPI_INCLUDE_DIR LIBHIDAPI_LIBRARY)

if (LIBHIDAPI_FOUND)
    message(STATUS "found HIDAPI")
endif ()
