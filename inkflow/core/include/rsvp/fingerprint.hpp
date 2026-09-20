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
///
/// The algorithm is specified here rather than left to the implementation because the
/// result is written into every `.rsvp` file as `documentId` and gates every resume
/// point. It is on-disk format, and changing it invalidates every file and bookmark
/// already on a card. A reader in another language must be able to reproduce it, and
/// the golden values in the tests are checked against this description:
///
///   hash = 2166136261                       (FNV-1a 32-bit offset basis)
///   mix(count)
///   if tokens and count > 0, then for index in {0, count/2, count-1}:
///       mix(offset)  mix(length)  mix(orp)  mix(flags)
///
///   mix(v): for each of the four bytes of v, least significant first:
///       hash ^= byte;  hash *= 16777619     (FNV-1a 32-bit prime)
///
/// The three sample indices collapse onto token zero when count == 1, which mixes that
/// token in three times. That is intended, not an edge case to be fixed.
std::uint32_t documentFingerprint(const Token* tokens, std::uint32_t count) noexcept;

}  // namespace rsvp

#endif  // RSVP_FINGERPRINT_HPP
