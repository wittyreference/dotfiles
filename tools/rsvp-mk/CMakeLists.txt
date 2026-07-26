# ABOUTME: Build definition for rsvp-mk, the host-side text to .rsvp converter,
# ABOUTME: plus its unit tests. Host-only: this never builds for the device.

add_library(rsvpmk_convert STATIC convert.cpp)
target_include_directories(rsvpmk_convert PUBLIC .)
target_link_libraries(rsvpmk_convert PUBLIC rsvp_core)
target_compile_features(rsvpmk_convert PUBLIC cxx_std_17)
target_compile_options(rsvpmk_convert PRIVATE
    -Wall -Wextra -Wpedantic -Werror
    -Wconversion -Wsign-conversion -Wshadow -Wold-style-cast
)

add_executable(rsvp_mk main.cpp)
target_link_libraries(rsvp_mk PRIVATE rsvpmk_convert)
target_compile_options(rsvp_mk PRIVATE
    -Wall -Wextra -Wpedantic -Werror
    -Wconversion -Wsign-conversion -Wshadow -Wold-style-cast
)

add_executable(rsvpmk_tests
    ${CMAKE_SOURCE_DIR}/core/tests/main.cpp
    tests/test_convert.cpp
)
target_link_libraries(rsvpmk_tests PRIVATE rsvpmk_convert)
# doctest is vendored under core/tests; reuse it rather than a second copy.
target_include_directories(rsvpmk_tests PRIVATE ${CMAKE_SOURCE_DIR}/core/tests)
target_compile_features(rsvpmk_tests PRIVATE cxx_std_17)

add_test(NAME rsvpmk_tests COMMAND rsvpmk_tests)

# End-to-end test against the built binary: file in, file out, exit codes, and
# determinism. Unit tests cannot catch a broken argument parser or a bad default
# output path, which is exactly what ships to a user first.
add_test(
    NAME rsvpmk_e2e
    COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/tests/e2e.sh $<TARGET_FILE:rsvp_mk>
)
