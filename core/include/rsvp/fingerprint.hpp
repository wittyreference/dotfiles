// ABOUTME: Cheap deterministic fingerprint of a token array, used to bind a resume
// ABOUTME: point and an .rsvp file to the document they were produced from.

#ifndef RSVP_FINGERPRINT_HPP
#define RSVP_FINGERPRINT_HPP

#include "rsvp/token.hpp"

#include <cstdint>

namespace rsvp {

/// Returns a fingerprint identifying a token array.
///
/// Samples three tokens rather than hashing all of them. This is computed on every
/// resume-point save and on every file open, and a book is hundreds of thousands
/// of tokens; it only has to answer "is this the same document?", not resist an
/// adversary. The failure it prevents is dropping a reader into a random paragraph
/// of the wrong book, which any of the samples catches.
///
/// Deliberately shared between the player and the `.rsvp` format so a file's
/// stored id and a live player's computed id cannot drift apart. Two
/// implementations of a fingerprint is a bug waiting to happen.
std::uint32_t documentFingerprint(const Token* tokens, std::uint32_t count) noexcept;

}  // namespace rsvp

#endif  // RSVP_FINGERPRINT_HPP
