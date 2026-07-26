// ABOUTME: The Token value type -- a slice of source text plus the boundary
// ABOUTME: classification and pivot index that RSVP playback needs to display it.

#ifndef RSVP_TOKEN_HPP
#define RSVP_TOKEN_HPP

#include <cstdint>

namespace rsvp {

/// Boundary and content classification for a token.
///
/// These drive the timing model: a reader needs a longer beat after a full stop
/// than after a comma, and a longer one still at a paragraph break. Storing the
/// classification rather than re-deriving it at playback keeps the hot loop free
/// of text inspection, which matters on a 160 MHz single core.
enum TokenFlags : std::uint8_t {
    kTokenFlagNone = 0,

    /// Token ends a clause -- comma, semicolon, colon, dash.
    kTokenFlagClauseEnd = 1u << 0,

    /// Token ends a sentence -- period, question mark, exclamation mark.
    kTokenFlagSentenceEnd = 1u << 1,

    /// Token is the last one in its paragraph.
    kTokenFlagParagraphEnd = 1u << 2,

    /// Token contains digits. Numerals read slower than words of the same
    /// length, so the timing model gives them extra time.
    kTokenFlagNumeric = 1u << 3,

    /// Token holds characters outside ASCII, so byte length != character count.
    /// Lets the renderer skip UTF-8 decoding for the common all-ASCII case.
    kTokenFlagMultibyte = 1u << 4,
};

/// One unit of RSVP display: a word, with its punctuation still attached.
///
/// Deliberately 8 bytes and trivially copyable. A token array is the bulk of an
/// `.rsvp` sidecar file, so width here is file size on the SD card, and the type
/// has to be memcpy-able straight out of a read buffer.
///
/// The token does *not* carry a duration. Duration is a function of the token
/// plus the reader's current words-per-minute setting, and WPM changes at
/// runtime -- baking durations into the file would mean regenerating it every
/// time the reader nudged their speed.
struct Token {
    /// Byte offset of the token's first byte within the source text.
    std::uint32_t offset;

    /// Length of the token in bytes.
    std::uint16_t length;

    /// Index of the pivot character -- the optimal recognition point -- counted
    /// in characters, not bytes. Playback aligns this character to the screen's
    /// fixed focal column so the eye never has to travel.
    std::uint8_t orp;

    /// Bitwise OR of TokenFlags values.
    std::uint8_t flags;

    /// True when any of `mask`'s bits are set.
    constexpr bool has(std::uint8_t mask) const noexcept {
        return (flags & mask) != 0u;
    }
};

static_assert(sizeof(Token) == 8, "Token must stay 8 bytes: it is the on-disk record width");

}  // namespace rsvp

#endif  // RSVP_TOKEN_HPP
