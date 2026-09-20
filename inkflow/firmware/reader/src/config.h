// ABOUTME: Hardware pin map, panel rotation and WiFi transfer settings for the Xteink X4.
// ABOUTME: Pin values are confirmed working on hardware, not copied from a guide.

#pragma once

#include <cstdint>

// The reading frame's geometry and the document limits live in the shared module, not
// here. They were duplicated once, and a device drawing a layout the simulator does not
// is the failure this file must not reintroduce -- so the definitions are pulled in
// rather than restated.
#include "reader/document.hpp"
#include "reader/layout.hpp"

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

// Landscape, not portrait. The panel is natively 800x480 and a page-based reader is
// held portrait, but RSVP wants one wide line and no height at all. Landscape gives a
// chunk 500px to the right of the focal column where portrait gives 290px, and portrait
// overran the right edge by 88px. reader/layout.hpp carries the geometry itself and the
// rest of that reasoning; this is the rotation that produces it.
//
// sim/ renders reader::kLandscape and exits non-zero if any frame overflows, so the
// geometry is checked rather than assumed -- which only holds while the device and the
// simulator read it from the same place.
static constexpr uint8_t kRotation = 0;  // 800x480 landscape

// WiFi transfer. The radio is the largest single power draw on a 650 mAh cell, so it
// runs only while the reader is explicitly in transfer mode, never in the background.
// Power is a firmware responsibility on this device: there is no latch, and "off" is
// ESP32 deep sleep with the power button armed as the wake source. A device that never
// sleeps runs until its cell is flat, which is a plausible contributor to the panel
// trouble recorded in hardware-notes/bringup-20260918.md.
//
// One second, matching the community sample firmware's documented behaviour, so the
// device behaves the way an X4 owner already expects. A deliberate hold rather than a tap
// because this button is the one thing that cannot be undone by pressing it again, and a
// reader should not lose their page to a brush against a pocket.
static constexpr uint32_t kPowerHoldMs = 1000u;

static constexpr char kApSsid[] = "inkflow";
static constexpr char kApPassword[] = "inkflow-reader";  // WPA2 needs 8+ characters
static constexpr uint16_t kHttpPort = 80;
