cmake_minimum_required(VERSION 3.20)

project(balder_tracker
    VERSION 1.0.0
    DESCRIPTION "Battery-powered tracker simulation for the firmware interview case"
    LANGUAGES CXX)

# ---------------------------------------------------------------------------
# Standard and global settings
# ---------------------------------------------------------------------------

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Always emit compile_commands.json so clangd and the VS Code C++ extension
# pick up the include paths without extra configuration.
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Debug by default: this is a teaching case, so a debuggable binary matters
# more than a fast one. Override with -DCMAKE_BUILD_TYPE=Release.
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Debug CACHE STRING "Build type" FORCE)
    set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS Debug Release RelWithDebInfo MinSizeRel)
endif()

option(BALDER_TRACKER_WARNINGS "Enable a strict warning set" ON)
option(BALDER_TRACKER_WARNINGS_AS_ERRORS "Turn warnings into errors (for CI)" OFF)
option(BALDER_TRACKER_SANITIZERS "Build with the address and undefined behaviour sanitizers" OFF)
option(BALDER_TRACKER_BUILD_TESTS "Build the acceptance tests" ON)

# ---------------------------------------------------------------------------
# Toolchain capability check
# ---------------------------------------------------------------------------
#
# The newest library feature the sources use is C++20 <format>, including
# formatting of chrono durations. libstdc++ has it from gcc 13, the Ubuntu
# 24.04 default; libc++ from LLVM 17. Checking here turns an old toolchain
# into one clear message instead of a wall of errors from every file.

include(CheckCXXSourceCompiles)

set(CMAKE_REQUIRED_FLAGS "${CMAKE_CXX20_STANDARD_COMPILE_OPTION}")
check_cxx_source_compiles("
    #include <chrono>
    #include <format>
    int main() { return std::format(\"{:%T}\", std::chrono::seconds{1}).empty(); }
" BALDER_TRACKER_HAVE_CXX20_FORMAT)
unset(CMAKE_REQUIRED_FLAGS)

if(NOT BALDER_TRACKER_HAVE_CXX20_FORMAT)
    message(FATAL_ERROR
        "${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION} cannot compile C++20 "
        "<format> with chrono formatting.\n"
        "What works: gcc 13 or newer, or clang with libstdc++ 13+ or libc++ 17+.\n"
        "Apple clang 17 (Xcode 16.3) or newer is known to work on macOS.\n"
        "Reconfigure in a clean build directory after changing the compiler.")
endif()

# ---------------------------------------------------------------------------
# Shared compile options
# ---------------------------------------------------------------------------

add_library(tracker_options INTERFACE)

# The sources are clean under all of these.
if(BALDER_TRACKER_WARNINGS)
    if(MSVC)
        target_compile_options(tracker_options INTERFACE /W4)
    else()
        target_compile_options(tracker_options INTERFACE
            -Wall
            -Wextra
            -Wpedantic
            -Wshadow
            -Wconversion
            -Wsign-conversion
            -Wdouble-promotion
            -Wnon-virtual-dtor)
    endif()
endif()

if(BALDER_TRACKER_WARNINGS_AS_ERRORS)
    if(MSVC)
        target_compile_options(tracker_options INTERFACE /WX)
    else()
        target_compile_options(tracker_options INTERFACE -Werror)
    endif()
endif()

if(BALDER_TRACKER_SANITIZERS)
    if(MSVC)
        message(WARNING "BALDER_TRACKER_SANITIZERS is not supported for MSVC; ignoring.")
    else()
        target_compile_options(tracker_options INTERFACE
            -fsanitize=address,undefined -fno-omit-frame-pointer -g)
        target_link_options(tracker_options INTERFACE -fsanitize=address,undefined)
    endif()
endif()

# ---------------------------------------------------------------------------
# Libraries
# ---------------------------------------------------------------------------
#
# src/platform.hpp is the hardware API the firmware codes against.
# platform_simulation implements it in software; on a real board a driver
# library would take its place and nothing in tracker_core would change.
#
# Headers are listed so they show up in IDE project trees. They take part in
# no compilation of their own.

add_library(platform_simulation STATIC
    simulation/platform.cpp
    src/platform.hpp)

target_include_directories(platform_simulation PUBLIC
    "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src>")

target_link_libraries(platform_simulation PUBLIC tracker_options)

# Everything both the application and the tests need.
add_library(tracker_core STATIC
    src/tracker.cpp
    src/environment_service.hpp
    src/expected.hpp
    src/gnss_service.hpp
    src/ring_buffer.hpp
    src/scope_exit.hpp
    src/service.hpp
    src/tracker.hpp)

target_link_libraries(tracker_core PUBLIC platform_simulation)

# ---------------------------------------------------------------------------
# Application
# ---------------------------------------------------------------------------

add_executable(tracker src/main.cpp)
target_link_libraries(tracker PRIVATE tracker_core)

# ---------------------------------------------------------------------------
# Tests
# ---------------------------------------------------------------------------
#
# tests.cpp isolates each test with fork(), alarm() and wait(), so it needs a
# POSIX host. The whole binary is registered as a single CTest test because it
# takes no test-name filter on the command line; it prints per-test verdicts
# itself and exits non-zero if any test failed.

if(BALDER_TRACKER_BUILD_TESTS)
    if(WIN32)
        message(WARNING
            "tests/tests.cpp uses POSIX fork(); skipping the test target on this platform.")
    else()
        enable_testing()

        add_executable(run_tests tests/tests.cpp)
        target_link_libraries(run_tests PRIVATE tracker_core)

        add_test(NAME acceptance COMMAND run_tests)
        # Each test self-limits with alarm(5); this is the backstop for the suite.
        set_tests_properties(acceptance PROPERTIES TIMEOUT 60)
    endif()
endif()
