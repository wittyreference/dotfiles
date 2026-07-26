// ABOUTME: Streaming tokenizer implementation -- skips whitespace, emits the
// ABOUTME: whitespace-delimited run that follows, and never allocates.

#include "rsvp/tokenizer.hpp"

#include <limits>

namespace rsvp {
namespace {

/// Longest token we can describe, bounded by Token::length's width.
constexpr std::size_t kMaxTokenLength = std::numeric_limits<std::uint16_t>::max();

/// ASCII whitespace test.
///
/// Hand-rolled rather than std::isspace because that consults the C locale and
/// is undefined for negative char values -- both of which are hazards on a
/// freestanding target fed arbitrary UTF-8. Every byte of a multi-byte UTF-8
/// sequence has its high bit set, so no continuation byte can be mistaken for
/// whitespace here.
constexpr bool isWhitespace(char c) noexcept {
    return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' || c == '\v';
}

}  // namespace

Tokenizer::Tokenizer(const char* text, std::size_t length) noexcept
    : text_(text), length_(text == nullptr ? 0u : length), pos_(0u) {}

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

    out.offset = static_cast<std::uint32_t>(start);
    out.length = static_cast<std::uint16_t>(span);
    out.orp = 0u;
    out.flags = kTokenFlagNone;
    return true;
}

}  // namespace rsvp
