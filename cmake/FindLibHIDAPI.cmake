# FindLibHIDAPI.cmake
# Once done this will define
#
#  LIBHIDAPI_FOUND - System has libserialport
#  LIBHIDAPI_INCLUDE_DIR - The libserialport include directory
#  LIBHIDAPI_LIBRARY - The libraries needed to use libserialport
#  LIBHIDAPI_DEFINITIONS - Compiler switches required for using libserialport

find_package(PkgConfig QUIET)
if(PkgConfig_FOUND)
    pkg_check_modules(PC_HIDAPI QUIET hidapi-libusb)
endif()

# FreeBSD
FIND_PATH(LIBHIDAPI_INCLUDE_DIR NAMES hidapi.h
        HINTS
        ${PC_HIDAPI_INCLUDE_DIRS}
        /usr
        /usr/local
        /opt
        PATH_SUFFIXES hidapi
        )

find_library(LIBHIDAPI_LIBRARY NAMES hidapi-libusb hidapi
        HINTS
        ${PC_HIDAPI_LIBRARY_DIRS}
        /usr
        /usr/local
        /opt)

include(FindPackageHandleStandardArgs)
FIND_PACKAGE_HANDLE_STANDARD_ARGS(LibHIDAPI DEFAULT_MSG LIBHIDAPI_LIBRARY LIBHIDAPI_INCLUDE_DIR)

mark_as_advanced(LIBHIDAPI_INCLUDE_DIR LIBHIDAPI_LIBRARY)

if (LIBHIDAPI_FOUND)
    message(STATUS "found HIDAPI")
endif ()
