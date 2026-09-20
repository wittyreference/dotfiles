// ABOUTME: Pins the contract of chunkHoldMs -- sum the unclamped per-token holds, then
// ABOUTME: apply the refresh floor once to the total, never to each token in turn.

#include "vendor/doctest.h"

#include "rsvp/chunker.hpp"
#include "rsvp/timing.hpp"
#include "rsvp/tokenizer.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace {

std::vector<rsvp::Token> tokenize(const std::string& text) {
    std::vector<rsvp::Token> out;
    rsvp::Tokenizer t(text.data(), text.size());
    rsvp::Token tok{};
    while (t.next(tok)) {
        out.push_back(tok);
    }
    return out;
}

}  // namespace

TEST_CASE("chunkHoldMs sums unclamped holds and applies the floor once") {
    // This is the whole contract of the function, and nothing tested it directly -- which
    // is exactly how a consumer came to reimplement it as a max of already-floored
    // per-token holds. That inversion makes the timing model inert: the floor wins on
    // every token, so the max is always the floor, and every boundary pause the model
    // computes is silently discarded.
    const auto tokens = tokenize("the cat sat on the mat and so on");
    REQUIRE(tokens.size() >= 3u);
    const auto count = static_cast<std::uint32_t>(tokens.size());

    rsvp::TimingConfig timing{};
    timing.wpm = 300u;                                // 60000 / 300 = 200ms per token
    timing.minHoldMs = rsvp::kPanelPartialRefreshMs;  // 542ms

    // Three three-letter words, each below the five-character length baseline and none
    // carrying a boundary, so each is exactly the 200ms base and the arithmetic below is
    // readable rather than incidental.
    for (std::uint32_t i = 0; i < 3u; ++i) {
        CHECK(tokens[i].length == 3u);
        CHECK(tokens[i].flags == rsvp::kTokenFlagNone);
    }

    const std::uint32_t hold = rsvp::chunkHoldMs(tokens.data(), count, 0u, 3u, timing);

    // Summed first, then floored: 3 x 200 = 600, which already clears the 542 floor.
    CHECK(hold == 600u);

    // A max of per-token holds gives 542 here, because each 200ms token clamps up to the
    // floor on its own. Pinning the gap is the point of this test.
    std::uint32_t maxOfFloored = 0;
    for (std::uint32_t i = 0; i < 3u; ++i) {
        const std::uint32_t h = rsvp::holdMs(tokens[i], timing);
        if (h > maxOfFloored) {
            maxOfFloored = h;
        }
    }
    CHECK(maxOfFloored == rsvp::kPanelPartialRefreshMs);
    CHECK(hold > maxOfFloored);
}

TEST_CASE("summing does not defeat the refresh floor") {
    // At 900 wpm three tokens are 66ms each. The panel physically cannot present that, so
    // the total still clamps up -- the floor is applied once, not never.
    const auto tokens = tokenize("the cat sat on the mat and so on");
    const auto count = static_cast<std::uint32_t>(tokens.size());

    rsvp::TimingConfig timing{};
    timing.wpm = 900u;
    timing.minHoldMs = rsvp::kPanelPartialRefreshMs;

    CHECK(rsvp::chunkHoldMs(tokens.data(), count, 0u, 3u, timing)
          == rsvp::kPanelPartialRefreshMs);
}

TEST_CASE("a boundary pause survives into the chunk total") {
    // The reason the sum matters: a chunk ending a sentence must carry that sentence's
    // pause. Under a max of floored holds this difference vanishes entirely.
    const auto plain = tokenize("the cat sat on the mat and so on");
    const auto ending = tokenize("the cat sat. on the mat and so on");

    rsvp::TimingConfig timing{};
    timing.wpm = 300u;
    timing.minHoldMs = rsvp::kPanelPartialRefreshMs;

    const std::uint32_t a = rsvp::chunkHoldMs(
        plain.data(), static_cast<std::uint32_t>(plain.size()), 0u, 3u, timing);
    const std::uint32_t b = rsvp::chunkHoldMs(
        ending.data(), static_cast<std::uint32_t>(ending.size()), 0u, 3u, timing);
    CHECK(b > a);
}

TEST_CASE("chunkHoldMs refuses a degenerate request") {
    const auto tokens = tokenize("the cat sat on the mat");
    const auto count = static_cast<std::uint32_t>(tokens.size());
    rsvp::TimingConfig timing{};

    CHECK(rsvp::chunkHoldMs(nullptr, count, 0u, 3u, timing) == 0u);
    CHECK(rsvp::chunkHoldMs(tokens.data(), count, count, 3u, timing) == 0u);
    CHECK(rsvp::chunkHoldMs(tokens.data(), count, 0u, 0u, timing) == 0u);
}
