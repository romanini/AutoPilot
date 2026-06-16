cmake_minimum_required(VERSION 3.15)
project(autopilot_pi VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# ---------------------------------------------------------------------------
# OpenCPN plugin header
#
# In a Flatpak build this is supplied by the runtime extension; point
# OCPN_INCLUDE_DIR at the installed header.  For a native development
# build, point it at the checked-out OpenCPN source.
#
# Default: looks for ocpn_plugin.h in the directory above this repo root
# (i.e. alongside the OpenCPN source tree if you have both checked out).
# Override on the cmake command line with -DOCPN_INCLUDE_DIR=<path>.
# ---------------------------------------------------------------------------
if(NOT DEFINED OCPN_INCLUDE_DIR)
    set(OCPN_INCLUDE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../../../include"
        CACHE PATH "Path to the directory containing ocpn_plugin.h")
endif()

if(NOT EXISTS "${OCPN_INCLUDE_DIR}/ocpn_plugin.h")
    message(FATAL_ERROR
        "ocpn_plugin.h not found in OCPN_INCLUDE_DIR='${OCPN_INCLUDE_DIR}'. "
        "Pass -DOCPN_INCLUDE_DIR=<path-to-OpenCPN-include> to cmake.")
endif()

# ---------------------------------------------------------------------------
# wxWidgets
# ---------------------------------------------------------------------------
find_package(wxWidgets REQUIRED COMPONENTS core base net)
include(${wxWidgets_USE_FILE})

# ---------------------------------------------------------------------------
# Sources
# ---------------------------------------------------------------------------
set(SRCS
    src/autopilot_pi.cpp
    src/AutoPilotLink.cpp
    src/AutoPilotPanel.cpp
)

set(HDRS
    include/autopilot_pi.h
    include/AutoPilotLink.h
    include/AutoPilotPanel.h
    include/version.h
)

# ---------------------------------------------------------------------------
# Shared library
# ---------------------------------------------------------------------------
add_library(${PROJECT_NAME} SHARED ${SRCS} ${HDRS})

target_include_directories(${PROJECT_NAME}
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${OCPN_INCLUDE_DIR}
)

target_link_libraries(${PROJECT_NAME} PRIVATE ${wxWidgets_LIBRARIES})

if(NOT MSVC)
    target_compile_options(${PROJECT_NAME} PRIVATE -Wall -Wextra -O2)
endif()

# Strip debug symbols in release builds
if(CMAKE_BUILD_TYPE STREQUAL "Release")
    add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
        COMMAND ${CMAKE_STRIP} $<TARGET_FILE:${PROJECT_NAME}>)
endif()

# ---------------------------------------------------------------------------
# Install
#
# For Flatpak extension installs, flatpak-builder overrides this via
# --prefix and --libdir.  For a plain local install:
#   cmake --install build/ --prefix ~/.local
# then the .so lands in <prefix>/lib/opencpn/autopilot_pi.so
# which OpenCPN's user-plugin search path picks up.
# ---------------------------------------------------------------------------
install(TARGETS ${PROJECT_NAME}
    LIBRARY DESTINATION lib/opencpn)
