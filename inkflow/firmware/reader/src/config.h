// ABOUTME: Hardware pin map and reader tuning constants for the Xteink X4.
// ABOUTME: Pin values are confirmed working on hardware, not copied from a guide.

#pragma once

#include <cstdint>

// Display (SSD1677 / GDEQ0426T82), confirmed on device
#define EPD_SCLK 8
#define EPD_MOSI 10
#define EPD_CS 21
#define EPD_DC 4
#define EPD_RST 5
#define EPD_BUSY 6

#define SD_SPI_CS 12
#define SD_SPI_MISO 7

static constexpr uint32_t kSpiHz = 40000000;

// Buttons, as indexed by the community SDK's InputManager
enum Button : uint8_t {
    kBtnBack = 0,
    kBtnConfirm = 1,
    kBtnLeft = 2,
    kBtnRight = 3,
    kBtnUp = 4,
    kBtnDown = 5,
    kBtnPower = 6,
};

// Document limits.
//
// The device has 400 KB of SRAM with no PSRAM, and a full 800x480 framebuffer already
// costs 48 KB. These caps keep text plus its token index inside what remains, and are
// deliberately modest: roughly 6000 words is about half an hour of reading, which is a
// session rather than a novel.
static constexpr size_t kMaxTextBytes = 48u * 1024u;
static constexpr size_t kMaxTokens = 6000u;

// Reading band: where words appear, in the rotated 480x800 portrait frame.
static constexpr int16_t kBandY = 330;
static constexpr int16_t kBandH = 120;

/// Horizontal position of the optimal recognition point.
///
/// Slightly left of centre, matching where the eye settles on a word. Every chunk is
/// positioned so its pivot character lands on this column, which is the whole mechanism
/// -- the eye fixates here once and never travels.
static constexpr int16_t kFocalX = 190;

// A full refresh clears ghosting but costs 1958ms (measured), so it is spent only where
// the timing model already inserts a pause and the reader will not feel it as a stall.
static constexpr uint32_t kPartialsBeforeFlush = 60u;
