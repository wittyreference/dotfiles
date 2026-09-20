# ABOUTME: Builds the simulator and the reading-experience tests that run through it.
# ABOUTME: The simulator is a test, not a viewer: it exits non-zero when the layout breaks.

find_package(ZLIB REQUIRED)

# The pixel canvas and its writers -- PNG for a single screen, GIF for a session played
# at its real pace -- shared by the simulator and its tests.
add_library(sim_canvas STATIC src/png.cpp src/gif.cpp)
target_include_directories(sim_canvas PUBLIC src vendor)
target_link_libraries(sim_canvas PUBLIC rsvp_reader ZLIB::ZLIB)

add_executable(sim src/main.cpp)
target_link_libraries(sim PRIVATE sim_canvas)

# Drives the shipped reading loop through the simulated panel. This is where the reading
# experience is asserted on, because it is the only place it can be observed without
# someone reading a device screen aloud.
add_executable(reader_tests
    ${CMAKE_SOURCE_DIR}/core/tests/main.cpp
    tests/test_reader.cpp
    tests/test_gif.cpp
    tests/test_picker.cpp
    tests/test_input.cpp
)
target_include_directories(reader_tests PRIVATE ${CMAKE_SOURCE_DIR}/core/tests)
target_link_libraries(reader_tests PRIVATE sim_canvas)

add_test(NAME reader_tests COMMAND reader_tests)

# The real pipeline, end to end: a document is converted by rsvp-mk and then played
# through the reading loop, which is how a book actually reaches the panel. Pointing the
# simulator at raw Markdown instead would test a fixture rather than the product -- link
# targets survive as single 60-character "words" that are wider than the screen.
add_test(
    NAME sim_pipeline
    COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/tests/pipeline.sh
            $<TARGET_FILE:rsvp_mk>
            $<TARGET_FILE:sim>
            ${CMAKE_SOURCE_DIR}/docs/REFRESH-MEASUREMENTS.md
)
