// ABOUTME: A reader::Surface backed by GxEPD2, so the shared reading loop draws on the
// ABOUTME: panel without knowing about paged framebuffers or panel colour constants.

#pragma once

#include <Fonts/FreeMonoBold12pt7b.h>
#include <Fonts/FreeMonoBold18pt7b.h>
#include <GxEPD2_BW.h>

#include "reader/surface.hpp"

/// Draws the reading frame onto the e-paper panel.
///
/// Templated on the display rather than written against a GxEPD2 base class, so the panel
/// model is named in exactly one place -- the instantiation in main.cpp -- and swapping it
/// does not reach in here.
template <typename Display>
class GxEpd2Surface : public reader::Surface {
public:
    explicit GxEpd2Surface(Display& display) : display_(display) {}

    int16_t width() const override { return display_.width(); }
    int16_t height() const override { return display_.height(); }

    int16_t textWidth(reader::Font font, const char* str) const override {
        // The font has to be selected before the glyphs can be measured, so this leaves a
        // different one selected than it found. Harmless: every draw selects its own.
        display_.setFont(fontFor(font));
        int16_t x1 = 0, y1 = 0;
        uint16_t w = 0, h = 0;
        display_.getTextBounds(str, 0, 0, &x1, &y1, &w, &h);
        return static_cast<int16_t>(w);
    }

    void renderFull(const reader::Painter& painter) override {
        display_.setFullWindow();
        paint(painter);
    }

    void renderBand(int16_t y, int16_t h, const reader::Painter& painter) override {
        display_.setPartialWindow(0, windowCoord(y), static_cast<uint16_t>(display_.width()),
                                  windowCoord(h));
        paint(painter);
    }

    void fill(reader::Ink ink) override { display_.fillScreen(colour(ink)); }

    void rect(int16_t x, int16_t y, int16_t w, int16_t h, reader::Ink ink) override {
        display_.fillRect(x, y, w, h, colour(ink));
    }

    void text(reader::Font font, int16_t x, int16_t y, const char* str,
              reader::Ink ink) override {
        display_.setFont(fontFor(font));
        display_.setTextColor(colour(ink));
        display_.setCursor(x, y);
        display_.print(str);
    }

private:
    /// Walks the framebuffer a page at a time, redrawing the same contents for each.
    ///
    /// This is the paging contract in surface.hpp seen from the other side: GxEPD2 holds
    /// only part of the screen at once and calls back until it has covered all of it, so
    /// a Painter that carried state between calls would draw a different thing per page.
    void paint(const reader::Painter& painter) {
        display_.firstPage();
        do {
            painter.paint(*this);
            // GxEPD2 hands control back once per page, and nextPage() is where the panel
            // actually spends its half second. Servicing here is the only chance the
            // device has to notice a button while the display is working -- without it
            // the reader is deaf for three quarters of every reading cycle.
            serviceWhileBusy();
        } while (display_.nextPage());
        serviceWhileBusy();
    }

    /// GxEPD2 describes a partial window with unsigned bounds where the surface contract
    /// is signed. Nothing above the top edge can be redrawn, so a negative bound becomes
    /// zero rather than wrapping to a window tens of thousands of pixels tall.
    static uint16_t windowCoord(int16_t v) {
        if (v < 0) {
            return 0;
        }
        return static_cast<uint16_t>(v);
    }

    static uint16_t colour(reader::Ink ink) {
        return ink == reader::Ink::kBlack ? GxEPD_BLACK : GxEPD_WHITE;
    }

    static const GFXfont* fontFor(reader::Font font) {
        return font == reader::Font::kChunk ? &FreeMonoBold18pt7b : &FreeMonoBold12pt7b;
    }

    Display& display_;
};
