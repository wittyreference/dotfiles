// ABOUTME: Timing model deciding how long each token is held on screen, using
// ABOUTME: integer-only arithmetic and honouring the display's refresh floor.

#ifndef RSVP_TIMING_HPP
#define RSVP_TIMING_HPP

#include "rsvp/token.hpp"

#include <cstdint>

namespace rsvp {

/// Median latency of a 120px windowed partial update on an Xteink X4.
///
/// Measured, not estimated: SSD1677 driving a GDEQ0426T82, 2026-09-18, 12 samples per
/// configuration with the first two discarded as warm-up. Raw data in
/// `hardware-notes/eink-bench-20260918.csv`.
///
/// The striking part of that run is how little the window size matters. A 40px band
/// took 524 ms and a full-screen 800px partial took 704 ms -- a 20x area difference for
/// 34% more time -- because the panel's partial waveform is a fixed ~501 ms and only
/// the SPI transfer scales. Shrinking the update region is therefore not the lever it
/// was assumed to be; chunk size is.
constexpr std::uint16_t kPanelPartialRefreshMs = 542u;

/// Median latency of a full refresh on the same panel, same run.
///
/// Roughly 3.6x a partial. Far too slow to advance a word, so full refreshes exist only
/// to clear accumulated ghosting, and belong at sentence or paragraph pauses where the
/// timing model already inserts a beat.
constexpr std::uint16_t kPanelFullRefreshMs = 1958u;

/// Tunable parameters for the timing model.
///
/// Everything is integer and expressed in percent rather than floating point.
/// The ESP32-C3 has no FPU, so floats here would cost soft-float calls in the
/// hot loop; integers also make the tests exact instead of approximate.
struct TimingConfig {
    /// Target reading speed at cruise, in words per minute.
    std::uint16_t wpm = 300u;

    /// Floor on any single hold, in milliseconds.
    ///
    /// This is the e-ink constraint, and the reason this field exists at all. The panel
    /// physically cannot redraw faster than its waveform allows, so scheduling a token
    /// for less than that just drops frames.
    ///
    /// Defaults to the measured panel latency rather than zero, because a default of
    /// zero silently models a device that does not exist. Zero remains available and
    /// meaningful -- the desktop simulator and most unit tests set it explicitly.
    std::uint16_t minHoldMs = kPanelPartialRefreshMs;

    /// Ceiling on any single hold. Stops a mis-set speed or a stray flag from
    /// looking like the device has locked up.
    std::uint16_t maxHoldMs = 5000u;

    /// Boundary pauses, as a percentage of the base hold. Applied as the single
    /// strongest match rather than compounded.
    ///
    /// 16-bit rather than 8-bit: a paragraph pause wants to exceed 255% of base,
    /// and a percentage field that silently caps at 2.55x is a trap.
    std::uint16_t clausePercent = 150u;
    std::uint16_t sentencePercent = 200u;
    std::uint16_t paragraphPercent = 260u;

    /// Extra time for digit strings, as a percentage of the base hold. A numeral
    /// has no familiar word shape, so it needs longer despite often being short.
    std::uint16_t numericPercent = 130u;

    /// Words at or below this length get no length bonus. The base hold already
    /// reflects an average word.
    std::uint8_t lengthBaselineChars = 5u;

    /// Additional percent of base hold per character beyond the baseline.
    std::uint8_t lengthPercentPerChar = 3u;

    /// Number of tokens over which speed ramps up to `wpm` at session start.
    /// Zero disables the ramp.
    std::uint16_t rampTokens = 40u;

    /// Speed at the first token, as a percentage of `wpm`.
    std::uint8_t rampStartPercent = 60u;
};

/// Returns how long `token` should be held on screen, in milliseconds.
///
/// Composition is additive on top of a 100% base: the strongest boundary pause,
/// plus a length bonus, plus a numeral bonus. Boundaries are not compounded --
/// a sentence that also ends a paragraph gets the paragraph pause, not their
/// product, which would stall long enough to read as a freeze.
///
/// The result is clamped to `[minHoldMs, maxHoldMs]`, with the floor applied
/// last so it always wins.
///
/// Length is measured in bytes, not characters. For Latin text the two are the
/// same; for scripts with multi-byte characters this over-weights word length,
/// which is acceptable because the boundary and pause model would need rethinking
/// for those scripts regardless.
std::uint32_t holdMs(const Token& token, const TimingConfig& config) noexcept;

/// Returns the effective words-per-minute at `tokenIndex` tokens into a session.
///
/// Reading starts below the target speed and converges on it linearly over
/// `rampTokens`. Cold-starting at cruise is a common way for a reader to bounce
/// off RSVP entirely -- the first few seconds are when the eye is still finding
/// the focal column. Converges exactly on `wpm`, and stays there.
std::uint16_t rampedWpm(std::uint32_t tokenIndex, const TimingConfig& config) noexcept;

}  // namespace rsvp

#endif  // RSVP_TIMING_HPP
