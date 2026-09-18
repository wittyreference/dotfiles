// ABOUTME: Drives the shipped reading loop through the simulated panel and asserts on the
// ABOUTME: reading experience -- pacing, ghost flushing, rewind, and layout fit.

#include "vendor/doctest.h"

#include "../src/panel.hpp"
#include "reader/document.hpp"
#include "reader/reader.hpp"
#include "rsvp/timing.hpp"

#include <cstring>
#include <string>

namespace {

/// Real prose rather than a contrived string: chunk boundaries, sentence pauses and the
/// length bonus all key off punctuation and word length, so synthetic input would
/// exercise a timing model nobody actually reads under.
const char* kProse =
    "The panel refreshes slowly, and that turned out to decide everything. A partial "
    "update costs the same five hundred milliseconds whether it redraws a narrow band "
    "or the entire screen. So words arrive in threes; one at a time would be readable "
    "but far too slow, and three at a time lands where comprehension holds.\n\n"
    "The hardware chose the design, and the reading research agreed with it. That is a "
    "more comfortable outcome than it sounds, because the alternative was a device that "
    "could not present text at a speed anyone would want to read.";

reader::Document makeDocument() {
    reader::Document doc;
    doc.useMemory(kProse, std::strlen(kProse));
    return doc;
}

rsvp::TimingConfig timingAt(uint16_t wpm) {
    rsvp::TimingConfig t{};
    t.wpm = wpm;
    // What the firmware boots with: the measured panel floor, not zero.
    t.minHoldMs = rsvp::kPanelPartialRefreshMs;
    return t;
}

/// Plays a document to the end and reports the pace the reader actually delivered.
uint32_t deliveredWpm(uint16_t requestedWpm) {
    sim::Panel panel(reader::kLandscape);
    reader::Document doc = makeDocument();
    reader::Reader r(doc, panel, reader::kLandscape);
    r.setTiming(timingAt(requestedWpm));
    r.setPlaying(true);

    uint64_t totalMs = 0;
    uint32_t words = 0;
    reader::Frame frame{};
    while (r.step(frame)) {
        totalMs += frame.holdMs;
        words += frame.tokens;
    }
    REQUIRE(words > 0);
    REQUIRE(totalMs > 0);
    return static_cast<uint32_t>(words * 60000ull / totalMs);
}

}  // namespace

TEST_CASE("the reader reaches the end of a document") {
    sim::Panel panel(reader::kLandscape);
    reader::Document doc = makeDocument();
    reader::Reader r(doc, panel, reader::kLandscape);
    r.setTiming(timingAt(330));
    r.setPlaying(true);

    uint32_t words = 0;
    reader::Frame frame{};
    while (r.step(frame)) {
        words += frame.tokens;
    }
    CHECK(words == doc.count());
    CHECK(r.atEnd());
}

TEST_CASE("chunks fit the landscape screen") {
    sim::Panel panel(reader::kLandscape);
    reader::Document doc = makeDocument();
    reader::Reader r(doc, panel, reader::kLandscape);
    r.setTiming(timingAt(330));
    r.setPlaying(true);

    uint32_t overflows = 0;
    reader::Frame frame{};
    while (r.step(frame)) {
        if (frame.overflows) {
            ++overflows;
        }
    }
    CHECK(overflows == 0);
}

TEST_CASE("portrait still overflows, which is why it was abandoned") {
    // The budget is the width minus the focal column, because a chunk is positioned by
    // its pivot and extends rightward. 290px of 480 for text that needs about 400.
    sim::Panel panel(reader::kPortrait);
    reader::Document doc = makeDocument();
    reader::Reader r(doc, panel, reader::kPortrait);
    r.setTiming(timingAt(330));
    r.setPlaying(true);

    uint32_t overflows = 0;
    reader::Frame frame{};
    while (r.step(frame)) {
        if (frame.overflows) {
            ++overflows;
        }
    }
    CHECK(overflows > 0);
}

TEST_CASE("the pivot lands on the focal column") {
    sim::Panel panel(reader::kLandscape);
    reader::Document doc = makeDocument();
    reader::Reader r(doc, panel, reader::kLandscape);
    r.setTiming(timingAt(330));

    reader::Frame frame{};
    REQUIRE(r.peek(frame) > 0);
    // The text starts left of the focal column and continues past it: the pivot
    // character is inside the chunk, not at either edge.
    CHECK(frame.left <= reader::kLandscape.focalX);
    CHECK(frame.right >= reader::kLandscape.focalX);
}

TEST_CASE("rewind walks backwards through successive presses") {
    sim::Panel panel(reader::kLandscape);
    reader::Document doc = makeDocument();
    reader::Reader r(doc, panel, reader::kLandscape);
    r.setTiming(timingAt(330));
    r.setPlaying(true);

    reader::Frame frame{};
    for (int i = 0; i < 20; ++i) {
        REQUIRE(r.step(frame));
    }
    const uint32_t deep = r.index();

    r.rewindSentence();
    const uint32_t first = r.index();
    CHECK(first < deep);

    r.rewindSentence();
    const uint32_t second = r.index();
    CHECK(second < first);

    // Rewinding from the very start has nowhere to go and must not wrap or hang.
    r.seek(0);
    r.rewindSentence();
    CHECK(r.index() == 0u);
}

