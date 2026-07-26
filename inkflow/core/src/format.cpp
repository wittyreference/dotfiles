// ABOUTME: .rsvp container serialisation -- explicit little-endian header fields, a
// ABOUTME: memcpy'd token array, a CRC-32 over the payload, and fail-closed validation.

#include "rsvp/format.hpp"

#include "rsvp/fingerprint.hpp"

#include <cstring>

namespace rsvp {
namespace {

// Header field offsets. Named rather than inlined so the layout documented in
// format.hpp and the code implementing it cannot drift apart.
constexpr std::size_t kOffMagic = 0u;
constexpr std::size_t kOffVersion = 4u;
constexpr std::size_t kOffHeaderSize = 6u;
constexpr std::size_t kOffTokenCount = 8u;
constexpr std::size_t kOffTextOffset = 12u;
constexpr std::size_t kOffTextLength = 16u;
constexpr std::size_t kOffDocumentId = 20u;
constexpr std::size_t kOffFlags = 24u;
constexpr std::size_t kOffChecksum = 28u;

// Byte-explicit accessors rather than memcpy of a uint32_t: they define the
// little-endian layout in code instead of inheriting it from the host, and they
// work at any alignment. Only the header goes through these -- the token array is
// bulk-copied, which is where the format's little-endian requirement really lives.
void put16(unsigned char* p, std::uint16_t v) noexcept {
    p[0] = static_cast<unsigned char>(v & 0xFFu);
    p[1] = static_cast<unsigned char>((v >> 8) & 0xFFu);
}

void put32(unsigned char* p, std::uint32_t v) noexcept {
    p[0] = static_cast<unsigned char>(v & 0xFFu);
    p[1] = static_cast<unsigned char>((v >> 8) & 0xFFu);
    p[2] = static_cast<unsigned char>((v >> 16) & 0xFFu);
    p[3] = static_cast<unsigned char>((v >> 24) & 0xFFu);
}

std::uint16_t get16(const unsigned char* p) noexcept {
    return static_cast<std::uint16_t>(static_cast<std::uint32_t>(p[0]) |
                                      (static_cast<std::uint32_t>(p[1]) << 8));
}

std::uint32_t get32(const unsigned char* p) noexcept {
    return static_cast<std::uint32_t>(p[0]) | (static_cast<std::uint32_t>(p[1]) << 8) |
           (static_cast<std::uint32_t>(p[2]) << 16) | (static_cast<std::uint32_t>(p[3]) << 24);
}

/// CRC-32 (IEEE 802.3, reflected polynomial), computed bitwise.
///
/// Bitwise rather than table-driven on purpose: a 256-entry table costs 1 KB of
/// flash on a device where that matters, and this runs at most once per file open,
/// never in the playback loop. Speed is irrelevant here; footprint is not.
std::uint32_t crc32(const unsigned char* data, std::size_t size) noexcept {
    std::uint32_t crc = 0xFFFFFFFFu;
    for (std::size_t i = 0u; i < size; ++i) {
        crc ^= static_cast<std::uint32_t>(data[i]);
        for (unsigned bit = 0u; bit < 8u; ++bit) {
            crc = (crc & 1u) != 0u ? ((crc >> 1) ^ 0xEDB88320u) : (crc >> 1);
        }
    }
    return ~crc;
}

}  // namespace

std::size_t rsvpFileSize(std::uint32_t tokenCount, std::uint32_t textLength) noexcept {
    return kRsvpHeaderSize + static_cast<std::size_t>(tokenCount) * sizeof(Token) +
           static_cast<std::size_t>(textLength);
}

RsvpStatus writeRsvp(const Token* tokens, std::uint32_t tokenCount, const char* text,
                     std::uint32_t textLength, unsigned char* out, std::size_t outSize,
                     std::size_t& bytesWritten) noexcept {
    bytesWritten = 0u;

    // Guard the size arithmetic before trusting it. On a 32-bit target
    // tokenCount * 8 can wrap, and a wrapped total would pass the capacity check
    // and then overrun the buffer -- so check for the wrap rather than assuming the
    // inputs are sane.
    const std::size_t tokenBytes = static_cast<std::size_t>(tokenCount) * sizeof(Token);
    if (tokenCount != 0u && tokenBytes / sizeof(Token) != tokenCount) {
        return RsvpStatus::kBufferTooSmall;
    }
    const std::size_t needed = rsvpFileSize(tokenCount, textLength);
    if (needed < tokenBytes) {
        return RsvpStatus::kBufferTooSmall;
    }
    if (out == nullptr || outSize < needed) {
        return RsvpStatus::kBufferTooSmall;
    }

    const std::size_t textOffset = kRsvpHeaderSize + tokenBytes;

    // Payload first, so the checksum is taken over the bytes actually stored.
    if (tokens != nullptr && tokenCount > 0u) {
        std::memcpy(out + kRsvpHeaderSize, tokens, tokenBytes);
    }
    if (text != nullptr && textLength > 0u) {
        std::memcpy(out + textOffset, text, textLength);
    }

    put32(out + kOffMagic, kRsvpMagic);
    put16(out + kOffVersion, kRsvpVersion);
    put16(out + kOffHeaderSize, static_cast<std::uint16_t>(kRsvpHeaderSize));
    put32(out + kOffTokenCount, tokenCount);
    put32(out + kOffTextOffset, static_cast<std::uint32_t>(textOffset));
    put32(out + kOffTextLength, textLength);
    put32(out + kOffDocumentId, documentFingerprint(tokens, tokenCount));
    put32(out + kOffFlags, 0u);
    put32(out + kOffChecksum, crc32(out + kRsvpHeaderSize, needed - kRsvpHeaderSize));

    bytesWritten = needed;
    return RsvpStatus::kOk;
}

RsvpStatus readRsvpHeader(const unsigned char* data, std::size_t size, RsvpHeader& out) noexcept {
    if (data == nullptr || size < kRsvpHeaderSize) {
        return RsvpStatus::kTruncated;
    }
    if (get32(data + kOffMagic) != kRsvpMagic) {
        return RsvpStatus::kBadMagic;
    }

    const std::uint16_t version = get16(data + kOffVersion);
    if (version != kRsvpVersion) {
        return RsvpStatus::kUnsupportedVersion;
    }

    RsvpHeader header{};
    header.version = version;
    header.headerSize = get16(data + kOffHeaderSize);
    header.tokenCount = get32(data + kOffTokenCount);
    header.textOffset = get32(data + kOffTextOffset);
    header.textLength = get32(data + kOffTextLength);
    header.documentId = get32(data + kOffDocumentId);
    header.flags = get32(data + kOffFlags);
    header.checksum = get32(data + kOffChecksum);

    // A header smaller than version 1's is malformed, not merely old.
    if (header.headerSize < kRsvpHeaderSize) {
        return RsvpStatus::kTruncated;
    }

    // Everything the header claims must actually be present. Validating it here is
    // what lets callers treat the pointers returned below as safe to read.
    const std::size_t tokenBytes = static_cast<std::size_t>(header.tokenCount) * sizeof(Token);
    if (tokenBytes / sizeof(Token) != header.tokenCount) {
        return RsvpStatus::kTruncated;
    }
    if (static_cast<std::size_t>(header.headerSize) + tokenBytes > size) {
        return RsvpStatus::kTruncated;
    }
    const std::size_t textEnd =
        static_cast<std::size_t>(header.textOffset) + static_cast<std::size_t>(header.textLength);
    if (header.textOffset > size || textEnd > size || textEnd < header.textOffset) {
        return RsvpStatus::kTruncated;
    }

    out = header;
    return RsvpStatus::kOk;
}

RsvpStatus verifyRsvpChecksum(const unsigned char* data, std::size_t size) noexcept {
    RsvpHeader header{};
    const RsvpStatus status = readRsvpHeader(data, size, header);
    if (status != RsvpStatus::kOk) {
        return status;
    }

    const std::size_t textEnd =
        static_cast<std::size_t>(header.textOffset) + static_cast<std::size_t>(header.textLength);
    const std::uint32_t actual =
        crc32(data + header.headerSize, textEnd - static_cast<std::size_t>(header.headerSize));

    return actual == header.checksum ? RsvpStatus::kOk : RsvpStatus::kChecksumMismatch;
}

const Token* rsvpTokens(const unsigned char* data, const RsvpHeader& header) noexcept {
    if (data == nullptr) {
        return nullptr;
    }
    // Both the buffer and the token array's offset within it must be aligned, or the
    // resulting pointer is unusable. Fail closed: a null return is a bug the caller
    // can see, whereas an unaligned Token* is a trap on RISC-V.
    const std::uintptr_t address = reinterpret_cast<std::uintptr_t>(data);
    if ((address % kRsvpAlignment) != 0u ||
        (static_cast<std::size_t>(header.headerSize) % kRsvpAlignment) != 0u) {
        return nullptr;
    }
    return reinterpret_cast<const Token*>(data + header.headerSize);
}

const char* rsvpText(const unsigned char* data, const RsvpHeader& header) noexcept {
    if (data == nullptr) {
        return nullptr;
    }
    return reinterpret_cast<const char*>(data + header.textOffset);
}

}  // namespace rsvp
