// ABOUTME: Tests for the playback state machine -- pause, rewind-by-sentence, and
// ABOUTME: resume, which are the affordances that make RSVP usable rather than a demo.

#include "vendor/doctest.h"

#include "rsvp/player.hpp"
#include "rsvp/tokenizer.hpp"

#include <string>
#include <vector>

namespace {

// Four sentences, so rewinding can walk back through several.
const std::string kText =
    "One two three. Four five six. Seven eight nine. Ten eleven twelve.";

std::vector<rsvp::Token> tokenize(const std::string& text) {
    rsvp::Tokenizer tokenizer(text.data(), text.size());
    std::vector<rsvp::Token> tokens;
    rsvp::Token token{};
    while (tokenizer.next(token)) {
        tokens.push_back(token);
    }
    return tokens;
}

const std::vector<rsvp::Token>& corpus() {
    static const std::vector<rsvp::Token> tokens = tokenize(kText);
    return tokens;
}

rsvp::Player makePlayer(const rsvp::TimingConfig& config = rsvp::TimingConfig{}) {
    const auto& tokens = corpus();
    return rsvp::Player(tokens.data(), static_cast<std::uint32_t>(tokens.size()), config);
}

}  // namespace

TEST_CASE("the corpus has the shape the rewind tests assume") {
    // Guard the fixture: if tokenization changes, the index expectations below
    // would silently start testing something else.
    const auto& tokens = corpus();
    REQUIRE(tokens.size() == 12u);
    CHECK(tokens[2].has(rsvp::kTokenFlagSentenceEnd));   // "three."
    CHECK(tokens[5].has(rsvp::kTokenFlagSentenceEnd));   // "six."
    CHECK(tokens[8].has(rsvp::kTokenFlagSentenceEnd));   // "nine."
    CHECK(tokens[11].has(rsvp::kTokenFlagSentenceEnd));  // "twelve."
}

TEST_CASE("a new player is paused at the beginning") {
    auto player = makePlayer();
    CHECK(player.position() == 0u);
    CHECK_FALSE(player.isPlaying());
    CHECK(player.current() != nullptr);
}

TEST_CASE("play and pause toggle the running state without moving position") {
    auto player = makePlayer();
    player.seek(4u);

    player.play();
    CHECK(player.isPlaying());
    CHECK(player.position() == 4u);

    player.pause();
    CHECK_FALSE(player.isPlaying());
    CHECK(player.position() == 4u);
}

TEST_CASE("advancing walks the token stream and stops at the end") {
    auto player = makePlayer();

    for (std::uint32_t i = 0u; i < 11u; ++i) {
        CHECK(player.advance());
    }
    CHECK(player.position() == 11u);

    // Advancing off the last token ends playback rather than wrapping.
    CHECK_FALSE(player.advance());
    CHECK(player.isAtEnd());
    CHECK_FALSE(player.isPlaying());
}

TEST_CASE("rewind from mid-sentence returns to that sentence's first token") {
    auto player = makePlayer();
    player.seek(7u);  // "eight", inside the third sentence (tokens 6-8)

    player.rewindSentence();
    CHECK(player.position() == 6u);  // "Seven"
}

TEST_CASE("rewind at a sentence start steps back to the previous sentence") {
    // This is what makes repeated presses useful: the first press recovers the
    // sentence you just lost, and further presses keep walking back.
    auto player = makePlayer();
    player.seek(6u);  // already the first token of sentence three

    player.rewindSentence();
    CHECK(player.position() == 3u);  // "Four" -- start of sentence two

    player.rewindSentence();
    CHECK(player.position() == 0u);  // "One" -- start of sentence one
}

TEST_CASE("rewind at the very beginning stays put") {
    auto player = makePlayer();
    player.rewindSentence();
    CHECK(player.position() == 0u);
}

TEST_CASE("rewind clears the end state so playback can resume") {
    auto player = makePlayer();
    player.seek(11u);
    CHECK_FALSE(player.advance());
    REQUIRE(player.isAtEnd());

    player.rewindSentence();
    CHECK_FALSE(player.isAtEnd());
    CHECK(player.current() != nullptr);
}

TEST_CASE("seek clamps rather than running off the end") {
    auto player = makePlayer();
    player.seek(9999u);
    CHECK(player.position() == 11u);
}

TEST_CASE("the speed ramp restarts whenever playback resumes") {
    // Un-pausing straight back into cruise speed is jarring, and after a pause is
    // exactly when the reader has lost the thread. Each resume eases back in.
    rsvp::TimingConfig config{};
    config.wpm = 400u;
    config.minHoldMs = 0u;
    config.rampTokens = 20u;
    config.rampStartPercent = 50u;

    auto player = makePlayer(config);
    player.play();
    const auto firstHold = player.currentHoldMs();

    // Play well past the ramp so we are at cruise speed.
    for (std::uint32_t i = 0u; i < 10u; ++i) {
        player.advance();
    }
    const auto cruiseHold = player.currentHoldMs();
    CHECK(cruiseHold < firstHold);

    // Pause, resume: the next token should be held like a session opening again.
    player.pause();
    player.play();
    CHECK(player.currentHoldMs() > cruiseHold);
}

TEST_CASE("a resume point round-trips") {
    auto player = makePlayer();
    player.seek(7u);

    const auto point = player.resumePoint();

    auto restored = makePlayer();
    CHECK(restored.restore(point));
    CHECK(restored.position() == 7u);
}

TEST_CASE("a resume point from a different document is rejected") {
    // Resume state lives on an SD card next to the book. Applying one book's
    // position to another would drop the reader into a random paragraph, so the
    // point carries a document fingerprint and a mismatch is refused.
    auto player = makePlayer();
    player.seek(7u);

    auto point = player.resumePoint();
    point.documentId ^= 0xFFFFFFFFu;

    auto other = makePlayer();
    CHECK_FALSE(other.restore(point));
    CHECK(other.position() == 0u);
}

TEST_CASE("a resume point past the end of the document is rejected") {
    auto player = makePlayer();
    auto point = player.resumePoint();
    point.tokenIndex = 9999u;

    CHECK_FALSE(player.restore(point));
    CHECK(player.position() == 0u);
}

TEST_CASE("remaining time shrinks as the reader progresses") {
    rsvp::TimingConfig config{};
    config.rampTokens = 0u;
    config.minHoldMs = 0u;

    auto player = makePlayer(config);
    const auto atStart = player.remainingMs();

    player.seek(6u);
    const auto midway = player.remainingMs();

    player.seek(11u);
    const auto nearEnd = player.remainingMs();

    CHECK(atStart > midway);
    CHECK(midway > nearEnd);
    CHECK(nearEnd > 0u);
}

TEST_CASE("an empty document is immediately at the end") {
    rsvp::Player player(nullptr, 0u, rsvp::TimingConfig{});
    CHECK(player.isAtEnd());
    CHECK(player.current() == nullptr);
    CHECK_FALSE(player.advance());
    CHECK(player.remainingMs() == 0u);

    // And must not crash when asked to do things anyway.
    player.play();
    player.rewindSentence();
    player.seek(5u);
    CHECK(player.position() == 0u);
}
