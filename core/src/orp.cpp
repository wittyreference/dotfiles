// ABOUTME: Optimal recognition point calculation -- picks the pivot character over
// ABOUTME: a token's letter core, in character units, excluding surrounding punctuation.

#include "rsvp/orp.hpp"

#include <limits>

namespace rsvp {
namespace {

/// True for the second and later bytes of a multi-byte UTF-8 sequence.
///
/// Continuation bytes match 10xxxxxx. Counting only non-continuation bytes gives
/// the character count without needing to decode code points, which is all the
/// pivot calculation requires.
constexpr bool isUtf8Continuation(char c) noexcept {
    return (static_cast<unsigned char>(c) & 0xC0u) == 0x80u;
}

/// True for characters that form part of a word's visual shape.
///
/// ASCII letters and digits qualify; digits because a numeral has a shape the eye
/// centres on just as a word does. Every non-ASCII byte is treated as a letter:
/// full Unicode classification would need tables this engine cannot afford on a
/// 380 KB device, and the assumption holds for the accented letters and
/// non-Latin scripts that actually appear mid-word. The known cost is that
/// leading non-ASCII punctuation -- typographic quotes, CJK brackets -- counts as
/// part of the core, shifting the pivot right by one. Visible only on tokens that
/// open with such a mark, and cheap next to carrying a Unicode table.
constexpr bool isWordByte(char c) noexcept {
    const unsigned char b = static_cast<unsigned char>(c);
    return (b >= 'a' && b <= 'z') || (b >= 'A' && b <= 'Z') ||
           (b >= '0' && b <= '9') || b >= 0x80u;
}

/// Maps a word's character count to its pivot offset within that word.
///
/// Banded rather than proportional, and biased left of centre: both are findings
/// from reading research on where the eye actually recognises a word, not a
/// midpoint calculation.
constexpr std::uint8_t pivotForCoreLength(std::size_t coreLength) noexcept {
    if (coreLength <= 1u) {
        return 0u;
    }
    if (coreLength <= 5u) {
        return 1u;
    }
    if (coreLength <= 9u) {
        return 2u;
    }
    if (coreLength <= 13u) {
        return 3u;
    }
    return 4u;
}

}  // namespace

std::uint8_t computeOrp(const char* text, std::size_t length) noexcept {
    if (text == nullptr || length == 0u) {
        return 0u;
    }

    // Walk the token once, in characters, recording where the letter core starts
    // and ends. Everything outside that span is punctuation we ignore when
    // choosing the pivot but still count when reporting its index.
    constexpr std::size_t kNone = static_cast<std::size_t>(-1);
    std::size_t coreStart = kNone;
    std::size_t coreEnd = kNone;
    std::size_t charIndex = 0u;

    for (std::size_t byteIndex = 0u; byteIndex < length; ++byteIndex) {
        if (isUtf8Continuation(text[byteIndex])) {
            continue;  // Same character as the byte before it.
        }
        if (isWordByte(text[byteIndex])) {
            if (coreStart == kNone) {
                coreStart = charIndex;
            }
            coreEnd = charIndex;
        }
        ++charIndex;
    }

    if (coreStart == kNone) {
        return 0u;  // No word characters: an em dash, an ellipsis. Nothing to centre.
    }

    const std::size_t coreLength = coreEnd - coreStart + 1u;
    const std::size_t pivot = coreStart + pivotForCoreLength(coreLength);

    // Token::orp is one byte. A token opening with 250+ punctuation marks is not
    // prose, but clamp rather than wrap so the renderer still gets a valid index.
    constexpr std::size_t kMaxOrp = std::numeric_limits<std::uint8_t>::max();
    return static_cast<std::uint8_t>(pivot > kMaxOrp ? kMaxOrp : pivot);
}

}  // namespace rsvp
