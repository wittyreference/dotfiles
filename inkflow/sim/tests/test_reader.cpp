// ABOUTME: Drives the shipped reading loop through the simulated panel and asserts on the
// ABOUTME: reading experience -- pacing, ghost flushing, rewind, and layout fit.

#include "vendor/doctest.h"

#include "../../firmware/reader/src/card_path.h"
#include "../src/panel.hpp"
#include "../src/file_source.hpp"
#include "reader/document.hpp"
#include "reader/reader.hpp"
#include "rsvp/format.hpp"
#include "rsvp/player.hpp"
#include "rsvp/timing.hpp"

#include <cstdio>
#include <cstring>
#include <initializer_list>
#include <string>
#include <vector>

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

TEST_CASE("pausing reveals the sentence the reader is inside") {
    // Pausing is what a reader does when they have lost the thread. Showing the sentence
    // they are inside answers that directly, and is cheaper than rewinding back through
    // it -- the two complement each other rather than competing.
    sim::Panel panel(reader::kLandscape);
    reader::Document doc = makeDocument();
    reader::Reader r(doc, panel, reader::kLandscape);
    r.setTiming(timingAt(330));
    r.setPlaying(true);

    reader::Frame frame{};
    for (int i = 0; i < 6; ++i) {
        REQUIRE(r.step(frame));
    }

    char context[256];
    const size_t n = r.contextText(context, sizeof(context));
    CAPTURE(context);
    REQUIRE(n > 0);

    // The chunk on screen must appear inside the sentence being shown, or the context is
    // context for somewhere else.
    reader::Frame current{};
    REQUIRE(r.peek(current) > 0);
    const std::string sentence(context);
    const std::string firstWord(current.text, std::strcspn(current.text, " "));
    CHECK(sentence.find(firstWord) != std::string::npos);

    // It is a sentence, not the whole document.
    CHECK(sentence.size() < std::strlen(kProse));
}

TEST_CASE("context is bounded by the buffer it is given") {
    sim::Panel panel(reader::kLandscape);
    reader::Document doc = makeDocument();
    reader::Reader r(doc, panel, reader::kLandscape);
    r.setTiming(timingAt(330));

    char tiny[8];
    const size_t n = r.contextText(tiny, sizeof(tiny));
    CHECK(n < sizeof(tiny));
    CHECK(tiny[n] == '\0');

    // A degenerate request must not write anywhere.
    CHECK(r.contextText(nullptr, 100) == 0u);
    char one[1];
    CHECK(r.contextText(one, 0) == 0u);
}

TEST_CASE("the context is drawn only while paused") {
    // Asserting on the text alone would pass even if nothing reached the panel. This
    // counts actual ink below the reading band.
    auto inkBelowBand = [](const sim::Panel& p) {
        uint32_t dark = 0;
        // Below the lower guide tick, which lives at bandH + 10 and is 16px tall. The
        // ticks are meant to be there; counting them would be counting the wrong thing.
        const int top = reader::kLandscape.bandY + reader::kLandscape.bandH + 30;
        for (int y = top; y < reader::kLandscape.height; ++y) {
            for (int x = 0; x < reader::kLandscape.width; ++x) {
                if (p.canvas().pixel(x, y) == sim::kBlack) {
                    ++dark;
                }
            }
        }
        return dark;
    };

    sim::Panel panel(reader::kLandscape);
    reader::Document doc = makeDocument();
    reader::Reader r(doc, panel, reader::kLandscape);
    r.setTiming(timingAt(330));
    r.setPlaying(true);

    reader::Frame frame{};
    for (int i = 0; i < 6; ++i) {
        REQUIRE(r.step(frame));
    }

    // Playing: the space below the band stays empty, because a second thing to look at is
    // the exact cost RSVP exists to remove.
    r.renderFull();
    CHECK(inkBelowBand(panel) == 0u);

    // Paused: the sentence appears.
    r.setPlaying(false);
    r.renderFull();
    CHECK(inkBelowBand(panel) > 0u);

    // And it goes away again on resume, rather than lingering as ghost furniture.
    r.setPlaying(true);
    r.renderFull();
    CHECK(inkBelowBand(panel) == 0u);
}

