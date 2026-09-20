// ABOUTME: Turns a name supplied over the network into a path on the SD card, or refuses.
// ABOUTME: Free functions and no Arduino types, so the host test suite can exercise it.

#pragma once

#include <cstddef>

/// Builds a card path from a filename that arrived over HTTP.
///
/// The upload and delete handlers used to concatenate `"/" + filename` straight from the
/// request. Nothing on the far side of that is trusted: the name is whatever the browser
/// sent, and the card is the only writable storage the device has. A name carrying a
/// separator or a `..` segment is not a filename, so this refuses rather than sanitising
/// -- there is no legitimate upload this rejects, and guessing at intent is how a guard
/// becomes a bypass.
///
/// Returns false and leaves `out` untouched when the name is empty, is not a plain
/// filename, or does not fit in `cap` including its leading slash and terminator.
/// Room for a leading slash, a FAT long filename, and a terminator.
inline constexpr std::size_t kMaxCardPath = 258u;

inline bool safeCardPath(const char* filename, char* out, std::size_t cap) {
    if (filename == nullptr || out == nullptr || cap < 3u) {
        return false;
    }

    std::size_t len = 0u;
    while (filename[len] != '\0') {
        const char c = filename[len];
        // Separators, NULs and control characters: not part of any filename we accept.
        if (c == '/' || c == '\\' || static_cast<unsigned char>(c) < 0x20u) {
            return false;
        }
        ++len;
    }

    if (len == 0u) {
        return false;
    }
    // "." and ".." are directory references, not files. Checked as whole names rather
    // than as a substring, so a book called "Vol..2.rsvp" is still uploadable.
    if ((len == 1u && filename[0] == '.') ||
        (len == 2u && filename[0] == '.' && filename[1] == '.')) {
        return false;
    }
    // One for the leading slash, one for the terminator.
    if (len + 2u > cap) {
        return false;
    }

    out[0] = '/';
    for (std::size_t i = 0u; i < len; ++i) {
        out[i + 1u] = filename[i];
    }
    out[len + 1u] = '\0';
    return true;
}
