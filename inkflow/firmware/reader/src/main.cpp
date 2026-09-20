// ABOUTME: The inkflow RSVP reader -- streams chunks of a text file at a fixed focal
// ABOUTME: point on e-paper, with pause, rewind-by-sentence, and adjustable speed.

#include <Arduino.h>
#include <esp_sleep.h>
#include <FS.h>
#include <Fonts/FreeMonoBold12pt7b.h>
#include <Fonts/FreeMonoBold18pt7b.h>
#include <GxEPD2_BW.h>
#include <SD.h>
#include <SPI.h>

#include <Preferences.h>

#include "InputManager.h"
#include "config.h"
#include "gxepd2_surface.h"
#include "sd_source.h"
#include "transfer.h"
#include "reader/document.hpp"
#include "reader/picker.hpp"
#include "reader/layout.hpp"
#include "reader/reader.hpp"
#include "rsvp/timing.hpp"

using Display = GxEPD2_BW<GxEPD2_426_GDEQ0426T82, GxEPD2_426_GDEQ0426T82::HEIGHT>;

Display display(GxEPD2_426_GDEQ0426T82(EPD_CS, EPD_DC, EPD_RST, EPD_BUSY));

static InputManager g_input;

// The reading loop itself is in reader/, shared with the simulator. What is left here is
// the wiring: a panel, a card, buttons and NVS. Anything about how reading behaves --
// chunking, pacing, refresh policy, rewind -- belongs on the other side of that line,
// where a host test can see it.
static GxEpd2Surface<Display> g_surface(display);
static SdSource g_source;
static reader::Document g_doc;
static reader::Reader g_reader(g_doc, g_surface, reader::kLandscape);
/// The book list. Populated only when the reader asks for it, because the walk that
/// fills it is the one that once took 108 seconds in silence.
static reader::Picker g_picker(g_surface, reader::kLandscape);

static Preferences g_prefs;
static Transfer g_transfer;
/// The SDK's own names, in its own index order. Copied rather than borrowed because the
/// SDK keeps its table private, and vendored code is not ours to edit.
static const char* const kButtonNames[] = {"Back",  "Confirm", "Left", "Right",
                                           "Up",    "Down",    "Power"};

static bool g_transferMode = false;
/// Whether the card mounted at boot. A missing card is a normal state: the reader falls
/// back to a built-in passage, and the diagnostics simply go unwritten.
static bool g_sdReady = false;
static char g_docName[64] = "built-in";

/// Shown when the SD card has nothing to read, so the device is never a blank screen.
static const char kFallback[] =
    "The panel refreshes slowly, and that turned out to decide everything. "
    "A partial update costs the same five hundred milliseconds whether it "
    "redraws a narrow band or the entire screen.\n\n"
    "So words arrive in threes. One at a time would be readable but far too "
    "slow; three at a time lands where comprehension holds. The hardware "
    "chose the design, and the reading research agreed with it.";

// ---------------------------------------------------------------- document

/// Records what the reader is reading. The name is both the status line and the key the
/// resume position is stored under, so the two can never disagree.
static void setDocumentName(const char* name) {
    strncpy(g_docName, name, sizeof(g_docName) - 1);
    g_docName[sizeof(g_docName) - 1] = '\0';
    g_reader.setName(g_docName);
}

// Nothing is printed and nothing is drawn until loadDocument() returns, so a long walk of
// the card's root is indistinguishable from a hang: a card holding neither a .rsvp nor a
// .txt walked its entire root for 108 seconds in silence on hardware.
//
// So the scan reports progress and stops. Progress goes on a time interval rather than
// every N entries because the symptom is silence -- an interval puts a line on the port
// every two seconds however slow the card is, where a count stays quiet for as long as a
// slow card takes to grind through it. Per-entry logging would flood the port at 115200
// baud and the printing would itself slow the scan.
//
// The stop is counted in entries, not seconds, because a limit in entries is a limit on
// what can be missed: 4000 root entries is room for a thousand books and their sidecars,
// doubled again for the AppleDouble twins a Mac leaves beside every file it copies to a
// card. A root larger than that is a scratch card, not a reader's shelf.
static constexpr uint32_t kScanReportMs = 2000u;
static constexpr uint32_t kScanMaxEntries = 4000u;

