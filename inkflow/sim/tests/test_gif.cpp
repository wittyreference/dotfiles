// ABOUTME: Asserts the simulator's animated GIF output by parsing and LZW-decoding the
// ABOUTME: files it writes, so the pixels and the reading pace are proven, not assumed.

#include "vendor/doctest.h"

#include "../src/gif.hpp"
#include "../src/panel.hpp"
#include "reader/document.hpp"
#include "reader/layout.hpp"
#include "reader/reader.hpp"
#include "rsvp/timing.hpp"

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace {

/// A real file on disk, removed when the test ends.
///
/// There are no mock writers in this project, so the encoder is checked by reading back
/// what it actually put on a filesystem.
class TempFile {
public:
    explicit TempFile(const char* name) : path_(name) { std::remove(path_.c_str()); }
    ~TempFile() { std::remove(path_.c_str()); }

    TempFile(const TempFile&) = delete;
    TempFile& operator=(const TempFile&) = delete;

    const char* path() const { return path_.c_str(); }

private:
    std::string path_;
};

std::vector<uint8_t> readFile(const char* path) {
    std::vector<uint8_t> bytes;
    std::FILE* f = std::fopen(path, "rb");
    if (f == nullptr) {
        return bytes;
    }
    uint8_t buf[4096];
    size_t got = 0;
    while ((got = std::fread(buf, 1, sizeof(buf), f)) > 0) {
        bytes.insert(bytes.end(), buf, buf + got);
    }
    std::fclose(f);
    return bytes;
}

/// What a GIF89a stream turned out to contain.
struct ParsedGif {
    bool walked = false;  ///< The block structure was followed cleanly to the trailer.
    bool signature = false;
    bool trailer = false;
    bool loops = false;
    uint16_t width = 0;
    uint16_t height = 0;
    /// Frame delays in hundredths of a second, one per image descriptor.
    std::vector<uint16_t> delays;
    std::vector<uint8_t> minCodeSizes;
    /// Each frame's LZW data with the sub-block framing removed.
    std::vector<std::vector<uint8_t>> imageData;
};

/// Walks the GIF block structure. Deliberately does not assume byte offsets: a parser
/// that seeks to a position the writer was assumed to use would pass on a file no viewer
/// could open.
ParsedGif parseGif(const std::vector<uint8_t>& b) {
    ParsedGif g;
    size_t p = 0;

    const auto want = [&](size_t n) { return p + n <= b.size(); };
    const auto le16 = [&]() {
        const uint16_t v = static_cast<uint16_t>(b[p] | (b[p + 1] << 8));
        p += 2;
        return v;
    };
    // Sub-blocks are length-prefixed runs ended by a zero length. Returns their contents.
    const auto subBlocks = [&](std::vector<uint8_t>* into) {
        while (want(1) && b[p] != 0) {
            const size_t n = b[p++];
            if (!want(n)) {
                return false;
            }
            if (into != nullptr) {
                into->insert(into->end(), b.begin() + static_cast<long>(p),
                             b.begin() + static_cast<long>(p + n));
            }
            p += n;
        }
        if (!want(1)) {
            return false;
        }
        ++p;  // the zero-length block that ends the run
        return true;
    };

    if (b.size() < 13 || std::memcmp(b.data(), "GIF89a", 6) != 0) {
        return g;
    }
    g.signature = true;
    g.trailer = b.back() == 0x3B;
    p = 6;
    g.width = le16();
    g.height = le16();
    const uint8_t packed = b[p];
    p += 3;  // packed fields, background colour index, pixel aspect ratio
    if ((packed & 0x80) != 0) {
        const size_t entries = size_t(1) << ((packed & 0x07) + 1);
        if (!want(entries * 3)) {
            return g;
        }
        p += entries * 3;
    }

    uint16_t pendingDelay = 0;
    while (want(1)) {
        const uint8_t block = b[p++];
        if (block == 0x3B) {
            g.walked = p == b.size();
            return g;
        }
        if (block == 0x21) {
            if (!want(1)) {
                return g;
            }
            const uint8_t label = b[p++];
            std::vector<uint8_t> body;
            if (!subBlocks(&body)) {
                return g;
            }
            if (label == 0xF9 && body.size() >= 4) {
                pendingDelay = static_cast<uint16_t>(body[1] | (body[2] << 8));
            } else if (label == 0xFF && body.size() >= 11 &&
                       std::memcmp(body.data(), "NETSCAPE2.0", 11) == 0) {
                g.loops = true;
            }
        } else if (block == 0x2C) {
            if (!want(9)) {
                return g;
            }
            const uint8_t imagePacked = b[p + 8];
            p += 9;
            if ((imagePacked & 0x80) != 0) {
                const size_t entries = size_t(1) << ((imagePacked & 0x07) + 1);
                if (!want(entries * 3)) {
                    return g;
                }
                p += entries * 3;
            }
            if (!want(1)) {
                return g;
            }
            g.minCodeSizes.push_back(b[p++]);
            std::vector<uint8_t> data;
            if (!subBlocks(&data)) {
                return g;
            }
            g.imageData.push_back(data);
            g.delays.push_back(pendingDelay);
            pendingDelay = 0;
        } else {
            return g;  // not a block type GIF89a defines
        }
    }
    return g;
}

/// Expands GIF LZW back into palette indices.
///
/// Present because "the bytes differ" is not evidence that an image is right. Decoding
/// is what catches a code-size increment that is off by one -- the encoder's easiest
/// mistake, and one that still produces a plausible-looking file.
bool lzwDecode(const std::vector<uint8_t>& data, uint8_t minCodeSize,
               std::vector<uint8_t>& out) {
    const uint16_t clear = static_cast<uint16_t>(1u << minCodeSize);
    const uint16_t eoi = static_cast<uint16_t>(clear + 1u);
    std::vector<std::vector<uint8_t>> table;
    uint8_t codeSize = static_cast<uint8_t>(minCodeSize + 1u);
    size_t bit = 0;

    const auto reset = [&]() {
        table.clear();
        for (uint16_t i = 0; i < clear; ++i) {
            table.push_back({static_cast<uint8_t>(i)});
        }
        table.push_back({});  // clear code
        table.push_back({});  // end of information
        codeSize = static_cast<uint8_t>(minCodeSize + 1u);
    };
    // Codes are packed least-significant-bit first and straddle byte boundaries.
    const auto readCode = [&](uint16_t& code) {
        uint32_t value = 0;
        for (uint8_t i = 0; i < codeSize; ++i) {
            const size_t index = (bit + i) / 8u;
            if (index >= data.size()) {
                return false;
            }
            if ((data[index] >> ((bit + i) % 8u)) & 1u) {
                value |= 1u << i;
            }
        }
        bit += codeSize;
        code = static_cast<uint16_t>(value);
        return true;
    };

    reset();
    int previous = -1;
    uint16_t code = 0;
    while (readCode(code)) {
        if (code == clear) {
            reset();
            previous = -1;
            continue;
        }
        if (code == eoi) {
            return true;
        }
        std::vector<uint8_t> entry;
        if (code < table.size()) {
            entry = table[code];
        } else if (previous >= 0 && code == table.size()) {
            entry = table[static_cast<size_t>(previous)];
            entry.push_back(entry[0]);
        } else {
            return false;
        }
        if (entry.empty()) {
            return false;
        }
        out.insert(out.end(), entry.begin(), entry.end());
        if (previous >= 0 && table.size() < 4096) {
            std::vector<uint8_t> added = table[static_cast<size_t>(previous)];
            added.push_back(entry[0]);
            table.push_back(added);
            if (table.size() == (size_t(1) << codeSize) && codeSize < 12) {
                ++codeSize;
            }
        }
        previous = code;
    }
    return false;  // ran out of bits before the end-of-information code
}

/// The palette indices a canvas should encode to, row by row from the top.
///
/// Index 0 is black and index 1 is white, which is the order sim::kBlack and sim::kWhite
/// already have, so a canvas pixel is its own palette index.
std::vector<uint8_t> canvasIndices(const sim::Canvas& canvas) {
    std::vector<uint8_t> indices;
    indices.reserve(size_t(canvas.width()) * size_t(canvas.height()));
    for (int y = 0; y < canvas.height(); ++y) {
        for (int x = 0; x < canvas.width(); ++x) {
            indices.push_back(canvas.pixel(x, y) == sim::kBlack ? 0u : 1u);
        }
    }
    return indices;
}

constexpr int kTestWidth = 40;
constexpr int kTestHeight = 24;

/// Marks a canvas with a black bar of the given width, so two frames can be made to
/// differ by pixels alone.
void drawBar(sim::Canvas& canvas, int barWidth) {
    canvas.fillScreen(sim::kWhite);
    canvas.fillRect(2, 4, barWidth, 6, sim::kBlack);
}

}  // namespace