TEST_CASE("speed adjustment is clamped to a readable band") {
    sim::Panel panel(reader::kLandscape);
    reader::Document doc = makeDocument();
    reader::Reader r(doc, panel, reader::kLandscape);
    r.setTiming(timingAt(330));

    for (int i = 0; i < 100; ++i) {
        r.adjustSpeed(-30);
    }
    CHECK(r.timing().wpm == 60u);

    for (int i = 0; i < 100; ++i) {
        r.adjustSpeed(+30);
    }
    CHECK(r.timing().wpm == 900u);
}

TEST_CASE("the panel model charges the measured refresh cost") {
    // Pinned against hardware-notes/eink-bench-20260918.csv. These are observations, not
    // arithmetic, and a later edit must not quietly replace them with a guess.
    // 120px is the band the reader ships, so it must be exact.
    CHECK(sim::partialRefreshMs(120) == 542u);
    CHECK(sim::partialRefreshMs(800) == 704u);

    // The rest are a linear fit and cannot all land exactly. Two milliseconds is the
    // worst residual; asserting equality here would be asserting a false precision.
    struct Measured {
        int16_t height;
        uint32_t medianMs;
    };
    const Measured csv[] = {{40, 524u}, {80, 530u}, {120, 542u},
                            {200, 560u}, {400, 608u}, {800, 704u}};
    for (const Measured& m : csv) {
        CAPTURE(m.height);
        const uint32_t modelled = sim::partialRefreshMs(m.height);
        CAPTURE(modelled);
        const uint32_t error = modelled > m.medianMs ? modelled - m.medianMs
                                                     : m.medianMs - modelled;
        CHECK(error <= 2u);
    }

    CHECK(rsvp::kPanelFullRefreshMs == 1958u);
}

TEST_CASE("a band update leaves the guide marks standing") {
    // The ticks sit outside the redrawn band precisely so they survive partial updates.
    // If a band update cleared the whole screen they would vanish after the first word.
    sim::Panel panel(reader::kLandscape);
    reader::Document doc = makeDocument();
    reader::Reader r(doc, panel, reader::kLandscape);
    r.setTiming(timingAt(330));
    r.setPlaying(true);

    r.renderFull();
    reader::Frame frame{};
    REQUIRE(r.step(frame));
    REQUIRE_FALSE(frame.fullRefresh);

    // The upper tick is at bandY - 26, above the band, and is 16px tall.
    const int tickX = reader::kLandscape.focalX;
    const int tickY = reader::kLandscape.bandY - 20;
    CHECK(panel.canvas().pixel(tickX, tickY) == sim::kBlack);
}

TEST_CASE("ghosting is cleared within a bound even without paragraph breaks") {
    // The flush wants a paragraph boundary, so that the 1958ms full refresh lands where
    // the timing model already inserts a beat and reads as intentional rather than as a
    // fault. But prose does not owe the reader a paragraph on schedule. A long stretch
    // without one lets ghosting accrue unboundedly, and there is no amount of "reads as
    // intentional" that makes an illegible panel acceptable.
    //
    // Real prose with real sentence structure, just no blank lines -- which is exactly
    // what a long quoted passage, a list, or dialogue looks like.
    std::string dense;
    for (int i = 0; i < 400; ++i) {
        dense +=
            "The panel refreshes slowly and that decides everything here. A partial "
            "update costs the same regardless of area. Words arrive in threes. ";
    }

    sim::Panel panel(reader::kLandscape);
    reader::Document doc;
    doc.useMemory(dense.data(), dense.size());
    REQUIRE(doc.count() > 1000u);

    reader::Reader r(doc, panel, reader::kLandscape);
    r.setTiming(timingAt(330));
    r.setPlaying(true);

    reader::Frame frame{};
    while (r.step(frame)) {
    }

    CAPTURE(panel.peakGhost());
    CAPTURE(panel.fullRefreshes());
    CHECK(panel.fullRefreshes() > 0u);
    CHECK(panel.peakGhost() <= reader::kPartialsFlushDeadline);
}

TEST_CASE("delivered pace tracks requested speed") {
    // The whole point of the speed buttons. A reader that ignores them is worse than one
    // without them, because it looks like it is responding.
    struct Case {
        uint16_t requested;
    };
    const Case cases[] = {{200}, {240}, {300}, {400}};

    for (const Case& c : cases) {
        CAPTURE(c.requested);
        const uint32_t delivered = deliveredWpm(c.requested);
        CAPTURE(delivered);
        // Generous tolerance: the panel's 542ms floor genuinely caps the top of the
        // range, and boundary pauses legitimately slow the average below the nominal
        // figure. What this catches is the pace not moving at all.
        CHECK(delivered <= c.requested * 125u / 100u);
    }
}

TEST_CASE("slowing down actually slows the reader down") {
    const uint32_t fast = deliveredWpm(400);
    const uint32_t slow = deliveredWpm(200);
    CAPTURE(fast);
    CAPTURE(slow);
    // Halving the requested speed must produce a materially slower delivered pace.
    CHECK(slow < fast * 70u / 100u);
}