/// Opens the first .rsvp on the card, else the first .txt, else the built-in passage.
///
/// .rsvp is preferred because it streams: a real book is far past what RAM holds, and
/// the sidecar exists so the device can seek into a flat token array rather than build
/// one. A .txt is still accepted for short pieces and is tokenised in memory.
/// Fills the picker with everything on the card a reader could open.
///
/// The same walk `loadDocument` does, and bounded the same way, but keeping every match
/// rather than stopping at the first sidecar. Separate rather than folded together
/// because they answer different questions -- "what do I open now" wants to stop early,
/// "what could I open" cannot -- and because the reader only pays for this walk when it
/// asks for the list.
static void listDocuments() {
    g_picker.clear();

    File root = SD.open("/");
    if (!root) {
        return;
    }
    uint32_t scanned = 0;
    uint32_t reportedAt = millis();
    for (File f = root.openNextFile(); f; f = root.openNextFile()) {
        const char* name = f.name();
        const size_t n = strlen(name);
        const bool dir = f.isDirectory();
        const bool rsvp = n > 5 && strcasecmp(name + n - 5, ".rsvp") == 0;
        const bool txt = n > 4 && strcasecmp(name + n - 4, ".txt") == 0;
        if (!dir && (rsvp || txt)) {
            // Without its leading slash: the name is what the reader reads, and the
            // picker hands it back to be opened with one put on again.
            if (!g_picker.add(name[0] == '/' ? name + 1 : name)) {
                Serial.println("inkflow: picker full, later books not listed");
                f.close();
                break;
            }
        }
        f.close();
        if (++scanned >= kScanMaxEntries) {
            Serial.printf("inkflow: sd scan stopped at %u entries\n",
                          static_cast<unsigned>(scanned));
            break;
        }
        const uint32_t now = millis();
        if (now - reportedAt >= kScanReportMs) {
            reportedAt = now;
            Serial.printf("inkflow: scanning sd, %u entries\n",
                          static_cast<unsigned>(scanned));
        }
    }
    root.close();
    Serial.printf("inkflow: %u books on the card\n",
                  static_cast<unsigned>(g_picker.count()));
}

static void loadDocument() {
    g_doc.close();
    g_source.close();

    char best[64] = {0};
    bool bestIsSidecar = false;

    File root = SD.open("/");
    if (root) {
        uint32_t scanned = 0;
        uint32_t reportedAt = millis();
        for (File f = root.openNextFile(); f; f = root.openNextFile()) {
            const char* name = f.name();
            const size_t n = strlen(name);
            const bool dir = f.isDirectory();
            const bool rsvp = n > 5 && strcasecmp(name + n - 5, ".rsvp") == 0;
            const bool txt = n > 4 && strcasecmp(name + n - 4, ".txt") == 0;
            if (!dir && (rsvp || txt) && (rsvp || !bestIsSidecar) && (rsvp ? !bestIsSidecar : best[0] == 0)) {
                snprintf(best, sizeof(best), "/%s", name[0] == '/' ? name + 1 : name);
                bestIsSidecar = rsvp;
            }
            f.close();
            if (bestIsSidecar) {
                break;
            }
            // Announced rather than silent: a truncated scan that said nothing would be
            // indistinguishable from an empty card, and whatever .txt was found before
            // the limit is still the document about to be opened.
            if (++scanned >= kScanMaxEntries) {
                Serial.printf("inkflow: sd scan stopped at %u entries\n",
                              static_cast<unsigned>(scanned));
                break;
            }
            const uint32_t now = millis();
            if (now - reportedAt >= kScanReportMs) {
                reportedAt = now;
                Serial.printf("inkflow: scanning sd, %u entries\n",
                              static_cast<unsigned>(scanned));
            }
        }
        root.close();
    }

    if (bestIsSidecar) {
        if (g_source.open(best) && g_doc.openSidecar(g_source)) {
            setDocumentName(best + 1);
            return;
        }
        // A sidecar that will not parse must not keep the card's handle open behind the
        // fallback the reader is about to show instead.
        g_source.close();
    }
    if (best[0] != 0) {
        File f = SD.open(best, FILE_READ);
        if (f) {
            static char buffer[reader::kMaxTextBytes];
            const size_t got =
                f.read(reinterpret_cast<uint8_t*>(buffer), reader::kMaxTextBytes);
            f.close();
            if (got > 0) {
                g_doc.useMemory(buffer, got);
                setDocumentName(best + 1);
                return;
            }
        }
    }
    g_doc.useMemory(kFallback, sizeof(kFallback) - 1);
    setDocumentName("built-in");
}

