// ABOUTME: Turns a document name into the NVS key its resume position is stored under.
// ABOUTME: Free function, no Arduino types, so the host test suite can exercise it.

#pragma once

#include <cstddef>
#include <cstdint>

/// Builds the NVS key holding the reading position for `name`.
///
/// Resume used to be a single slot -- one "doc" name and one "pos" -- which works for a
/// device that can only open one book. With a picker it silently loses a reader's place
/// in the first book the moment they open a second.
///
/// The name cannot be the key: NVS refuses anything longer than 15 characters, and it
/// refuses it at runtime, on a device with nobody watching. So the key is a hash --
/// FNV-1a, the same algorithm the `.rsvp` fingerprint uses, for the same reason that a
/// hash written into persistent state is a format and wants to be one people recognise.
///
/// Returns false and leaves `out` untouched when it will not fit. Refused rather than
/// truncated: truncated keys collide with each other, and a collision drops the reader
/// into someone else's page, which is worse than not saving at all.
inline bool resumeKey(const char* name, char* out, std::size_t cap) {
    // "p" + 8 hex digits + terminator.
    constexpr std::size_t kNeeded = 10u;
    if (name == nullptr || out == nullptr || cap < kNeeded) {
        return false;
    }

    std::uint32_t hash = 2166136261u;
    for (std::size_t i = 0u; name[i] != '\0'; ++i) {
        hash ^= static_cast<std::uint32_t>(static_cast<unsigned char>(name[i]));
        hash *= 16777619u;
    }

    static const char kHex[] = "0123456789abcdef";
    out[0] = 'p';
    for (std::size_t i = 0u; i < 8u; ++i) {
        const std::uint32_t nibble = (hash >> ((7u - i) * 4u)) & 0xfu;
        out[i + 1u] = kHex[nibble];
    }
    out[9] = '\0';
    return true;
}
