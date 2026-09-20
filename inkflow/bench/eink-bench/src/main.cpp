// ABOUTME: On-device measurement of SSD1677 refresh latency and power draw, writing
// ABOUTME: results to SD as CSV. Produces the numbers TimingConfig::minHoldMs needs.

#include <Arduino.h>
#include <FS.h>
#include <Fonts/FreeMonoBold12pt7b.h>
#include <GxEPD2_BW.h>
#include <SD.h>
#include <SPI.h>

#include "bench/samples.hpp"

// Pinout confirmed on hardware, not taken from a guide.
#define EPD_SCLK 8
#define EPD_MOSI 10
#define EPD_CS 21
#define EPD_DC 4
#define EPD_RST 5
#define EPD_BUSY 6
#define SD_SPI_CS 12
#define SD_SPI_MISO 7
#define BAT_GPIO0 0
#define USB_DETECT 20

static constexpr uint32_t kSpiHz = 40000000;

// Native panel is 800x480. Rotation 3 presents it as 480 wide x 800 tall, which is how
// the device is physically held, so measurements are taken in that frame.
GxEPD2_BW<GxEPD2_426_GDEQ0426T82, GxEPD2_426_GDEQ0426T82::HEIGHT> display(
    GxEPD2_426_GDEQ0426T82(EPD_CS, EPD_DC, EPD_RST, EPD_BUSY));

static bool g_sdReady = false;

/// Band heights swept for windowed partial updates, in the rotated frame.
///
/// This sweep is the project's central question. RSVP only ever changes one word, so if
/// a narrow band redraws far faster than the full screen, the refresh budget stops
/// being the binding constraint. 800 is the whole screen, included as the baseline the
/// others are compared against.
static const uint16_t kBandHeights[] = {40, 80, 120, 200, 400, 800};
static constexpr size_t kBandCount = sizeof(kBandHeights) / sizeof(kBandHeights[0]);

static constexpr size_t kWarmup = 2;      // discarded: the first refresh after idle is atypical
static constexpr size_t kRepeats = 12;    // per configuration
static constexpr size_t kSustained = 100; // for the power/ghosting run

/// Battery voltage in millivolts, averaged to damp ADC noise.
static uint32_t batteryMilliVolts() {
    uint32_t total = 0;
    for (int i = 0; i < 16; ++i) {
        total += static_cast<uint32_t>(analogReadMilliVolts(BAT_GPIO0));
    }
    return total / 16u;
}

static bool usbConnected() { return digitalRead(USB_DETECT) == HIGH; }

/// Draws a band of alternating content so each refresh actually changes pixels.
///
/// Measuring a redraw of identical content would be dishonest: controllers can skip
/// unchanged regions, and the result would flatter the panel.
static void drawBand(uint16_t y, uint16_t h, bool inverted) {
    display.fillScreen(inverted ? GxEPD_BLACK : GxEPD_WHITE);
    display.setTextColor(inverted ? GxEPD_WHITE : GxEPD_BLACK);
    display.setFont(&FreeMonoBold12pt7b);
    display.setCursor(20, static_cast<int16_t>(y + (h / 2)));
    display.printf("band %u h=%u", static_cast<unsigned>(y), static_cast<unsigned>(h));
}

/// One windowed partial update, returning how long it took in microseconds.
static uint32_t timeWindowedUpdate(uint16_t y, uint16_t h, bool inverted) {
    const uint32_t start = micros();
    display.setPartialWindow(0, y, display.width(), h);
    display.firstPage();
    do {
        drawBand(y, h, inverted);
    } while (display.nextPage());
    return micros() - start;
}

static uint32_t timeFullUpdate(bool inverted) {
    const uint32_t start = micros();
    display.setFullWindow();
    display.firstPage();
    do {
        drawBand(0, display.height(), inverted);
    } while (display.nextPage());
    return micros() - start;
}

/// Appends a CSV row, creating the file with a header on first write.
static void emit(const char* phase, uint32_t param, const bench::Samples<128>& s,
                 uint32_t mvStart, uint32_t mvEnd) {
    Serial.printf("%s,%u,%u,%u,%u,%u,%u,%u,%u,%u\n", phase, static_cast<unsigned>(param),
                  static_cast<unsigned>(s.count()), static_cast<unsigned>(s.min()),
                  static_cast<unsigned>(s.median()), static_cast<unsigned>(s.mean()),
                  static_cast<unsigned>(s.p95()), static_cast<unsigned>(s.max()),
                  static_cast<unsigned>(mvStart), static_cast<unsigned>(mvEnd));

    if (!g_sdReady) {
        return;
    }
    const bool fresh = !SD.exists("/bench-results.csv");
    File f = SD.open("/bench-results.csv", FILE_APPEND);
    if (!f) {
        return;
    }
    if (fresh) {
        f.println("phase,param,n,min_us,median_us,mean_us,p95_us,max_us,mv_start,mv_end");
    }
    f.printf("%s,%u,%u,%u,%u,%u,%u,%u,%u,%u\n", phase, static_cast<unsigned>(param),
             static_cast<unsigned>(s.count()), static_cast<unsigned>(s.min()),
             static_cast<unsigned>(s.median()), static_cast<unsigned>(s.mean()),
             static_cast<unsigned>(s.p95()), static_cast<unsigned>(s.max()),
             static_cast<unsigned>(mvStart), static_cast<unsigned>(mvEnd));
    f.close();
}