TEST_CASE("the pivot offset moves the recognition point") {
    // The banded ORP heuristic is a default, not a law. A reader who wants the fixation
    // a character earlier or later should be able to say so.
    sim::Panel panel(reader::kLandscape);
    reader::Document doc = makeDocument();
    reader::Reader r(doc, panel, reader::kLandscape);
    r.setTiming(timingAt(330));

    reader::Frame neutral{};
    REQUIRE(r.peek(neutral) > 0);

    // A later pivot means more of the chunk sits left of the focal column, so the text
    // starts further left on screen.
    r.setPivotOffset(2);
    reader::Frame later{};
    REQUIRE(r.peek(later) > 0);
    CHECK(later.left < neutral.left);

    // And an earlier pivot pushes it right.
    r.setPivotOffset(-1);
    reader::Frame earlier{};
    REQUIRE(r.peek(earlier) > 0);
    CHECK(earlier.left > neutral.left);

    // Whatever the offset, the chunk must still be the same words.
    CHECK(std::string(later.text) == std::string(neutral.text));
}

TEST_CASE("an extreme pivot offset cannot push the text off the panel") {
    sim::Panel panel(reader::kLandscape);
    reader::Document doc = makeDocument();
    reader::Reader r(doc, panel, reader::kLandscape);
    r.setTiming(timingAt(330));
    r.setPlaying(true);

    for (int8_t offset : {int8_t(-120), int8_t(120)}) {
        CAPTURE(offset);
        r.seek(0);
        r.setPivotOffset(offset);
        reader::Frame f{};
        REQUIRE(r.peek(f) > 0);
        CHECK(f.left >= 0);
        CHECK(f.right <= reader::kLandscape.width);
    }
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

TEST_CASE("the sleep screen is unmistakable, and leaves a clean image behind") {
    // E-paper holds its image with no power, so an off device shows whatever was drawn
    // last. Without a screen of its own, "off" and "paused mid-sentence" are the same
    // picture, and a reader who finds their device showing three words has no way to know
    // whether it is waiting for them or dead.
    sim::Panel panel(reader::kLandscape);
    reader::Document doc = makeDocument();
    reader::Reader r(doc, panel, reader::kLandscape);
    r.setTiming(timingAt(330));
    r.setPlaying(true);

    // Accrue some ghosting first, and get far enough in that the progress figure is not
    // zero -- both are things the sleep screen has to deal with rather than ignore.
    r.renderFull();
    reader::Frame frame{};
    for (int i = 0; i < 12; ++i) {
        REQUIRE(r.step(frame));
    }
    REQUIRE(panel.ghost() > 0u);
    REQUIRE(r.index() > 0u);

    const uint32_t fullsBefore = panel.fullRefreshes();
    r.renderSleep();

    // A full refresh, so the retained image is clean rather than however much ghosting
    // had accumulated by the time the reader pressed power.
    CHECK(panel.fullRefreshes() == fullsBefore + 1u);
    CHECK(panel.ghost() == 0u);

    const int mid = reader::kLandscape.bandY + reader::kChunkBaseline;

    // The guide ticks still stand on the focal column, so an off device looks like itself
    // rather than like a crash.
    CHECK(panel.canvas().pixel(reader::kLandscape.focalX, mid - 70) == sim::kBlack);
    CHECK(panel.canvas().pixel(reader::kLandscape.focalX, mid + 34) == sim::kBlack);

    // A rule running nearly the full width of the panel: nothing a half-finished refresh
    // would ever leave behind. Black at both ends, and the margins left white so it reads
    // as a drawn line rather than as a smear.
    const int ruleY = mid + 72;
    CHECK(panel.canvas().pixel(61, ruleY) == sim::kBlack);
    CHECK(panel.canvas().pixel(reader::kLandscape.width - 62, ruleY) == sim::kBlack);
    CHECK(panel.canvas().pixel(10, ruleY) == sim::kWhite);
    CHECK(panel.canvas().pixel(reader::kLandscape.width - 11, ruleY) == sim::kWhite);
}

TEST_CASE("the sleep screen shows how far through the reader got") {
    // The progress figure is the one piece of state worth retaining on a screen nobody is
    // looking at: it is what tells you which of two devices on the table is the one you
    // were part-way through.
    auto sleepRow = [](uint32_t steps) {
        sim::Panel panel(reader::kLandscape);
        reader::Document doc = makeDocument();
        reader::Reader r(doc, panel, reader::kLandscape);
        r.setTiming(timingAt(330));
        r.setPlaying(true);
        reader::Frame frame{};
        for (uint32_t i = 0; i < steps; ++i) {
            REQUIRE(r.step(frame));
        }
        r.renderSleep();

        // Count the ink on the status line rather than read it: the test is that the
        // figure changes with position, not what typeface it is set in.
        const int y = reader::kLandscape.bandY + reader::kChunkBaseline + 72 + 44;
        int ink = 0;
        for (int dy = -24; dy <= 4; ++dy) {
            for (int x = 0; x < reader::kLandscape.width; ++x) {
                if (panel.canvas().pixel(x, y + dy) == sim::kBlack) {
                    ++ink;
                }
            }
        }
        return ink;
    };

    const int early = sleepRow(1);
    const int late = sleepRow(20);
    CHECK(early > 0);
    CHECK(late > 0);
    CHECK(early != late);
}

TEST_CASE("the sleep screen survives a document name longer than its buffer") {
    // setName takes 64 bytes and the sleep line formats into 96 with a percentage after
    // it. A name that fills its own buffer must truncate rather than run off the end of
    // the line buffer -- the failure mode being a stack smash on a device with no
    // debugger attached.
    sim::Panel panel(reader::kLandscape);
    reader::Document doc = makeDocument();
    reader::Reader r(doc, panel, reader::kLandscape);
    r.setTiming(timingAt(330));

    const std::string huge(200, 'x');
    r.setName(huge.c_str());
    r.renderSleep();

    CHECK(panel.fullRefreshes() == 1u);
}

// The upload and delete handlers build a card path out of a name that arrived over HTTP.
// Nothing on the far side of that is trusted, and the card is the only writable storage
// the device has. The policy lives in a header with no Arduino types precisely so it can
// be tested here rather than only on a device nobody is watching.
TEST_CASE("a card path is built only from a plain filename") {
    char out[64];

    SUBCASE("an ordinary book is accepted, with its leading slash") {
        REQUIRE(safeCardPath("agents.rsvp", out, sizeof(out)));
        CHECK(std::string(out) == "/agents.rsvp");
    }

    SUBCASE("separators are refused rather than stripped") {
        CHECK_FALSE(safeCardPath("../secret", out, sizeof(out)));
        CHECK_FALSE(safeCardPath("sub/book.rsvp", out, sizeof(out)));
        CHECK_FALSE(safeCardPath("/absolute.rsvp", out, sizeof(out)));
        CHECK_FALSE(safeCardPath("back\\slash.rsvp", out, sizeof(out)));
    }

    SUBCASE("directory references are refused, but a dotted name is not") {
        CHECK_FALSE(safeCardPath(".", out, sizeof(out)));
        CHECK_FALSE(safeCardPath("..", out, sizeof(out)));
        REQUIRE(safeCardPath("Vol..2.rsvp", out, sizeof(out)));
        CHECK(std::string(out) == "/Vol..2.rsvp");
    }

    SUBCASE("an empty name is not a filename") {
        CHECK_FALSE(safeCardPath("", out, sizeof(out)));
        CHECK_FALSE(safeCardPath(nullptr, out, sizeof(out)));
    }

    SUBCASE("a name that does not fit is refused, not truncated") {
        // Truncating would silently write to a different file than the one uploaded.
        const std::string longName(80, 'b');
        CHECK_FALSE(safeCardPath(longName.c_str(), out, sizeof(out)));

        char tight[8];
        CHECK(safeCardPath("abcdef", tight, sizeof(tight)));
        CHECK_FALSE(safeCardPath("abcdefg", tight, sizeof(tight)));
    }

    SUBCASE("control characters are refused") {
        CHECK_FALSE(safeCardPath("book\nname.rsvp", out, sizeof(out)));
    }
}

// Document has two text paths and they were not symmetric. The RAM branch clamps the
// request against the text it holds; the streaming branch seeked and read, and relied on
// the file simply running out to bound it. That is fine for a well-formed sidecar -- and
// a sidecar that arrived over an upload path with no test coverage is exactly the thing
// that stops being well-formed first.
TEST_CASE("streamed text is bounded by the document, not by the end of the file") {
    // A valid sidecar with extra bytes appended after it. A short read cannot bound the
    // request here, because there are plenty more bytes in the file: only textLength can.
    const std::string body = "one two three";
    const std::string trailer = "SHOULD NOT BE READABLE AS DOCUMENT TEXT";

    const size_t sidecarSize = rsvp::rsvpFileSize(0u, uint32_t(body.size()));
    std::string file(sidecarSize, '\0');
    size_t written = 0u;
    REQUIRE(rsvp::writeRsvp(nullptr, 0u, body.data(), uint32_t(body.size()),
                            reinterpret_cast<unsigned char*>(&file[0]), file.size(),
                            written) == rsvp::RsvpStatus::kOk);
    REQUIRE(written == sidecarSize);
    file += trailer;

    const std::string path = "/tmp/inkflow-bounded-text.rsvp";
    {
        std::FILE* f = std::fopen(path.c_str(), "wb");
        REQUIRE(f != nullptr);
        std::fwrite(file.data(), 1, file.size(), f);
        std::fclose(f);
    }

    sim::FileSource source(path.c_str());
    REQUIRE(source.valid());
    reader::Document doc;
    REQUIRE(doc.openSidecar(source));

    char out[128];

    // Asking for more than the blob holds must stop at the blob, not spill into what
    // follows it in the file.
    const size_t got = doc.text(0, uint16_t(body.size() + trailer.size()), out, sizeof(out));
    CHECK(got == body.size());
    CHECK(std::string(out) == body);

    // And an offset past the end hands back nothing rather than a window into the trailer.
    CHECK(doc.text(uint32_t(body.size() + 4u), 16u, out, sizeof(out)) == 0u);
    CHECK(out[0] == '\0');

    std::remove(path.c_str());
}

// rsvp::Player and reader::Reader both know how to walk backwards to the start of a
// sentence, and they are separate implementations: Player is the host-side estimator that
// rsvp-mk's reading-time figure runs through, reader::Reader is what ships on the device
// over a streaming Document.
//
// Having two of anything is how this project's worst bug happened -- the firmware
// reimplemented the chunk hold, took a max where the engine takes a sum, and the
// simulator stayed green for weeks because it was running the other copy. Deleting one of
// these is not the fix (Player backs a host tool; Reader has hardware evidence behind
// it), so instead they are made to prove they agree.
TEST_CASE("the estimator and the reading loop agree about sentence starts") {
    reader::Document doc = makeDocument();
    REQUIRE(doc.count() > 20u);

    // Player borrows a token array; Document hands them out one at a time.
    std::vector<rsvp::Token> tokens;
    for (uint32_t i = 0; i < doc.count(); ++i) {
        const rsvp::Token* t = doc.token(i);
        REQUIRE(t != nullptr);
        tokens.push_back(*t);
    }

    sim::Panel panel(reader::kLandscape);
    reader::Reader r(doc, panel, reader::kLandscape);
    r.setTiming(timingAt(330));

    rsvp::Player p(tokens.data(), uint32_t(tokens.size()), timingAt(330));

    // From every position in the document, one rewind must land in the same place.
    for (uint32_t start = 0; start < doc.count(); ++start) {
        r.seek(start);
        p.seek(start);
        r.rewindSentence();
        p.rewindSentence();
        REQUIRE(r.index() == p.position());
    }

    // And repeated presses must walk backwards together rather than one of them sticking.
    r.seek(doc.count() - 1u);
    p.seek(doc.count() - 1u);
    for (int press = 0; press < 12; ++press) {
        r.rewindSentence();
        p.rewindSentence();
        REQUIRE(r.index() == p.position());
    }
    CHECK(r.index() == 0u);
}
