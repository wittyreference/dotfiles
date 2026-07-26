// ABOUTME: Tests for the .rsvp sidecar container -- round-trip fidelity, rejection
// ABOUTME: of malformed or corrupt files, forward compatibility, and alignment safety.

#include "vendor/doctest.h"

#include "rsvp/fingerprint.hpp"
#include "rsvp/format.hpp"
#include "rsvp/tokenizer.hpp"

#include <cstring>
#include <string>
#include <vector>

namespace {

const std::string kText = "One two three. Four five six.\n\nSeven eight nine.";

std::vector<rsvp::Token> tokenize(const std::string& text) {
    rsvp::Tokenizer tokenizer(text.data(), text.size());
    std::vector<rsvp::Token> tokens;
    rsvp::Token token{};
    while (tokenizer.next(token)) {
        tokens.push_back(token);
    }
    return tokens;
}

/// Serializes kText into a fresh, correctly aligned buffer.
struct Built {
    std::vector<std::uint32_t> aligned;  // uint32_t element type forces 4-byte alignment
    std::size_t size = 0u;
    std::vector<rsvp::Token> tokens;

    unsigned char* bytes() { return reinterpret_cast<unsigned char*>(aligned.data()); }
    const unsigned char* bytes() const {
        return reinterpret_cast<const unsigned char*>(aligned.data());
    }
};

Built build(const std::string& text) {
    Built built;
    built.tokens = tokenize(text);

    const std::size_t needed = rsvp::rsvpFileSize(
        static_cast<std::uint32_t>(built.tokens.size()), static_cast<std::uint32_t>(text.size()));
    built.aligned.assign(needed / 4u + 1u, 0u);

    const auto status = rsvp::writeRsvp(
        built.tokens.data(), static_cast<std::uint32_t>(built.tokens.size()), text.data(),
        static_cast<std::uint32_t>(text.size()), built.bytes(), needed, built.size);
    REQUIRE(status == rsvp::RsvpStatus::kOk);
    return built;
}

}  // namespace

TEST_CASE("file size is the header plus the token array plus the text") {
    CHECK(rsvp::rsvpFileSize(0u, 0u) == rsvp::kRsvpHeaderSize);
    CHECK(rsvp::rsvpFileSize(10u, 100u) == rsvp::kRsvpHeaderSize + 80u + 100u);
}

TEST_CASE("a written file round-trips its tokens and text exactly") {
    auto built = build(kText);
    CHECK(built.size == rsvp::rsvpFileSize(static_cast<std::uint32_t>(built.tokens.size()),
                                           static_cast<std::uint32_t>(kText.size())));

    rsvp::RsvpHeader header{};
    REQUIRE(rsvp::readRsvpHeader(built.bytes(), built.size, header) == rsvp::RsvpStatus::kOk);

    CHECK(header.version == rsvp::kRsvpVersion);
    CHECK(header.headerSize == rsvp::kRsvpHeaderSize);
    CHECK(header.tokenCount == built.tokens.size());
    CHECK(header.textLength == kText.size());

    const rsvp::Token* tokens = rsvp::rsvpTokens(built.bytes(), header);
    REQUIRE(tokens != nullptr);
    for (std::uint32_t i = 0u; i < header.tokenCount; ++i) {
        CHECK(tokens[i].offset == built.tokens[i].offset);
        CHECK(tokens[i].length == built.tokens[i].length);
        CHECK(tokens[i].orp == built.tokens[i].orp);
        CHECK(tokens[i].flags == built.tokens[i].flags);
    }

    const char* text = rsvp::rsvpText(built.bytes(), header);
    REQUIRE(text != nullptr);
    CHECK(std::string(text, header.textLength) == kText);
}

TEST_CASE("the stored document id matches the live fingerprint of the tokens") {
    // If these ever diverge, every saved resume point silently stops restoring.
    auto built = build(kText);

    rsvp::RsvpHeader header{};
    REQUIRE(rsvp::readRsvpHeader(built.bytes(), built.size, header) == rsvp::RsvpStatus::kOk);

    CHECK(header.documentId == rsvp::documentFingerprint(
                                   built.tokens.data(),
                                   static_cast<std::uint32_t>(built.tokens.size())));
}

TEST_CASE("writing into a buffer that is too small is refused, not truncated") {
    const auto tokens = tokenize(kText);
    std::vector<std::uint32_t> tooSmall(4u, 0u);
    std::size_t written = 999u;

    const auto status = rsvp::writeRsvp(
        tokens.data(), static_cast<std::uint32_t>(tokens.size()), kText.data(),
        static_cast<std::uint32_t>(kText.size()),
        reinterpret_cast<unsigned char*>(tooSmall.data()), tooSmall.size() * 4u, written);

    CHECK(status == rsvp::RsvpStatus::kBufferTooSmall);
    CHECK(written == 0u);
}

TEST_CASE("a file with the wrong magic is rejected") {
    auto built = build(kText);
    built.bytes()[0] = 'X';

    rsvp::RsvpHeader header{};
    CHECK(rsvp::readRsvpHeader(built.bytes(), built.size, header) == rsvp::RsvpStatus::kBadMagic);
}