TEST_CASE("a written animation is a GIF89a file with a trailer") {
    TempFile file("gif_signature.gif");
    sim::Canvas canvas(kTestWidth, kTestHeight);
    drawBar(canvas, 10);
    {
        sim::GifWriter gif(file.path(), kTestWidth, kTestHeight);
        REQUIRE(gif.ok());
        CHECK(gif.addFrame(canvas, 542));
        CHECK(gif.finish());
    }

    const std::vector<uint8_t> bytes = readFile(file.path());
    REQUIRE(bytes.size() > 13);
    const ParsedGif gif = parseGif(bytes);
    CHECK(gif.signature);
    CHECK(gif.trailer);
    CHECK(gif.walked);
    CHECK(gif.width == kTestWidth);
    CHECK(gif.height == kTestHeight);
}

TEST_CASE("looping is what the NETSCAPE2.0 extension is for") {
    sim::Canvas canvas(kTestWidth, kTestHeight);
    drawBar(canvas, 10);

    TempFile looping("gif_looping.gif");
    {
        sim::GifWriter gif(looping.path(), kTestWidth, kTestHeight, true);
        REQUIRE(gif.ok());
        CHECK(gif.addFrame(canvas, 542));
        CHECK(gif.finish());
    }
    const ParsedGif loopingGif = parseGif(readFile(looping.path()));
    CHECK(loopingGif.walked);
    CHECK(loopingGif.loops);

    // Without the extension a viewer stops on the last chunk, so the absence has to be
    // asserted too -- otherwise the check above only proves the parser finds something.
    TempFile once("gif_once.gif");
    {
        sim::GifWriter gif(once.path(), kTestWidth, kTestHeight, false);
        REQUIRE(gif.ok());
        CHECK(gif.addFrame(canvas, 542));
        CHECK(gif.finish());
    }
    const ParsedGif onceGif = parseGif(readFile(once.path()));
    CHECK(onceGif.walked);
    CHECK_FALSE(onceGif.loops);
}

