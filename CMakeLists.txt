cmake_minimum_required(VERSION 3.25)

project(readout
    VERSION 0.1.0
    DESCRIPTION "System monitor for the KDE Plasma and GNOME Shell panels"
    HOMEPAGE_URL "https://github.com/aakash1122/readout"
    LANGUAGES CXX
)

include(GNUInstallDirs)
include(CTest)

option(READOUT_BUILD_DAEMON "Build readoutd and readoutctl" ON)
option(READOUT_BUILD_PLASMA "Build the KDE Plasma widget" ON)
option(READOUT_BUILD_GNOME "Install the GNOME Shell extension" ON)
option(READOUT_WERROR "Treat compiler warnings as errors" OFF)
option(READOUT_FUZZ "Build libFuzzer targets (requires Clang)" OFF)
set(READOUT_SANITIZE "" CACHE STRING "Sanitizers to enable, for example 'address,undefined'")

set(READOUT_APP_ID "io.github.aakash1122.Readout")
set(READOUT_BUS_NAME "io.github.aakash1122.Readout1")
set(READOUT_OBJECT_PATH "/io/github/aakash1122/Readout1")

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE RelWithDebInfo CACHE STRING "Build type" FORCE)
endif()

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(READOUT_SANITIZE)
    add_compile_options(-fsanitize=${READOUT_SANITIZE} -fno-omit-frame-pointer)
    add_link_options(-fsanitize=${READOUT_SANITIZE})
endif()

if(READOUT_BUILD_DAEMON)
    add_subdirectory(daemon)
    add_subdirectory(tools/readoutctl)
endif()

if(READOUT_BUILD_PLASMA)
    add_subdirectory(frontends/plasma)
endif()

if(READOUT_BUILD_GNOME)
    add_subdirectory(frontends/gnome)
endif()
