// ABOUTME: Unit tests for the timing model that decides how long each token is
// ABOUTME: held on screen, including the display refresh floor e-ink imposes.

#include "vendor/doctest.h"

#include "rsvp/timing.hpp"

namespace {

/// A plain word token of `length` bytes with no boundary flags.
rsvp::Token word(std::uint16_t length, std::uint8_t flags = rsvp::kTokenFlagNone) {
    rsvp::Token token{};
    token.offset = 0u;
    token.length = length;
    token.orp = 1u;
    token.flags = flags;
    return token;
}

/// Default config with the refresh floor disabled, so tests isolate the model.
rsvp::TimingConfig unconstrained(std::uint16_t wpm = 300u) {
    rsvp::TimingConfig config{};
    config.wpm = wpm;
    config.minHoldMs = 0u;
    return config;
}

}  // namespace

TEST_CASE("an average word at 300 wpm is held for 200 ms") {
    // 60000 ms / 300 words = 200 ms per word. The arithmetic is the contract.
    CHECK(rsvp::holdMs(word(5), unconstrained(300)) == 200u);
}

TEST_CASE("hold time scales inversely with words per minute") {
    CHECK(rsvp::holdMs(word(5), unconstrained(600)) == 100u);
    CHECK(rsvp::holdMs(word(5), unconstrained(150)) == 400u);
    CHECK(rsvp::holdMs(word(5), unconstrained(60)) == 1000u);
}

TEST_CASE("longer words are held longer") {
    const auto config = unconstrained();
    CHECK(rsvp::holdMs(word(15), config) > rsvp::holdMs(word(5), config));
    CHECK(rsvp::holdMs(word(25), config) > rsvp::holdMs(word(15), config));
}

TEST_CASE("words shorter than the baseline are not rushed below the base hold") {
    // Base hold already reflects an average word. Shrinking it further for "a"
    // produces flashes too brief to read and, on e-ink, too brief to draw.
    const auto config = unconstrained();
    CHECK(rsvp::holdMs(word(1), config) == rsvp::holdMs(word(5), config));
    CHECK(rsvp::holdMs(word(3), config) == rsvp::holdMs(word(5), config));
}

TEST_CASE("boundaries add pauses in increasing strength") {
    const auto config = unconstrained();
    const auto plain = rsvp::holdMs(word(5), config);
    const auto clause = rsvp::holdMs(word(5, rsvp::kTokenFlagClauseEnd), config);
    const auto sentence = rsvp::holdMs(word(5, rsvp::kTokenFlagSentenceEnd), config);
    const auto paragraph = rsvp::holdMs(word(5, rsvp::kTokenFlagParagraphEnd), config);

    CHECK(plain < clause);
    CHECK(clause < sentence);
    CHECK(sentence < paragraph);
}

TEST_CASE("multiple boundary flags take the strongest, they do not compound") {
    // A sentence that ends a paragraph sets both flags. Multiplying the two
    // pauses together stalls long enough to feel like the device has frozen.
    const auto config = unconstrained();
    const std::uint8_t both = rsvp::kTokenFlagSentenceEnd | rsvp::kTokenFlagParagraphEnd;

    CHECK(rsvp::holdMs(word(5, both), config) ==
          rsvp::holdMs(word(5, rsvp::kTokenFlagParagraphEnd), config));
}

TEST_CASE("numerals are held longer than words of the same length") {
    // Digit strings have no familiar word shape, so they need more time even
    // though they are short.
    const auto config = unconstrained();
    CHECK(rsvp::holdMs(word(2, rsvp::kTokenFlagNumeric), config) >
          rsvp::holdMs(word(2), config));
}

TEST_CASE("the display refresh floor is never violated") {
    // This is the e-ink constraint that makes the whole project uncertain. If the
    // panel needs 180 ms to draw, no token may be scheduled for less, whatever
    // the reader set their words-per-minute to.
    rsvp::TimingConfig config{};
    config.wpm = 1200u;  // 50 ms per word -- far below any e-paper refresh
    config.minHoldMs = 180u;

    CHECK(rsvp::holdMs(word(5), config) == 180u);
    CHECK(rsvp::holdMs(word(1), config) >= 180u);
    CHECK(rsvp::holdMs(word(30), config) >= 180u);
}

TEST_CASE("hold time is capped so a stray flag cannot stall playback") {
    rsvp::TimingConfig config{};
    config.wpm = 1u;  // 60 seconds per word
    config.maxHoldMs = 2000u;

    CHECK(rsvp::holdMs(word(5), config) == 2000u);
}

TEST_CASE("a zero words-per-minute config degrades safely rather than dividing by zero") {
    rsvp::TimingConfig config{};
    config.wpm = 0u;

    const auto hold = rsvp::holdMs(word(5), config);
    CHECK(hold > 0u);
    CHECK(hold <= config.maxHoldMs);
}

TEST_CASE("the ramp eases into the target speed at session start") {
    // Cold-starting at cruise speed is how a reader bounces off RSVP. The ramp
    // begins slower and converges on the target, and must converge exactly.
    rsvp::TimingConfig config{};
    config.wpm = 400u;
    config.rampTokens = 20u;
    config.rampStartPercent = 50u;  // begin at half speed

    const auto first = rsvp::rampedWpm(0u, config);
    const auto middle = rsvp::rampedWpm(10u, config);
    const auto atEnd = rsvp::rampedWpm(20u, config);
    const auto beyond = rsvp::rampedWpm(500u, config);

    CHECK(first == 200u);
    CHECK(first < middle);
    CHECK(middle < atEnd);
    CHECK(atEnd == 400u);
    CHECK(beyond == 400u);
}

TEST_CASE("a disabled ramp returns the target speed immediately") {
    rsvp::TimingConfig config{};
    config.wpm = 400u;
    config.rampTokens = 0u;

    CHECK(rsvp::rampedWpm(0u, config) == 400u);
}
