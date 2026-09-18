// ABOUTME: Pins the timing constants measured on real hardware, so a future edit that
// ABOUTME: quietly reintroduces an unmeasured guess fails instead of shipping.

#include "vendor/doctest.h"

#include "rsvp/chunker.hpp"
#include "rsvp/timing.hpp"

namespace {

rsvp::Token plainWord(std::uint16_t length = 5u) {
    rsvp::Token t{};
    t.length = length;
    return t;
}

/// Words per minute achievable when every update costs `holdMs` and shows `chunk` words.
std::uint32_t wpmAt(std::uint32_t holdMs, std::uint32_t chunk) {
    return chunk * 60000u / holdMs;
}

}  // namespace

TEST_CASE("the refresh floor default is the measured panel latency, not a guess") {
    // Measured on an Xteink X4 (SSD1677 / GDEQ0426T82) on 2026-09-18: a 120px windowed
    // partial update has a median latency of 542 ms. See
    // hardware-notes/eink-bench-20260918.csv.
    CHECK(rsvp::kPanelPartialRefreshMs == 542u);

    rsvp::TimingConfig config{};
    CHECK(config.minHoldMs == rsvp::kPanelPartialRefreshMs);
}

TEST_CASE("single-word RSVP cannot reach the useful comprehension band on this panel") {
    // Comprehension holds against normal reading at 250-350 WPM and degrades above it.
    // One word per refresh at the measured floor falls far short, which is why chunking
    // is a requirement rather than an optimisation.
    const std::uint32_t singleWord = wpmAt(rsvp::kPanelPartialRefreshMs, 1u);
    CHECK(singleWord < 250u);
}

TEST_CASE("three-word chunks land inside the useful band") {
    const std::uint32_t three = wpmAt(rsvp::kPanelPartialRefreshMs, 3u);
    CHECK(three >= 250u);
    CHECK(three <= 400u);
}

TEST_CASE("two-word chunks reach the bottom of the band but no further") {
    const std::uint32_t two = wpmAt(rsvp::kPanelPartialRefreshMs, 2u);
    CHECK(two >= 200u);
    CHECK(two < 250u);
}

TEST_CASE("the panel floor dominates any requested speed above it") {
    rsvp::TimingConfig config{};
    config.wpm = 600u;

    // Asking for 600 WPM does not produce 100 ms holds; the hardware sets the pace.
    CHECK(rsvp::holdMs(plainWord(), config) == rsvp::kPanelPartialRefreshMs);
}

TEST_CASE("a full refresh is far too slow to use per word") {
    // Measured median 1958 ms. Full refreshes are for clearing ghosting at natural
    // pauses, never for advancing a word.
    CHECK(rsvp::kPanelFullRefreshMs == 1958u);
    CHECK(wpmAt(rsvp::kPanelFullRefreshMs, 3u) < 100u);
}

TEST_CASE("the simulator can still disable the floor") {
    // Host-side tuning and the desktop simulator need to model speeds the panel cannot
    // reach, so zero must remain meaningful.
    rsvp::TimingConfig config{};
    config.minHoldMs = 0u;
    config.wpm = 600u;
    CHECK(rsvp::holdMs(plainWord(), config) == 100u);
}
