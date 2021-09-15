cmake_minimum_required(VERSION 3.15)
project(dcled VERSION 3.0)


# Config
set(CMAKE_MODULE_PATH "${PROJECT_SOURCE_DIR}/cmake" ${CMAKE_MODULE_PATH})
set(CMAKE_CXX_STANDARD 14)


# Dependencies
include(GNUInstallDirs)

find_package(LibHIDAPI REQUIRED)
include_directories(${LIBHIDAPI_INCLUDE_DIR})


# OS Specific Dependencies
if(APPLE)
    find_library(CF CoreFoundation)
    find_library(IOK IOKit AppKit)
endif()


# Check if lib m is required
include(CheckLibraryExists)
CHECK_LIBRARY_EXISTS(m sin "" HAVE_LIB_M)


# Add definitions
add_definitions(-DDCLEDVERSION="${PROJECT_VERSION}")
add_definitions(-DFONTDIR="${CMAKE_INSTALL_DATADIR}/${PROJECT_NAME}")


# Applications
add_executable(dcled
        dcled.c)
target_link_libraries(dcled ${LIBHIDAPI_LIBRARY})
if(APPLE)
    target_link_libraries(dcled "-framework AppKit")
    target_link_libraries(dcled ${CF} ${IOK})
endif()

if (HAVE_LIB_M)
    target_link_libraries(dcled m)
endif()


# Installation information
install(TARGETS dcled
        CONFIGURATIONS Release
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(DIRECTORY ${PROJECT_SOURCE_DIR}/fonts/ DESTINATION ${CMAKE_INSTALL_DATADIR}/${PROJECT_NAME})
