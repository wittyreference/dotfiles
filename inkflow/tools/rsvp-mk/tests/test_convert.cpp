// ABOUTME: Tests for the host-side converter -- markdown normalisation to prose and
// ABOUTME: assembly of a valid .rsvp sidecar that plays back to the original words.

#include "vendor/doctest.h"

#include "convert.hpp"

#include "rsvp/format.hpp"
#include "rsvp/player.hpp"
#include "rsvp/tokenizer.hpp"

#include <string>
#include <vector>

namespace {

std::vector<std::string> wordsOf(const std::string& text) {
    rsvp::Tokenizer tokenizer(text.data(), text.size());
    std::vector<std::string> words;
    rsvp::Token token{};
    while (tokenizer.next(token)) {
        words.push_back(text.substr(token.offset, token.length));
    }
    return words;
}

}  // namespace

TEST_CASE("plain text passes through markdown normalisation unchanged") {
    const std::string plain = "Just a sentence. And another one.";
    CHECK(rsvpmk::markdownToText(plain) == plain);
}

TEST_CASE("heading markers are stripped but the heading text is kept") {
    CHECK(rsvpmk::markdownToText("# Title") == "Title");
    CHECK(rsvpmk::markdownToText("### Deeper heading") == "Deeper heading");
}

TEST_CASE("list markers are stripped") {
    CHECK(rsvpmk::markdownToText("- first") == "first");
    CHECK(rsvpmk::markdownToText("* second") == "second");
    CHECK(rsvpmk::markdownToText("1. third") == "third");
    CHECK(rsvpmk::markdownToText("  - indented") == "indented");
}

TEST_CASE("blockquote markers are stripped") {
    CHECK(rsvpmk::markdownToText("> quoted line") == "quoted line");
}

TEST_CASE("emphasis and inline code markers are removed, content preserved") {
    CHECK(rsvpmk::markdownToText("a **strong** word") == "a strong word");
    CHECK(rsvpmk::markdownToText("an *emphatic* word") == "an emphatic word");
    CHECK(rsvpmk::markdownToText("an _underscored_ word") == "an underscored word");
    CHECK(rsvpmk::markdownToText("some `inline code` here") == "some inline code here");
}

TEST_CASE("links keep their text and drop their target") {
    // The URL is not prose. Flashing "https://example.com/a/b" one word at a time
    // is noise at best.
    CHECK(rsvpmk::markdownToText("see [the docs](https://example.com) now") ==
          "see the docs now");
}

TEST_CASE("images are dropped entirely") {
    CHECK(rsvpmk::markdownToText("before ![alt text](img.png) after") == "before  after");
}

TEST_CASE("fenced code blocks are dropped") {
    // Reading source one word at a time is useless, and the punctuation would
    // trigger spurious sentence pauses throughout.
    const std::string input =
        "Prose before.\n"
        "```cpp\n"
        "int main() { return 0; }\n"
        "```\n"
        "Prose after.";
    const auto out = rsvpmk::markdownToText(input);

    CHECK(out.find("int main") == std::string::npos);
    CHECK(out.find("Prose before.") != std::string::npos);
    CHECK(out.find("Prose after.") != std::string::npos);
}

TEST_CASE("horizontal rules are dropped") {
    const auto out = rsvpmk::markdownToText("above\n\n---\n\nbelow");
    CHECK(out.find("---") == std::string::npos);
    CHECK(out.find("above") != std::string::npos);
    CHECK(out.find("below") != std::string::npos);
}

TEST_CASE("blank lines survive normalisation") {
    // Load-bearing: paragraph detection keys off a blank line, so a normaliser that
    // collapsed them would silently remove every paragraph pause in the document.
    const auto out = rsvpmk::markdownToText("# Title\n\nFirst para.\n\nSecond para.");
    CHECK(out.find("\n\n") != std::string::npos);

    const auto tokens = wordsOf(out);
    rsvp::Tokenizer tokenizer(out.data(), out.size());
    rsvp::Token token{};
    std::size_t paragraphEnds = 0u;
    while (tokenizer.next(token)) {
        paragraphEnds += token.has(rsvp::kTokenFlagParagraphEnd) ? 1u : 0u;
    }

    REQUIRE(tokens.size() > 3u);
    CHECK(paragraphEnds == 3u);  // after "Title", "para." and the final token
}

TEST_CASE("building a sidecar produces a file the reader accepts") {
    const std::string text = "One two three. Four five six.\n\nSeven eight nine.";
    const auto file = rsvpmk::buildRsvp(text);

    REQUIRE(file.size() > rsvp::kRsvpHeaderSize);

    rsvp::RsvpHeader header{};
    REQUIRE(rsvp::readRsvpHeader(file.data(), file.size(), header) == rsvp::RsvpStatus::kOk);
    CHECK(rsvp::verifyRsvpChecksum(file.data(), file.size()) == rsvp::RsvpStatus::kOk);
    CHECK(header.tokenCount == wordsOf(text).size());
    CHECK(header.textLength == text.size());
}

TEST_CASE("a built sidecar plays back the original words in order") {
    // The end-to-end property that matters: text in, identical words out, via the
    // real file format rather than an in-memory shortcut.
    const std::string text = "One two three. Four five six.\n\nSeven eight nine.";
    const auto file = rsvpmk::buildRsvp(text);

    rsvp::RsvpHeader header{};
    REQUIRE(rsvp::readRsvpHeader(file.data(), file.size(), header) == rsvp::RsvpStatus::kOk);

    const rsvp::Token* tokens = rsvp::rsvpTokens(file.data(), header);
    const char* blob = rsvp::rsvpText(file.data(), header);
    REQUIRE(tokens != nullptr);
    REQUIRE(blob != nullptr);

    rsvp::Player player(tokens, header.tokenCount, rsvp::TimingConfig{});

    std::vector<std::string> played;
    do {
        const rsvp::Token* token = player.current();
        REQUIRE(token != nullptr);
        played.emplace_back(blob + token->offset, token->length);
    } while (player.advance());

    CHECK(played == wordsOf(text));
}

TEST_CASE("a resume point saved against a built sidecar restores against it") {
    const std::string text = "One two three. Four five six.\n\nSeven eight nine.";
    const auto file = rsvpmk::buildRsvp(text);

    rsvp::RsvpHeader header{};
    REQUIRE(rsvp::readRsvpHeader(file.data(), file.size(), header) == rsvp::RsvpStatus::kOk);
    const rsvp::Token* tokens = rsvp::rsvpTokens(file.data(), header);
    REQUIRE(tokens != nullptr);

    rsvp::Player first(tokens, header.tokenCount, rsvp::TimingConfig{});
    first.seek(5u);
    const auto point = first.resumePoint();

    // The id the file recorded must match the one the player computes, or resume
    // silently breaks across a restart.
    CHECK(point.documentId == header.documentId);

    rsvp::Player second(tokens, header.tokenCount, rsvp::TimingConfig{});
    CHECK(second.restore(point));
    CHECK(second.position() == 5u);
}

TEST_CASE("an empty input produces a valid, empty sidecar") {
    const auto file = rsvpmk::buildRsvp("");
    rsvp::RsvpHeader header{};
    REQUIRE(rsvp::readRsvpHeader(file.data(), file.size(), header) == rsvp::RsvpStatus::kOk);
    CHECK(header.tokenCount == 0u);
}
