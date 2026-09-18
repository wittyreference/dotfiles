// ABOUTME: Runs the shipped reading loop against a simulated panel and writes the frames
// ABOUTME: to PNG, exiting non-zero when the layout overflows or ghosting never clears.

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "file_source.hpp"
#include "panel.hpp"
#include "reader/document.hpp"
#include "reader/reader.hpp"

namespace {

/// Shown when no input file is given, so the simulator always has something to draw.
const char kFallback[] =
    "The panel refreshes slowly, and that turned out to decide everything. "
    "A partial update costs the same five hundred milliseconds whether it "
    "redraws a narrow band or the entire screen.\n\n"
    "So words arrive in threes. One at a time would be readable but far too "
    "slow; three at a time lands where comprehension holds. The hardware "
    "chose the design, and the reading research agreed with it.";

void usage() {
    std::fputs(
        "usage: sim [input] [output-dir] [frames] [--portrait] [--wpm n] [--no-png]\n"
        "\n"
        "  input        .rsvp sidecar (streamed, as the device does) or .txt\n"
        "  output-dir   where frame-NN.png files are written\n"
        "  frames       how many chunks to present, 0 for the whole document\n"
        "  --portrait   render the layout that was rejected, which must overflow\n"
        "  --wpm n      requested reading speed, default 330\n"
        "  --no-png     run the reading loop without writing images\n",
        stderr);
}

bool endsWith(const std::string& s, const char* suffix) {
    const size_t n = std::strlen(suffix);
    return s.size() >= n && s.compare(s.size() - n, n, suffix) == 0;
}

bool readWholeFile(const char* path, std::string& out) {
    std::FILE* f = std::fopen(path, "rb");
    if (f == nullptr) {
        return false;
    }
    char buf[8192];
    size_t got = 0;
    while ((got = std::fread(buf, 1, sizeof(buf), f)) > 0) {
        out.append(buf, got);
    }
    std::fclose(f);
    return true;
}

}  // namespace

int main(int argc, char** argv) {
    // Landscape is the default because it is what the firmware ships. The simulator
    // previously defaulted to portrait and put landscape behind a compile flag, which
    // meant the unflagged build rendered a layout the device does not produce.
    const reader::Layout* layout = &reader::kLandscape;
    const char* input = nullptr;
    const char* outDir = ".";
    uint32_t frames = 12;
    uint16_t wpm = 330;
    bool writePng = true;
    int positional = 0;

    for (int i = 1; i < argc; ++i) {
        const char* a = argv[i];
        if (std::strcmp(a, "-h") == 0 || std::strcmp(a, "--help") == 0) {
            usage();
            return 0;
        }
        if (std::strcmp(a, "--portrait") == 0) {
            layout = &reader::kPortrait;
        } else if (std::strcmp(a, "--no-png") == 0) {
            writePng = false;
        } else if (std::strcmp(a, "--wpm") == 0) {
            if (i + 1 >= argc) {
                std::fputs("sim: --wpm needs a value\n", stderr);
                return 2;
            }
            wpm = static_cast<uint16_t>(std::atoi(argv[++i]));
            if (wpm == 0) {
                std::fputs("sim: --wpm must be positive\n", stderr);
                return 2;
            }
        } else if (a[0] == '-' && a[1] != '\0') {
            std::fprintf(stderr, "sim: unknown option %s\n", a);
            usage();
            return 2;
        } else {
            switch (positional++) {
                case 0: input = a; break;
                case 1: outDir = a; break;
                case 2: frames = static_cast<uint32_t>(std::atoi(a)); break;
                default:
                    std::fprintf(stderr, "sim: unexpected argument %s\n", a);
                    return 2;
            }
        }
    }

    sim::Panel panel(*layout);
    reader::Document doc;
    sim::FileSource source(input != nullptr ? input : "");
    std::string text;

    const char* name = "built-in";
    if (input != nullptr && endsWith(input, ".rsvp")) {
        // The path the device actually runs: seek into a flat token array rather than
        // hold one. Exercising it here is the only way to observe it without hardware.
        if (!doc.openSidecar(source)) {
            std::fprintf(stderr, "sim: cannot read sidecar %s\n", input);
            return 1;
        }
        name = input;
    } else if (input != nullptr) {
        if (!readWholeFile(input, text)) {
            std::fprintf(stderr, "sim: cannot read %s\n", input);
            return 1;
        }
        doc.useMemory(text.data(), text.size());
        name = input;
    } else {
        doc.useMemory(kFallback, sizeof(kFallback) - 1);
    }

    if (doc.count() == 0) {
        std::fputs("sim: document has no tokens\n", stderr);
        return 1;
    }

    reader::Reader r(doc, panel, *layout);
    r.setName(name);
    rsvp::TimingConfig timing{};
    timing.wpm = wpm;
    timing.minHoldMs = rsvp::kPanelPartialRefreshMs;
    r.setTiming(timing);
    r.setPlaying(true);

    std::printf("%u tokens; %s layout %dx%d at %u wpm\n", doc.count(),
                layout == &reader::kPortrait ? "portrait" : "landscape", layout->width,
                layout->height, wpm);

    r.renderFull();

    uint32_t overflows = 0;
    uint32_t presented = 0;
    uint32_t words = 0;
    uint64_t holdTotal = 0;
    reader::Frame frame{};

    while ((frames == 0 || presented < frames) && r.step(frame)) {
        if (writePng) {
            char path[512];
            std::snprintf(path, sizeof(path), "%s/frame-%02u.png", outDir, presented);
            panel.writePng(path);
        }
        std::printf("  %2u  %u words  hold %4u ms  x=[%d,%d]%s%s  \"%s\"\n", presented,
                    frame.tokens, frame.holdMs, frame.left, frame.right,
                    frame.fullRefresh ? "  FULL" : "",
                    frame.overflows ? "  <-- OVERFLOWS" : "", frame.text);
        if (frame.overflows) {
            ++overflows;
        }
        words += frame.tokens;
        holdTotal += frame.holdMs;
        ++presented;
    }

    const uint32_t delivered =
        holdTotal > 0 ? static_cast<uint32_t>(words * 60000ull / holdTotal) : 0u;
    std::printf(
        "\n%u chunks, %u words, %llu ms of holds, %llu ms of panel time\n"
        "delivered %u wpm against %u requested\n"
        "%u full refreshes, %u partial, worst ghosting %u partials deep\n",
        presented, words, static_cast<unsigned long long>(holdTotal),
        static_cast<unsigned long long>(panel.elapsedMs()), delivered, wpm,
        panel.fullRefreshes(), panel.partialRefreshes(), panel.peakGhost());

    // The simulator is a test, not a viewer. A layout fault is invisible in code review
    // and awkward to describe over chat; making it a non-zero exit turns it into a build
    // failure instead.
    if (overflows > 0) {
        std::fprintf(stderr,
                     "\n%u of %u chunks overflowed. The budget is the width minus the "
                     "focal column -- %d px of %d, not %d -- because a chunk is "
                     "positioned by its pivot and extends rightward.\n",
                     overflows, presented, layout->rightBudget(), layout->width,
                     layout->width);
        return 1;
    }

    return 0;
}
