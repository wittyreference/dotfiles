// ABOUTME: Integration tests driving real prose through the whole engine chain --
// ABOUTME: tokenize, classify, pivot, then schedule -- asserting end-to-end properties.

#include "vendor/doctest.h"

#include "rsvp/timing.hpp"
#include "rsvp/tokenizer.hpp"

#include <string>
#include <vector>

namespace {

// Real prose, not a synthetic fixture: two paragraphs with dialogue, a numeral,
// an accented word, an ellipsis, and a hard-wrapped line.
const std::string kPassage =
    "The panel refreshes slowly. \"Wait,\" she said, \"how slowly?\"\n"
    "About 180 milliseconds for a windowed update -- maybe less.\n"
    "\n"
    "He shrugged. It was a café, not a laboratory, and the answer\n"
    "mattered less than the question... which was whether it mattered at all.";

std::vector<rsvp::Token> tokenize(const std::string& text) {
    rsvp::Tokenizer tokenizer(text.data(), text.size());
    std::vector<rsvp::Token> tokens;
    rsvp::Token token{};
    while (tokenizer.next(token)) {
        tokens.push_back(token);
    }
    return tokens;
}

/// Character count of a token's text, for pivot-bounds checking.
std::size_t charCount(const std::string& source, const rsvp::Token& token) {
    std::size_t count = 0u;
    for (std::size_t i = 0u; i < token.length; ++i) {
        if ((static_cast<unsigned char>(source[token.offset + i]) & 0xC0u) != 0x80u) {
            ++count;
        }
    }
    return count;
}

}  // namespace

TEST_CASE("tokenizing real prose loses no text") {
    // The strongest invariant available: every non-whitespace byte of the source
    // must appear in exactly one token, in order. If this holds, the tokenizer is
    // not dropping or duplicating content.
    const auto tokens = tokenize(kPassage);

    std::string rejoined;
    for (const auto& token : tokens) {
        rejoined += kPassage.substr(token.offset, token.length);
    }

    std::string expected;
    for (const char c : kPassage) {
        const bool isSpace = c == ' ' || c == '\t' || c == '\n' || c == '\r';
        if (!isSpace) {
            expected += c;
        }
    }

    CHECK(rejoined == expected);
}

TEST_CASE("tokens are ordered and non-overlapping") {
    const auto tokens = tokenize(kPassage);
    REQUIRE(tokens.size() > 10u);

    for (std::size_t i = 1u; i < tokens.size(); ++i) {
        const auto prevEnd = tokens[i - 1u].offset + tokens[i - 1u].length;
        CHECK(tokens[i].offset >= prevEnd);
    }
}

TEST_CASE("every pivot lands inside its own token") {
    // A pivot past the end of the token would index out of bounds in the renderer.
    const auto tokens = tokenize(kPassage);

    for (const auto& token : tokens) {
        CHECK(token.orp < charCount(kPassage, token));
    }
}

TEST_CASE("the passage's boundaries are classified as a reader would expect") {
    const auto tokens = tokenize(kPassage);

    std::size_t sentenceEnds = 0u;
    std::size_t paragraphEnds = 0u;
    std::size_t numerics = 0u;
    std::size_t multibytes = 0u;

    for (const auto& token : tokens) {
        sentenceEnds += token.has(rsvp::kTokenFlagSentenceEnd) ? 1u : 0u;
        paragraphEnds += token.has(rsvp::kTokenFlagParagraphEnd) ? 1u : 0u;
        numerics += token.has(rsvp::kTokenFlagNumeric) ? 1u : 0u;
        multibytes += token.has(rsvp::kTokenFlagMultibyte) ? 1u : 0u;
    }

    // "slowly.", "slowly?"", "less.", "shrugged.", "all." -- and not the ellipsis.
    CHECK(sentenceEnds == 5u);

    // The blank line after "-- maybe less." plus the end of input.
    CHECK(paragraphEnds == 2u);

    CHECK(numerics == 1u);    // "180"
    CHECK(multibytes == 1u);  // "café,"
}

TEST_CASE("total scheduled time for the passage is plausible at 300 wpm") {
    const auto tokens = tokenize(kPassage);

    rsvp::TimingConfig config{};
    config.wpm = 300u;
    config.minHoldMs = 0u;
    config.rampTokens = 0u;

    std::uint32_t totalMs = 0u;
    for (const auto& token : tokens) {
        totalMs += rsvp::holdMs(token, config);
    }

    // At 300 wpm a bare word count would predict tokens * 200 ms. Pauses and
    // length bonuses push the real figure above that, but not wildly -- if it
    // exceeded 2x, the pause weights would be making reading feel like waiting.
    const std::uint32_t naive = static_cast<std::uint32_t>(tokens.size()) * 200u;
    CHECK(totalMs > naive);
    CHECK(totalMs < naive * 2u);
}

TEST_CASE("the refresh floor dominates the schedule at high speeds") {
    // The project's central risk, expressed as a test. Ask for 900 wpm on a panel
    // that needs 180 ms per draw and you do not get 900 wpm -- you get the panel's
    // limit. This must be visible in the numbers rather than silently swallowed.
    const auto tokens = tokenize(kPassage);

    rsvp::TimingConfig fast{};
    fast.wpm = 900u;
    fast.minHoldMs = 180u;
    fast.rampTokens = 0u;

    std::uint32_t flooredTotal = 0u;
    std::size_t flooredTokens = 0u;
    for (const auto& token : tokens) {
        const auto hold = rsvp::holdMs(token, fast);
        flooredTotal += hold;
        flooredTokens += hold == fast.minHoldMs ? 1u : 0u;
    }

    // Most tokens are pinned to the floor, so effective speed is set by hardware.
    CHECK(flooredTokens > tokens.size() / 2u);

    const std::uint32_t effectiveWpm =
        static_cast<std::uint32_t>(tokens.size()) * 60000u / flooredTotal;
    CHECK(effectiveWpm < 900u);
    CHECK(effectiveWpm <= 60000u / fast.minHoldMs);
}

TEST_CASE("the ramp makes the opening of a session measurably slower") {
    const auto tokens = tokenize(kPassage);
    REQUIRE(tokens.size() > 40u);

    rsvp::TimingConfig config{};
    config.wpm = 400u;
    config.minHoldMs = 0u;
    config.rampTokens = 40u;
    config.rampStartPercent = 60u;

    // Schedule the first ten tokens under the ramp, then the same ten at cruise.
    std::uint32_t ramped = 0u;
    std::uint32_t cruise = 0u;
    for (std::size_t i = 0u; i < 10u; ++i) {
        rsvp::TimingConfig atIndex = config;
        atIndex.wpm = rsvp::rampedWpm(static_cast<std::uint32_t>(i), config);
        ramped += rsvp::holdMs(tokens[i], atIndex);
        cruise += rsvp::holdMs(tokens[i], config);
    }

    CHECK(ramped > cruise);
}
