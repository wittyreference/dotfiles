// ABOUTME: Timing model implementation -- integer-only hold calculation with
// ABOUTME: boundary pauses, a length bonus, a speed ramp, and the refresh floor.

#include "rsvp/timing.hpp"

namespace rsvp {
namespace {

constexpr std::uint32_t kMillisecondsPerMinute = 60000u;
constexpr std::uint32_t kBasePercent = 100u;

/// Strongest applicable boundary pause, as a percentage of base hold.
///
/// Takes the maximum rather than compounding. A sentence ending a paragraph sets
/// both flags, and multiplying 200% by 260% yields a stall that reads as a crash.
/// Max rather than a priority chain so an unusual config -- someone wanting clause
/// pauses longer than sentence pauses -- still behaves sanely.
std::uint32_t boundaryPercent(const Token& token, const TimingConfig& config) noexcept {
    std::uint32_t percent = kBasePercent;

    if (token.has(kTokenFlagClauseEnd) && config.clausePercent > percent) {
        percent = config.clausePercent;
    }
    if (token.has(kTokenFlagSentenceEnd) && config.sentencePercent > percent) {
        percent = config.sentencePercent;
    }
    if (token.has(kTokenFlagParagraphEnd) && config.paragraphPercent > percent) {
        percent = config.paragraphPercent;
    }
    return percent;
}

/// Extra percentage earned by a token longer than the configured baseline.
///
/// One-sided on purpose: short words get no reduction. The base hold already
/// reflects an average word, and shrinking it for "a" produces a flash too brief
/// to read -- and, on e-paper, too brief to physically draw.
std::uint32_t lengthBonusPercent(const Token& token, const TimingConfig& config) noexcept {
    if (token.length <= config.lengthBaselineChars) {
        return 0u;
    }
    const std::uint32_t extra =
        static_cast<std::uint32_t>(token.length) - config.lengthBaselineChars;
    return extra * config.lengthPercentPerChar;
}

/// Extra percentage for digit strings, above the 100% baseline.
std::uint32_t numericBonusPercent(const Token& token, const TimingConfig& config) noexcept {
    if (!token.has(kTokenFlagNumeric) || config.numericPercent <= kBasePercent) {
        return 0u;
    }
    return static_cast<std::uint32_t>(config.numericPercent) - kBasePercent;
}

}  // namespace

std::uint32_t holdMs(const Token& token, const TimingConfig& config) noexcept {
    // A zero wpm is a misconfiguration, not a request for an infinite hold. Treat
    // it as the slowest real speed and let the ceiling below catch the result,
    // rather than dividing by zero.
    const std::uint32_t wpm = config.wpm == 0u ? 1u : config.wpm;
    const std::uint32_t base = kMillisecondsPerMinute / wpm;

    const std::uint32_t percent = boundaryPercent(token, config) +
                                  lengthBonusPercent(token, config) +
                                  numericBonusPercent(token, config);

    std::uint32_t hold = (base * percent) / kBasePercent;

    // Ceiling first, then floor. The floor is a hardware fact -- the panel cannot
    // draw faster -- so it has to be the last word even against a low ceiling.
    if (hold > config.maxHoldMs) {
        hold = config.maxHoldMs;
    }
    if (hold < config.minHoldMs) {
        hold = config.minHoldMs;
    }
    return hold;
}

std::uint16_t rampedWpm(std::uint32_t tokenIndex, const TimingConfig& config) noexcept {
    if (config.rampTokens == 0u || tokenIndex >= config.rampTokens) {
        return config.wpm;
    }

    // Linear interpolation from the ramp's starting speed up to the target.
    // Integer division truncates, which only ever runs slightly slow -- and the
    // exact target is returned by the early exit above, so the ramp converges
    // precisely rather than approaching the target and stopping short of it.
    const std::uint32_t target = config.wpm;
    const std::uint32_t start = (target * config.rampStartPercent) / kBasePercent;
    const std::uint32_t gained = ((target - start) * tokenIndex) / config.rampTokens;

    return static_cast<std::uint16_t>(start + gained);
}

}  // namespace rsvp