static void runFullRefresh() {
    bench::Samples<128> s;
    const uint32_t mv0 = batteryMilliVolts();
    for (size_t i = 0; i < kWarmup + kRepeats; ++i) {
        const uint32_t us = timeFullUpdate(i % 2 == 0);
        if (i >= kWarmup) {
            s.add(us);
        }
    }
    emit("full", display.height(), s, mv0, batteryMilliVolts());
}

static void runBandSweep() {
    for (size_t b = 0; b < kBandCount; ++b) {
        const uint16_t h = kBandHeights[b];
        const uint16_t y = static_cast<uint16_t>((display.height() - h) / 2);
        bench::Samples<128> s;
        const uint32_t mv0 = batteryMilliVolts();
        for (size_t i = 0; i < kWarmup + kRepeats; ++i) {
            const uint32_t us = timeWindowedUpdate(y, h, i % 2 == 0);
            if (i >= kWarmup) {
                s.add(us);
            }
        }
        emit("partial_band", h, s, mv0, batteryMilliVolts());
    }
}

/// Sustained run at a realistic RSVP band, for power draw and ghosting.
///
/// Normal e-reading refreshes once per 30 s; RSVP refreshes several times a second. The
/// battery delta across this run is the first evidence of what that costs. Ghosting is
/// assessed by eye at the end -- there is no sensor for it.
static void runSustained() {
    const uint16_t h = 120;
    const uint16_t y = static_cast<uint16_t>((display.height() - h) / 2);
    bench::Samples<128> s;
    const uint32_t mv0 = batteryMilliVolts();
    for (size_t i = 0; i < kSustained; ++i) {
        const uint32_t us = timeWindowedUpdate(y, h, i % 2 == 0);
        if (i < 128) {
            s.add(us);
        }
    }
    emit("sustained", h, s, mv0, batteryMilliVolts());
}

/// Windowed updates with concurrent SD reads, since SD and EPD share the SPI bus.
static void runSdContention() {
    if (!g_sdReady) {
        return;
    }
    const uint16_t h = 120;
    const uint16_t y = static_cast<uint16_t>((display.height() - h) / 2);
    bench::Samples<128> s;
    const uint32_t mv0 = batteryMilliVolts();
    uint8_t scratch[512];
    for (size_t i = 0; i < kWarmup + kRepeats; ++i) {
        File f = SD.open("/bench-results.csv", FILE_READ);
        if (f) {
            f.read(scratch, sizeof(scratch));
            f.close();
        }
        const uint32_t us = timeWindowedUpdate(y, h, i % 2 == 0);
        if (i >= kWarmup) {
            s.add(us);
        }
    }
    emit("sd_contention", h, s, mv0, batteryMilliVolts());
}

void setup() {
    Serial.begin(115200);
    delay(2000);

    pinMode(USB_DETECT, INPUT);
    analogReadResolution(12);

    SPI.begin(EPD_SCLK, SD_SPI_MISO, EPD_MOSI, EPD_CS);
    SPISettings spi(kSpiHz, MSBFIRST, SPI_MODE0);
    // The 4th argument is GxEPD2's reset_duration in milliseconds. It was 2, against the
    // 10 ms the panel's own documentation specifies for the reset pulse -- see
    // open-x4-sdk/libs/display/EInkDisplay/doc/SSD1677_GUIDE.md, "Reset pulse | 10ms".
    // A short pulse is one of the two named suspects for the display that stopped updating
    // in hardware-notes/bringup-20260918.md; the other, a flat battery, is addressed by the
    // power button. Raised on the panel maker's authority rather than on a measurement,
    // because there is no measurement that distinguishes them.
    display.init(115200, true, 10, false, SPI, spi);
    display.setRotation(3);

    g_sdReady = SD.begin(SD_SPI_CS, SPI, kSpiHz);

    Serial.println("=== eink-bench ===");
    Serial.printf("panel %ux%u (rotated), sd=%s, usb=%s, batt=%umV\n",
                  static_cast<unsigned>(display.width()),
                  static_cast<unsigned>(display.height()), g_sdReady ? "ok" : "MISSING",
                  usbConnected() ? "yes" : "no",
                  static_cast<unsigned>(batteryMilliVolts()));
    Serial.println("phase,param,n,min_us,median_us,mean_us,p95_us,max_us,mv_start,mv_end");

    runFullRefresh();
    runBandSweep();
    runSdContention();
    runSustained();

    // Final full refresh clears accumulated ghosting so the closing screen is readable.
    display.setFullWindow();
    display.firstPage();
    do {
        display.fillScreen(GxEPD_WHITE);
        display.setTextColor(GxEPD_BLACK);
        display.setFont(&FreeMonoBold12pt7b);
        display.setCursor(20, 60);
        display.print("eink-bench complete");
        display.setCursor(20, 110);
        display.printf("SD: %s", g_sdReady ? "/bench-results.csv" : "NOT WRITTEN");
        display.setCursor(20, 160);
        display.printf("batt %umV", static_cast<unsigned>(batteryMilliVolts()));
    } while (display.nextPage());

    Serial.println("=== done ===");
    display.hibernate();
}

void loop() { delay(10000); }
