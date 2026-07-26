// ABOUTME: Optimal recognition point (pivot) calculation -- which character of a
// ABOUTME: token gets aligned to the reader's fixed focal column during playback.

#ifndef RSVP_ORP_HPP
#define RSVP_ORP_HPP

#include <cstddef>
#include <cstdint>

namespace rsvp {

/// Returns the character index of the token's pivot -- its optimal recognition
/// point -- counted in characters, not bytes.
///
/// RSVP works by removing eye movement: every token is drawn with one chosen
/// character at the same screen column, so the eye fixates once and never
/// travels. Which character that is matters. Reading research finds the
/// recognition point sits *left* of a word's midpoint, and that it advances in
/// bands as words get longer rather than scaling smoothly.
///
/// Punctuation is excluded from the calculation but not from the index: the
/// pivot is chosen over the token's letter core, then shifted right by however
/// many leading characters precede that core. So `hello` and `hello,` pivot on
/// the same letter, while `"hello` pivots one character further along -- the eye
/// lands in the same place on the word either way, which is the point.
///
/// A token with no word characters at all (an em dash, an ellipsis) has no core
/// to centre on and returns 0.
///
/// `text` may be null when `length` is 0.
std::uint8_t computeOrp(const char* text, std::size_t length) noexcept;

}  // namespace rsvp

#endif  // RSVP_ORP_HPP
