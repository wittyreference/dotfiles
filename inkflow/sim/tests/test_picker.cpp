// ABOUTME: Asserts on the book picker -- what it lists, what it selects, what it draws.
// ABOUTME: Runs the shipped picker through the simulated panel, as the reading tests do.

#include "vendor/doctest.h"

#include "../src/panel.hpp"
#include "reader/layout.hpp"
#include "../../firmware/reader/src/resume_key.h"
#include "reader/picker.hpp"

#include <string>

namespace {

reader::Picker makeFull(sim::Panel& panel) {
    reader::Picker p(panel, reader::kLandscape);
    p.add("agents.rsvp");
    p.add("moby-dick.rsvp");
    p.add("notes.txt");
    return p;
}

}  // namespace

TEST_CASE("the picker lists what it was given, in the order it was given") {
    sim::Panel panel(reader::kLandscape);
    reader::Picker p = makeFull(panel);

    REQUIRE(p.count() == 3u);
    CHECK(std::string(p.name(0)) == "agents.rsvp");
    CHECK(std::string(p.name(1)) == "moby-dick.rsvp");
    CHECK(std::string(p.name(2)) == "notes.txt");
    // Selection starts at the top rather than nowhere: there is no null state a reader
    // could be in, and the first entry is the one their thumb is already next to.
    CHECK(p.selected() == 0u);
}

TEST_CASE("selection moves and stops at the ends") {
    sim::Panel panel(reader::kLandscape);
    reader::Picker p = makeFull(panel);

    p.moveDown();
    CHECK(p.selected() == 1u);
    p.moveDown();
    CHECK(p.selected() == 2u);
    // Clamped rather than wrapped. Wrapping means a reader holding the rocker sails past
    // the book they wanted and has to go round again; stopping is what a list does.
    p.moveDown();
    CHECK(p.selected() == 2u);

    p.moveUp();
    CHECK(p.selected() == 1u);
    p.moveUp();
    CHECK(p.selected() == 0u);
    p.moveUp();
    CHECK(p.selected() == 0u);
}

TEST_CASE("an empty card is a state the picker can be in") {
    // The card can be empty, or hold nothing this reader can open. That is not an error
    // and must not be a crash: the reader still has the built-in passage.
    sim::Panel panel(reader::kLandscape);
    reader::Picker p(panel, reader::kLandscape);

    CHECK(p.count() == 0u);
    CHECK(p.selected() == 0u);
    CHECK(p.name(0) == nullptr);
    p.moveUp();
    p.moveDown();
    CHECK(p.selected() == 0u);

    p.render();
    CHECK(panel.fullRefreshes() == 1u);
}

TEST_CASE("the picker refuses more than it can hold, rather than overrunning") {
    sim::Panel panel(reader::kLandscape);
    reader::Picker p(panel, reader::kLandscape);

    size_t added = 0;
    for (size_t i = 0; i < reader::Picker::kMaxEntries + 8u; ++i) {
        const std::string name = "book-" + std::to_string(i) + ".rsvp";
        if (p.add(name.c_str())) {
            ++added;
        }
    }
    CHECK(added == reader::Picker::kMaxEntries);
    CHECK(p.count() == reader::Picker::kMaxEntries);
}

TEST_CASE("a name longer than the buffer is truncated, not dropped") {
    // A book with a very long filename is still a book the reader owns, and showing a
    // shortened name beats pretending the card is empty.
    sim::Panel panel(reader::kLandscape);
    reader::Picker p(panel, reader::kLandscape);

    const std::string huge(reader::Picker::kMaxNameBytes + 40u, 'x');
    REQUIRE(p.add(huge.c_str()));
    REQUIRE(p.count() == 1u);
    CHECK(std::string(p.name(0)).size() == reader::Picker::kMaxNameBytes - 1u);
}

TEST_CASE("the picker draws the selection so it can be told from the rest") {
    sim::Panel panel(reader::kLandscape);
    reader::Picker p = makeFull(panel);

    p.render();
    CHECK(panel.fullRefreshes() == 1u);

    // Count ink on the row of the selected entry and on an unselected one. The selected
    // row carries a marker the others do not, so it must be strictly inkier -- this is
    // the whole affordance, and on a 1-bit panel there is no colour to fall back on.
    auto rowInk = [&panel](int16_t y) {
        int ink = 0;
        for (int16_t dy = -24; dy <= 6; ++dy) {
            for (int16_t x = 0; x < reader::kLandscape.width; ++x) {
                if (panel.canvas().pixel(x, y + dy) == sim::kBlack) {
                    ++ink;
                }
            }
        }
        return ink;
    };

    const int first = rowInk(p.rowY(0));
    const int second = rowInk(p.rowY(1));
    CHECK(first > 0);
    CHECK(second > 0);
    CHECK(first > second);

    // And it moves with the selection.
    p.moveDown();
    p.render();
    CHECK(rowInk(p.rowY(1)) > rowInk(p.rowY(0)));
}

// Resume position used to live in one NVS slot: a single "doc" name and a single "pos".
// With a picker that is a bug waiting to happen -- open a second book and the first one
// silently loses the reader's place. Each document needs its own slot, and NVS keys are
// capped at 15 characters, so the name cannot be the key.
TEST_CASE("every document gets its own resume key") {
    char a[16];
    char b[16];

    SUBCASE("the same name always gives the same key") {
        REQUIRE(resumeKey("agents.rsvp", a, sizeof(a)));
        REQUIRE(resumeKey("agents.rsvp", b, sizeof(b)));
        CHECK(std::string(a) == std::string(b));
    }

    SUBCASE("different names give different keys") {
        REQUIRE(resumeKey("agents.rsvp", a, sizeof(a)));
        REQUIRE(resumeKey("moby-dick.rsvp", b, sizeof(b)));
        CHECK(std::string(a) != std::string(b));

        // Names differing in one character must not collide either -- books on one card
        // are often a series with a number on the end.
        REQUIRE(resumeKey("vol-1.rsvp", a, sizeof(a)));
        REQUIRE(resumeKey("vol-2.rsvp", b, sizeof(b)));
        CHECK(std::string(a) != std::string(b));
    }

    SUBCASE("the key fits what NVS will accept") {
        // NVS refuses a key longer than 15 characters, and refuses it at runtime on a
        // device with nobody watching, so this is checked here instead.
        const char* names[] = {"a", "agents.rsvp", "a-really-quite-long-book-name-indeed.rsvp", ""};
        for (const char* n : names) {
            REQUIRE(resumeKey(n, a, sizeof(a)));
            const std::string key(a);
            CHECK(key.size() <= 15u);
            CHECK(key.size() > 0u);
            // Printable ASCII, so a key is greppable in a dump rather than mojibake.
            for (char c : key) {
                CHECK(c > 0x20);
                CHECK(c < 0x7f);
            }
        }
    }

    SUBCASE("a buffer too small is refused rather than truncated") {
        // A truncated key would collide with other truncated keys, which is worse than
        // not saving: the reader would be dropped into someone else's page.
        char tiny[4];
        CHECK_FALSE(resumeKey("agents.rsvp", tiny, sizeof(tiny)));
        CHECK_FALSE(resumeKey("agents.rsvp", a, 0u));
        CHECK_FALSE(resumeKey(nullptr, a, sizeof(a)));
    }
}