TEST_CASE("every frame added comes back out as an image") {
    TempFile file("gif_frames.gif");
    sim::Canvas canvas(kTestWidth, kTestHeight);
    {
        sim::GifWriter gif(file.path(), kTestWidth, kTestHeight);
        REQUIRE(gif.ok());
        for (int i = 0; i < 7; ++i) {
            drawBar(canvas, 4 + i);
            CHECK(gif.addFrame(canvas, 500));
        }
        CHECK(gif.frames() == 7u);
        CHECK(gif.finish());
    }

    const ParsedGif gif = parseGif(readFile(file.path()));
    CHECK(gif.walked);
    CHECK(gif.imageData.size() == 7u);
    CHECK(gif.delays.size() == 7u);
}

TEST_CASE("frames that differ in pixels differ in image data") {
    TempFile file("gif_distinct.gif");
    sim::Canvas canvas(kTestWidth, kTestHeight);
    {
        sim::GifWriter gif(file.path(), kTestWidth, kTestHeight);
        REQUIRE(gif.ok());
        drawBar(canvas, 10);
        CHECK(gif.addFrame(canvas, 500));
        CHECK(gif.addFrame(canvas, 500));
        drawBar(canvas, 20);
        CHECK(gif.addFrame(canvas, 500));
        CHECK(gif.finish());
    }

    const ParsedGif gif = parseGif(readFile(file.path()));
    REQUIRE(gif.imageData.size() == 3u);
    CHECK(gif.imageData[0] == gif.imageData[1]);
    CHECK(gif.imageData[0] != gif.imageData[2]);
}

