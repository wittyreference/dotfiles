// ABOUTME: Tests that the tokenizer classifies boundaries and content, since the
// ABOUTME: timing model's pauses are driven entirely by the flags set here.

#include "vendor/doctest.h"

#include "rsvp/tokenizer.hpp"

#include <string>
#include <vector>

namespace {

std::vector<rsvp::Token> collect(const std::string& text) {
    rsvp::Tokenizer tokenizer(text.data(), text.size());
    std::vector<rsvp::Token> tokens;
    rsvp::Token token{};
    while (tokenizer.next(token)) {
        tokens.push_back(token);
    }
    return tokens;
}

}  // namespace

TEST_CASE("a plain word carries no flags") {
    const auto tokens = collect("hello world");
    REQUIRE(tokens.size() == 2);
    CHECK(tokens[0].flags == rsvp::kTokenFlagNone);
}

TEST_CASE("clause-ending punctuation is flagged") {
    const auto tokens = collect("first, second; third: fourth");
    REQUIRE(tokens.size() == 4);
    CHECK(tokens[0].has(rsvp::kTokenFlagClauseEnd));
    CHECK(tokens[1].has(rsvp::kTokenFlagClauseEnd));
    CHECK(tokens[2].has(rsvp::kTokenFlagClauseEnd));
    CHECK_FALSE(tokens[3].has(rsvp::kTokenFlagClauseEnd));
}

TEST_CASE("sentence-ending punctuation is flagged") {
    const auto tokens = collect("Stop. Really? Yes! Go");
    REQUIRE(tokens.size() == 4);
    CHECK(tokens[0].has(rsvp::kTokenFlagSentenceEnd));
    CHECK(tokens[1].has(rsvp::kTokenFlagSentenceEnd));
    CHECK(tokens[2].has(rsvp::kTokenFlagSentenceEnd));
    CHECK_FALSE(tokens[3].has(rsvp::kTokenFlagSentenceEnd));
}

TEST_CASE("sentence end is seen through closing quotes and brackets") {
    // Dialogue ends sentences constantly, and the terminator sits inside the
    // quote. Missing this would drop the pause on most fiction.
    const auto tokens = collect("\"Stop.\" (Later.) [Done!]");
    REQUIRE(tokens.size() == 3);
    CHECK(tokens[0].has(rsvp::kTokenFlagSentenceEnd));
    CHECK(tokens[1].has(rsvp::kTokenFlagSentenceEnd));
    CHECK(tokens[2].has(rsvp::kTokenFlagSentenceEnd));
}

TEST_CASE("an ellipsis is a pause, not a full stop") {
    // "..." mid-sentence is hesitation. Treating it as a sentence boundary would
    // insert a long beat in the middle of a thought.
    const auto tokens = collect("well... maybe");
    REQUIRE(tokens.size() == 2);
    CHECK(tokens[0].has(rsvp::kTokenFlagClauseEnd));
    CHECK_FALSE(tokens[0].has(rsvp::kTokenFlagSentenceEnd));
}

TEST_CASE("a blank line ends a paragraph") {
    const auto tokens = collect("end of one.\n\nstart of two");
    REQUIRE(tokens.size() == 6);
    CHECK(tokens[2].has(rsvp::kTokenFlagParagraphEnd));   // "one."
    CHECK_FALSE(tokens[3].has(rsvp::kTokenFlagParagraphEnd));
}

TEST_CASE("a single newline does not end a paragraph") {
    // Hard-wrapped plain text puts newlines mid-sentence. Treating each as a
    // paragraph break would pause every ten words.
    const auto tokens = collect("wrapped line\none continues");
    REQUIRE(tokens.size() == 4);
    CHECK_FALSE(tokens[1].has(rsvp::kTokenFlagParagraphEnd));
}

TEST_CASE("the final token ends a paragraph") {
    // Whatever else it is, the last token in a text closes the last paragraph --
    // so playback holds it rather than snapping to a blank screen.
    const auto tokens = collect("the very end");
    REQUIRE(tokens.size() == 3);
    CHECK(tokens[2].has(rsvp::kTokenFlagParagraphEnd));
}

TEST_CASE("tokens containing digits are flagged numeric") {
    const auto tokens = collect("chapter 12 covers 3.5 things");
    REQUIRE(tokens.size() == 5);
    CHECK_FALSE(tokens[0].has(rsvp::kTokenFlagNumeric));
    CHECK(tokens[1].has(rsvp::kTokenFlagNumeric));
    CHECK(tokens[3].has(rsvp::kTokenFlagNumeric));
}

TEST_CASE("tokens with non-ASCII bytes are flagged multibyte") {
    // Lets the renderer skip UTF-8 decoding entirely for the common ASCII case.
    const auto tokens = collect("plain café");
    REQUIRE(tokens.size() == 2);
    CHECK_FALSE(tokens[0].has(rsvp::kTokenFlagMultibyte));
    CHECK(tokens[1].has(rsvp::kTokenFlagMultibyte));
}

TEST_CASE("the pivot is populated on every token") {
    // The tokenizer is the only place that sees the token text, so it computes the
    // pivot. A downstream consumer holding only the Token cannot.
    const auto tokens = collect("a hello internationally");
    REQUIRE(tokens.size() == 3);
    CHECK(tokens[0].orp == 0);
    CHECK(tokens[1].orp == 1);
    CHECK(tokens[2].orp == 4);
}

TEST_CASE("a paragraph-ending sentence carries both flags") {
    const auto tokens = collect("done.\n\nnext");
    REQUIRE(tokens.size() == 2);
    CHECK(tokens[0].has(rsvp::kTokenFlagSentenceEnd));
    CHECK(tokens[0].has(rsvp::kTokenFlagParagraphEnd));
}
