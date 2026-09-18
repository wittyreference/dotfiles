# ABOUTME: Builds rsvp-reader -- the portable reading loop layered on rsvp-core, with no
# ABOUTME: panel, card or clock of its own so the firmware and the simulator share it.

add_library(rsvp_reader STATIC
    src/document.cpp
    src/reader.cpp
)

target_include_directories(rsvp_reader PUBLIC include)
target_link_libraries(rsvp_reader PUBLIC rsvp_core)

# The same warning set the engine is held to. This code ships to the device.
target_compile_options(rsvp_reader PRIVATE
    -Wall -Wextra -Wpedantic -Werror
    -Wconversion -Wsign-conversion -Wshadow -Wold-style-cast
)
