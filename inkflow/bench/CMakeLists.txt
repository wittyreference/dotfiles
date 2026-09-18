# ABOUTME: Host tests for the eink-bench measurement logic. The firmware itself builds
# ABOUTME: via PlatformIO under eink-bench/; only the portable parts are tested here.

add_library(bench_core INTERFACE)
target_include_directories(bench_core INTERFACE include)
target_compile_features(bench_core INTERFACE cxx_std_17)

add_executable(bench_tests
    ${CMAKE_SOURCE_DIR}/core/tests/main.cpp
    tests/test_samples.cpp
)
target_link_libraries(bench_tests PRIVATE bench_core)
# doctest is vendored under core/tests; reuse it rather than carrying a second copy.
target_include_directories(bench_tests PRIVATE ${CMAKE_SOURCE_DIR}/core/tests)
target_compile_options(bench_tests PRIVATE
    -Wall -Wextra -Wpedantic -Werror
    -Wconversion -Wsign-conversion -Wshadow -Wold-style-cast
)

add_test(NAME bench_tests COMMAND bench_tests)
