# Status and handoff

Last updated **2026-09-18**, after the first hardware session. Written so a fresh session
can resume without re-deriving anything.

## Read first

1. This document.
2. [`REFRESH-MEASUREMENTS.md`](REFRESH-MEASUREMENTS.md) — the measured panel numbers that
   decide the product's shape. Everything about chunking follows from them.
3. [`../CONTRIBUTING.md`](../CONTRIBUTING.md) — TDD discipline, engine constraints, the
   license boundary.
4. [`PLATFORM-MATRIX.md`](PLATFORM-MATRIX.md) and [`FEATURE-SURVEY.md`](FEATURE-SURVEY.md)
   for background; both have been corrected against hardware and say so inline.

`LOCKED-UNIT.md` is **superseded** and carries a banner saying so. The unit is not locked.

## Where things are

| | |
|---|---|
| Repo | `/Users/michael/inkflow` — a clone of `wittyreference/dotfiles`; the project is the `inkflow/` subdirectory |
| Branch | `claude/xteink-x4-rsvp-reader-212m18` |
| Golden flash backup | `~/inkflow-backups/x4-stock-golden-20260918T015022Z.bin`, sha256 `4e512a0b02b9bfa2451d998d553422644b4ca6c902e7391dc07fe84a3ef04bc2` |
| Test book | `~/inkflow-books/agents.rsvp` (1.4 MB, 98,633 tokens) and `agents.txt` |
| Host toolchain | `./scripts/setup-host.sh` installs esptool and PlatformIO into `~/.inkflow-tools` |

**The golden backup is the only stock X4 image that exists anywhere as far as this
project can tell. Nothing in the repo protects it. Copy it somewhere durable.**

## The device

An **unlocked** Xteink X4. Confirmed by reading it, not inferred:

```
ESP32-C3, RISC-V, 160MHz, 400KB SRAM, no PSRAM
16MB flash, manufacturer 0x85, device 0x2018
MAC 14:63:93:f6:82:4c
USB JTAG/serial debug unit, VID 0x303a PID 0x1001 -> /dev/cu.usbmodem1101

SECURE_BOOT_EN        False        SPI_BOOT_CRYPT_CNT   Disable
DIS_USB_SERIAL_JTAG   Enable       DIS_DOWNLOAD_MODE    False
WR_DIS 0              RD_DIS 0     -- nothing is burned
```

Partition table read out of the backup is byte-for-byte identical to
`crosspoint-reader/partitions.csv`. `app0` is at `0x10000`, and `otadata` says the device
boots from it.

## Three things that will waste a session if unknown

**1. Always pass `--after no-reset` to esptool.** The stock firmware does not keep
USB-Serial/JTAG alive once it boots, so any command ending in `Hard resetting via RTS
pin` takes the connection away and costs a power-cycle. Custom firmware built with
`ARDUINO_USB_CDC_ON_BOOT=1` *does* hold USB up, so this matters most when stock is on.

**2. `build_unflags = -std=gnu++11` is load-bearing.** rsvp-core is C++17; the
Arduino-ESP32 default is C++11, under which a `constexpr` function must be a single
return statement, and the engine will not compile.

**3. Never construct a `WebServer` (or anything needing the Arduino core) as a global.**
Its constructor runs before `setup()` and before `Serial.begin`, and the result is a hang
with no serial output and no display — indistinguishable from dead hardware. It cost most
of an evening. `transfer.h` constructs it lazily in `begin()`.

## What exists

| Component | State |
|---|---|
| `core/` — rsvp-core | Tokenizer, boundary flags, ORP pivot, timing model, chunker, player, `.rsvp` format. **90 cases, 377 assertions, green** |
| `tools/rsvp-mk/` | text/Markdown to `.rsvp`. Works |
| `tools/epub-to-text/` | EPUB to text, spine order preserved. Works |
| `bench/eink-bench/` | Panel measurement firmware. **Run; results in `hardware-notes/`** |
| `sim/` | Desktop renderer, **exits non-zero on layout overflow** |
| `firmware/reader/` | The reader. Streams `.rsvp` from SD, WiFi transfer, resume. **Built for landscape but NOT yet flashed** |

Host tests are compiled directly with `c++` rather than CMake (cmake was not installed
initially; it is now). See any recent commit for the exact invocation, e.g.

```sh
c++ -std=c++17 -O1 -Wall -Wextra -Wpedantic -Werror -Wconversion -Wsign-conversion \
    -Wshadow -Wold-style-cast -Icore/include -Icore/tests \
    core/tests/main.cpp core/tests/test_*.cpp core/src/*.cpp -o /tmp/core_tests && /tmp/core_tests
```

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

Late in the session the display stopped updating **even with the known-good community
sample firmware flashed**, which rules out the reader firmware as the cause. The serial
port has since reappeared, so the device is alive at the USB level.

Not yet established: whether the panel recovers on a full power-cycle, whether the
battery was simply flat after an evening of sustained refreshing and WiFi, or whether
something left the display controller in a bad state. `eink-bench` ends with
`display.hibernate()`, which is a candidate worth ruling in or out.

**Next step is to establish a known-good baseline again** — flash the sample, power-cycle
properly, confirm the panel draws — before flashing anything new. A mistake made this
session was flashing three successively more complex firmwares without confirming each
one rendered, which left it ambiguous which change broke things.

## What to do next

1. **Recover the panel** and confirm the sample firmware draws.
2. **Flash the landscape reader.** It is built but unflashed. Portrait overran the right
   edge by 88 px; the simulator catches this now and landscape passes.
3. **Transfer the book.** Press Right on the reader for WiFi transfer, join network
   `inkflow` / `inkflow-reader`, open `http://192.168.4.1`, upload `agents.rsvp`, press
   Back.
4. **Read it, and tune from the experience.** Nobody has yet confirmed the pacing works.
   That is the one question no amount of measurement answers.
5. **Measure power on battery.** The bench ran on USB, so its battery figures are
   meaningless. Sustained-refresh cost on a 650 mAh cell is the last open hardware risk.

## Known gaps

- The reader has never displayed a real book. Everything is verified except the thing
  itself.
- Layout constants are duplicated between `sim/src/main.cpp` and
  `firmware/reader/src/config.h`. They must be changed together; a divergent simulator is
  worse than none.
- `rsvp-mk` has no EPUB support; `tools/epub-to-text` covers it as a separate step.
- Ghosting is unquantified — there is no sensor, only eyes.
- The repo still lives inside `dotfiles`. `inkflow/scripts/publish-standalone.sh` extracts
  it to its own repository and is re-runnable.