// ---------------------------------------------------------------- rendering

/// Transfer-mode screen: how to connect, and what has arrived.
///
/// Drawn straight onto the panel rather than through the reader's surface: this is not a
/// reading frame, it has no chunk and no focal column, and nothing about it needs to be
/// the same code the simulator runs.
static void renderTransfer() {
    display.setFullWindow();
    display.firstPage();
    do {
        display.fillScreen(GxEPD_WHITE);
        display.setTextColor(GxEPD_BLACK);
        display.setFont(&FreeMonoBold18pt7b);
        display.setCursor(20, 60);
        display.print("WiFi transfer");

        display.setFont(&FreeMonoBold12pt7b);
        display.setCursor(20, 130);
        display.printf("network: %s", kApSsid);
        display.setCursor(20, 165);
        display.printf("password: %s", kApPassword);
        display.setCursor(20, 200);
        display.printf("open: http://%s", g_transfer.ip().toString().c_str());

        display.setCursor(20, 270);
        display.printf("connected: %u", static_cast<unsigned>(g_transfer.clients()));

        if (g_transfer.lastBytes() > 0) {
            display.setCursor(20, 320);
            display.print("received:");
            display.setCursor(20, 355);
            display.printf("%s", g_transfer.lastUpload());
            display.setCursor(20, 390);
            display.printf("%u KB", static_cast<unsigned>(g_transfer.lastBytes() / 1024u));
        }

        display.setCursor(20, 470);
        display.print("Back to exit");
    } while (display.nextPage());
}

// ---------------------------------------------------------------- lifecycle

/// Saves the reading position. Called before sleeping and as playback advances.
///
/// Sleeping without this would cost the reader their page, which is exactly the thing the
/// resume machinery exists to protect.
static void savePosition() {
    g_prefs.putString("doc", g_docName);
    g_prefs.putUInt("pos", g_reader.index());
}

/// Appends one reading to `/battery.csv` on the card.
///
/// Opened and closed per line rather than held open: the measurement that matters is
/// taken on battery, and a run on battery ends when the cell does. A file held open
/// across that loses whatever was buffered, which is the tail of the curve -- the part
/// worth having. One open per 16 tokens is roughly one per ten seconds, against a card
/// that is already being read from continuously.
///
/// Silent on failure. A missing card is a normal state for this device, and a reader
/// mid-sentence should not be interrupted to be told that the diagnostics are not being
/// written.
static void logBattery(uint32_t index, uint32_t millivolts) {
    if (!g_sdReady) {
        return;
    }
    File f = SD.open("/battery.csv", FILE_APPEND);
    if (!f) {
        return;
    }
    // millis() rather than a wall clock: the device has no RTC, and elapsed time since
    // boot is what a discharge curve is plotted against anyway.
    f.printf("%lu,%u,%u\n", static_cast<unsigned long>(millis()),
             static_cast<unsigned>(index), static_cast<unsigned>(millivolts));
    f.close();
}

/// Switches the device off. E-paper holds its image with no power, so the last thing drawn
/// is what the reader sees while it is off -- which is why this draws a screen of its own
/// rather than leaving three words sitting there looking like a device still waiting.
static void enterDeepSleep() {
    Serial.println("inkflow: sleeping");
    savePosition();
    g_reader.setPlaying(false);
    // After savePosition, so the progress figure drawn is the one NVS holds.
    g_reader.renderSleep();

    // Arm the wake source before sleeping. Forgetting this is how a device fails to come
    // back on and looks bricked.
    esp_deep_sleep_enable_gpio_wakeup(1ULL << InputManager::POWER_BUTTON_PIN, ESP_GPIO_WAKEUP_GPIO_LOW);
    Serial.flush();
    esp_deep_sleep_start();
}

