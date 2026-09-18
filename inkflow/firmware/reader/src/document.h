// ABOUTME: Document access for the reader -- either a .rsvp sidecar streamed from SD,
// ABOUTME: or a small text file tokenised into RAM. One interface over both.

#pragma once

#include <FS.h>
#include <SD.h>

#include "config.h"
#include "rsvp/format.hpp"
#include "rsvp/token.hpp"
#include "rsvp/tokenizer.hpp"

/// Reads tokens and their text without assuming the document fits in memory.
///
/// A real book is far past what 400 KB of SRAM can hold -- the test volume is 98,667
/// tokens over 643 KB of text, a 1.4 MB sidecar. The `.rsvp` format exists precisely so
/// the device can seek into a flat array instead of holding one, and this is the piece
/// that takes advantage of it.
class Document {
public:
    /// Opens a `.rsvp` sidecar and streams from it. Nothing is read up front beyond the
    /// header.
    bool openSidecar(const char* path) {
        close();
        file_ = SD.open(path, FILE_READ);
        if (!file_) {
            return false;
        }
        uint8_t head[rsvp::kRsvpHeaderSize];
        if (file_.read(head, sizeof(head)) != sizeof(head)) {
            close();
            return false;
        }
        // The header declares the payload sizes, so validate against the real file
        // length rather than trusting them.
        if (rsvp::readRsvpHeader(head, file_.size(), header_) != rsvp::RsvpStatus::kOk) {
            close();
            return false;
        }
        streaming_ = true;
        cacheStart_ = kNoCache;
        return true;
    }

    /// Tokenises `text` into RAM. For short documents and the built-in passage.
    void useMemory(const char* text, size_t length) {
        close();
        ramText_ = text;
        ramLen_ = length;
        rsvp::Tokenizer t(text, length);
        rsvp::Token tok{};
        ramCount_ = 0;
        while (ramCount_ < kMaxTokens && t.next(tok)) {
            ramTokens_[ramCount_++] = tok;
        }
        streaming_ = false;
    }

    void close() {
        if (file_) {
            file_.close();
        }
        streaming_ = false;
        cacheStart_ = kNoCache;
    }

    uint32_t count() const { return streaming_ ? header_.tokenCount : ramCount_; }

    /// Returns the token at `index`, or nullptr when out of range.
    ///
    /// Streaming reads are cached in aligned blocks. Rewinding scans backwards through
    /// tokens, so an unaligned window would thrash at every step; aligned blocks reload
    /// at most once per block crossed, and rewind is user-initiated rather than in the
    /// per-word path.
    const rsvp::Token* token(uint32_t index) {
        if (index >= count()) {
            return nullptr;
        }
        if (!streaming_) {
            return &ramTokens_[index];
        }
        const uint32_t block = (index / kTokenWindow) * kTokenWindow;
        if (block != cacheStart_) {
            const uint32_t want = min(kTokenWindow, header_.tokenCount - block);
            const uint32_t at = header_.headerSize + block * sizeof(rsvp::Token);
            if (!file_.seek(at)) {
                return nullptr;
            }
            const size_t bytes = want * sizeof(rsvp::Token);
            if (file_.read(reinterpret_cast<uint8_t*>(cache_), bytes) != bytes) {
                return nullptr;
            }
            cacheStart_ = block;
            cacheCount_ = want;
        }
        const uint32_t offset = index - cacheStart_;
        return offset < cacheCount_ ? &cache_[offset] : nullptr;
    }

    /// Copies a token's bytes into `out`, NUL-terminated. Returns bytes written.
    size_t text(uint32_t offset, uint16_t length, char* out, size_t cap) {
        if (cap == 0) {
            return 0;
        }
        size_t want = length < cap - 1 ? length : cap - 1;
        if (!streaming_) {
            if (offset + want > ramLen_) {
                want = ramLen_ > offset ? ramLen_ - offset : 0;
            }
            memcpy(out, ramText_ + offset, want);
        } else {
            if (!file_.seek(header_.textOffset + offset)) {
                want = 0;
            } else {
                want = file_.read(reinterpret_cast<uint8_t*>(out), want);
            }
        }
        out[want] = '\0';
        return want;
    }

    bool streaming() const { return streaming_; }

private:
    // 256 tokens is 2 KB -- enough that sequential reading almost never touches the
    // card, and small enough to be irrelevant against the framebuffer.
    static constexpr uint32_t kTokenWindow = 256u;
    static constexpr uint32_t kNoCache = 0xFFFFFFFFu;

    File file_;
    rsvp::RsvpHeader header_{};
    bool streaming_ = false;

    rsvp::Token cache_[kTokenWindow];
    uint32_t cacheStart_ = kNoCache;
    uint32_t cacheCount_ = 0;

    const char* ramText_ = nullptr;
    size_t ramLen_ = 0;
    rsvp::Token ramTokens_[kMaxTokens];
    uint32_t ramCount_ = 0;
};
