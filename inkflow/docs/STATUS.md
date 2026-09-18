# Status and handoff

Last updated **2026-09-17**, after the simulator was made authoritative and the firmware
was rebound onto it. Written so a fresh session can resume without re-deriving anything.

## Read first

1. This document.
2. [`REFRESH-MEASUREMENTS.md`](REFRESH-MEASUREMENTS.md) — the measured panel numbers that
   decide the product's shape. Everything about chunking follows from them.
3. [`../CONTRIBUTING.md`](../CONTRIBUTING.md) — TDD discipline, engine constraints, the
   license boundary.
4. [`PLATFORM-MATRIX.md`](PLATFORM-MATRIX.md) and [`FEATURE-SURVEY.md`](FEATURE-SURVEY.md)
   for background; both have been corrected against hardware and say so inline.

`LOCKED-UNIT.md` is **superseded** and carries a banner saying so. The unit is not locked.

## The rule that now governs the work

**Nothing gets flashed until the simulator clears it.**

The simulator used to be a lookalike: it drew the reader's screen with its own code rather
than running the reader's. A real fault lived in that gap for as long as it existed — the
firmware took the *maximum* of per-token hold times, each already clamped up to the panel's
542 ms floor, so the floor won every time and every boundary pause the timing model
computed was thrown away. Requesting 200 WPM and requesting 400 both delivered about 300,
and the simulator was green throughout because it was running different code.

The reading loop now lives in `reader/` and both the firmware and the simulator link it.
That is the only reason a green simulator means anything.

## Where things are

| | |
|---|---|
| Repo | `/Users/michael/inkflow` — a clone of `wittyreference/dotfiles`; the project is the `inkflow/` subdirectory |
| Standalone | `wittyreference/inkflow`, synced by `scripts/publish-standalone.sh` |
| Golden flash backup | `~/inkflow-backups/x4-stock-golden-20260918T015022Z.bin`, sha256 `4e512a0b02b9bfa2451d998d553422644b4ca6c902e7391dc07fe84a3ef04bc2`, copied to `~/Documents/inkflow-golden-backup/` |
| Test book | `~/inkflow-books/agents.rsvp` (1.4 MB, 98,633 tokens) and `agents.txt` |
| Host toolchain | `./scripts/setup-host.sh` installs esptool and PlatformIO into `~/.inkflow-tools` |

**The golden backup is the only stock X4 image that exists anywhere as far as this project
can tell.** It is gitignored, correctly — it is stock vendor firmware. It now exists in two
places rather than one. Keep it that way.

## The device

An **unlocked** Xteink X4. Confirmed by reading it, not inferred:

```
ESP32-C3, RISC-V, 160MHz, 400KB SRAM, no PSRAM
16MB flash, manufacturer 0x85, device 0x2018
USB JTAG/serial debug unit, VID 0x303a PID 0x1001 -> /dev/cu.usbmodem1101

SECURE_BOOT_EN        False        SPI_BOOT_CRYPT_CNT   Disable
DIS_USB_SERIAL_JTAG   Enable       DIS_DOWNLOAD_MODE    False
WR_DIS 0              RD_DIS 0     -- nothing is burned
```

Partition table read out of the backup is byte-for-byte identical to
`crosspoint-reader/partitions.csv`. `app0` is at `0x10000`, and `otadata` says the device
boots from it.

## Four things that will waste a session if unknown

**1. Always pass `--after no-reset` to esptool.** The stock firmware does not keep
USB-Serial/JTAG alive once it boots, so any command ending in `Hard resetting via RTS pin`
takes the connection away and costs a power-cycle. Custom firmware built with
`ARDUINO_USB_CDC_ON_BOOT=1` *does* hold USB up, so this matters most when stock is on. All
the scripts now pass it; they did not before.

**2. `--port` and `--after` are global options on esptool v5.** They must precede the
subcommand. `esptool chip-id --port X` prints a usage error; `esptool --port X chip-id`
works. `00-probe.sh` had them the wrong way round and had almost certainly never been run
against a real device.

**3. `build_unflags = -std=gnu++11` is load-bearing.** rsvp-core is C++17; the
Arduino-ESP32 default is C++11, under which a `constexpr` function must be a single return
statement, and the engine will not compile.

**4. Never construct a `WebServer` (or anything needing the Arduino core) as a global.**
Its constructor runs before `setup()` and before `Serial.begin`, and the result is a hang
with no serial output and no display — indistinguishable from dead hardware. It cost most
of an evening. `transfer.h` constructs it lazily in `begin()`.

## What exists

