// ABOUTME: Unit tests for the rsvp-core tokenizer, which slices UTF-8 text into
// ABOUTME: word tokens carrying boundary classification and a pivot index.

#include "vendor/doctest.h"

#include "rsvp/tokenizer.hpp"

#include <string>
#include <vector>

namespace {

// Collects every token the tokenizer yields for `text`. Test-only helper: the
// tokenizer itself never allocates, but tests are free to.
std::vector<rsvp::Token> collect(const std::string& text) {
    rsvp::Tokenizer tokenizer(text.data(), text.size());
    std::vector<rsvp::Token> tokens;
    rsvp::Token token{};
    while (tokenizer.next(token)) {
        tokens.push_back(token);
    }
    return tokens;
}

// The source bytes a token points at.
std::string textOf(const std::string& source, const rsvp::Token& token) {
    return source.substr(token.offset, token.length);
}

}  // namespace

TEST_CASE("empty input yields no tokens") {
    CHECK(collect("").empty());
}

TEST_CASE("whitespace-only input yields no tokens") {
    CHECK(collect("   \t\n  ").empty());
}

TEST_CASE("a single word yields one token spanning it") {
    const std::string source = "hello";
    const auto tokens = collect(source);

    REQUIRE(tokens.size() == 1);
    CHECK(tokens[0].offset == 0);
    CHECK(tokens[0].length == 5);
    CHECK(textOf(source, tokens[0]) == "hello");
}

TEST_CASE("words separated by whitespace become separate tokens") {
    const std::string source = "the quick brown fox";
    const auto tokens = collect(source);

    REQUIRE(tokens.size() == 4);
    CHECK(textOf(source, tokens[0]) == "the");
    CHECK(textOf(source, tokens[1]) == "quick");
    CHECK(textOf(source, tokens[2]) == "brown");
    CHECK(textOf(source, tokens[3]) == "fox");
}

TEST_CASE("leading and trailing whitespace is skipped") {
    const std::string source = "  hello   world  ";
    const auto tokens = collect(source);

    REQUIRE(tokens.size() == 2);
    CHECK(textOf(source, tokens[0]) == "hello");
    CHECK(textOf(source, tokens[1]) == "world");
}

TEST_CASE("punctuation stays attached to its word") {
    // RSVP displays the word as written -- stripping punctuation would lose the
    // reading cue that a clause just ended.
    const std::string source = "Wait, stop!";
    const auto tokens = collect(source);

    REQUIRE(tokens.size() == 2);
    CHECK(textOf(source, tokens[0]) == "Wait,");
    CHECK(textOf(source, tokens[1]) == "stop!");
}

TEST_CASE("a null text is treated as empty") {
    // A caller whose buffer read failed hands us null. Guarding here means no
    // caller needs its own special case before constructing a tokenizer.
    rsvp::Tokenizer tokenizer(nullptr, 100u);

    rsvp::Token token{};
    CHECK_FALSE(tokenizer.next(token));
    CHECK(tokenizer.position() == 0u);
}

TEST_CASE("next leaves its out-param untouched at end of input") {
    // Callers loop on `while (next(token))` and may still read the last token
    // after the loop ends, so the failing call must not scribble over it.
    const std::string source = "hi";
    rsvp::Tokenizer tokenizer(source.data(), source.size());

    rsvp::Token token{};
    REQUIRE(tokenizer.next(token));
    const rsvp::Token last = token;

    CHECK_FALSE(tokenizer.next(token));
    CHECK(token.offset == last.offset);
    CHECK(token.length == last.length);
    CHECK(token.orp == last.orp);
    CHECK(token.flags == last.flags);
}

TEST_CASE("a run longer than Token::length can hold splits without losing bytes") {
    // Token::length is 16 bits, so a 70 KB run -- a data URI or a base64 blob,
    // not a word -- cannot be described by one token. The tokenizer emits the
    // largest fragment that fits and resumes at the cut, so the remainder is
    // still delivered rather than silently dropped.
    const std::size_t maxTokenLength = 65535u;
    const std::size_t remainder = 4465u;
    const std::string blob(maxTokenLength + remainder, 'a');
    const std::string source = blob + " tail";
    const auto tokens = collect(source);

    REQUIRE(tokens.size() == 3);
    CHECK(tokens[0].offset == 0u);
    CHECK(tokens[0].length == maxTokenLength);
    CHECK(tokens[1].offset == maxTokenLength);
    CHECK(tokens[1].length == remainder);
    CHECK(textOf(source, tokens[0]) + textOf(source, tokens[1]) == blob);
    CHECK(textOf(source, tokens[2]) == "tail");
}

TEST_CASE("a trailing dash ends a clause") {
    // An interrupted line -- "wait-" -- is a clause boundary the same way a
    // comma is, and the reader expects the same beat after it.
    const std::string source = "wait- stop";
    const auto tokens = collect(source);

    REQUIRE(tokens.size() == 2);
    CHECK(tokens[0].has(rsvp::kTokenFlagClauseEnd));
}

TEST_CASE("form feed and vertical tab separate tokens") {
    // Both turn up in text extracted from PDFs and in older plain-text files,
    // where a form feed can be the only thing between two words.
    const std::string source = "one\ftwo\vthree";
    const auto tokens = collect(source);

    REQUIRE(tokens.size() == 3);
    CHECK(textOf(source, tokens[0]) == "one");
    CHECK(textOf(source, tokens[1]) == "two");
    CHECK(textOf(source, tokens[2]) == "three");
}

TEST_CASE("CRLF line endings are classified like bare newlines") {
    // Text files from Windows are the common case, not an edge case: if only
    // "\n\n" counted, every paragraph pause in such a file would be lost.
    SUBCASE("a CRLF blank line ends a paragraph") {
        const auto tokens = collect("one\r\n\r\ntwo");

        REQUIRE(tokens.size() == 2);
        CHECK(tokens[0].has(rsvp::kTokenFlagParagraphEnd));
    }

    SUBCASE("a single CRLF is hard wrapping, not a paragraph break") {
        const auto tokens = collect("one\r\ntwo");

        REQUIRE(tokens.size() == 2);
        CHECK_FALSE(tokens[0].has(rsvp::kTokenFlagParagraphEnd));
    }
}

TEST_CASE("a token with no ASCII in it at all is flagged multibyte") {
    const std::string source = "\xE6\x97\xA5\xE6\x9C\xAC\xE8\xAA\x9E";  // 日本語
    const auto tokens = collect(source);

    REQUIRE(tokens.size() == 1);
    CHECK(tokens[0].length == 9u);  // three characters, three bytes each
    CHECK(tokens[0].has(rsvp::kTokenFlagMultibyte));
    CHECK_FALSE(tokens[0].has(rsvp::kTokenFlagNumeric));
    CHECK(tokens[0].orp == 1u);  // pivot counts characters, so this is band 1
}

TEST_CASE("position reports progress through the buffer, whitespace included") {
    // position() is a progress counter for the one buffer this tokenizer was
    // constructed over, not a streaming cursor -- see the note on the accessor.
    const std::string source = "hello world  ";
    rsvp::Tokenizer tokenizer(source.data(), source.size());
    rsvp::Token token{};

    CHECK(tokenizer.position() == 0u);
    REQUIRE(tokenizer.next(token));
    CHECK(tokenizer.position() == 5u);
    REQUIRE(tokenizer.next(token));
    CHECK(tokenizer.position() == 11u);

    // The failing call still consumes the trailing whitespace it scanned.
    CHECK_FALSE(tokenizer.next(token));
    CHECK(tokenizer.position() == source.size());
}
