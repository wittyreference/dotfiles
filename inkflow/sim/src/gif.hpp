// ABOUTME: An animated GIF89a writer for the simulator's 1-bit frames, so a reading
// ABOUTME: session can be watched at its real pace instead of read aloud off a device.

#pragma once

#include <cstddef>
#include <cstdint>
#include <cstdio>

#include "canvas.hpp"

namespace sim {

/// Streams Canvas frames into an animated GIF.
///
/// GIF rather than a video container because it needs no encoder, no dependency and no
/// player: it opens in a browser. The panel is 1-bit, so a two-entry palette covers it
/// exactly and the encoder stays small enough to read in one sitting.
///
/// Frames are written as they arrive rather than collected first. A whole book is tens
/// of thousands of chunks, and holding that many 800x480 framebuffers is not something
/// the simulator should do to a laptop on the way to producing a file.
class GifWriter {
public:
    /// Opens `path` and writes the header. Check `ok()` before adding frames.
    ///
    /// `loop` writes the NETSCAPE2.0 application extension, which is what makes a viewer
    /// repeat the animation rather than freeze on the last chunk.
    GifWriter(const char* path, int width, int height, bool loop = true);

    ~GifWriter();

    GifWriter(const GifWriter&) = delete;
    GifWriter& operator=(const GifWriter&) = delete;

    bool ok() const { return file_ != nullptr && !failed_; }

    /// Appends `canvas`, held for `holdMs`.
    ///
    /// `holdMs` is the frame's real hold time from the reading loop, not a playback
    /// speed chosen here. Pacing is the one thing about this reader nobody has been able
    /// to confirm, so an animation that ran at any other rate would be worse than none.
    ///
    /// The whole screen is encoded every frame. The reading band is a narrow strip of a
    /// mostly-white panel and a diffed GIF would be smaller, but the file is already
    /// trivial at this bit depth and a frame that shows the whole screen is a frame that
    /// can be trusted.
    ///
    /// Returns false if the canvas is the wrong size or the file has already failed.
    bool addFrame(const Canvas& canvas, uint32_t holdMs);

    /// Writes the trailer and closes. Called by the destructor if it has not been.
    bool finish();

    uint32_t frames() const { return frames_; }

private:
    void put(uint8_t byte);
    void putLe16(uint16_t value);
    void put(const void* data, size_t n);

    /// LZW-compresses one frame's palette indices into GIF image data sub-blocks.
    ///
    /// GIF predates deflate and is not zlib-compatible, so this is written out rather
    /// than borrowed from the zlib the PNG writer already links.
    void encodeImageData(const Canvas& canvas);

    std::FILE* file_ = nullptr;
    bool failed_ = false;
    bool finished_ = false;
    int width_ = 0;
    int height_ = 0;
    uint32_t frames_ = 0;
};

}  // namespace sim