/// Rejects a wake that was not deliberate.
///
/// The wake source is a level on the power pin, so anything that holds it low -- a pocket,
/// a bag -- wakes the device. Requiring the press to persist means an accidental nudge
/// costs a moment of deep-sleep current rather than an evening of battery.
static void requireDeliberateWake() {
    if (esp_sleep_get_wakeup_cause() != ESP_SLEEP_WAKEUP_GPIO) {
        return;
    }
    const uint32_t started = millis();
    while (millis() - started < kPowerHoldMs) {
        if (digitalRead(InputManager::POWER_BUTTON_PIN) != LOW) {
            esp_deep_sleep_enable_gpio_wakeup(1ULL << InputManager::POWER_BUTTON_PIN, ESP_GPIO_WAKEUP_GPIO_LOW);
            esp_deep_sleep_start();
        }
        delay(10);
    }
}

void setup() {
    Serial.begin(115200);
    // Long enough for the host to reopen the port after USB re-enumerates, otherwise
    // every startup line is lost and a healthy boot looks identical to a hang.
    delay(2500);
    Serial.println("inkflow: boot");

    g_input.begin();
    Serial.println("inkflow: input ok");

    // Before anything expensive -- the display costs two seconds to bring up -- reject a
    // wake that was not deliberate. This runs after begin() because it reads the power pin
    // directly and begin() is what configures its pull-up; reading it first would sample a
    // floating input.
    requireDeliberateWake();
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
    display.setRotation(kRotation);
    Serial.println("inkflow: display ok");

    g_sdReady = SD.begin(SD_SPI_CS, SPI, kSpiHz);
    Serial.printf("inkflow: sd %s\n", g_sdReady ? "ok" : "absent");
    loadDocument();
    Serial.println("inkflow: document loaded");

    // Resume where the last session stopped. The position is keyed by document name, so
    // swapping the card does not drop the reader into a random paragraph of another book.
    // A position past the end of this document is ignored rather than honoured.
    g_prefs.begin("inkflow", false);
    // Guarded by isKey() because reading an absent key logs inside Preferences at ERROR
    // level, and a first run has no saved position -- an error line for the normal case
    // is what makes a real fault hard to spot in the log later. isKey() reaches NVS
    // directly and says nothing; the comparison below is unchanged when the key is there.
    if (g_prefs.isKey("doc") && g_prefs.getString("doc", "") == String(g_docName)) {
        g_reader.seek(g_prefs.getUInt("pos", 0u));
    }

    // Three words per update is not a preference. At the measured 542ms refresh floor
    // one word yields 111 WPM, far below the speed at which comprehension holds.
    rsvp::TimingConfig timing;
    timing.wpm = 330u;
    timing.minHoldMs = rsvp::kPanelPartialRefreshMs;
    g_reader.setTiming(timing);

    Serial.printf("inkflow: %s, %u tokens, %s, resume at %u\n", g_docName,
                  static_cast<unsigned>(g_doc.count()),
                  g_doc.streaming() ? "streamed" : "in RAM",
                  static_cast<unsigned>(g_reader.index()));

    // The press that woke the device is still down. Letting the main loop see its release
    // would read the wake as a request to sleep again.
    while (digitalRead(InputManager::POWER_BUTTON_PIN) == LOW) {
        delay(10);
    }

    g_reader.renderFull();
}

