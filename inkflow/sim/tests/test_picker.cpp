// ABOUTME: Asserts on the book picker -- what it lists, what it selects, what it draws.
// ABOUTME: Runs the shipped picker through the simulated panel, as the reading tests do.

#include "vendor/doctest.h"

#include "../src/panel.hpp"
#include "reader/layout.hpp"
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
