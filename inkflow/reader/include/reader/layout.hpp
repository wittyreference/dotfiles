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

/// Portrait. Not shipped, and no longer impossible.
///
/// It was abandoned on measurement: at 480px wide the budget right of the focal column is
/// 290px, against text that needed about 400, and chunks overran the right edge by 88px.
/// That was with FreeMonoBold and a chunker that counted characters. With FreeSansBold and
/// chunks that give words back until they fit, the same document plays through portrait
/// with no overflow at 258 wpm, against 263 in landscape.
///
/// Still not shipped -- the device is held landscape and nothing asks for the other -- but
/// it is now a choice rather than a constraint, and the pipeline test asserts it keeps
/// working so that stays true.
inline constexpr Layout kPortrait{480, 800, 330, 120, 190};

/// Baseline of the chunk text, measured down from the top of the band.
inline constexpr int16_t kChunkBaseline = 70;

/// The inverted cell drawn behind the recognition-point character, measured from the
/// chunk's baseline.
///
/// Tight to the character's own advance rather than generous around it. The cell is
/// painted over text that is already drawn, so every pixel it adds beyond the pivot eats
/// a neighbour: at two pixels of horizontal padding it cut the descender off the `y` in
/// "systems". A monospace advance is the right width by construction.
inline constexpr int16_t kPivotAbove = 30;
inline constexpr int16_t kPivotBelow = 7;
/// How far the inverted cell extends past the ink it contains, each side.
///
/// One pixel, because one pixel is what this font has to give. Measured gaps between
/// adjacent glyph ink run from 5px down to **minus two** -- `mm` and `MM` overlap outright
/// -- so a cell that guarantees clear space either side of itself cannot exist here. This
/// is the tightest box that still reads as a box, and it encroaches on a neighbour only
/// where the letters were already touching.
inline constexpr int16_t kPivotGutter = 1;

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

/// Where a physical button sits against the edge of the panel, and what it does.
///
/// The device labels none of its buttons, and the SDK that reads them records only names
/// on a resistor ladder -- so the only way to learn them has been to press one and watch.
/// This is that knowledge, placed rather than listed: the sleep screen draws each label
/// against the edge the button is actually on, at the offset it actually sits at, so the
/// label is read next to the thing it names.
///
/// `at` and `span` are along the edge -- x for the top edge, y for the right edge -- in
/// panel pixels. They describe the button's footprint, not the label's.
struct ControlLabel {
    int16_t at;
    int16_t span;
    /// What the button does. Two entries for a rocker, in edge order; the second is null
    /// for a single button.
    const char* first;
    const char* second;
};

/// The Xteink X4's controls, measured against the panel in its shipped orientation.
///
/// Three physical controls carrying seven logical buttons: Power alone, and three rockers.
/// See docs/CONTROLS.md for how this was established, and for the fact that it could not
/// be read out of any code.
struct ControlMap {
    ControlLabel power;
    ControlLabel volume;
    ControlLabel upperThumb;
    ControlLabel lowerThumb;
};

}  // namespace reader
