# ABOUTME: Build definition for rsvp-core, the portable RSVP engine, plus its
# ABOUTME: host test binary and a freestanding compile check that guards portability.

add_library(rsvp_core STATIC
    src/tokenizer.cpp
    src/orp.cpp
)

target_include_directories(rsvp_core PUBLIC include)

target_compile_features(rsvp_core PUBLIC cxx_std_17)

# The engine ships to an ESP32-C3 with 380 KB of RAM and no PSRAM. Warnings are
# errors here because a silent narrowing conversion is a real bug on that target.
target_compile_options(rsvp_core PRIVATE
    -Wall -Wextra -Wpedantic -Werror
    -Wconversion -Wsign-conversion -Wshadow -Wold-style-cast
)

# --- Host tests -------------------------------------------------------------

add_executable(rsvp_core_tests
    tests/main.cpp
    tests/test_tokenizer.cpp
    tests/test_orp.cpp
)

target_link_libraries(rsvp_core_tests PRIVATE rsvp_core)
target_include_directories(rsvp_core_tests PRIVATE tests)
target_compile_features(rsvp_core_tests PRIVATE cxx_std_17)

add_test(NAME rsvp_core_tests COMMAND rsvp_core_tests)

# --- Freestanding compile check --------------------------------------------
#
# rsvp-core claims to be usable on a bare-metal target: no exceptions, no RTTI,
# no dynamic allocation. A claim like that rots the moment someone adds a
# std::string to a header, so it is compiled, not asserted in prose. This target
# builds the same sources with exceptions and RTTI disabled -- if anything in the
# engine starts depending on them, the build breaks here rather than at the point
# where someone tries to flash it.
add_library(rsvp_core_freestanding STATIC
    src/tokenizer.cpp
    src/orp.cpp
)

target_include_directories(rsvp_core_freestanding PUBLIC include)
target_compile_features(rsvp_core_freestanding PUBLIC cxx_std_17)
target_compile_options(rsvp_core_freestanding PRIVATE
    -Wall -Wextra -Wpedantic -Werror
    -Wconversion -Wsign-conversion -Wshadow -Wold-style-cast
    -fno-exceptions -fno-rtti
)
