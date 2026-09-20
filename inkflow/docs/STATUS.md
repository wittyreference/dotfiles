# Status and handoff

Last updated **2026-09-20**, after the reader read a real book at the pace the simulator
predicted, gained a book picker and a control scheme built around where the hands are, and
had its input loop -- the last surface nothing simulated -- brought under test. Written so a
fresh session can resume without re-deriving anything.

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

## Five things that will waste a session if unknown

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

**5. Two host toolchain traps on this Mac, both from the macOS 27 upgrade.**

The Command Line Tools now ship `MacOSX27.0.sdk`, whose `libSystem.tbd` and `libc++.tbd`
name architectures (`arm64e.x1`) the installed Apple clang 17 does not know. Every CMake
configure dies in `Check for working CXX compiler` with `tapi error: malformed file`.
Point at the Xcode SDK instead:

```sh
export DEVELOPER_DIR=/Library/Developer/CommandLineTools
cmake -B build -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_OSX_SYSROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.sdk
```

`DEVELOPER_DIR` is the load-bearing half. `xcode-select` points at Xcode.app, and every
tool that shims through `xcrun` -- `git` and `cc` among them -- then refuses to run with
*"You have not agreed to the Xcode license agreements"*, which wants `sudo` and a terminal
that can prompt. Pointing `DEVELOPER_DIR` at the Command Line Tools sidesteps it entirely:
the CLT carry no licence gate. The same export is why `git` works.

And `pio run` dies with `riscv32-esp-elf-g++: Bad CPU type in executable`. The RISC-V
toolchain espressif32 pins for the Arduino framework is an x86_64 binary; the registry
advertises a `darwin_arm64` package for it but serves the x86_64 one, so it needs Rosetta:
`softwareupdate --install-rosetta --agree-to-license`. There is no native arm64 build of
that toolchain to switch to — the arm64 `toolchain-riscv32-esp@15.2.0` exists but
`platform.py` selects it only for pure ESP-IDF projects, never for `framework = arduino`.
CI compiles the firmware on an x86_64 Ubuntu runner, so this is a flashing convenience
rather than a gate on firmware changes.

## What exists

| Component | State |
|---|---|
| `core/` — rsvp-core | Tokenizer, boundary flags, ORP pivot, timing model, chunker, player, `.rsvp` format. **123 cases, 487 assertions** |
| `reader/` — rsvp-reader | The reading loop, the book picker and the input latch, shared by the firmware and the simulator. **41 cases, 677 assertions** (run through the simulator) |
| `tools/rsvp-mk/` | text/Markdown to `.rsvp`. **14 cases, 48 assertions, 26 CLI checks** |
| `tools/epub-to-text/` | EPUB to text, spine order preserved. **10 tests** |
| `bench/eink-bench/` | Panel measurement firmware. Run; results in `hardware-notes/` |
| `sim/` | Runs the shipped loop against a modelled panel; fails the build on a layout fault; writes a GIF at true reading pace |
| `firmware/reader/` | The reader. **Reads a 98,633-token book off the card**, picks between books, sleeps and wakes, labels its own controls while off. CI builds it on every push |

Everything green: `cmake -B build -DCMAKE_BUILD_TYPE=Debug && cmake --build build -j &&
ctest --test-dir build` gives 7/7.

## The rule the input loop taught, which is the old rule again

**Anything the device does that the simulator does not run will eventually be wrong.**

The reading loop was extracted into `reader/` because the firmware had reimplemented the
chunk hold and got it backwards, and the simulator stayed green for weeks because it was
running the other copy. That lesson was learnt, written up in the README, and then applied
to exactly one thing.

The input loop was never extracted. It sampled buttons once per turn while the panel
blocked for 500ms of every 650ms cycle, so the device was deaf three quarters of the time
it was reading -- documented as a known limitation, and reported as "pausing doesn't seem
to be working" by the first person to read a book on it. Same shape, same gap, second time.

`Surface` now carries a `Servicer` that backends call while they block, and
`reader::ButtonLatch` samples through it. The firmware calls it from GxEPD2's paging loop;
the simulator calls it as it advances its modelled clock. Tests sweep a tap across every
phase of the refresh cycle, and with the servicing removed they fail 49 assertions.

What is still platform-only, and therefore still able to hide a fault like this: the card
walk, the WiFi transfer, NVS, and the battery ADC. Each is a place to look first when
something works on a laptop and not on the device.

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

## The panel recovered

The display that stopped updating at the end of the bring-up session **comes back on a
proper power-cycle.** Nothing had to be changed. Full account in
[`../hardware-notes/panel-recovery-20260918.md`](../hardware-notes/panel-recovery-20260918.md).

