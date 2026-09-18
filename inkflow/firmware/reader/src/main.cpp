// ABOUTME: The inkflow RSVP reader -- streams chunks of a text file at a fixed focal
// ABOUTME: point on e-paper, with pause, rewind-by-sentence, and adjustable speed.

#include <Arduino.h>
#include <FS.h>
#include <Fonts/FreeMonoBold12pt7b.h>
#include <Fonts/FreeMonoBold18pt7b.h>
#include <GxEPD2_BW.h>
#include <SD.h>
#include <SPI.h>

#include "InputManager.h"
#include "config.h"
#include "rsvp/chunker.hpp"
#include "rsvp/player.hpp"
#include "rsvp/timing.hpp"
#include "rsvp/tokenizer.hpp"

GxEPD2_BW<GxEPD2_426_GDEQ0426T82, GxEPD2_426_GDEQ0426T82::HEIGHT> display(
    GxEPD2_426_GDEQ0426T82(EPD_CS, EPD_DC, EPD_RST, EPD_BUSY));

static InputManager g_input;

static char g_text[kMaxTextBytes];
static size_t g_textLen = 0;
static rsvp::Token g_tokens[kMaxTokens];
static uint32_t g_tokenCount = 0;

static rsvp::TimingConfig g_timing;
static rsvp::ChunkConfig g_chunking;
static uint32_t g_index = 0;
static bool g_playing = false;
static uint32_t g_partialsSinceFlush = 0;
static char g_docName[64] = "built-in";

/// Shown when the SD card has nothing to read, so the device is never a blank screen.
static const char kFallback[] =
    "The panel refreshes slowly, and that turned out to decide everything. "
    "A partial update costs the same five hundred milliseconds whether it "
    "redraws a narrow band or the entire screen.\n\n"
    "So words arrive in threes. One at a time would be readable but far too "
    "slow; three at a time lands where comprehension holds. The hardware "
    "chose the design, and the reading research agreed with it.";

// ---------------------------------------------------------------- text loading

static void loadFallback() {
    g_textLen = sizeof(kFallback) - 1;
    memcpy(g_text, kFallback, g_textLen);
    strncpy(g_docName, "built-in", sizeof(g_docName) - 1);
}

/// Loads the first .txt at the SD root. Returns false if there is nothing to load.
static bool loadFromSd() {
    File root = SD.open("/");
    if (!root) {
        return false;
    }
    for (File f = root.openNextFile(); f; f = root.openNextFile()) {
        const char* name = f.name();
        const size_t n = strlen(name);
        const bool isTxt = n > 4 && strcasecmp(name + n - 4, ".txt") == 0;
        if (f.isDirectory() || !isTxt) {
            f.close();
            continue;
        }
        g_textLen = f.read(reinterpret_cast<uint8_t*>(g_text), kMaxTextBytes);
        strncpy(g_docName, name, sizeof(g_docName) - 1);
        f.close();
        root.close();
        return g_textLen > 0;
    }
    root.close();
    return false;
}

static void tokenizeDocument() {
    rsvp::Tokenizer tokenizer(g_text, g_textLen);
    rsvp::Token token{};
    g_tokenCount = 0;
    while (g_tokenCount < kMaxTokens && tokenizer.next(token)) {
        g_tokens[g_tokenCount++] = token;
    }
}

// ---------------------------------------------------------------- rendering

/// Builds the chunk starting at `start` into `out`, reporting its pivot character index.
static uint32_t buildChunk(uint32_t start, char* out, size_t cap, uint8_t& pivotChar) {
    const uint32_t n = rsvp::chunkLength(g_tokens, g_tokenCount, start, g_chunking);
    size_t len = 0;
    for (uint32_t i = 0; i < n; ++i) {
        if (i > 0 && len + 1 < cap) {
            out[len++] = ' ';
        }
        const rsvp::Token& t = g_tokens[start + i];
        for (uint16_t b = 0; b < t.length && len + 1 < cap; ++b) {
            out[len++] = g_text[t.offset + b];
        }
    }
    out[len] = '\0';
    // The pivot belongs to the chunk's first word, which is where the eye lands.
    pivotChar = g_tokens[start].orp;
    return n;
}

/// Width in pixels of the first `chars` characters of `s`, in the current font.
static int16_t measurePrefix(const char* s, uint8_t chars) {
    char buf[96];
    size_t n = 0;
    for (const char* p = s; *p && n < chars && n + 1 < sizeof(buf); ++p) {
        buf[n++] = *p;
    }
    buf[n] = '\0';
    int16_t x1 = 0, y1 = 0;
    uint16_t w = 0, h = 0;
    display.getTextBounds(buf, 0, 0, &x1, &y1, &w, &h);
    return static_cast<int16_t>(w);
}

/// Static guide marks bracketing the focal column.
///
/// A colour highlight is the usual way to mark the recognition point, and a 1-bit panel
/// cannot do it. These ticks sit outside the band that gets redrawn, so they are drawn
/// once per full refresh and cost nothing per word.
static void drawGuides() {
    display.fillRect(kFocalX - 1, kBandY - 26, 3, 16, GxEPD_BLACK);
    display.fillRect(kFocalX - 1, kBandY + kBandH + 10, 3, 16, GxEPD_BLACK);
}

static void drawStatus() {
    display.setFont(&FreeMonoBold12pt7b);
    display.setCursor(20, 60);
    display.printf("%s", g_docName);
    display.setCursor(20, 95);
    const uint32_t pct = g_tokenCount ? (g_index * 100u) / g_tokenCount : 0u;
    display.printf("%u wpm  %u%%  %s", static_cast<unsigned>(g_timing.wpm),
                   static_cast<unsigned>(pct), g_playing ? "" : "[paused]");
}

