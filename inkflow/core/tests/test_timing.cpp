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

TEST_CASE("the length bonus follows the configured baseline and per-character rate") {
    // Exact figures rather than comparisons: at 300 wpm the base hold is 200 ms,
    // so every bonus is checkable to the millisecond. A rate that silently
    // changed would still be monotonic, and monotonicity is all the neighbouring
    // length tests can see.
    auto config = unconstrained(300u);
    config.lengthBaselineChars = 3u;
    config.lengthPercentPerChar = 10u;

    CHECK(rsvp::holdMs(word(3), config) == 200u);  // at the baseline, no bonus
    CHECK(rsvp::holdMs(word(4), config) == 220u);  // one character over, +10%
    CHECK(rsvp::holdMs(word(8), config) == 300u);  // five characters over, +50%
}

TEST_CASE("raising the length baseline exempts words a lower one would charge for") {
    auto config = unconstrained(300u);
    config.lengthBaselineChars = 10u;
    config.lengthPercentPerChar = 5u;

    CHECK(rsvp::holdMs(word(8), config) == 200u);
    CHECK(rsvp::holdMs(word(10), config) == 200u);
    CHECK(rsvp::holdMs(word(20), config) == 300u);  // ten characters over, +50%
}

TEST_CASE("a zero per-character rate disables the length bonus entirely") {
    auto config = unconstrained(300u);
    config.lengthPercentPerChar = 0u;

    CHECK(rsvp::holdMs(word(30), config) == 200u);
    CHECK(rsvp::holdMs(word(30), config) == rsvp::holdMs(word(1), config));
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

TEST_CASE("a boundary percent below the base does not shorten the hold") {
    // The boundary pause starts at the 100% base and only ever takes a larger
    // value, so a config asking for a 50% clause pause gets an ordinary hold
    // rather than half of one. Pauses lengthen; they never hurry the reader.
    auto config = unconstrained(300u);
    config.clausePercent = 50u;

    CHECK(rsvp::holdMs(word(5, rsvp::kTokenFlagClauseEnd), config) == 200u);
    CHECK(rsvp::holdMs(word(5, rsvp::kTokenFlagClauseEnd), config) ==
          rsvp::holdMs(word(5), config));
}

TEST_CASE("numerals are held longer than words of the same length") {
    // Digit strings have no familiar word shape, so they need more time even
    // though they are short.
    const auto config = unconstrained();
    CHECK(rsvp::holdMs(word(2, rsvp::kTokenFlagNumeric), config) >
          rsvp::holdMs(word(2), config));
}

TEST_CASE("a numeric percent at or below the base turns the numeral bonus off") {
    // One-sided like the length bonus: a config set below 100% switches the
    // bonus off instead of rushing numerals past the words around them.
    auto config = unconstrained(300u);

    config.numericPercent = 100u;
    CHECK(rsvp::holdMs(word(2, rsvp::kTokenFlagNumeric), config) == 200u);

    config.numericPercent = 50u;
    CHECK(rsvp::holdMs(word(2, rsvp::kTokenFlagNumeric), config) == 200u);
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

TEST_CASE("the refresh floor outranks a ceiling set beneath it") {
    // Ceiling first, floor last. The floor is a hardware fact -- the panel
    // cannot draw any faster -- so when the two settings contradict each other
    // it is the ceiling that gives way, not the physics.
    rsvp::TimingConfig config{};
    config.wpm = 300u;  // 200 ms base, above the ceiling below
    config.maxHoldMs = 100u;
    config.minHoldMs = 500u;

    CHECK(rsvp::holdMs(word(5), config) == 500u);
    CHECK(rsvp::holdMs(word(40), config) == 500u);  // long enough for the cap to bite
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

TEST_CASE("a zero ramp start begins the session stopped and climbs from there") {
    // Zero is a legal setting, and the ramp handles it: the first token asks for
    // zero words per minute, which holdMs treats as the slowest real speed and
    // the ceiling then bounds, so it costs one long hold rather than a stall.
    rsvp::TimingConfig config{};
    config.wpm = 400u;
    config.rampTokens = 20u;
    config.rampStartPercent = 0u;

    CHECK(rsvp::rampedWpm(0u, config) == 0u);
    CHECK(rsvp::rampedWpm(10u, config) == 200u);
    CHECK(rsvp::rampedWpm(20u, config) == 400u);
}

TEST_CASE("a ramp starting above the target does not stay between the two speeds") {
    // A defect pinned in place, not a contract. rampedWpm subtracts the ramp's
    // starting speed from the target in unsigned arithmetic, so a start above
    // the target wraps to just under 2^32 and the interpolation returns a speed
    // unrelated to either endpoint. Both endpoints are still correct, which is
    // why the shipped default of 60% never shows it.
    //
    // The repair belongs in timing.cpp. This pins the current output so that
    // repair cannot land unnoticed; when it does, these become bounds checks.
    rsvp::TimingConfig config{};
    config.wpm = 400u;
    config.rampTokens = 20u;
    config.rampStartPercent = 150u;

    CHECK(rsvp::rampedWpm(0u, config) == 600u);
    CHECK(rsvp::rampedWpm(20u, config) == 400u);
    CHECK(rsvp::rampedWpm(10u, config) == 52928u);  // 400 - 600 wrapped, then truncated
}
