# ABOUTME: Registers the epub-to-text test suite with ctest. The tool is a stdlib-only
# ABOUTME: Python script, so there is nothing here to compile -- only something to run.

# REQUIRED rather than a quiet skip: a test that disappears when an interpreter is
# missing stops defending anything, and does it silently. Nothing is downloaded --
# the suite is standard library only, so a python3 on PATH is the whole dependency.
find_package(Python3 REQUIRED COMPONENTS Interpreter)

add_test(
    NAME epub_to_text_tests
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_epub_to_text.py
)
