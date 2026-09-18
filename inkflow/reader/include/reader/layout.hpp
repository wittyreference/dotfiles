// ABOUTME: Screen geometry for the RSVP reading frame, shared by the firmware and the
// ABOUTME: simulator so a layout the simulator clears is the layout the device draws.

#pragma once

#include <cstdint>

namespace reader {

/// Where the reading frame puts things on a panel of a given size.
///
/// This is one struct rather than a set of loose constants because the firmware and the
/// simulator must agree on all of it or none of it. They previously carried independent
/// copies, and a simulator that confidently renders a layout the device does not produce
/// is worse than having no simulator at all.
struct Layout {
    int16_t width;
    int16_t height;
    /// Top of the band a partial refresh redraws.
    int16_t bandY;
    /// Height of that band.
    int16_t bandH;
    /// Horizontal position of the optimal recognition point.
    ///
    /// Every chunk is positioned so its pivot character lands on this column. That is
    /// the whole mechanism: the eye fixates here once and never travels.
    int16_t focalX;

    /// Pixels available to the right of the focal column.
    ///
    /// This, not `width`, is the budget a chunk has to fit in. A chunk is positioned by
    /// its pivot, which sits near the start of the first word, so nearly the whole chunk
    /// extends rightward.
    constexpr int16_t rightBudget() const { return static_cast<int16_t>(width - focalX); }
};

/// The shipped layout: 800x480, the panel's native orientation.
///
/// A page-based reader is held portrait, but RSVP wants one wide line and no height at
/// all. See `kPortrait` for why that decides the orientation.
inline constexpr Layout kLandscape{800, 480, 180, 120, 300};

/// Portrait, which does not fit and is not shipped.
///
/// Kept because the simulator's overflow test asserts that it still overflows. At 480px
/// wide the budget right of the focal column is 290px for text that needs about 400, and
/// chunks overran the right edge by 88px. Deleting this would delete the evidence.
inline constexpr Layout kPortrait{480, 800, 330, 120, 190};

/// Baseline of the chunk text, measured down from the top of the band.
inline constexpr int16_t kChunkBaseline = 70;

/// Full refresh clears ghosting but costs 1958ms (measured), so it is spent where the
/// timing model already inserts a pause and the reader will not feel it as a stall.
///
/// Three tiers, because prose does not owe the reader a paragraph on schedule. Preferring
/// a paragraph boundary is right, but waiting for one indefinitely is not: on a real book
/// that let ghosting reach 135 partial updates deep, more than twice the intended bound,
/// and there is no amount of landing-on-a-beat that makes an illegible panel acceptable.
inline constexpr uint32_t kPartialsBeforeFlush = 60u;

/// Past this, a sentence boundary is good enough. Still a beat, just a smaller one.
inline constexpr uint32_t kPartialsBeforeSentenceFlush = 90u;

/// Past this, flush regardless. A full refresh mid-phrase reads as a fault, and so does
/// a screen this ghosted; the difference is that this one recovers.
inline constexpr uint32_t kPartialsFlushDeadline = 120u;

}  // namespace reader
