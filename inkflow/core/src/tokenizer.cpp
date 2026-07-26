// ABOUTME: Streaming tokenizer implementation -- slices whitespace-delimited runs
// ABOUTME: and classifies each one's boundaries, content, and pivot without allocating.

#include "rsvp/tokenizer.hpp"

#include "rsvp/orp.hpp"

#include <limits>

namespace rsvp {
namespace {

/// Longest token we can describe, bounded by Token::length's width.
constexpr std::size_t kMaxTokenLength = std::numeric_limits<std::uint16_t>::max();

/// ASCII whitespace test.
///
/// Hand-rolled rather than std::isspace because that consults the C locale and
/// is undefined for negative char values -- both hazards on a freestanding target
/// fed arbitrary UTF-8. Every byte of a multi-byte UTF-8 sequence has its high
/// bit set, so no continuation byte can be mistaken for whitespace here.
constexpr bool isWhitespace(char c) noexcept {
    return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' || c == '\v';
}

constexpr bool isDigit(char c) noexcept {
    const unsigned char b = static_cast<unsigned char>(c);
    return b >= '0' && b <= '9';
}

constexpr bool isNonAscii(char c) noexcept {
    return (static_cast<unsigned char>(c) & 0x80u) != 0u;
}

/// Punctuation that can sit *after* a sentence terminator.
///
/// Dialogue puts the full stop inside the quote -- `"Stop."` -- so the terminator
/// has to be found by looking past any closing marks. ASCII only: typographic
/// quotes and CJK brackets are multi-byte and are not recognised yet, which means
/// a sentence closed with a curly quote loses its pause. Known gap, and cheaper to
/// leave than to carry a Unicode table for.
constexpr bool isClosingMark(char c) noexcept {
    return c == '"' || c == '\'' || c == ')' || c == ']' || c == '}';
}

constexpr bool isClauseTerminator(char c) noexcept {
    return c == ',' || c == ';' || c == ':' || c == '-';
}

constexpr bool isSentenceTerminator(char c) noexcept {
    return c == '.' || c == '!' || c == '?';
}

}  // namespace

bool Tokenizer::next(Token& out) noexcept {
    // Skip any run of whitespace separating us from the next token.
    while (pos_ < length_ && isWhitespace(text_[pos_])) {
        ++pos_;
    }

    if (pos_ >= length_) {
        return false;
    }

    const std::size_t start = pos_;
    while (pos_ < length_ && !isWhitespace(text_[pos_])) {
        ++pos_;
    }

    std::size_t span = pos_ - start;

    // A run longer than Token::length can hold is not a word -- it is a data URI,
    // a base64 blob, or corrupt text. Emit what fits and resume at the cut so no
    // bytes are silently dropped; the next call continues through the remainder.
    if (span > kMaxTokenLength) {
        span = kMaxTokenLength;
        pos_ = start + span;
    }

    std::uint8_t flags = kTokenFlagNone;

    // Content classification: one pass over the token's bytes.
    for (std::size_t i = start; i < start + span; ++i) {
        if (isDigit(text_[i])) {
            flags |= kTokenFlagNumeric;
        }
        if (isNonAscii(text_[i])) {
            flags |= kTokenFlagMultibyte;
        }
    }

    // Boundary classification: walk back past closing marks to the real terminator.
    std::size_t terminal = start + span;
    while (terminal > start && isClosingMark(text_[terminal - 1u])) {
        --terminal;
    }
    if (terminal > start) {
        const char last = text_[terminal - 1u];
        if (isSentenceTerminator(last)) {
            // An ellipsis is hesitation, not a full stop. Treating "well..." as a
            // sentence boundary drops a long beat into the middle of a thought.
            const bool isEllipsis =
                last == '.' && terminal - 1u > start && text_[terminal - 2u] == '.';
            flags |= isEllipsis ? kTokenFlagClauseEnd : kTokenFlagSentenceEnd;
        } else if (isClauseTerminator(last)) {
            flags |= kTokenFlagClauseEnd;
        }
    }

    // Paragraph detection needs lookahead, not consumption: peek at the whitespace
    // run that follows without moving the cursor, so the next call still sees it.
    // A blank line means two newlines; a single newline is just hard wrapping and
    // must not pause, or hard-wrapped plain text would break every ten words.
    std::size_t look = pos_;
    std::size_t newlines = 0u;
    while (look < length_ && isWhitespace(text_[look])) {
        if (text_[look] == '\n') {
            ++newlines;
        }
        ++look;
    }
    if (look >= length_ || newlines >= 2u) {
        // End of input closes the final paragraph, so playback holds the last
        // token rather than snapping to a blank screen.
        flags |= kTokenFlagParagraphEnd;
    }

    out.offset = static_cast<std::uint32_t>(start);
    out.length = static_cast<std::uint16_t>(span);
    out.orp = computeOrp(text_ + start, span);
    out.flags = flags;
    return true;
}

Tokenizer::Tokenizer(const char* text, std::size_t length) noexcept
    : text_(text), length_(text == nullptr ? 0u : length), pos_(0u) {}

}  // namespace rsvp
