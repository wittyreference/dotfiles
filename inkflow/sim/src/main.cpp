// ABOUTME: Renders the reader's screens to PNG on a desktop, so layout can be seen and
// ABOUTME: checked without a device in hand.

#include "canvas.hpp"

#include "rsvp/chunker.hpp"
#include "rsvp/timing.hpp"
#include "rsvp/tokenizer.hpp"

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#define FREEMONOBOLD18PT7B_H_  // the headers guard themselves inconsistently
#include "../vendor/Fonts/FreeMonoBold12pt7b.h"
#include "../vendor/Fonts/FreeMonoBold18pt7b.h"

namespace {

// Orientation is a real design choice, not a detail. The panel is natively 800x480.
// Portrait (rotation 3) is how a page-based reader is held; landscape (rotation 0) is
// how RSVP wants to be read, because RSVP needs one wide line and no height at all.
#ifdef SIM_LANDSCAPE
constexpr int kW = 800;
constexpr int kH = 480;
constexpr int kBandY = 180;
constexpr int kFocalX = 300;
#else
constexpr int kW = 480;
constexpr int kH = 800;
constexpr int kBandY = 330;
constexpr int kFocalX = 190;
#endif

// Must match firmware/reader/src/config.h. Divergence here would make the simulator
// confidently show a layout the device does not produce, which is worse than no
// simulator at all.
constexpr int kBandH = 120;

std::vector<rsvp::Token> tokenize(const std::string& text) {
    std::vector<rsvp::Token> out;
    rsvp::Tokenizer t(text.data(), text.size());
    rsvp::Token tok{};
    while (t.next(tok)) {
        out.push_back(tok);
    }
    return out;
}

/// Builds a chunk exactly as the firmware does, reporting its pivot character index.
uint32_t buildChunk(const std::string& text, const std::vector<rsvp::Token>& toks,
                    uint32_t start, const rsvp::ChunkConfig& cfg, std::string& out,
                    uint8_t& pivot) {
    const uint32_t n =
        rsvp::chunkLength(toks.data(), uint32_t(toks.size()), start, cfg);
    out.clear();
    for (uint32_t i = 0; i < n; ++i) {
        if (i > 0) {
            out += ' ';
        }
        out.append(text, toks[start + i].offset, toks[start + i].length);
    }
    pivot = n > 0 ? toks[start].orp : 0;
    return n;
}

void drawGuides(sim::Canvas& c) {
    c.fillRect(kFocalX - 1, kBandY - 26, 3, 16, sim::kBlack);
    c.fillRect(kFocalX - 1, kBandY + kBandH + 10, 3, 16, sim::kBlack);
}

/// One reading frame, laid out the way the firmware lays it out.
void renderReading(sim::Canvas& c, const std::string& docName, uint16_t wpm, uint32_t pct,
                   bool playing, const std::string& chunk, uint8_t pivot, bool markBand) {
    c.fillScreen(sim::kWhite);
    c.setTextColor(sim::kBlack);

    c.setFont(&FreeMonoBold12pt7b);
    c.setCursor(20, 60);
    c.print(docName.c_str());
    char status[96];
    std::snprintf(status, sizeof(status), "%u wpm  %u%%  %s", unsigned(wpm), unsigned(pct),
                  playing ? "" : "[paused]");
    c.setCursor(20, 95);
    c.print(status);

    drawGuides(c);

    // The pivot character is positioned on the focal column; everything else falls
    // where it falls. That alignment is the whole mechanism, and it is exactly what is
    // worth being able to see.
    c.setFont(&FreeMonoBold18pt7b);
    std::string prefix = chunk.substr(0, pivot);
    int pw = 0, ph = 0;
    c.textBounds(prefix.c_str(), pw, ph);
    c.setCursor(kFocalX - pw, kBandY + 70);
    c.print(chunk.c_str());

    if (markBand) {
        // Outline the region a partial refresh actually redraws.
        c.frameRect(0, kBandY, kW, kBandH, sim::kBlack);
    }
}

/// Horizontal extent a chunk would occupy, for checking it fits before drawing it.
///
/// The simulator exists to catch this class of fault. Portrait at 20 characters ran 88px
/// off the right edge, because the chunk is positioned by its pivot -- which sits near
/// the start -- so nearly all of it extends rightward, and a budget computed as though
/// it were centred is simply wrong.
void chunkExtent(sim::Canvas& c, const std::string& chunk, uint8_t pivot, int& left,
                 int& right) {
    c.setFont(&FreeMonoBold18pt7b);
    int pw = 0, ph = 0, fw = 0, fh = 0;
    c.textBounds(chunk.substr(0, pivot).c_str(), pw, ph);
    c.textBounds(chunk.c_str(), fw, fh);
    left = kFocalX - pw;
    right = left + fw;
}

}  // namespace

int main(int argc, char** argv) {
    const std::string text =
        argc > 1 ? [&] {
            std::FILE* f = std::fopen(argv[1], "rb");
            if (!f) return std::string();
            std::string s;
            char buf[4096];
            size_t n;
            while ((n = std::fread(buf, 1, sizeof(buf), f)) > 0) s.append(buf, n);
            std::fclose(f);
            return s;
        }()
                 : std::string("The panel refreshes slowly. Three words arrive together "
                               "now, because one at a time would be far too slow.\n\n"
                               "The hardware chose the design.");

    const auto toks = tokenize(text);
    if (toks.empty()) {
        std::fprintf(stderr, "sim: no tokens\n");
        return 1;
    }

    rsvp::ChunkConfig chunking{};
    rsvp::TimingConfig timing{};
    timing.wpm = 330;

    const char* outDir = argc > 2 ? argv[2] : ".";
    const int frames = argc > 3 ? std::atoi(argv[3]) : 6;

    std::printf("%zu tokens; rendering %d frames to %s\n", toks.size(), frames, outDir);

    uint32_t at = 0;
    int overflows = 0;
    for (int frame = 0; frame < frames && at < toks.size(); ++frame) {
        std::string chunk;
        uint8_t pivot = 0;
        const uint32_t n = buildChunk(text, toks, at, chunking, chunk, pivot);
        if (n == 0) break;

        const uint32_t hold =
            rsvp::chunkHoldMs(toks.data(), uint32_t(toks.size()), at, n, timing);

        sim::Canvas c(kW, kH);
        renderReading(c, "simulator", timing.wpm,
                      uint32_t(at * 100u / toks.size()), true, chunk, pivot, true);

        char path[512];
        std::snprintf(path, sizeof(path), "%s/frame-%02d.png", outDir, frame);
        if (!c.writePng(path)) {
            std::fprintf(stderr, "sim: could not write %s\n", path);
            return 1;
        }
        int left = 0, right = 0;
        chunkExtent(c, chunk, pivot, left, right);
        const bool fits = left >= 0 && right <= kW;
        if (!fits) {
            overflows++;
        }
        std::printf("  %s  %u words, hold %4u ms, x=[%d,%d]%s  \"%s\"\n", path, n, hold,
                    left, right, fits ? "" : "  <-- OVERFLOWS", chunk.c_str());
        at += n;
    }

    if (overflows > 0) {
        std::fprintf(stderr,
                     "\nsim: %d of %d frames overflow the %dpx screen.\n"
                     "The chunk is positioned by its pivot, so most of it sits right of\n"
                     "the focal column at x=%d -- the usable budget is %d px, not %d.\n",
                     overflows, frames, kW, kFocalX, kW - kFocalX, kW);
        return 1;
    }
    return 0;
}
