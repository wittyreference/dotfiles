// ABOUTME: Groups tokens into the multi-word chunks a slow e-paper panel requires,
// ABOUTME: bounded by word count, rendered width, and sentence boundaries.

#ifndef RSVP_CHUNKER_HPP
#define RSVP_CHUNKER_HPP

#include "rsvp/timing.hpp"
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
    /// A width budget in characters rather than pixels, because the engine carries no
    /// font metrics. Derived rather than guessed: the device's usable width is 480 px in
    /// its natural portrait orientation, FreeMonoBold18pt7b advances 21 px per glyph,
    /// and 40 px of margin keeps the text off both edges -- so 440/21 = 20.
    ///
    /// Setting this too low is silently expensive. At 14 the chunker averaged 1.5 words
    /// per update instead of 3, which on a panel with a fixed refresh cost halves the
    /// reading speed for no visible reason.
    std::uint16_t maxChars = 20u;

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

/// Total milliseconds to read `count` tokens, accounting for chunking.
///
/// Summing per-token holds overstates the time by roughly the chunk size, because a
/// chunk is one update however many words it shows. On a panel whose refresh floor
/// dominates every hold, that is the difference between estimating a five-hour book at
/// five hours and at fifteen.
///
/// Each chunk is held for the *sum* of its tokens' reading time, because a chunk of
/// three words is three words of reading however few updates it costs to show them.
std::uint32_t chunkedDurationMs(const Token* tokens, std::uint32_t count,
                                const TimingConfig& timing,
                                const ChunkConfig& chunking) noexcept;

/// How long one chunk of `length` tokens starting at `start` should stay on screen.
///
/// The refresh floor is a property of a display *update*, not of a word, and a chunk is
/// one update. So per-token durations are summed with the floor disabled, and the floor
/// is applied once to the total.
///
/// Getting this backwards makes the whole timing model inert: at 330 WPM a single word
/// is 182 ms and a sentence-ending word 364 ms, both of which clamp to a 542 ms floor,
/// so every pause the model computes vanishes and the text advances metronomically.
std::uint32_t chunkHoldMs(const Token* tokens, std::uint32_t count, std::uint32_t start,
                          std::uint32_t length, const TimingConfig& timing) noexcept;

}  // namespace rsvp

#endif  // RSVP_CHUNKER_HPP