TEST_CASE("encoded pixels decode back to the canvas that was drawn") {
    TempFile file("gif_roundtrip.gif");
    sim::Canvas first(kTestWidth, kTestHeight);
    sim::Canvas second(kTestWidth, kTestHeight);
    drawBar(first, 9);
    drawBar(second, 31);
    {
        sim::GifWriter gif(file.path(), kTestWidth, kTestHeight);
        REQUIRE(gif.ok());
        CHECK(gif.addFrame(first, 542));
        CHECK(gif.addFrame(second, 542));
        CHECK(gif.finish());
    }

    const ParsedGif gif = parseGif(readFile(file.path()));
    REQUIRE(gif.walked);
    REQUIRE(gif.imageData.size() == 2u);
    REQUIRE(gif.minCodeSizes.size() == 2u);

    std::vector<uint8_t> decoded;
    REQUIRE(lzwDecode(gif.imageData[0], gif.minCodeSizes[0], decoded));
    CHECK(decoded == canvasIndices(first));

    decoded.clear();
    REQUIRE(lzwDecode(gif.imageData[1], gif.minCodeSizes[1], decoded));
    CHECK(decoded == canvasIndices(second));
}

TEST_CASE("a canvas of the wrong size is refused rather than written crookedly") {
    TempFile file("gif_mismatch.gif");
    sim::Canvas wrong(kTestWidth + 1, kTestHeight);
    sim::GifWriter gif(file.path(), kTestWidth, kTestHeight);
    REQUIRE(gif.ok());
    CHECK_FALSE(gif.addFrame(wrong, 542));
    CHECK(gif.frames() == 0u);
}

TEST_CASE("frame delays are the reading loop's own hold times") {
    // The point of the animation is pacing, so the delays are taken from a real reading
    // session rather than from numbers chosen to make the arithmetic tidy.
    const char* prose =
        "The panel refreshes slowly, and that turned out to decide everything. A partial "
        "update costs the same five hundred milliseconds whether it redraws a narrow "
        "band or the entire screen. So words arrive in threes.";

    sim::Panel panel(reader::kLandscape);
    reader::Document doc;
    doc.useMemory(prose, std::strlen(prose));
    reader::Reader r(doc, panel, reader::kLandscape);
    rsvp::TimingConfig timing{};
    timing.wpm = 330;
    timing.minHoldMs = rsvp::kPanelPartialRefreshMs;
    r.setTiming(timing);
    r.setPlaying(true);
    r.renderFull();

    TempFile file("gif_pacing.gif");
    std::vector<uint32_t> holds;
    {
        sim::GifWriter gif(file.path(), reader::kLandscape.width, reader::kLandscape.height);
        REQUIRE(gif.ok());
        reader::Frame frame{};
        while (holds.size() < 8 && r.step(frame)) {
            REQUIRE(gif.addFrame(panel.canvas(), frame.holdMs));
            holds.push_back(frame.holdMs);
        }
        CHECK(gif.finish());
    }
    REQUIRE(holds.size() == 8u);

    const ParsedGif gif = parseGif(readFile(file.path()));
    REQUIRE(gif.walked);
    REQUIRE(gif.delays.size() == holds.size());
    for (size_t i = 0; i < holds.size(); ++i) {
        // GIF measures delay in hundredths of a second, rounded to nearest.
        CHECK(gif.delays[i] == static_cast<uint16_t>((holds[i] + 5u) / 10u));
        // A real hold is around half a second, far above the ~2cs floor viewers clamp
        // to, so nothing here is at the mercy of that clamp.
        CHECK(gif.delays[i] > 2u);
    }
}

