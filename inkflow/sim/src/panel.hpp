// ABOUTME: A software stand-in for the SSD1677 panel: draws through the pixel canvas and
// ABOUTME: charges the measured refresh cost, so pacing and ghosting are observable here.

#pragma once

#include <cstdint>

#include "canvas.hpp"
#include "reader/layout.hpp"
#include "reader/surface.hpp"
#include "rsvp/timing.hpp"

#include "../vendor/Fonts/FreeMonoBold12pt7b.h"
#include "../vendor/Fonts/FreeMonoBold18pt7b.h"

namespace sim {

/// Milliseconds a partial refresh of `bandHeight` pixels costs.
///
/// A least-squares fit to the six band heights in hardware-notes/eink-bench-20260918.csv.
/// The panel's partial waveform is a fixed cost and only the SPI transfer scales with
/// area, which is why a 20x larger window costs 34% more time rather than 20x -- that
/// large constant term is the shape of the whole finding.
///
/// Integer arithmetic to match the engine's rule against floating point. Residuals
/// against the measured medians, in milliseconds:
///
///    40px  523 vs 524  (-1)      200px  561 vs 560  (+1)
///    80px  532 vs 530  (+2)      400px  609 vs 608  (+1)
///   120px  542 vs 542  ( 0)      800px  704 vs 704  ( 0)
///
/// 120px is the band the reader actually ships, and it is exact. A linear model cannot
/// hit all six -- the 40-to-80px step in the data is smaller than the trend -- so the
/// tests assert a tolerance rather than pretending to an accuracy this does not have.
constexpr uint32_t partialRefreshMs(int16_t bandHeight) {
    const uint32_t h = bandHeight < 0 ? 0u : static_cast<uint32_t>(bandHeight);
    return 513u + (239u * h + 500u) / 1000u;
}

/// Draws through a Canvas and keeps the books the real panel would keep.
///
/// The simulator previously modelled no panel at all -- no refresh latency, no ghosting,
/// no partial window. That left the one thing nobody had confirmed, the reading
/// experience itself, as exactly the thing it could not show.
class Panel : public reader::Surface {
public:
    explicit Panel(const reader::Layout& layout)
        : canvas_(layout.width, layout.height) {}

    int16_t width() const override { return static_cast<int16_t>(canvas_.width()); }
    int16_t height() const override { return static_cast<int16_t>(canvas_.height()); }

    int16_t textWidth(reader::Font font, const char* str) const override {
        int w = 0, h = 0;
        // textBounds does not draw, but it advances nothing either, so measuring through
        // a const_cast of the font selection is safe and keeps one glyph walk in the code.
        Canvas& c = const_cast<Canvas&>(canvas_);
        const GFXfont* previous = c.currentFont();
        c.setFont(fontFor(font));
        c.textBounds(str, w, h);
        c.setFont(previous);
        return static_cast<int16_t>(w);
    }

    void renderFull(const reader::Painter& painter) override {
        painter.paint(*this);
        elapsedMs_ += rsvp::kPanelFullRefreshMs;
        ++fullRefreshes_;
        ghost_ = 0;
    }

    void renderBand(int16_t y, int16_t h, const reader::Painter& painter) override {
        bandY_ = y;
        bandH_ = h;
        clipped_ = true;
        painter.paint(*this);
        clipped_ = false;
        elapsedMs_ += partialRefreshMs(h);
        ++partialRefreshes_;
        ++ghost_;
        if (ghost_ > peakGhost_) {
            peakGhost_ = ghost_;
        }
    }

    void fill(reader::Ink ink) override {
        const uint16_t colour = ink == reader::Ink::kBlack ? kBlack : kWhite;
        if (clipped_) {
            // A partial update only touches its window. Filling the whole screen here
            // would erase the guide marks, which is precisely the bug the device does not
            // have: the ticks live outside the band so they survive every band update.
            canvas_.fillRect(0, bandY_, canvas_.width(), bandH_, colour);
        } else {
            canvas_.fillScreen(colour);
        }
    }

    void rect(int16_t x, int16_t y, int16_t w, int16_t h, reader::Ink ink) override {
        canvas_.fillRect(x, y, w, h, ink == reader::Ink::kBlack ? kBlack : kWhite);
    }

    void text(reader::Font font, int16_t x, int16_t y, const char* str) override {
        canvas_.setFont(fontFor(font));
        canvas_.setTextColor(kBlack);
        canvas_.setCursor(x, y);
        canvas_.print(str);
    }

    /// Total time the panel would have spent refreshing, in milliseconds.
    uint64_t elapsedMs() const { return elapsedMs_; }
    uint32_t fullRefreshes() const { return fullRefreshes_; }
    uint32_t partialRefreshes() const { return partialRefreshes_; }
    /// Partial updates since the last full refresh -- how much ghosting has accrued.
    uint32_t ghost() const { return ghost_; }
    /// The worst that figure ever reached. A flush that never fires shows up here.
    uint32_t peakGhost() const { return peakGhost_; }

    const Canvas& canvas() const { return canvas_; }
    bool writePng(const char* path) const { return canvas_.writePng(path); }

private:
    static const GFXfont* fontFor(reader::Font font) {
        return font == reader::Font::kChunk ? &FreeMonoBold18pt7b : &FreeMonoBold12pt7b;
    }

    Canvas canvas_;

    uint64_t elapsedMs_ = 0;
    uint32_t fullRefreshes_ = 0;
    uint32_t partialRefreshes_ = 0;
    uint32_t ghost_ = 0;
    uint32_t peakGhost_ = 0;

    bool clipped_ = false;
    int16_t bandY_ = 0;
    int16_t bandH_ = 0;
};

}  // namespace sim
