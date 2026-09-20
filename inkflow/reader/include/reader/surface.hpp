// ABOUTME: The drawing surface the reader renders through, with no panel behind it.
// ABOUTME: GxEPD2 backs it on the device; the simulator's pixel canvas backs it on a host.

#pragma once

#include <cstdint>

namespace reader {

/// One bit per pixel, because the panel is monochrome.
///
/// This is why the recognition point is marked with guide ticks rather than the red
/// letter every RSVP reader since Spritz has used.
enum class Ink : uint8_t { kWhite = 0, kBlack = 1 };

/// The two type sizes the reading frame uses.
///
/// Named rather than passed as font pointers so this header stays free of Adafruit_GFX
/// types, which drag in Arduino headers and would make the module unbuildable on a host.
enum class Font : uint8_t { kStatus, kChunk };

class Surface;

/// Draws one frame's contents.
///
/// A backend may call `paint` more than once for a single frame -- GxEPD2 walks the
/// framebuffer in pages -- so an implementation must draw the same thing every time and
/// must not carry state across calls.
class Painter {
public:
    virtual void paint(Surface& s) const = 0;

protected:
    ~Painter() = default;
};

/// Everything the reader needs in order to draw, and nothing about how it reaches glass.
class Surface {
public:
    virtual int16_t width() const = 0;
    virtual int16_t height() const = 0;

    /// Width in pixels that `text` would occupy in `font`. Must not draw anything.
    ///
    /// The reader positions a chunk by measuring the text left of its pivot, so this has
    /// to be exact rather than estimated -- an approximation here shifts the focal column
    /// on every word, which is the one thing RSVP cannot tolerate.
    virtual int16_t textWidth(Font font, const char* text) const = 0;

    /// Redraws the whole screen. Clears accumulated ghosting. ~1958ms on the panel.
    virtual void renderFull(const Painter& painter) = 0;

    /// Redraws only a horizontal band. The hot path, ~542ms on the panel.
    ///
    /// Window size barely matters: the panel's partial waveform is a fixed ~501ms and
    /// only the SPI transfer scales, so a 20x smaller area costs 34% less time rather
    /// than 20x less. The band is narrow for ghosting and power, not for speed.
    virtual void renderBand(int16_t y, int16_t h, const Painter& painter) = 0;

    // The calls below are valid only from inside Painter::paint.
    virtual void fill(Ink ink) = 0;
    virtual void rect(int16_t x, int16_t y, int16_t w, int16_t h, Ink ink) = 0;
    /// Draws `text` with its baseline at `y` and its left edge at `x`.
    virtual void text(Font font, int16_t x, int16_t y, const char* str) = 0;

protected:
    ~Surface() = default;
};

}  // namespace reader