| Component | State |
|---|---|
| `core/` — rsvp-core | Tokenizer, boundary flags, ORP pivot, timing model, chunker, player, `.rsvp` format. **123 cases, 487 assertions** |
| `reader/` — rsvp-reader | The reading loop, shared by the firmware and the simulator. **18 cases, 147 assertions** (run through the simulator) |
| `tools/rsvp-mk/` | text/Markdown to `.rsvp`. **14 cases, 48 assertions, 26 CLI checks** |
| `tools/epub-to-text/` | EPUB to text, spine order preserved. **10 tests** |
| `bench/eink-bench/` | Panel measurement firmware. Run; results in `hardware-notes/` |
| `sim/` | Runs the shipped loop against a modelled panel; fails the build on a layout fault; writes a GIF at true reading pace |
| `firmware/reader/` | The reader. **Compiles and links for esp32-c3 — RAM 122300, flash 847684 — and has never been flashed** |

Everything green: `cmake -B build -DCMAKE_BUILD_TYPE=Debug && cmake --build build -j &&
ctest --test-dir build` gives 7/7.

## The measurements that shape everything

| | Median |
|---|---|
| Full refresh | **1958 ms** |
| Partial, 40px band | **524 ms** |
| Partial, 120px band | **542 ms** |
| Partial, full screen | **704 ms** |

**Window size barely matters.** The panel's partial waveform is a fixed ~501 ms; only SPI
transfer scales. A 20x area difference costs 34% more time. The plan's "key bet" on
windowed partial updates was wrong.

**So chunking is mandatory.** At 542 ms: one word is 111 WPM, two is 221, three is 332.
Comprehension holds at 250–350 WPM, so single-word RSVP is impossible on this panel.

SPI contention between SD and EPD is a non-issue: 0.6 ms of 542. Timing is deterministic:
p95 equals median across 100 updates.

## Open problem: the panel froze

At the end of the hardware session the display stopped updating **even with the known-good
community sample firmware flashed**, which rules out the reader firmware as the cause. The
serial port has since reappeared, so the device is alive at the USB level. Nothing has been
flashed since.

Not yet established: whether the panel recovers on a full power-cycle, whether the battery
was simply flat after an evening of sustained refreshing and WiFi, or whether something left
the display controller in a bad state.

Two named suspects, cheapest first. `bench/eink-bench` ends with `display.hibernate()`, and
the vendored `open-x4-sdk/libs/display/EInkDisplay/doc/SSD1677_GUIDE.md` says waking from
hibernate *"requires hardware reset via RST pin"* — while both firmwares init with
`reset_duration = 2` ms against the guide's specified 10 ms. Raising it is a one-line test.
A flat battery is the other candidate.

**Next step is to establish a known-good baseline again** — flash the sample, power-cycle
properly, confirm the panel draws — before flashing anything new. A mistake made in the
hardware session was flashing three successively more complex firmwares without confirming
each one rendered, which left it ambiguous which change broke things.

Note the community sample firmware is **not in this repo and not on the machine**. It has
to be re-cloned from `open-x4-epaper/sample-firmware` before step 1 can run.

## What to do next

1. **Recover the panel** and confirm the sample firmware draws. This is the only step that
   needs someone to look at the device.
2. **Flash the reader** with `scripts/02-flash.sh`. It writes app0 at `0x10000` only and
   refuses an image without the `0xE9` ESP magic. `scripts/03-restore.sh` puts stock back,
   verifying the golden image's sha256 before writing rather than after.
3. **Transfer the book.** Press Right on the reader for WiFi transfer, join network
   `inkflow` / `inkflow-reader`, open `http://192.168.4.1`, upload `agents.rsvp`, press
   Back.
4. **Read it, and tune from the experience.** Nobody has yet confirmed the pacing works.
   That is the one question no amount of measurement answers — but the speed control does
   respond now, which it did not before, so tuning against it will produce real data.
5. **Measure power on battery.** The bench ran on USB, so its battery figures are
   meaningless. Sustained-refresh cost on a 650 mAh cell is the last open hardware risk.

## Known gaps

- The reader has never displayed a real book. Everything is verified except the thing
  itself.
- Ghosting is bounded but unquantified. The reader now forces a full refresh within 120
  partial updates, and the simulator counts them, but "how ghosted is too ghosted" has been
  assessed by eye, on one panel, once.
- The panel model reproduces timing, not waveforms. It is a fit to six measured band
  heights, and it does not model ghosting as anything but a count.
- `rsvp-mk` has no EPUB support; `tools/epub-to-text` covers it as a separate step. HTML and
  PDF are not started.
- The repo still lives inside `dotfiles`. `inkflow/scripts/publish-standalone.sh` extracts
  it to its own repository and is re-runnable.
