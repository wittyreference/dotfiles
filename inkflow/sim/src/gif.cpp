// ABOUTME: Animated GIF89a encoder for the simulator, including the LZW coder GIF needs
// ABOUTME: and zlib cannot supply, writing 1-bit frames against a two-colour palette.

#include "gif.hpp"

#include <algorithm>
#include <vector>

namespace sim {
namespace {

/// GIF's smallest legal LZW code size, and what a two-colour image uses.
///
/// The format floors it at 2 even for a 1-bit image, so the code space starts four roots
/// wide: 0 and 1 are the palette, 4 is clear, 5 is end-of-information, and the first
/// dictionary entry is 6.
constexpr uint8_t kMinCodeSize = 2u;
constexpr uint16_t kClearCode = 1u << kMinCodeSize;
constexpr uint16_t kEndCode = kClearCode + 1u;
/// 12 bits is the format's ceiling; past it the dictionary must be cleared and rebuilt.
constexpr uint16_t kMaxCodes = 4096u;
constexpr uint8_t kMaxCodeSize = 12u;
constexpr size_t kMaxSubBlock = 255u;

/// Collects LZW codes and hands back GIF image data.
///
/// Two framings at once: codes are packed least-significant-bit first and straddle byte
/// boundaries, and the resulting bytes are then cut into length-prefixed sub-blocks of at
/// most 255. Keeping both here is what lets the coder below talk in codes alone.
class CodeStream {
public:
    void push(uint16_t code, uint8_t bits) {
        pending_ |= static_cast<uint32_t>(code) << count_;
        count_ = static_cast<uint8_t>(count_ + bits);
        while (count_ >= 8u) {
            emit(static_cast<uint8_t>(pending_ & 0xFFu));
            pending_ >>= 8;
            count_ = static_cast<uint8_t>(count_ - 8u);
        }
    }

    /// Flushes the partial byte and closes the sub-block run.
    void finish() {
        if (count_ > 0u) {
            emit(static_cast<uint8_t>(pending_ & 0xFFu));
            pending_ = 0;
            count_ = 0;
        }
        flushBlock();
        out_.push_back(0u);  // the zero-length block that ends image data
    }

    const std::vector<uint8_t>& bytes() const { return out_; }

private:
    void emit(uint8_t byte) {
        block_.push_back(byte);
        if (block_.size() == kMaxSubBlock) {
            flushBlock();
        }
    }

    void flushBlock() {
        if (block_.empty()) {
            return;
        }
        out_.push_back(static_cast<uint8_t>(block_.size()));
        out_.insert(out_.end(), block_.begin(), block_.end());
        block_.clear();
    }

