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
