// ABOUTME: Unit tests for optimal-recognition-point (pivot) calculation, the
// ABOUTME: character an RSVP reader aligns to the screen's fixed focal column.

#include "vendor/doctest.h"

#include "rsvp/orp.hpp"

#include <string>

namespace {

std::uint8_t orpOf(const std::string& word) {
    return rsvp::computeOrp(word.data(), word.size());
}

}  // namespace

TEST_CASE("degenerate tokens pivot on the first character") {
    CHECK(orpOf("") == 0);
    CHECK(orpOf("a") == 0);
    CHECK(orpOf("I") == 0);
}

TEST_CASE("pivot sits left of centre and grows in steps with word length") {
    // The published RSVP heuristic: the recognition point is not the midpoint,
    // it is biased left, and it advances in bands rather than continuously.
    SUBCASE("2 to 5 characters pivot on index 1") {
        CHECK(orpOf("at") == 1);
        CHECK(orpOf("the") == 1);
        CHECK(orpOf("word") == 1);
        CHECK(orpOf("hello") == 1);
    }

    SUBCASE("6 to 9 characters pivot on index 2") {
        CHECK(orpOf("reader") == 2);
        CHECK(orpOf("attention") == 2);
    }

    SUBCASE("10 to 13 characters pivot on index 3") {
        CHECK(orpOf("incredible") == 3);
        CHECK(orpOf("unbelievable") == 3);
    }

    SUBCASE("14 or more characters pivot on index 4") {
        CHECK(orpOf("internationally") == 4);
        CHECK(orpOf("incomprehensibility") == 4);
    }
}

TEST_CASE("trailing punctuation does not move the pivot") {
    // The eye should land in the same place for a word whether or not a comma
    // follows it. Punctuation is not part of the word's shape.
    CHECK(orpOf("hello,") == orpOf("hello"));
    CHECK(orpOf("hello.") == orpOf("hello"));
    CHECK(orpOf("reader!") == orpOf("reader"));
    CHECK(orpOf("really?") == orpOf("really"));
}

TEST_CASE("leading punctuation shifts the pivot to keep it on the word") {
    // The returned index is relative to the whole token, because the renderer
    // indexes into the token directly. So an opening quote pushes it right by one.
    CHECK(orpOf("\"hello") == orpOf("hello") + 1);
    CHECK(orpOf("(hello") == orpOf("hello") + 1);
    CHECK(orpOf("\"hello,\"") == orpOf("hello") + 1);
}

TEST_CASE("pivot is counted in characters, not bytes") {
    // "ñandú" is 5 characters but 7 bytes. A byte-based pivot would land on a
    // UTF-8 continuation byte and render as garbage.
    CHECK(orpOf("ñandú") == 1);
    CHECK(orpOf("café") == 1);
    CHECK(orpOf("Ünicode") == 2);
}

TEST_CASE("tokens with no letters pivot on the first character") {
    // Em dashes and ellipses appear as standalone tokens in real books. There is
    // no word core to centre on, so fall back to the start rather than guessing.
    CHECK(orpOf("---") == 0);
    CHECK(orpOf("...") == 0);
    CHECK(orpOf("42") == 1);  // digits are word characters: a numeral has a shape
}

TEST_CASE("a null token pivots on zero rather than dereferencing") {
    // The helper above cannot reach this branch -- std::string::data() is never
    // null -- but a caller holding a failed read can, so it is tested directly.
    CHECK(rsvp::computeOrp(nullptr, 0u) == 0);
}

TEST_CASE("a pivot past the width of Token::orp is clamped, not wrapped") {
    // Token::orp is one byte. A token opening with hundreds of punctuation marks
    // is not prose, but the renderer indexes with whatever it is handed, so the
    // pivot has to stay a valid index instead of wrapping round to a small one.
    const std::string deepPadding(300u, '(');
    CHECK(orpOf(deepPadding + "hello") == 255);

    // Just below the clamp the real index still comes through untouched.
    const std::string shallowPadding(250u, '(');
    CHECK(orpOf(shallowPadding + "hello") == 251);
}

TEST_CASE("leading non-ASCII punctuation is counted into the word core") {
    // The documented cost of treating every non-ASCII byte as a letter: a
    // typographic quote joins the core instead of being skipped like its ASCII
    // twin, so the pivot lands a character earlier in the word than it should.
    const std::string curly = "\xE2\x80\x9C" "reader";  // U+201C, three bytes

    CHECK(orpOf("reader") == 2);
    CHECK(orpOf("\"reader") == 3);  // ASCII quote is skipped, pivot keeps its letter
    CHECK(orpOf(curly) == 2);       // curly quote is not, so the pivot slips left
}
