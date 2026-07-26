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
