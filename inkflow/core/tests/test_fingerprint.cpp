// ABOUTME: Pins documentFingerprint's exact output and its sampling behaviour, so a
// ABOUTME: change to the hash fails here instead of silently breaking every resume point.

#include "vendor/doctest.h"

#include "rsvp/fingerprint.hpp"
#include "rsvp/token.hpp"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace {

rsvp::Token token(std::uint32_t offset, std::uint16_t length, std::uint8_t orp,
                  std::uint8_t flags) {
    rsvp::Token t{};
    t.offset = offset;
    t.length = length;
    t.orp = orp;
    t.flags = flags;
    return t;
}

/// The array the golden value below was computed over. Editing it invalidates the pin.
std::vector<rsvp::Token> goldenTokens() {
    return {token(0u, 5u, 1u, rsvp::kTokenFlagNone),
            token(6u, 3u, 1u, rsvp::kTokenFlagClauseEnd),
            token(10u, 7u, 2u, rsvp::kTokenFlagSentenceEnd),
            token(18u, 4u, 1u, rsvp::kTokenFlagParagraphEnd)};
}

std::uint32_t fingerprint(const std::vector<rsvp::Token>& tokens) {
    return rsvp::documentFingerprint(tokens.data(), static_cast<std::uint32_t>(tokens.size()));
}

}  // namespace

TEST_CASE("the fingerprint of a fixed token array is exactly this value") {
    // A GOLDEN VALUE, and the reason this file exists. documentFingerprint is stored in
    // every .rsvp file as documentId, and a resume point is honoured only while the two
    // still match. Change the hash -- a different basis, a different sample set, a
    // different field order -- and every file and bookmark already on a card stops
    // resuming. Nothing else in the suite catches that, because every other comparison
    // recomputes both sides and they move together. Only a literal can hold this still.
    //
    // Derived independently from the algorithm fingerprint.hpp documents rather than
    // read back out of the implementation: FNV-1a (basis 2166136261, prime 16777619)
    // over the four little-endian bytes of each value, fed the token count and then
    // offset, length, orp and flags for tokens 0, count/2 and count-1.
    //
    // If this fails, the container's version must go up and existing files must be
    // migrated. Updating the number is not a fix.
    CHECK(fingerprint(goldenTokens()) == 0x3BEF896Bu);
}

TEST_CASE("a single-token document collapses all three samples onto token zero") {
    // With count == 1 the sample indices 0, count/2 and count-1 are all 0, so one token
    // is mixed in three times. Pinned for the same reason as the array above, and it is
    // the case that would read off the front of the array if count-1 ever wrapped.
    const std::vector<rsvp::Token> one{token(7u, 4u, 1u, rsvp::kTokenFlagSentenceEnd)};
    CHECK(fingerprint(one) == 0x0686A4C4u);
}

TEST_CASE("a one-token count does not reach past its own array") {
    // Same first token, a second one that differs in every field: if the sampling ever
    // walked past count, this would stop matching the pinned single-token value.
    const rsvp::Token pair[2] = {token(7u, 4u, 1u, rsvp::kTokenFlagSentenceEnd),
                                 token(99u, 9u, 3u, rsvp::kTokenFlagNumeric)};
    CHECK(rsvp::documentFingerprint(pair, 1u) == 0x0686A4C4u);
}

TEST_CASE("an empty document hashes without dereferencing the array") {
    // The count is mixed in before the array is looked at, which is what keeps a null
    // pointer safe and keeps documents of different lengths from sharing an id even
    // when the sampled tokens are unavailable.
    const auto tokens = goldenTokens();

    CHECK(rsvp::documentFingerprint(nullptr, 0u) == 0x4B95F515u);
    CHECK(rsvp::documentFingerprint(tokens.data(), 0u) == rsvp::documentFingerprint(nullptr, 0u));
    CHECK(rsvp::documentFingerprint(nullptr, 5u) != rsvp::documentFingerprint(nullptr, 0u));
}

TEST_CASE("changing any sampled field changes the fingerprint") {
    // Three tokens means every index is sampled, so every field of every token has to
    // move the result. A field that did not participate would let two different
    // documents share an id, and the reader would resume into the wrong book at a
    // position that looks plausible.
    const std::vector<rsvp::Token> tokens{token(0u, 5u, 1u, rsvp::kTokenFlagNone),
                                          token(6u, 3u, 1u, rsvp::kTokenFlagClauseEnd),
                                          token(10u, 7u, 2u, rsvp::kTokenFlagSentenceEnd)};
    const std::uint32_t baseline = fingerprint(tokens);

    for (std::size_t i = 0u; i < tokens.size(); ++i) {
        CAPTURE(i);

        auto offsetChanged = tokens;
        offsetChanged[i].offset += 1u;
        CHECK(fingerprint(offsetChanged) != baseline);

        auto lengthChanged = tokens;
        lengthChanged[i].length = static_cast<std::uint16_t>(lengthChanged[i].length + 1u);
        CHECK(fingerprint(lengthChanged) != baseline);

        auto orpChanged = tokens;
        orpChanged[i].orp = static_cast<std::uint8_t>(orpChanged[i].orp + 1u);
        CHECK(fingerprint(orpChanged) != baseline);

        auto flagsChanged = tokens;
        flagsChanged[i].flags =
            static_cast<std::uint8_t>(flagsChanged[i].flags | rsvp::kTokenFlagNumeric);
        CHECK(fingerprint(flagsChanged) != baseline);
    }
}
