// ABOUTME: Chunk grouping -- packs tokens until the word limit, width budget, or a
// ABOUTME: sentence boundary stops it, always advancing by at least one token.

#include "rsvp/chunker.hpp"

namespace rsvp {

std::uint32_t chunkLength(const Token* tokens, std::uint32_t count, std::uint32_t start,
                          const ChunkConfig& config) noexcept {
    if (tokens == nullptr || start >= count) {
        return 0u;
    }

    const std::uint32_t maxWords = config.maxWords == 0u ? 1u : config.maxWords;

    std::uint32_t taken = 0u;
    std::uint32_t width = 0u;

    for (std::uint32_t i = start; i < count && taken < maxWords; ++i) {
        // A space is rendered between words, so every token after the first costs one
        // more character than its own length.
        const std::uint32_t separator = taken == 0u ? 0u : 1u;
        const std::uint32_t projected = width + separator + tokens[i].length;

        // The first token is always taken, however wide. Refusing it would return 0 for
        // a valid position and hang the caller.
        if (taken > 0u && projected > config.maxChars) {
            break;
        }

        width = projected;
        ++taken;

        if (config.breakOnBoundary &&
            tokens[i].has(kTokenFlagSentenceEnd | kTokenFlagParagraphEnd)) {
            break;
        }
    }

    return taken;
}

std::uint32_t chunkHoldMs(const Token* tokens, std::uint32_t count, std::uint32_t start,
                          std::uint32_t length, const TimingConfig& timing) noexcept {
    if (tokens == nullptr || start >= count || length == 0u) {
        return 0u;
    }

    // Sum with the floor and ceiling disabled: they bound a display update, and the
    // update is the chunk, not the individual word.
    TimingConfig unclamped = timing;
    unclamped.minHoldMs = 0u;
    unclamped.maxHoldMs = 0xFFFFu;

    std::uint32_t total = 0u;
    for (std::uint32_t i = 0u; i < length && start + i < count; ++i) {
        total += holdMs(tokens[start + i], unclamped);
    }

    if (total > timing.maxHoldMs) {
        total = timing.maxHoldMs;
    }
    if (total < timing.minHoldMs) {
        total = timing.minHoldMs;
    }
    return total;
}

std::uint32_t chunkedDurationMs(const Token* tokens, std::uint32_t count,
                                const TimingConfig& timing,
                                const ChunkConfig& chunking) noexcept {
    std::uint32_t total = 0u;
    std::uint32_t at = 0u;
    while (at < count) {
        const std::uint32_t n = chunkLength(tokens, count, at, chunking);
        if (n == 0u) {
            break;  // chunkLength only returns 0 out of range; guard against a hang.
        }
        total += chunkHoldMs(tokens, count, at, n, timing);
        at += n;
    }
    return total;
}

}  // namespace rsvp
