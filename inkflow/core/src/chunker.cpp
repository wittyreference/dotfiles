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

}  // namespace rsvp
