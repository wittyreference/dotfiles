// ABOUTME: The .rsvp sidecar container -- a header, a flat token array, and the UTF-8
// ABOUTME: text blob. Serialises to and from caller-owned buffers; performs no file I/O.

#ifndef RSVP_FORMAT_HPP
#define RSVP_FORMAT_HPP

#include "rsvp/token.hpp"

#include <cstddef>
#include <cstdint>

namespace rsvp {

/// `"RSVP"` as a little-endian 32-bit word.
constexpr std::uint32_t kRsvpMagic = 0x50565352u;

constexpr std::uint16_t kRsvpVersion = 1u;

/// Size of the version-1 header. Readers must use the header's own `headerSize`
/// rather than this constant, so a later version can add fields without breaking
/// them.
constexpr std::size_t kRsvpHeaderSize = 32u;

/// Alignment the file buffer must satisfy, because the token array is read in
/// place as `Token`, whose widest member is 32-bit.
constexpr std::size_t kRsvpAlignment = 4u;

/// On-disk layout, version 1. All fields little-endian.
///
/// ```
///   0  magic       uint32   "RSVP"
///   4  version     uint16
///   6  headerSize  uint16   where the token array begins
///   8  tokenCount  uint32
///  12  textOffset  uint32   absolute offset of the text blob
///  16  textLength  uint32
///  20  documentId  uint32   documentFingerprint() over the token array
///  24  flags       uint32   reserved, must be zero in version 1
///  28  checksum    uint32   CRC-32 over everything after the header
///      ... token array: tokenCount * 8 bytes
///      ... text blob:   textLength bytes
/// ```
///
/// Little-endian is part of the format definition, not an accident of the host:
/// both the ESP32-C3 and every development machine we target are little-endian, so
/// the token array can be read in place with no byte-swapping. A big-endian port
/// would have to swap on read.
///
/// The text blob is stored once and tokens reference slices of it by offset, so a
/// device with no room for a parser can stream a flat, seekable array and still
/// render the original bytes.
struct RsvpHeader {
    std::uint16_t version;
    std::uint16_t headerSize;
    std::uint32_t tokenCount;
    std::uint32_t textOffset;
    std::uint32_t textLength;
    std::uint32_t documentId;
    std::uint32_t flags;
    std::uint32_t checksum;
};

enum class RsvpStatus : std::uint8_t {
    kOk = 0,

    /// The output buffer cannot hold the file. Nothing was written.
    kBufferTooSmall,

    /// Not an `.rsvp` file.
    kBadMagic,

    /// A version this build does not understand. Refused rather than guessed at --
    /// misreading an unknown layout renders a book as garbage, which is worse than
    /// declining to open it.
    kUnsupportedVersion,

    /// The buffer is shorter than the header says the file is. The usual cause is a
    /// card pulled mid-write.
    kTruncated,

    /// CRC mismatch: the bytes are not what was written.
    kChecksumMismatch,
};

/// Total file size for a document with these dimensions.
std::size_t rsvpFileSize(std::uint32_t tokenCount, std::uint32_t textLength) noexcept;

/// Serialises a document into `out`, writing the number of bytes used to
/// `bytesWritten`.
///
/// On any failure `bytesWritten` is set to 0 and `out` is left untouched, so a
/// failed write cannot leave a half-valid file behind.
RsvpStatus writeRsvp(const Token* tokens, std::uint32_t tokenCount, const char* text,
                     std::uint32_t textLength, unsigned char* out, std::size_t outSize,
                     std::size_t& bytesWritten) noexcept;

/// Parses and validates the header, including that the declared payload fits in
/// `size`.
///
/// Does **not** verify the checksum: that is linear in file size, and a reader
/// opening a 3 MB book wants to start displaying words immediately. Call
/// `verifyRsvpChecksum` separately when integrity matters more than latency.
///
/// Alignment-safe -- fields are copied out rather than read in place, so this works
/// on any buffer.
RsvpStatus readRsvpHeader(const unsigned char* data, std::size_t size, RsvpHeader& out) noexcept;

/// Recomputes the CRC over the payload and compares it to the header. Linear in
/// file size.
RsvpStatus verifyRsvpChecksum(const unsigned char* data, std::size_t size) noexcept;

/// Returns the token array in place, or null if `data` is not
/// `kRsvpAlignment`-aligned.
///
/// Reading a `Token` from an odd address is undefined behaviour and can trap on
/// RISC-V, so this fails closed rather than handing back a pointer that faults on
/// first use. Callers load files into an aligned buffer; the check exists so a
/// mistake surfaces as a null pointer instead of a crash on device.
const Token* rsvpTokens(const unsigned char* data, const RsvpHeader& header) noexcept;

/// Returns the UTF-8 text blob in place. No alignment requirement -- it is bytes.
const char* rsvpText(const unsigned char* data, const RsvpHeader& header) noexcept;

}  // namespace rsvp

#endif  // RSVP_FORMAT_HPP
