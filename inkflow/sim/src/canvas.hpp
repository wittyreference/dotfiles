// ABOUTME: A 1-bit drawing surface with the Adafruit GFX text model, rendered in
// ABOUTME: software so the reader's screen can be inspected without hardware.

#pragma once

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>

#include "../vendor/gfxfont.h"

namespace sim {

constexpr uint16_t kBlack = 0;
constexpr uint16_t kWhite = 1;

/// A monochrome framebuffer that draws text exactly as the device does.
///
/// The glyph walk below is the same one Adafruit_GFX performs: per-glyph bitmaps packed
/// MSB-first, positioned by xOffset/yOffset from the cursor baseline, advanced by
/// xAdvance. Reimplemented rather than linked because the library is entangled with
/// Arduino headers, and the part that matters is forty lines of bit-walking.
///
/// Because it is the same walk over the same font data, what this produces is what the
/// panel produces. That is the entire point: a layout bug can be seen here instead of
/// being read aloud off a device.
class Canvas {
public:
    Canvas(int w, int h) : w_(w), h_(h), px_(new uint8_t[size_t(w) * size_t(h)]) {
        fillScreen(kWhite);
    }
    ~Canvas() { delete[] px_; }
    Canvas(const Canvas&) = delete;
    Canvas& operator=(const Canvas&) = delete;

    int width() const { return w_; }
    int height() const { return h_; }

    void fillScreen(uint16_t colour) {
        std::memset(px_, colour == kBlack ? 0 : 255, size_t(w_) * size_t(h_));
    }

    void fillRect(int x, int y, int w, int h, uint16_t colour) {
        for (int j = y; j < y + h; ++j) {
            for (int i = x; i < x + w; ++i) {
                setPixel(i, j, colour);
            }
        }
    }

    /// Outlines a region without filling it, for marking update windows.
    void frameRect(int x, int y, int w, int h, uint16_t colour) {
        for (int i = x; i < x + w; ++i) {
            setPixel(i, y, colour);
            setPixel(i, y + h - 1, colour);
        }
        for (int j = y; j < y + h; ++j) {
            setPixel(x, j, colour);
            setPixel(x + w - 1, j, colour);
        }
    }

    void setFont(const GFXfont* f) { font_ = f; }
    void setTextColor(uint16_t c) { colour_ = c; }
    void setCursor(int x, int y) { cx_ = x; cy_ = y; }

    void print(const char* s) {
        for (const char* p = s; *p; ++p) {
            drawChar(*p);
        }
    }

    /// Width and height the string would occupy, matching GFX's getTextBounds.
    void textBounds(const char* s, int& w, int& h) const {
        w = 0;
        h = 0;
        if (font_ == nullptr) {
            return;
        }
        for (const char* p = s; *p; ++p) {
            const GFXglyph* g = glyph(*p);
            if (g == nullptr) {
                continue;
            }
            w += g->xAdvance;
            if (g->height > h) {
                h = g->height;
            }
        }
    }

    bool writePng(const char* path) const;

private:
    const GFXglyph* glyph(char c) const {
        const auto u = uint8_t(c);
        if (font_ == nullptr || u < font_->first || u > font_->last) {
            return nullptr;
        }
        return &font_->glyph[u - font_->first];
    }

    void setPixel(int x, int y, uint16_t colour) {
        if (x < 0 || y < 0 || x >= w_ || y >= h_) {
            return;
        }
        px_[size_t(y) * size_t(w_) + size_t(x)] = colour == kBlack ? 0 : 255;
    }

    void drawChar(char c) {
        const GFXglyph* g = glyph(c);
        if (g == nullptr) {
            return;
        }
        const uint8_t* bitmap = font_->bitmap + g->bitmapOffset;
        uint16_t bit = 0;
        uint8_t bits = 0;
        for (uint8_t yy = 0; yy < g->height; ++yy) {
            for (uint8_t xx = 0; xx < g->width; ++xx) {
                if ((bit++ & 7) == 0) {
                    bits = *bitmap++;
                }
                if (bits & 0x80) {
                    setPixel(cx_ + g->xOffset + xx, cy_ + g->yOffset + yy, colour_);
                }
                bits <<= 1;
            }
        }
        cx_ += g->xAdvance;
    }

    int w_, h_;
    uint8_t* px_;
    const GFXfont* font_ = nullptr;
    uint16_t colour_ = kBlack;
    int cx_ = 0, cy_ = 0;
};

}  // namespace sim
