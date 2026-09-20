// ABOUTME: Implements Document -- sidecar streaming with an aligned token cache, and the
// ABOUTME: in-RAM tokenised fallback for a short text file.

#include "reader/document.hpp"

namespace reader {

bool Document::openSidecar(ByteSource& source) {
    close();
    if (!source.valid()) {
        return false;
    }
    source_ = &source;
    if (!source_->seek(0)) {
        source_ = nullptr;
        return false;
    }
    uint8_t head[rsvp::kRsvpHeaderSize];
    if (source_->read(head, sizeof(head)) != sizeof(head)) {
        source_ = nullptr;
        return false;
    }
    // The header declares the payload sizes, so validate against the real file
    // length rather than trusting them.
    if (rsvp::readRsvpHeader(head, source_->size(), header_) != rsvp::RsvpStatus::kOk) {
        source_ = nullptr;
        return false;
    }
    streaming_ = true;
    cacheStart_ = kNoCache;
    return true;
}

void Document::useMemory(const char* text, size_t length) {
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

void Document::close() {
    source_ = nullptr;
    streaming_ = false;
    cacheStart_ = kNoCache;
}

const rsvp::Token* Document::token(uint32_t index) {
    if (index >= count()) {
        return nullptr;
    }
    if (!streaming_) {
        return &ramTokens_[index];
    }
    const uint32_t block = (index / kTokenWindow) * kTokenWindow;
    if (block != cacheStart_) {
        const uint32_t remaining = header_.tokenCount - block;
        const uint32_t want = kTokenWindow < remaining ? kTokenWindow : remaining;
        const uint32_t at =
            header_.headerSize + block * static_cast<uint32_t>(sizeof(rsvp::Token));
        if (!source_->seek(at)) {
            return nullptr;
        }
        const size_t bytes = want * sizeof(rsvp::Token);
        if (source_->read(reinterpret_cast<uint8_t*>(cache_), bytes) != bytes) {
            return nullptr;
        }
        cacheStart_ = block;
        cacheCount_ = want;
    }
    const uint32_t offset = index - cacheStart_;
    return offset < cacheCount_ ? &cache_[offset] : nullptr;
}

size_t Document::text(uint32_t offset, uint16_t length, char* out, size_t cap) {
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
        // Bounded by what the header says the text blob is, exactly as the RAM branch is
        // bounded by ramLen_. Relying on the file running out instead works only while
        // the blob is the last thing in the file, which is a property of the writer
        // rather than of the format -- and the reader is about to start accepting files
        // from anything that can reach its access point.
        if (offset >= header_.textLength) {
            want = 0;
        } else {
            const uint32_t available = header_.textLength - offset;
            if (want > available) {
                want = available;
            }
            if (!source_->seek(header_.textOffset + offset)) {
                want = 0;
            } else {
                want = source_->read(reinterpret_cast<uint8_t*>(out), want);
            }
        }
    }
    out[want] = '\0';
    return want;
}

}  // namespace reader
