// ABOUTME: Tests for chunk grouping, which the measured 542ms refresh floor makes
// ABOUTME: mandatory -- one word per update yields 111 WPM, far below readable speed.

#include "vendor/doctest.h"

#include "rsvp/chunker.hpp"
#include "rsvp/tokenizer.hpp"

#include <string>
#include <vector>

namespace {

std::vector<rsvp::Token> tokenize(const std::string& text) {
    rsvp::Tokenizer t(text.data(), text.size());
    std::vector<rsvp::Token> out;
    rsvp::Token tok{};
    while (t.next(tok)) {
        out.push_back(tok);
    }
    return out;
}

std::uint32_t lengthAt(const std::vector<rsvp::Token>& toks, std::uint32_t start,
                       const rsvp::ChunkConfig& cfg) {
    return rsvp::chunkLength(toks.data(), static_cast<std::uint32_t>(toks.size()), start, cfg);
}

}  // namespace

TEST_CASE("an empty or out-of-range request yields nothing") {
    rsvp::ChunkConfig cfg{};
    CHECK(rsvp::chunkLength(nullptr, 0u, 0u, cfg) == 0u);

    const auto toks = tokenize("one two");
    CHECK(lengthAt(toks, 5u, cfg) == 0u);
}

TEST_CASE("short words group up to the word limit") {
    const auto toks = tokenize("one two six ten");
    rsvp::ChunkConfig cfg{};
    cfg.maxWords = 3u;
    cfg.maxChars = 64u;
    CHECK(lengthAt(toks, 0u, cfg) == 3u);
}

TEST_CASE("the word limit is respected even when width allows more") {
    const auto toks = tokenize("a b c d e f");
    rsvp::ChunkConfig cfg{};
    cfg.maxWords = 2u;
    cfg.maxChars = 200u;
    CHECK(lengthAt(toks, 0u, cfg) == 2u);
}

TEST_CASE("width constrains the chunk before the word count does") {
    // The panel is 480px wide in portrait, so long words crowd out their neighbours.
    const auto toks = tokenize("extraordinarily complicated wording");
    rsvp::ChunkConfig cfg{};
    cfg.maxWords = 3u;
    cfg.maxChars = 20u;
    CHECK(lengthAt(toks, 0u, cfg) == 1u);
}

TEST_CASE("a chunk never spans a sentence boundary") {
    // The timing model puts its pause on the sentence-ending token. A chunk that
    // swallowed the following word would display the pause in the wrong place.
    const auto toks = tokenize("stop here. then go");
    rsvp::ChunkConfig cfg{};
    cfg.maxWords = 4u;
    cfg.maxChars = 64u;
    CHECK(lengthAt(toks, 0u, cfg) == 2u);  // "stop" "here." and no further
}

TEST_CASE("a chunk never spans a paragraph boundary") {
    const auto toks = tokenize("one two\n\nthree four");
    rsvp::ChunkConfig cfg{};
    cfg.maxWords = 4u;
    cfg.maxChars = 64u;
    CHECK(lengthAt(toks, 0u, cfg) == 2u);
}

TEST_CASE("a word too wide to fit is still shown alone rather than skipped") {
    // Returning zero here would hang the player in an infinite loop on device. A word
    // wider than the screen must still advance.
    const auto toks = tokenize("incomprehensibilities next");
    rsvp::ChunkConfig cfg{};
    cfg.maxWords = 3u;
    cfg.maxChars = 4u;
    CHECK(lengthAt(toks, 0u, cfg) == 1u);
}

TEST_CASE("the final chunk is truncated to what remains") {
    const auto toks = tokenize("one two three four five");
    rsvp::ChunkConfig cfg{};
    cfg.maxWords = 3u;
    cfg.maxChars = 64u;
    REQUIRE(toks.size() == 5u);
    CHECK(lengthAt(toks, 3u, cfg) == 2u);
}

TEST_CASE("walking the whole document covers every token exactly once") {
    // The property that matters: no token dropped, none shown twice, and termination.
    const auto toks = tokenize(
        "The panel refreshes slowly. Three words at a time is the only workable "
        "arrangement.\n\nIt was a compromise the hardware chose for us.");
    rsvp::ChunkConfig cfg{};

    std::uint32_t covered = 0u;
    std::uint32_t guard = 0u;
    while (covered < toks.size() && guard++ < 1000u) {
        const std::uint32_t n = lengthAt(toks, covered, cfg);
        REQUIRE(n > 0u);
        covered += n;
    }
    CHECK(covered == toks.size());
    CHECK(guard < 1000u);
}

TEST_CASE("the default config reflects the measured panel") {
    // 3 words at the measured 542ms floor is 332 WPM, inside the band where
    // comprehension holds. See docs/REFRESH-MEASUREMENTS.md.
    rsvp::ChunkConfig cfg{};
    CHECK(cfg.maxWords == 3u);
    CHECK(cfg.maxChars >= 12u);
}