/// Full redraw: status, guides, and the current chunk. Also clears ghosting.
static void renderFull() {
    char chunk[96];
    uint8_t pivot = 0;
    buildChunk(g_index, chunk, sizeof(chunk), pivot);

    display.setFullWindow();
    display.firstPage();
    do {
        display.fillScreen(GxEPD_WHITE);
        display.setTextColor(GxEPD_BLACK);
        drawStatus();
        drawGuides();
        display.setFont(&FreeMonoBold18pt7b);
        const int16_t x = static_cast<int16_t>(kFocalX - measurePrefix(chunk, pivot));
        display.setCursor(x, kBandY + 70);
        display.print(chunk);
    } while (display.nextPage());
    g_partialsSinceFlush = 0;
}

/// Redraws only the reading band. This is the hot path, ~542ms measured.
static void renderChunk() {
    char chunk[96];
    uint8_t pivot = 0;
    buildChunk(g_index, chunk, sizeof(chunk), pivot);

    display.setPartialWindow(0, kBandY, display.width(), kBandH);
    display.firstPage();
    do {
        display.fillScreen(GxEPD_WHITE);
        display.setTextColor(GxEPD_BLACK);
        display.setFont(&FreeMonoBold18pt7b);
        const int16_t x = static_cast<int16_t>(kFocalX - measurePrefix(chunk, pivot));
        display.setCursor(x, kBandY + 70);
        display.print(chunk);
    } while (display.nextPage());
    ++g_partialsSinceFlush;
}

// ---------------------------------------------------------------- navigation

/// First token of the sentence containing `from`, for rewind.
static uint32_t sentenceStart(uint32_t from) {
    for (uint32_t i = from; i > 0u; --i) {
        if (g_tokens[i - 1u].has(rsvp::kTokenFlagSentenceEnd)) {
            return i;
        }
    }
    return 0u;
}

/// Rewind is the feature, not a convenience -- suppressing the backward glance is the
/// one thing RSVP inherently does to a reader, and it measurably costs comprehension.
/// Pressing again while already at a sentence start steps into the previous sentence,
/// so repeated presses walk backwards instead of sticking.
static void rewindSentence() {
    const uint32_t start = sentenceStart(g_index);
    if (start < g_index) {
        g_index = start;
    } else if (g_index > 0u) {
        g_index = sentenceStart(g_index - 1u);
    }
}

static void adjustSpeed(int delta) {
    int wpm = static_cast<int>(g_timing.wpm) + delta;
    if (wpm < 60) {
        wpm = 60;
    }
    if (wpm > 900) {
        wpm = 900;
    }
    g_timing.wpm = static_cast<uint16_t>(wpm);
}

// ---------------------------------------------------------------- lifecycle

void setup() {
    Serial.begin(115200);
    delay(500);

    g_input.begin();
    SPI.begin(EPD_SCLK, SD_SPI_MISO, EPD_MOSI, EPD_CS);
    SPISettings spi(kSpiHz, MSBFIRST, SPI_MODE0);
    display.init(115200, true, 2, false, SPI, spi);
    display.setRotation(3);

    if (!SD.begin(SD_SPI_CS, SPI, kSpiHz) || !loadFromSd()) {
        loadFallback();
    }
    tokenizeDocument();

    // Three words per update is not a preference. At the measured 542ms refresh floor
    // one word yields 111 WPM, far below the speed at which comprehension holds.
    g_timing.wpm = 330u;
    g_timing.minHoldMs = rsvp::kPanelPartialRefreshMs;

    Serial.printf("inkflow: %s, %u bytes, %u tokens\n", g_docName,
                  static_cast<unsigned>(g_textLen), static_cast<unsigned>(g_tokenCount));

    renderFull();
}

void loop() {
    g_input.update();

    if (g_input.wasPressed(kBtnConfirm)) {
        g_playing = !g_playing;
        renderFull();
    } else if (g_input.wasPressed(kBtnLeft)) {
        rewindSentence();
        renderFull();
    } else if (g_input.wasPressed(kBtnUp)) {
        adjustSpeed(+30);
        renderFull();
    } else if (g_input.wasPressed(kBtnDown)) {
        adjustSpeed(-30);
        renderFull();
    } else if (g_input.wasPressed(kBtnBack)) {
        renderFull();
    }

    if (!g_playing || g_index >= g_tokenCount) {
        delay(20);
        return;
    }

    const uint32_t chunkLen =
        rsvp::chunkLength(g_tokens, g_tokenCount, g_index, g_chunking);
    if (chunkLen == 0u) {
        g_playing = false;
        return;
    }

    // The chunk is held for as long as its slowest token needs -- a chunk ending a
    // sentence carries that sentence's pause.
    uint32_t hold = 0;
    for (uint32_t i = 0; i < chunkLen; ++i) {
        const uint32_t h = rsvp::holdMs(g_tokens[g_index + i], g_timing);
        if (h > hold) {
            hold = h;
        }
    }

    const uint32_t started = millis();

    // Ghosting accrues over successive partial updates. Spend the 1958ms full refresh
    // on a paragraph boundary, where the timing model already inserts a beat, so it
    // reads as an intentional pause rather than a fault.
    const bool atParagraph =
        g_tokens[g_index + chunkLen - 1u].has(rsvp::kTokenFlagParagraphEnd);
    if (g_partialsSinceFlush >= kPartialsBeforeFlush && atParagraph) {
        renderFull();
    } else {
        renderChunk();
    }

    g_index += chunkLen;

    // Refresh time counts against the hold: the panel already spent it showing the word.
    const uint32_t spent = millis() - started;
    if (hold > spent) {
        delay(hold - spent);
    }
}
