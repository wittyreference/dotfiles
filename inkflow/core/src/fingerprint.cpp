// ABOUTME: FNV-1a based document fingerprint over sampled tokens, shared by the
// ABOUTME: playback resume path and the .rsvp file format.

#include "rsvp/fingerprint.hpp"

namespace rsvp {
namespace {

constexpr std::uint32_t kFnvOffsetBasis = 2166136261u;
constexpr std::uint32_t kFnvPrime = 16777619u;

void hashMix(std::uint32_t& hash, std::uint32_t value) noexcept {
    for (unsigned shift = 0u; shift < 32u; shift += 8u) {
        hash ^= (value >> shift) & 0xFFu;
        hash *= kFnvPrime;
    }
}

}  // namespace

std::uint32_t documentFingerprint(const Token* tokens, std::uint32_t count) noexcept {
    std::uint32_t hash = kFnvOffsetBasis;
    hashMix(hash, count);

    if (tokens != nullptr && count > 0u) {
        const std::uint32_t samples[3] = {0u, count / 2u, count - 1u};
        for (const std::uint32_t index : samples) {
            hashMix(hash, tokens[index].offset);
            hashMix(hash, static_cast<std::uint32_t>(tokens[index].length));
            hashMix(hash, static_cast<std::uint32_t>(tokens[index].orp));
            hashMix(hash, static_cast<std::uint32_t>(tokens[index].flags));
        }
    }
    return hash;
}

}  // namespace rsvp
