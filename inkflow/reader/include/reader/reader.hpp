// ABOUTME: The RSVP reading loop -- chunking, pacing, refresh policy and navigation, with
// ABOUTME: no panel, no card and no clock, so the device and the simulator run one copy.

#pragma once

#include <cstdint>

#include "reader/document.hpp"
#include "reader/layout.hpp"
#include "reader/surface.hpp"
#include "rsvp/chunker.hpp"
#include "rsvp/timing.hpp"

namespace reader {

/// Longest chunk the reader will assemble, in bytes.
///
/// A chunk is at most a handful of words and the panel fits about 20 characters, so this
/// is generous. It is a fixed buffer because the engine allocates nothing.
inline constexpr size_t kMaxChunkBytes = 96u;

/// What one presented chunk turned out to be.
///
/// Returned rather than kept private so a caller can observe pacing and geometry without
/// a panel attached. This is what makes the reading experience testable on a host: the
/// simulator asserts on these, and the device simply honours `holdMs`.
struct Frame {
    char text[kMaxChunkBytes];
    /// Number of tokens presented.
    uint32_t tokens;
    /// How long this chunk should stay on screen, refresh time included.
    uint32_t holdMs;
    /// True when this frame was drawn with a full refresh rather than a band update.
    bool fullRefresh;
    /// Leftmost and rightmost pixel the chunk occupies as drawn.
    int16_t left;
    int16_t right;
    /// True when the chunk could not be placed with its pivot on the focal column
    /// without running off the screen. The geometry is wrong, not the text.
    bool overflows;
    /// True when the chunk had to be slid off the focal column to stay on screen.
    ///
    /// Distinct from `overflows`: the text is fully visible, but the fixation point
    /// moved, which is the one thing RSVP is supposed to hold still. Rare, and always
    /// preferable to drawing a word with its tail cut off.
    bool shifted;
    /// True when the chunk is wider than the whole screen.
    ///
    /// No position can show it. This is a property of the text -- a 42-character URL, a
    /// table rule that survived conversion -- not of the layout, so it is reported
    /// separately rather than counted as a layout fault.
    bool oversize;
};

/// Drives an RSVP reading session over a document and a surface.
class Reader {
public:
    Reader(Document& doc, Surface& surface, const Layout& layout)
        : doc_(doc), surface_(surface), layout_(layout) {}

    /// Names the document on the status line. Not the file path -- what the reader sees.
    void setName(const char* name);

    void setTiming(const rsvp::TimingConfig& timing) { timing_ = timing; }
    const rsvp::TimingConfig& timing() const { return timing_; }

    void setChunking(const rsvp::ChunkConfig& chunking) { chunking_ = chunking; }
    const rsvp::ChunkConfig& chunking() const { return chunking_; }

    uint32_t index() const { return index_; }
    bool playing() const { return playing_; }
    bool atEnd() const { return index_ >= doc_.count(); }
    uint32_t partialsSinceFlush() const { return partialsSinceFlush_; }

    /// Resumes at `index`, ignoring a position past the end of this document.
    void seek(uint32_t index);

    void togglePlay() { playing_ = !playing_; }
    void setPlaying(bool playing) { playing_ = playing; }

    /// Rewind is the feature, not a convenience -- suppressing the backward glance is
    /// the one thing RSVP inherently does to a reader, and it measurably costs
    /// comprehension. Pressing again while already at a sentence start steps into the
    /// previous sentence, so repeated presses walk backwards instead of sticking.
    void rewindSentence();

    void adjustSpeed(int delta);

    /// Shifts the recognition point within the chunk's first word, in characters.
    ///
    /// The banded ORP heuristic is a reasonable default, not a law -- where the eye
    /// settles varies between readers, and the literature does not pin it precisely. A
    /// signed nudge is cheap and lets a reader tune it rather than argue with it.
    void setPivotOffset(int8_t characters) { pivotOffset_ = characters; }
    int8_t pivotOffset() const { return pivotOffset_; }

    /// Copies the sentence the reader is currently inside into `out`, NUL-terminated.
    ///
    /// Returns the number of bytes written. Shown while paused: pausing is what a reader
    /// does when they have lost the thread, and the sentence they are inside is a cheaper
    /// answer than rewinding through it. Complements rewind rather than replacing it.
    size_t contextText(char* out, size_t cap) const;

    /// Redraws everything, clearing ghosting. Use after any state change the status line
    /// shows, and on the first draw.
    void renderFull();

    /// Draws the screen the device shows while switched off.
    ///
    /// E-paper holds its image with no power, so an off device shows whatever was drawn
    /// last -- which makes "off" and "paused mid-sentence" look identical. This is the one
    /// screen whose job is to be unmistakable at a glance.
    void renderSleep();

    /// Presents the next chunk and advances past it.
    ///
    /// Returns false at the end of the document, having drawn nothing. The caller owns
    /// the waiting: `out.holdMs` is how long the chunk should remain visible in total,
    /// and the refresh has already consumed part of it.
    bool step(Frame& out);

    /// Builds the chunk at the current position without drawing or advancing.
    uint32_t peek(Frame& out) const;

private:
    uint32_t buildChunk(uint32_t start, Frame& out) const;
    int16_t prefixWidth(const char* text, uint8_t chars) const;
    bool sentenceEndsAt(uint32_t index) const;
    uint32_t sentenceStart(uint32_t from) const;
    uint32_t sentenceEndAfter(uint32_t from) const;
    void drawStatus(Surface& s) const;
    void drawGuides(Surface& s) const;
    void drawContext(Surface& s) const;
    void drawSleep(Surface& s) const;
    void drawChunk(Surface& s, const Frame& frame) const;

    // Painters are nested so a frame's contents can be redrawn on demand: GxEPD2 walks
    // the framebuffer in pages and calls back once per page.
    class FullPainter;
    class BandPainter;
    class SleepPainter;

    Document& doc_;
    Surface& surface_;
    Layout layout_;

    rsvp::TimingConfig timing_{};
    rsvp::ChunkConfig chunking_{};

    uint32_t index_ = 0;
    int8_t pivotOffset_ = 0;
    bool playing_ = false;
    uint32_t partialsSinceFlush_ = 0;
    char name_[64] = "built-in";
};

}  // namespace reader
