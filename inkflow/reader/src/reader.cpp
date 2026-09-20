// ABOUTME: Implements the RSVP reading loop: chunk assembly, pivot positioning, the
// ABOUTME: full-versus-partial refresh policy, and rewind-by-sentence.

#include "reader/reader.hpp"

#include <cstdio>
#include <cstring>

namespace reader {

namespace {

/// Widest chunk the chunker will be asked for, in tokens.
///
/// The window handed to `rsvp::chunkLength` is a fixed stack array, so the chunk size is
/// bounded here rather than by whatever a caller puts in ChunkConfig.
constexpr uint32_t kMaxChunkTokens = 8u;

}  // namespace

class Reader::FullPainter : public Painter {
public:
    FullPainter(const Reader& reader, const Frame& frame) : reader_(reader), frame_(frame) {}

    void paint(Surface& s) const override {
        s.fill(Ink::kWhite);
        reader_.drawStatus(s);
        reader_.drawGuides(s);
        reader_.drawChunk(s, frame_);
        reader_.drawContext(s);
    }

private:
    const Reader& reader_;
    const Frame& frame_;
};

class Reader::BandPainter : public Painter {
public:
    BandPainter(const Reader& reader, const Frame& frame) : reader_(reader), frame_(frame) {}

    void paint(Surface& s) const override {
        s.fill(Ink::kWhite);
        reader_.drawChunk(s, frame_);
    }

private:
    const Reader& reader_;
    const Frame& frame_;
};

void Reader::setName(const char* name) {
    if (name == nullptr) {
        return;
    }
    strncpy(name_, name, sizeof(name_) - 1);
    name_[sizeof(name_) - 1] = '\0';
}

void Reader::seek(uint32_t index) {
    if (index < doc_.count()) {
        index_ = index;
    }
}

int16_t Reader::prefixWidth(const char* text, uint8_t chars) const {
    char buf[kMaxChunkBytes];
    size_t n = 0;
    for (const char* p = text; *p != '\0' && n < chars && n + 1 < sizeof(buf); ++p) {
        buf[n++] = *p;
    }
    buf[n] = '\0';
    return surface_.textWidth(Font::kChunk, buf);
}

uint32_t Reader::buildChunk(uint32_t start, Frame& out) const {
    out.text[0] = '\0';
    out.tokens = 0;
    out.holdMs = 0;
    out.fullRefresh = false;
    out.left = 0;
    out.right = 0;
    out.overflows = false;
    out.shifted = false;
    out.oversize = false;

    // Gather the window the chunker needs. A chunk is at most maxWords tokens, so this
    // stays tiny whether the source is RAM or a streamed file.
    rsvp::Token window[kMaxChunkTokens];
    uint32_t have = 0;
    const uint32_t limit =
        chunking_.maxWords < kMaxChunkTokens ? chunking_.maxWords : kMaxChunkTokens;
    for (uint32_t i = 0; i < limit; ++i) {
        const rsvp::Token* t = doc_.token(start + i);
        if (t == nullptr) {
            break;
        }
        window[have++] = *t;
    }
    if (have == 0) {
        return 0;
    }

    const uint32_t n = rsvp::chunkLength(window, have, 0u, chunking_);
    size_t len = 0;
    for (uint32_t i = 0; i < n; ++i) {
        if (i > 0 && len + 1 < sizeof(out.text)) {
            out.text[len++] = ' ';
        }
        char word[64];
        const size_t got = doc_.text(window[i].offset, window[i].length, word, sizeof(word));
        for (size_t b = 0; b < got && len + 1 < sizeof(out.text); ++b) {
            out.text[len++] = word[b];
        }
    }
    out.text[len] = '\0';
    out.tokens = n;

    // A chunk is held for the sum of what its tokens need, with the panel's refresh
    // floor applied once to that total.
    //
    // Not the maximum of the per-token holds: each of those is already floored, so the
    // floor would win every time and the maximum would always be the floor itself. That
    // discards every boundary pause the timing model computes and flattens the speed
    // control -- requesting 200 wpm and requesting 400 both deliver about 300.
    out.holdMs = rsvp::chunkHoldMs(window, have, 0u, n, timing_);

    // The pivot belongs to the chunk's first word, which is where the eye lands. The
    // offset is a reader's own adjustment to that, clamped so it can only ever name a
    // character position rather than run off either end of the word.
    int32_t pivot = static_cast<int32_t>(window[0].orp) + pivotOffset_;
    if (pivot < 0) {
        pivot = 0;
    }
    if (pivot > 255) {
        pivot = 255;
    }
    const uint8_t pivotChar = static_cast<uint8_t>(pivot);
    const int16_t prefix = prefixWidth(out.text, pivotChar);
    const int16_t chunkWidth = surface_.textWidth(Font::kChunk, out.text);

    // Where the chunk wants to be: pivot character on the focal column.
    const int16_t wanted = static_cast<int16_t>(layout_.focalX - prefix);
    out.oversize = chunkWidth > layout_.width;
    out.overflows = !out.oversize && (wanted < 0 || wanted + chunkWidth > layout_.width);

    // Keep the text on screen even when the pivot cannot be honoured. A word drawn with
    // its tail cut off is unreadable, whereas a word whose fixation point moved is merely
    // imperfect -- so sliding is the lesser failure, and reporting it keeps it visible.
    int16_t placed = wanted;
    if (!out.oversize) {
        const int16_t rightmost = static_cast<int16_t>(layout_.width - chunkWidth);
        if (placed > rightmost) {
            placed = rightmost;
        }
        if (placed < 0) {
            placed = 0;
        }
    } else if (placed > 0) {
        // Nothing fits; show the beginning rather than the middle.
        placed = 0;
    }
    out.shifted = placed != wanted;
    out.left = placed;
    out.right = static_cast<int16_t>(placed + chunkWidth);
    return n;
}

uint32_t Reader::peek(Frame& out) const { return buildChunk(index_, out); }

bool Reader::sentenceEndsAt(uint32_t index) const {
    const rsvp::Token* t = doc_.token(index);
    return t != nullptr && t->has(rsvp::kTokenFlagSentenceEnd);
}

size_t Reader::contextText(char* out, size_t cap) const {
    if (out == nullptr || cap == 0) {
        return 0;
    }
    out[0] = '\0';
    const uint32_t count = doc_.count();
    if (count == 0u || index_ >= count) {
        return 0;
    }

    const uint32_t first = sentenceStart(index_);
    const uint32_t last = sentenceEndAfter(index_);

    size_t len = 0;
    for (uint32_t i = first; i <= last && i < count; ++i) {
        const rsvp::Token* t = doc_.token(i);
        if (t == nullptr) {
            break;
        }
        if (len > 0 && len + 1 < cap) {
            out[len++] = ' ';
        }
        char word[64];
        const size_t got = doc_.text(t->offset, t->length, word, sizeof(word));
        for (size_t b = 0; b < got && len + 1 < cap; ++b) {
            out[len++] = word[b];
        }
        // Stop cleanly on a full buffer rather than emitting a half word at the end.
        if (len + 1 >= cap) {
            break;
        }
    }
    out[len] = '\0';
    return len;
}

uint32_t Reader::sentenceEndAfter(uint32_t from) const {
    const uint32_t count = doc_.count();
    for (uint32_t i = from; i < count; ++i) {
        if (sentenceEndsAt(i)) {
            return i;
        }
    }
    return count > 0u ? count - 1u : 0u;
}

uint32_t Reader::sentenceStart(uint32_t from) const {
    for (uint32_t i = from; i > 0u; --i) {
        if (sentenceEndsAt(i - 1u)) {
            return i;
        }
    }
    return 0u;
}

void Reader::rewindSentence() {
    const uint32_t start = sentenceStart(index_);
    if (start < index_) {
        index_ = start;
    } else if (index_ > 0u) {
        index_ = sentenceStart(index_ - 1u);
    }
}

void Reader::adjustSpeed(int delta) {
    int wpm = static_cast<int>(timing_.wpm) + delta;
    if (wpm < 60) {
        wpm = 60;
    }
    if (wpm > 900) {
        wpm = 900;
    }
    timing_.wpm = static_cast<uint16_t>(wpm);
}

void Reader::drawStatus(Surface& s) const {
    s.text(Font::kStatus, 20, 60, name_);
    const uint32_t total = doc_.count();
    const uint32_t pct = total != 0u ? (index_ * 100u) / total : 0u;
    char line[64];
    snprintf(line, sizeof(line), "%u wpm  %u%%  %s", static_cast<unsigned>(timing_.wpm),
             static_cast<unsigned>(pct), playing_ ? "" : "[paused]");
    s.text(Font::kStatus, 20, 95, line);
}

void Reader::drawGuides(Surface& s) const {
    // A colour highlight is the usual way to mark the recognition point, and a 1-bit
    // panel cannot do it. These ticks sit outside the band that gets redrawn, so they are
    // drawn once per full refresh and cost nothing per word.
    s.rect(static_cast<int16_t>(layout_.focalX - 1), static_cast<int16_t>(layout_.bandY - 26),
           3, 16, Ink::kBlack);
    s.rect(static_cast<int16_t>(layout_.focalX - 1),
           static_cast<int16_t>(layout_.bandY + layout_.bandH + 10), 3, 16, Ink::kBlack);
}

void Reader::drawContext(Surface& s) const {
    // Only while paused. During playback the space below the band stays empty on purpose:
    // anything drawn there would be a second thing for the eye to travel to, which is the
    // exact cost RSVP exists to remove.
    if (playing_) {
        return;
    }

    char sentence[320];
    const size_t len = contextText(sentence, sizeof(sentence));
    if (len == 0) {
        return;
    }

    constexpr int16_t kMargin = 20;
    constexpr int16_t kLineHeight = 30;
    const int16_t usable = static_cast<int16_t>(layout_.width - 2 * kMargin);
    int16_t y = static_cast<int16_t>(layout_.bandY + layout_.bandH + 50);

    // Greedy wrap, measured through the same glyph walk that positions the chunk, so the
    // break points are the ones the panel will actually produce rather than an estimate.
    char line[320];
    size_t lineLen = 0;

    size_t at = 0;
    while (at < len) {
        const size_t wordStart = at;
        while (at < len && sentence[at] != ' ') {
            ++at;
        }
        const size_t wordLen = at - wordStart;
        while (at < len && sentence[at] == ' ') {
            ++at;
        }
        if (wordLen == 0) {
            continue;
        }

        // Does this word still fit on the line being built?
        char candidate[320];
        size_t candidateLen = 0;
        for (size_t i = 0; i < lineLen; ++i) {
            candidate[candidateLen++] = line[i];
        }
        if (lineLen > 0 && candidateLen + 1 < sizeof(candidate)) {
            candidate[candidateLen++] = ' ';
        }
        for (size_t i = 0; i < wordLen && candidateLen + 1 < sizeof(candidate); ++i) {
            candidate[candidateLen++] = sentence[wordStart + i];
        }
        candidate[candidateLen] = '\0';

        if (lineLen > 0 && s.textWidth(Font::kStatus, candidate) > usable) {
            line[lineLen] = '\0';
            s.text(Font::kStatus, kMargin, y, line);
            y = static_cast<int16_t>(y + kLineHeight);
            if (y > layout_.height) {
                return;
            }
            lineLen = 0;
            for (size_t i = 0; i < wordLen && lineLen + 1 < sizeof(line); ++i) {
                line[lineLen++] = sentence[wordStart + i];
            }
        } else {
            lineLen = candidateLen;
            for (size_t i = 0; i < candidateLen; ++i) {
                line[i] = candidate[i];
            }
        }
    }

    if (lineLen > 0 && y <= layout_.height) {
        line[lineLen] = '\0';
        s.text(Font::kStatus, kMargin, y, line);
    }
}

void Reader::drawChunk(Surface& s, const Frame& frame) const {
    s.text(Font::kChunk, frame.left,
           static_cast<int16_t>(layout_.bandY + kChunkBaseline), frame.text);
}

void Reader::renderFull() {
    Frame frame{};
    buildChunk(index_, frame);
    const FullPainter painter(*this, frame);
    surface_.renderFull(painter);
    partialsSinceFlush_ = 0;
}

bool Reader::step(Frame& out) {
    if (buildChunk(index_, out) == 0u) {
        playing_ = false;
        return false;
    }

    bool atParagraph = false;
    bool atSentence = false;
    for (uint32_t i = 0; i < out.tokens; ++i) {
        const rsvp::Token* t = doc_.token(index_ + i);
        if (t == nullptr) {
            break;
        }
        atParagraph = atParagraph || t->has(rsvp::kTokenFlagParagraphEnd);
        atSentence = atSentence || t->has(rsvp::kTokenFlagSentenceEnd);
    }

    // Ghosting accrues over successive partial updates. Spend the 1958ms full refresh on
    // a paragraph boundary, where the timing model already inserts a beat, so it reads as
    // an intentional pause rather than a fault -- but do not wait for one forever. A
    // sentence will do once the count has run on, and past the deadline the refresh
    // happens wherever it falls.
    out.fullRefresh = (partialsSinceFlush_ >= kPartialsBeforeFlush && atParagraph) ||
                      (partialsSinceFlush_ >= kPartialsBeforeSentenceFlush && atSentence) ||
                      (partialsSinceFlush_ >= kPartialsFlushDeadline);
    if (out.fullRefresh) {
        const FullPainter painter(*this, out);
        surface_.renderFull(painter);
        partialsSinceFlush_ = 0;
    } else {
        const BandPainter painter(*this, out);
        surface_.renderBand(layout_.bandY, layout_.bandH, painter);
        ++partialsSinceFlush_;
    }

    index_ += out.tokens;
    return true;
}

}  // namespace reader