TEST_CASE("a file from a future format version is refused rather than misread") {
    // Refusing is the whole point of versioning. Guessing at an unknown layout
    // would render a book as garbage, which is worse than declining to open it.
    auto built = build(kText);
    const std::uint16_t future = rsvp::kRsvpVersion + 1u;
    std::memcpy(built.bytes() + 4u, &future, sizeof(future));

    rsvp::RsvpHeader header{};
    CHECK(rsvp::readRsvpHeader(built.bytes(), built.size, header) ==
          rsvp::RsvpStatus::kUnsupportedVersion);
}

TEST_CASE("a file shorter than its header declares is rejected") {
    // The common real-world corruption: a card pulled mid-write.
    auto built = build(kText);

    rsvp::RsvpHeader header{};
    CHECK(rsvp::readRsvpHeader(built.bytes(), built.size - 10u, header) ==
          rsvp::RsvpStatus::kTruncated);
}

TEST_CASE("a buffer too small to even hold a header is rejected") {
    std::vector<std::uint32_t> tiny(2u, 0u);
    rsvp::RsvpHeader header{};
    CHECK(rsvp::readRsvpHeader(reinterpret_cast<unsigned char*>(tiny.data()), 8u, header) ==
          rsvp::RsvpStatus::kTruncated);
}

TEST_CASE("the checksum accepts an intact file and catches a flipped byte") {
    auto built = build(kText);
    CHECK(rsvp::verifyRsvpChecksum(built.bytes(), built.size) == rsvp::RsvpStatus::kOk);

    // Corrupt a byte in the text blob, as a failing SD sector would.
    built.bytes()[built.size - 5u] ^= 0x01u;
    CHECK(rsvp::verifyRsvpChecksum(built.bytes(), built.size) ==
          rsvp::RsvpStatus::kChecksumMismatch);
}

TEST_CASE("an empty document round-trips") {
    std::vector<std::uint32_t> buffer(rsvp::kRsvpHeaderSize / 4u + 1u, 0u);
    std::size_t written = 0u;

    REQUIRE(rsvp::writeRsvp(nullptr, 0u, nullptr, 0u,
                            reinterpret_cast<unsigned char*>(buffer.data()),
                            buffer.size() * 4u, written) == rsvp::RsvpStatus::kOk);
    CHECK(written == rsvp::kRsvpHeaderSize);

    rsvp::RsvpHeader header{};
    REQUIRE(rsvp::readRsvpHeader(reinterpret_cast<unsigned char*>(buffer.data()), written,
                                 header) == rsvp::RsvpStatus::kOk);
    CHECK(header.tokenCount == 0u);
    CHECK(header.textLength == 0u);
}

TEST_CASE("tokens are located via the header's declared size, not a hardcoded offset") {
    // Forward compatibility, exercised rather than merely intended: a later version
    // may add header fields, and a reader that hardcodes 32 would read the new
    // fields as tokens. Fabricate a 40-byte header and check we follow it.
    auto built = build(kText);

    const std::uint16_t biggerHeader = 40u;
    std::vector<std::uint32_t> grown(built.aligned.size() + 4u, 0u);
    unsigned char* out = reinterpret_cast<unsigned char*>(grown.data());

    // Copy the original header, widen it, then place the payload after 40 bytes.
    std::memcpy(out, built.bytes(), rsvp::kRsvpHeaderSize);
    std::memcpy(out + 6u, &biggerHeader, sizeof(biggerHeader));
    const std::size_t payload = built.size - rsvp::kRsvpHeaderSize;
    std::memcpy(out + biggerHeader, built.bytes() + rsvp::kRsvpHeaderSize, payload);

    // textOffset is absolute, so it shifts with the header.
    rsvp::RsvpHeader original{};
    REQUIRE(rsvp::readRsvpHeader(built.bytes(), built.size, original) == rsvp::RsvpStatus::kOk);
    const std::uint32_t shifted =
        original.textOffset + (biggerHeader - static_cast<std::uint32_t>(rsvp::kRsvpHeaderSize));
    std::memcpy(out + 12u, &shifted, sizeof(shifted));

    rsvp::RsvpHeader header{};
    REQUIRE(rsvp::readRsvpHeader(out, biggerHeader + payload, header) == rsvp::RsvpStatus::kOk);
    CHECK(header.headerSize == biggerHeader);

    const rsvp::Token* tokens = rsvp::rsvpTokens(out, header);
    REQUIRE(tokens != nullptr);
    CHECK(tokens[0].offset == built.tokens[0].offset);
    CHECK(tokens[0].flags == built.tokens[0].flags);

    const char* text = rsvp::rsvpText(out, header);
    REQUIRE(text != nullptr);
    CHECK(std::string(text, header.textLength) == kText);
}

TEST_CASE("a misaligned buffer is refused rather than risking a fault") {
    // Token contains a uint32_t, so casting to it from an odd address is undefined
    // and on RISC-V can trap. Fail closed and let the caller fix its buffer.
    auto built = build(kText);

    std::vector<unsigned char> shifted(built.size + 1u, 0u);
    std::memcpy(shifted.data() + 1u, built.bytes(), built.size);
    unsigned char* odd = shifted.data() + 1u;

    // Only meaningful if the shift actually misaligned it.
    if ((reinterpret_cast<std::uintptr_t>(odd) % 4u) != 0u) {
        rsvp::RsvpHeader header{};
        REQUIRE(rsvp::readRsvpHeader(odd, built.size, header) == rsvp::RsvpStatus::kOk);
        CHECK(rsvp::rsvpTokens(odd, header) == nullptr);
    }
}
