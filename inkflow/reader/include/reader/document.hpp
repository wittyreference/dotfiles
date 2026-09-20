// ABOUTME: Document access for the reader -- either a .rsvp sidecar streamed from a byte
// ABOUTME: source, or a small text file tokenised into RAM. One interface over both.

#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>

#include "reader/source.hpp"
#include "rsvp/format.hpp"
#include "rsvp/token.hpp"
#include "rsvp/tokenizer.hpp"

namespace reader {

// Document limits.
//
// The device has 400 KB of SRAM with no PSRAM, and a full 800x480 framebuffer already
// costs 48 KB. These caps keep text plus its token index inside what remains.
// These bound only the in-RAM fallback for a small .txt. A .rsvp sidecar streams and
// is not limited by them -- the test book is 98,633 tokens, forty-nine times this cap.
// Kept small deliberately: the WiFi stack needs the space more than a fallback path
// does, and anything long enough to care should be a sidecar.
inline constexpr size_t kMaxTextBytes = 16u * 1024u;
inline constexpr size_t kMaxTokens = 2000u;

/// Reads tokens and their text without assuming the document fits in memory.
///
/// A real book is far past what 400 KB of SRAM can hold -- the test volume is 98,633
/// tokens over 643 KB of text, a 1.4 MB sidecar. The `.rsvp` format exists precisely so
/// the device can seek into a flat array instead of holding one, and this is the piece
/// that takes advantage of it.
class Document {
public:
    /// Streams from an already-opened `.rsvp` sidecar. Nothing is read up front beyond
    /// the header. The source must outlive the document.
    bool openSidecar(ByteSource& source);

    /// Tokenises `text` into RAM. For short documents and the built-in passage.
    void useMemory(const char* text, size_t length);

    void close();

    uint32_t count() const { return streaming_ ? header_.tokenCount : ramCount_; }

    /// Returns the token at `index`, or nullptr when out of range.
    ///
    /// Streaming reads are cached in aligned blocks. Rewinding scans backwards through
    /// tokens, so an unaligned window would thrash at every step; aligned blocks reload
    /// at most once per block crossed, and rewind is user-initiated rather than in the
    /// per-word path.
    const rsvp::Token* token(uint32_t index);

    /// Copies a token's bytes into `out`, NUL-terminated. Returns bytes written.
    size_t text(uint32_t offset, uint16_t length, char* out, size_t cap);

    bool streaming() const { return streaming_; }

private:
    // 256 tokens is 2 KB -- enough that sequential reading almost never touches the
    // card, and small enough to be irrelevant against the framebuffer.
    static constexpr uint32_t kTokenWindow = 256u;
    static constexpr uint32_t kNoCache = 0xFFFFFFFFu;

    ByteSource* source_ = nullptr;
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

}  // namespace reader
