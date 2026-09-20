// ABOUTME: Asserts the device notices a button pressed while the panel is mid-refresh.
// ABOUTME: This is the failure a reader reported as "pausing doesn't seem to be working".

#include "vendor/doctest.h"

#include "../src/panel.hpp"
#include "reader/document.hpp"
#include "reader/input.hpp"
#include "reader/reader.hpp"
#include "rsvp/timing.hpp"

#include <cstring>

namespace {

const char* kProse =
    "The panel refreshes slowly, and that turned out to decide everything. A partial "
    "update costs the same five hundred milliseconds whether it redraws a narrow band "
    "or the entire screen. So words arrive in threes, and the reader who wants to stop "
    "has to be noticed while the panel is busy.";

/// A button held down between two instants on the panel's own clock.
///
/// Driven by the modelled elapsed time rather than by sample count, so a press is
/// described the way a thumb makes one -- "down for 120ms, starting here" -- rather than
/// in units of whatever the loop happens to do.
class TapSource : public reader::ButtonSource {
public:
    TapSource(const sim::Panel& panel, uint8_t button, uint32_t fromMs, uint32_t forMs)
        : panel_(panel), bit_(static_cast<uint16_t>(1u << button)), from_(fromMs),
          until_(fromMs + forMs) {}

    uint16_t levels() override {
        const uint32_t now = static_cast<uint32_t>(panel_.elapsedMs());
        return (now >= from_ && now < until_) ? bit_ : 0u;
    }

private:
    const sim::Panel& panel_;
    uint16_t bit_;
    uint32_t from_;
    uint32_t until_;
};

/// The panel's modelled clock, which is the only clock in a simulated device.
class PanelClock : public reader::Clock {
public:
    explicit PanelClock(const sim::Panel& panel) : panel_(panel) {}
    uint32_t nowMs() override { return static_cast<uint32_t>(panel_.elapsedMs()); }

private:
    const sim::Panel& panel_;
};

rsvp::TimingConfig timingAt(uint16_t wpm) {
    rsvp::TimingConfig t{};
    t.wpm = wpm;
    t.minHoldMs = rsvp::kPanelPartialRefreshMs;
    return t;
}

/// Plays chunks until the panel's clock passes `untilMs`, draining the latch each turn
/// exactly as the firmware's loop does. Returns whether the button was ever seen.
bool tapIsNoticed(uint32_t tapAtMs, uint32_t tapForMs) {
    constexpr uint8_t kButton = 1u;

    sim::Panel panel(reader::kLandscape);
    TapSource source(panel, kButton, tapAtMs, tapForMs);
    PanelClock clock(panel);
    reader::ButtonLatch latch(source, clock);
    panel.setServicer(&latch);

    reader::Document doc;
    doc.useMemory(kProse, std::strlen(kProse));
    reader::Reader r(doc, panel, reader::kLandscape);
    r.setTiming(timingAt(330));
    r.setPlaying(true);
    r.renderFull();

    bool noticed = false;
    reader::Frame frame{};
    while (panel.elapsedMs() < tapAtMs + tapForMs + 4000u) {
        // One turn of the loop: drain what was latched, then draw, which blocks.
        if ((latch.take() & static_cast<uint16_t>(1u << kButton)) != 0u) {
            noticed = true;
        }
        if (!r.step(frame)) {
            break;
        }
    }
    return noticed || (latch.take() & static_cast<uint16_t>(1u << kButton)) != 0u;
}

}  // namespace

TEST_CASE("a button pressed while the panel is refreshing is still noticed") {
    // The whole point. A reader reaching for pause does not know or care that the panel
    // is mid-waveform, and at a 650ms cycle with a 500ms refresh they will land in it
    // about three times in four.
    //
    // Swept across a couple of cycles so no single lucky phase can pass this: every
    // offset is a moment a thumb could plausibly arrive.
    for (uint32_t at = 700u; at < 2100u; at += 47u) {
        CAPTURE(at);
        CHECK(tapIsNoticed(at, 120u));
    }
}

TEST_CASE("even a brief tap is noticed") {
    // 60ms is a quick, decisive press. Shorter than one page of the panel's paging loop
    // is genuinely unnoticeable, and claiming otherwise would be a lie -- but this is
    // well inside what a thumb does.
    for (uint32_t at = 700u; at < 1600u; at += 53u) {
        CAPTURE(at);
        CHECK(tapIsNoticed(at, 60u));
    }
}

TEST_CASE("a press is acted on once, however many samples saw it") {
    // The latch is drained per turn, and a long hold spans many samples. A reader holding
    // pause for a second means pause, not pause-play-pause-play.
    constexpr uint8_t kButton = 1u;
    sim::Panel panel(reader::kLandscape);
    TapSource source(panel, kButton, 0u, 5000u);
    PanelClock clock(panel);
    reader::ButtonLatch latch(source, clock);
    panel.setServicer(&latch);

    reader::Document doc;
    doc.useMemory(kProse, std::strlen(kProse));
    reader::Reader r(doc, panel, reader::kLandscape);
    r.setTiming(timingAt(330));
    r.setPlaying(true);

    int seen = 0;
    reader::Frame frame{};
    for (int turn = 0; turn < 6; ++turn) {
        if ((latch.take() & static_cast<uint16_t>(1u << kButton)) != 0u) {
            ++seen;
        }
        if (!r.step(frame)) {
            break;
        }
    }
    CHECK(seen == 1);
    // And it is still held, which is how a hold gesture is told from a tap.
    CHECK(latch.held(kButton));
}

// Power is a hold, not a tap, and until now nothing tested holds at all. The device
// reported the press and then did nothing with it, because hold duration was measured
// between loop turns 650ms apart while the panel was blocking in between -- so a
// deliberate one-second hold could be measured as anything at all.
TEST_CASE("a hold is measured from inside the refresh, not between turns") {
    constexpr uint8_t kPower = 6u;

    auto holdIsSeen = [](uint32_t startMs, uint32_t forMs) {
        sim::Panel panel(reader::kLandscape);
        TapSource source(panel, kPower, startMs, forMs);
        PanelClock clock(panel);
        reader::ButtonLatch latch(source, clock);
        panel.setServicer(&latch);

        reader::Document doc;
        doc.useMemory(kProse, std::strlen(kProse));
        reader::Reader r(doc, panel, reader::kLandscape);
        r.setTiming(timingAt(330));
        r.setPlaying(true);
        r.renderFull();

        uint32_t longest = 0u;
        reader::Frame frame{};
        while (panel.elapsedMs() < startMs + forMs + 4000u) {
            const uint32_t held = latch.heldFor(kPower);
            if (held > longest) {
                longest = held;
            }
            latch.take();
            if (!r.step(frame)) {
                break;
            }
        }
        const uint32_t held = latch.heldFor(kPower);
        return held > longest ? held : longest;
    };

    SUBCASE("a deliberate hold is recognised as one") {
        // What a reader does when they mean to switch the device off. It must not be
        // possible for this to read as a tap.
        for (uint32_t at = 700u; at < 1800u; at += 61u) {
            CAPTURE(at);
            const uint32_t measured = holdIsSeen(at, 1200u);
            CHECK(measured >= 1000u);
        }
    }

    SUBCASE("a tap is not mistaken for a hold") {
        // The other direction matters more: switching off is the one action a reader
        // cannot undo by pressing again, and a brush against a pocket must not do it.
        for (uint32_t at = 700u; at < 1800u; at += 61u) {
            CAPTURE(at);
            const uint32_t measured = holdIsSeen(at, 120u);
            CHECK(measured < 1000u);
        }
    }
}
