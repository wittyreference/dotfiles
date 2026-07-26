# ABOUTME: Top-level build for inkflow -- the portable rsvp-core engine and the
# ABOUTME: host-side tools built on it. Device firmware builds via PlatformIO, not here.

cmake_minimum_required(VERSION 3.16)

project(inkflow
    VERSION 0.1.0
    DESCRIPTION "An RSVP reader for e-ink devices"
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Keep the build tree honest: no in-source builds.
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_BINARY_DIR)
    message(FATAL_ERROR "In-source builds are not allowed. Use: cmake -B build")
endif()

enable_testing()

add_subdirectory(core)