The community sample firmware was re-cloned, built and flashed to app0, and it drew. So
the freeze was transient: not a dead controller, not a bricked device. This does not
distinguish between the two suspects STATUS previously named — `eink-bench` ending in
`display.hibernate()` against a 2 ms reset pulse where the SSD1677 guide specifies 10 ms,
and a flat battery after an evening of sustained refreshing and WiFi — because a
power-cycle clears both. It does establish that neither leaves permanent damage.

Raising `reset_duration` from 2 ms to 10 ms is still worth doing on principle, and there
is now a known-good baseline to test it against. It was deliberately not done in the same
run: changing one variable while diagnosing another is how the previous session lost its
bisect.

## Current device state

`app0` holds an inkflow reader built before the SD-scan fix — 880,720 bytes, sha256
`d5b93156...34ad5318`. That image is no longer reproducible from HEAD, which is part of why
the platform is now pinned.

**It has run.** Six bring-up lines came out at startup — `boot`, `input ok`, `display ok`,
`sd ok`, `document loaded`, and a summary naming the document, its token count,
streamed-or-RAM and the resume index — and then, on Confirm, the built-in passage played
through 24 partial and 4 full refreshes and stopped cleanly. Full account in
[`../hardware-notes/first-reading-20260918.md`](../hardware-notes/first-reading-20260918.md).

Those six lines distinguish a panel fault from a card fault without anyone reading the
screen, which is why they exist. The serial port stays the fastest way to see what the
device is doing, because this firmware builds with `ARDUINO_USB_CDC_ON_BOOT=1` and holds
USB up after boot, unlike stock — which also means **our own builds need no power-cycle**:
`esptool --port <port> --after hard-reset chip-id` boots the freshly written app and USB
survives it. The `--after no-reset` rule above is about stock.

Stock is restorable at any time with `scripts/03-restore.sh`.

## What to do next

1. ~~Recover the panel~~ — **done.** The sample firmware drew after a power-cycle.
2. ~~Flash the reader~~ — **done.** Written and verified at `0x10000`.
3. ~~Power-cycle and watch the serial port~~ — **done.** All six bring-up lines came out
   and the built-in passage played end to end.
4. ~~Read a real book~~ — **done.** 98,633 tokens streamed off the card, 234 WPM delivered
   against the simulator's predicted 245, zero faults. See
   [`../hardware-notes/first-book-20260920.md`](../hardware-notes/first-book-20260920.md).
5. ~~Sleep and wake~~ — **done.** The power button saves position, draws the sleep screen
   and enters deep sleep, first run.
6. **Transfer a book over WiFi.** Still the largest untested surface in the tree: 167 lines
   of streaming upload that no test runs. Press Right for transfer mode, join `inkflow` /
   `inkflow-reader`, open `http://192.168.4.1`, upload `agents.rsvp`, press Right again.
   **Upload from a phone, not from this Mac** — its VPN pins `192.168.4.1` into a tunnel.
7. **Measure power on battery, with the cable out.** USB is also the charger: measured over
   serial the cell reads *rising*. The reader now writes `/battery.csv` to the card and the
   transfer page serves it back at `/get?f=battery.csv`, so the run is possible — unplug,
   read for a measured stretch, then pull the file from a phone.
8. **Judge the pacing.** 234 WPM in three-word chunks is what the device delivers. Whether
   that is comfortable for an hour is the one question no measurement answers.
9. **Replace the percentage with time remaining.** At token 832 of 98,633 the status line
   reads `0%`, and will for the first thousand words of any book that size. `remainingMs()`
   already computes reading time from the real timing model and nothing uses it; `player.hpp`
   argues for exactly this in a comment.

## Known gaps

- The WiFi transfer has never completed. The AP comes up and the server answers, but no
  file has ever moved.
- The pacing has never been judged by a reader.
- Peak ghosting cannot be inspected on the device: every button handler ends in a full
  refresh, pause included, so the action that would freeze the panel also clears it.
- Ghosting is bounded but unquantified. The reader now forces a full refresh within 120
  partial updates, and the simulator counts them, but "how ghosted is too ghosted" has been
  assessed by eye, on one panel, once.
- The panel model reproduces timing, not waveforms. It is a fit to six measured band
  heights, and it does not model ghosting as anything but a count.
- `rsvp-mk` has no EPUB support; `tools/epub-to-text` covers it as a separate step. HTML and
  PDF are not started.
- The repo still lives inside `dotfiles`. `inkflow/scripts/publish-standalone.sh` extracts
  it to its own repository and is re-runnable.