    std::vector<uint8_t> out_;
    std::vector<uint8_t> block_;
    uint32_t pending_ = 0;
    uint8_t count_ = 0;
};

/// LZW-compresses a canvas into GIF image data.
///
/// The dictionary is a flat array indexed by (prefix, symbol) rather than a hash table.
/// The symbol alphabet is four wide, so the whole thing is 32 KB on a host that has
/// gigabytes, and an exact index cannot collide or need a probe sequence.
std::vector<uint8_t> encode(const Canvas& canvas) {
    CodeStream stream;
    std::vector<uint16_t> table(size_t(kMaxCodes) * kClearCode, 0u);
    uint16_t next = static_cast<uint16_t>(kClearCode + 2u);
    uint8_t codeSize = static_cast<uint8_t>(kMinCodeSize + 1u);
    stream.push(kClearCode, codeSize);

    bool started = false;
    uint16_t prefix = 0;
    for (int y = 0; y < canvas.height(); ++y) {
        for (int x = 0; x < canvas.width(); ++x) {
            // The palette is ordered so kBlack and kWhite are their own indices.
            const uint16_t symbol = canvas.pixel(x, y) == kBlack ? uint16_t(0) : uint16_t(1);
            if (!started) {
                prefix = symbol;
                started = true;
                continue;
            }
            const size_t slot = size_t(prefix) * kClearCode + symbol;
            if (table[slot] != 0u) {
                prefix = table[slot];
                continue;
            }
            stream.push(prefix, codeSize);
            if (next == kMaxCodes) {
                stream.push(kClearCode, codeSize);
                std::fill(table.begin(), table.end(), uint16_t(0));
                next = static_cast<uint16_t>(kClearCode + 2u);
                codeSize = static_cast<uint8_t>(kMinCodeSize + 1u);
            } else {
                table[slot] = next++;
                // A decoder adds its entry one code later than the encoder does, so it
                // widens one entry later too. Widening at `==` here instead of `>` would
                // desynchronise the two and produce a file that decodes to noise.
                if (next > (1u << codeSize) && codeSize < kMaxCodeSize) {
                    ++codeSize;
                }
            }
            prefix = symbol;
        }
    }

    if (started) {
        stream.push(prefix, codeSize);
    }
    stream.push(kEndCode, codeSize);
    stream.finish();
    return stream.bytes();
}

}  // namespace

GifWriter::GifWriter(const char* path, int width, int height, bool loop)
    : width_(width), height_(height) {
    if (width <= 0 || height <= 0 || width > 0xFFFF || height > 0xFFFF) {
        failed_ = true;
        return;
    }
    file_ = std::fopen(path, "wb");
    if (file_ == nullptr) {
        failed_ = true;
        return;
    }

    put("GIF89a", 6);
    putLe16(static_cast<uint16_t>(width_));
    putLe16(static_cast<uint16_t>(height_));
    // Global colour table present, eight bits per primary, two entries.
    put(uint8_t(0xF0));
    put(uint8_t(kWhite));  // background: an unwritten panel is white
    put(uint8_t(0));       // pixel aspect ratio: square

    // Ordered so a canvas pixel is already its own palette index.
    static const uint8_t kPalette[6] = {0x00, 0x00, 0x00, 0xFF, 0xFF, 0xFF};
    put(kPalette, sizeof(kPalette));

    if (loop) {
        // Without this a viewer stops on the last chunk. A reading session is meant to be
        // watched repeatedly, at a pace nobody has seen yet.
        put("\x21\xFF\x0BNETSCAPE2.0\x03\x01", 16);
        putLe16(0);       // repeat forever
        put(uint8_t(0));  // end of the extension's sub-blocks
    }
}

GifWriter::~GifWriter() {
    if (!finished_) {
        finish();
    }
}

bool GifWriter::addFrame(const Canvas& canvas, uint32_t holdMs) {
    if (failed_ || finished_ || file_ == nullptr) {
        return false;
    }
    if (canvas.width() != width_ || canvas.height() != height_) {
        return false;
    }

    // GIF counts delay in hundredths of a second, so the reader's milliseconds are
    // rounded to the nearest centisecond. Most viewers clamp anything below about 2cs to
    // a default rate, which would silently replace the real pace with a made-up one --
    // but a partial refresh alone is 542ms, 54cs, so a real hold is never near the floor.
    // The clamp is floored at 1 anyway, because a zero delay means "as fast as possible".
    uint32_t centiseconds = (holdMs + 5u) / 10u;
    if (centiseconds == 0u) {
        centiseconds = 1u;
    }
    if (centiseconds > 0xFFFFu) {
        centiseconds = 0xFFFFu;
    }
    const uint16_t delay = static_cast<uint16_t>(centiseconds);

    put(uint8_t(0x21));  // extension introducer
    put(uint8_t(0xF9));  // graphic control label
    put(uint8_t(4));     // block size
    // Disposal method 1, leave the frame in place. Every frame carries the whole screen,
    // so there is nothing underneath that would need restoring.
    put(uint8_t(0x04));
    putLe16(delay);
    put(uint8_t(0));  // transparent colour index, unused
    put(uint8_t(0));  // end of sub-blocks

    put(uint8_t(0x2C));  // image descriptor
    putLe16(0);
    putLe16(0);
    putLe16(static_cast<uint16_t>(width_));
    putLe16(static_cast<uint16_t>(height_));
    put(uint8_t(0));  // no local colour table, not interlaced

    put(kMinCodeSize);
    encodeImageData(canvas);

    if (failed_) {
        return false;
    }
    ++frames_;
    return true;
}

bool GifWriter::finish() {
    if (finished_) {
        return !failed_;
    }
    finished_ = true;
    put(uint8_t(0x3B));  // trailer
    if (file_ != nullptr) {
        if (std::fclose(file_) != 0) {
            failed_ = true;
        }
        file_ = nullptr;
    }
    return !failed_;
}

void GifWriter::put(uint8_t byte) { put(&byte, 1); }

void GifWriter::putLe16(uint16_t value) {
    const uint8_t bytes[2] = {static_cast<uint8_t>(value & 0xFFu),
                              static_cast<uint8_t>(value >> 8)};
    put(bytes, sizeof(bytes));
}

void GifWriter::put(const void* data, size_t n) {
    if (failed_ || file_ == nullptr) {
        return;
    }
    if (std::fwrite(data, 1, n, file_) != n) {
        failed_ = true;
    }
}

void GifWriter::encodeImageData(const Canvas& canvas) {
    const std::vector<uint8_t> data = encode(canvas);
    put(data.data(), data.size());
}

}  // namespace sim