void loop() {
    static uint32_t lastSaved = 0;
    g_input.update();

    // Name every press on the serial port. The device labels none of its buttons and the
    // SDK records only names on a resistor ladder, so which physical button carries which
    // name is knowable one way: press one and read what comes out. Cheap enough to leave
    // in -- a line per press, and a press is a human-speed event.
    for (uint8_t b = 0; b <= kBtnPower; ++b) {
        if (g_input.wasPressed(b)) {
            Serial.printf("inkflow: btn %s\n", kButtonNames[b]);
        }
    }

    // Power is handled ahead of the mode branch so it works while reading and while in
    // transfer mode alike. A reader holding the power button means it, wherever they are.
    if (g_input.wasReleased(kBtnPower) && g_input.getHeldTime() > kPowerHoldMs) {
        if (g_transferMode) {
            g_transfer.end();
            g_transferMode = false;
            // Transfer mode closed the document so the upload server could have the card
            // to itself. Reopen before sleeping: the sleep screen states how far through
            // the book the reader got, and a closed document makes that 0% regardless of
            // where they actually are. Same call the Right-to-exit path makes, for the
            // same reason.
            loadDocument();
        }
        enterDeepSleep();
    }

    if (g_transferMode) {
        g_transfer.poll();

        // Redraw only when something actually changed. A full refresh costs 1958ms, and
        // polling one into every loop iteration would make the page unusably slow.
        if (g_transfer.dirty()) {
            g_transfer.clearDirty();
            renderTransfer();
        }

        // Right both enters and leaves transfer mode, so every button on the device owns
        // exactly one function. Back meant "redraw" while reading and "leave" here, which
        // is two jobs for one button and one more thing for a reader to hold in their head.
        if (g_input.wasPressed(kBtnRight)) {
            g_transfer.end();
            g_transferMode = false;

            // Pick up whatever just arrived. The reading position only resets when the
            // document is no longer the one it belongs to: entering transfer mode to
            // check a battery level and leaving again should not cost the reader its
            // place. A same-named file that got shorter is a different document too, so
            // a position now past the end resets as well.
            char previous[sizeof(g_docName)];
            snprintf(previous, sizeof(previous), "%s", g_docName);
            loadDocument();
            if (strcmp(g_docName, previous) != 0 || g_reader.index() >= g_doc.count()) {
                g_reader.seek(0);
            }
            g_reader.renderFull();
        }
        delay(5);
        return;
    }

    if (g_input.wasPressed(kBtnConfirm)) {
        g_reader.togglePlay();
        g_reader.renderFull();
    } else if (g_input.wasPressed(kBtnLeft)) {
        g_reader.rewindSentence();
        g_reader.renderFull();
    } else if (g_input.wasPressed(kBtnUp)) {
        g_reader.adjustSpeed(+30);
        g_reader.renderFull();
    } else if (g_input.wasPressed(kBtnDown)) {
        g_reader.adjustSpeed(-30);
        g_reader.renderFull();
    } else if (g_input.wasPressed(kBtnRight)) {
        g_transferMode = true;
        g_reader.setPlaying(false);
        // Let go of the card before the upload server touches it. SdSource holds the
        // document's file open for the document's life -- deliberately, because an
        // open-seek-close around every 256-token block would cost more than it saves --
        // and streamUpload removes the target before writing it. Re-uploading the book
        // you are currently reading would therefore unlink a file with a live read handle
        // on it and hand its clusters to the new write. The position is already in NVS,
        // and leaving transfer mode calls loadDocument, which reopens.
        g_doc.close();
        g_transfer.begin();
        renderTransfer();
    } else if (g_input.wasPressed(kBtnBack)) {
        g_reader.renderFull();
    }

    if (!g_reader.playing() || g_reader.atEnd()) {
        delay(20);
        return;
    }

    const uint32_t started = millis();

    // One chunk: assembled, positioned on the focal column, and drawn with whichever
    // refresh the ghosting budget calls for. All of that is the shared reader's.
    reader::Frame frame{};
    if (!g_reader.step(frame)) {
        return;
    }

    // Persist every few chunks rather than every one: NVS has finite write endurance,
    // and losing at most a few seconds of position is not worth wearing it out.
    const uint32_t index = g_reader.index();
    if ((index / 16u) != (lastSaved / 16u)) {
        lastSaved = index;
        savePosition();

        // A battery column, on the cadence that already exists. Nothing on a host defends
        // this -- there is no battery to read there -- so it stays a print rather than
        // logic, and costs a line against a 542ms refresh.
        //
        // It goes to the card as well as the serial port, because the two channels answer
        // different questions and only one of them is available when it matters. USB is
        // also the charger: measured over serial, the cell reads *rising*, which is the
        // opposite of the number this project needs. The discharge curve only exists
        // while the cable is out, and that is exactly when nothing is listening.
        const uint32_t mv = analogReadMilliVolts(kBatteryAdcPin);
        Serial.printf("inkflow: at %u, %u mV\n", static_cast<unsigned>(index),
                      static_cast<unsigned>(mv));
        logBattery(index, mv);
    }

    // Refresh time counts against the hold: the panel already spent it showing the word.
    const uint32_t spent = millis() - started;
    if (frame.holdMs > spent) {
        delay(frame.holdMs - spent);
    }
}
