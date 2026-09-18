// ABOUTME: Groups tokens into the multi-word chunks a slow e-paper panel requires,
// ABOUTME: bounded by word count, rendered width, and sentence boundaries.

#ifndef RSVP_CHUNKER_HPP
#define RSVP_CHUNKER_HPP

#include "rsvp/token.hpp"

#include <cstdint>

namespace rsvp {

/// Limits on how many tokens may be shown in a single update.
///
/// Chunking exists because of measurement, not preference. A partial refresh on this
/// panel costs ~542 ms whatever region it covers, so one word per update yields 111 WPM
/// -- less than half the speed at which reading comprehension holds. Three words per
/// update yields 332 WPM, inside that band. See `docs/REFRESH-MEASUREMENTS.md`.
struct ChunkConfig {
    /// Maximum tokens per chunk. Three is the measured sweet spot for this panel.
    std::uint8_t maxWords = 3u;

    /// Maximum rendered characters per chunk, including the spaces between words.
    ///
    /// A width budget expressed in characters rather than pixels, because the engine
    /// has no font metrics. The device's usable width is 480 px in its natural portrait
    /// orientation, which fits roughly this many characters at a readable size.
    std::uint16_t maxChars = 14u;

    /// Stop a chunk at a sentence or paragraph boundary.
    ///
    /// The timing model places its pause on the boundary-carrying token. A chunk that
    /// swallowed the following word would show that pause in the wrong place, so this
    /// defaults on and should stay on outside of experiments.
    bool breakOnBoundary = true;
};

/// Returns how many tokens starting at `start` belong in one chunk.
///
/// Returns 0 only when `start` is out of range. A single token wider than `maxChars` is
/// still returned alone rather than refused -- returning 0 for a valid position would
/// hang a player in an infinite loop, which on a device is indistinguishable from a
/// crash.
std::uint32_t chunkLength(const Token* tokens, std::uint32_t count, std::uint32_t start,
                          const ChunkConfig& config) noexcept;

}  // namespace rsvp

#endif  // RSVP_CHUNKER_HPP
