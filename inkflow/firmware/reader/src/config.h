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
// These bound only the in-RAM fallback for a small .txt. A .rsvp sidecar streams and
// is not limited by them -- the test book is 98,633 tokens, sixty times this cap.
// Kept small deliberately: the WiFi stack needs the space more than a fallback path
// does, and anything long enough to care should be a sidecar.
static constexpr size_t kMaxTextBytes = 16u * 1024u;
static constexpr size_t kMaxTokens = 2000u;

// Landscape, not portrait. The panel is natively 800x480 and a page-based reader is
// held portrait, but RSVP wants one wide line and no height at all. In portrait the
// chunk overran the right edge by 88px, because it is positioned by its pivot -- which
// sits near the start of the first word -- so nearly the whole chunk extends rightward.
// The budget is the screen width minus the focal column, and at 480px wide that is 290px
// for text that needs 400. Landscape gives 500px and the problem disappears.
//
// sim/ renders this layout and exits non-zero if any frame overflows, so the geometry
// below is checked rather than assumed.
static constexpr uint8_t kRotation = 0;  // 800x480 landscape
static constexpr int16_t kBandY = 180;
static constexpr int16_t kBandH = 120;

/// Horizontal position of the optimal recognition point.
///
/// Slightly left of centre, matching where the eye settles on a word. Every chunk is
/// positioned so its pivot character lands on this column, which is the whole mechanism
/// -- the eye fixates here once and never travels.
static constexpr int16_t kFocalX = 300;

// A full refresh clears ghosting but costs 1958ms (measured), so it is spent only where
// the timing model already inserts a pause and the reader will not feel it as a stall.
static constexpr uint32_t kPartialsBeforeFlush = 60u;

// WiFi transfer. The radio is the largest single power draw on a 650 mAh cell, so it
// runs only while the reader is explicitly in transfer mode, never in the background.
static constexpr char kApSsid[] = "inkflow";
static constexpr char kApPassword[] = "inkflow-reader";  // WPA2 needs 8+ characters
static constexpr uint16_t kHttpPort = 80;
