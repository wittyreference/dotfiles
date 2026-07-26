// ABOUTME: Streaming, allocation-free tokenizer that slices UTF-8 text into Tokens.
// ABOUTME: Pull-based so it can run over a fixed read buffer on a device with 380 KB RAM.

#ifndef RSVP_TOKENIZER_HPP
#define RSVP_TOKENIZER_HPP

#include "rsvp/token.hpp"

#include <cstddef>

namespace rsvp {

/// Splits text into display tokens, one at a time, without allocating.
///
/// Pull-based rather than returning a container: on the device this runs over a
/// fixed-size buffer read from the SD card, and there is no heap to put a vector
/// on. The caller owns the text; the Tokenizer only holds a pointer into it and
/// never copies or outlives it.
///
/// Tokens are whitespace-delimited runs with punctuation left attached -- RSVP
/// shows the word as written, because the comma *is* the cue that a clause ended.
class Tokenizer {
public:
    /// Constructs a tokenizer over `[text, text + length)`.
    ///
    /// `text` must outlive the tokenizer. A null `text` is treated as empty,
    /// so callers do not need a special case for it.
    Tokenizer(const char* text, std::size_t length) noexcept;

    /// Writes the next token to `out` and returns true; returns false at end of
    /// input, leaving `out` untouched.
    bool next(Token& out) noexcept;

    /// Byte offset of the next unconsumed character. Lets a caller resume
    /// tokenizing a later buffer without re-scanning what it already emitted.
    std::size_t position() const noexcept { return pos_; }

private:
    const char* text_;
    std::size_t length_;
    std::size_t pos_;
};

}  // namespace rsvp

#endif  // RSVP_TOKENIZER_HPP
