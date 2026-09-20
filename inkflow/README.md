# inkflow

An RSVP reader for e-ink devices, targeting the [Xteink X4](docs/PLATFORM-MATRIX.md).

RSVP — rapid serial visual presentation — displays text one word or short chunk at a time at a fixed screen position. Your eye stops moving: no line tracking, no saccades, no losing your place. For some readers, and specifically for readers with ADHD, removing the eye-movement and place-keeping overhead is the difference between reading and re-reading the same paragraph six times.

**Status: the reader reads a real book on the device, at the pace the simulator predicted.** See [Honest status](#honest-status) — that section is the contract, and it will not overstate what exists.

## What's here

| Component | What it is | State |
|---|---|---|
| `core/` | **`rsvp-core`** — the portable engine: tokenizer, pivot calculation, timing model, chunker, playback state machine, `.rsvp` container. C++17, no dependencies, no dynamic allocation, no I/O | Done and tested |
| `reader/` | **`rsvp-reader`** — the reading loop and the book picker: chunk assembly, pacing, refresh policy, rewind, document selection. Draws through an injected surface and reads through an injected byte source, so the device and the simulator run one copy | Done and tested |
| `firmware/reader/` | The reader for the Xteink X4. Streams a sidecar from SD, picks between books, sleeps and wakes, WiFi transfer | Reads a 98,633-token book off the card. **The WiFi transfer has never completed** |
| `tools/rsvp-mk/` | Host-side converter: text / markdown → a compact `.rsvp` sidecar | Working for TXT and Markdown. HTML and PDF not started |
| `tools/epub-to-text/` | EPUB → text, spine order preserved | Working, as a separate step before `rsvp-mk` |
| `bench/eink-bench/` | On-device harness measuring real SSD1677 refresh latency | Run on hardware. Results in `docs/REFRESH-MEASUREMENTS.md` |
| `sim/` | Desktop simulator. Runs the shipped reading loop against a modelled panel, and fails the build on a layout fault | Working |
| `docs/PLATFORM-MATRIX.md` | Sourced capability matrix for the X4 and its firmware ecosystem | Done |
| `docs/FEATURE-SURVEY.md` | Feature survey of open-source RSVP readers and the X4 ecosystem, with the per-project license boundary and what the comprehension research says | Done |

## Try it

```sh
cmake -B build -DCMAKE_BUILD_TYPE=Debug && cmake --build build -j
./build/tools/rsvp-mk/rsvp_mk docs/PLATFORM-MATRIX.md --wpm 400
```

```
docs/PLATFORM-MATRIX.md -> docs/PLATFORM-MATRIX.rsvp
  tokens    2835
  text      16516 bytes
  file      39228 bytes
  markdown  stripped
  at 400 wpm  8m 07s
```

The reading-time estimate comes from the real timing model, not from dividing words by WPM — boundary pauses and length bonuses make the honest figure noticeably longer.

A sidecar runs roughly 2.4x the size of its source text (an 8-byte token record per word, plus the text verbatim). For a 500 KB book that is about 1.2 MB, which is nothing on the microSD card the X4 reads from.

## The design in one page

**The engine is the deliverable, not a firmware.** The Xteink X4 already has good open-source firmware — [CrossPoint](https://github.com/crosspoint-reader/crosspoint-reader) (MIT, 6.5k stars) solves EPUB parsing, font rendering, SD I/O, input, and power management. Reimplementing that would be waste. `rsvp-core` is a small, portable library that any of those firmwares can host, and the intended destination for the device integration is an upstream pull request to CrossPoint rather than a competing reader.

**Book parsing happens on your computer, not on the device.** The X4 has 400 KB of SRAM and no PSRAM. Real Unicode segmentation and real hyphenation dictionaries are free on a laptop and unaffordable there. So `rsvp-mk` pre-processes a book into a flat, seekable array of 8-byte token records, and the device streams it. The device needs no parser, no allocator, and gets trivially cheap rewind and resume. Both sides link the same `rsvp-core`, so the timing model on your desktop is provably the one running on the device.

**Two constraints the panel imposes on the RSVP mechanic itself:**

- *The screen is monochrome, so the pivot cannot be a red letter.* Every RSVP reader since Spritz marks the recognition point by colouring one character, and 1-bit mono has no colour to spend. It does have inversion: inkflow draws the pivot character in white on a filled black cell, which marks the letter the eye should land on rather than the column it sits in. It costs no time — the cell is drawn inside the band already being redrawn for every chunk. The first answer here was static guide ticks bracketing the focal column, genuinely free but pointing at a column rather than a character.
- *800 px of width at a legible size fits about 10–14 characters.* Chunking two or three words together is the main lever for higher words-per-minute on a slow panel, but it collides with legibility on long words. So chunking is width-constrained, not word-count-constrained.

**The open question is physics, not access.** Flashing the X4 is a solved problem. Whether the panel can redraw fast enough is not, and nobody in this ecosystem has published the numbers. RSVP at 300 WPM needs a word every 200 ms; normal e-reading refreshes once every 30 seconds. That is a completely different duty cycle on a 650 mAh battery, and it is why `eink-bench` exists and why `TimingConfig::minHoldMs` is a first-class field rather than an afterthought. Measurements will land here as named calibration constants citing the run that produced them.

**Speed is not the point, and the research is clear about it.** Comprehension holds against normal reading at 250–350 WPM and drops significantly above that, with the worst damage to *inferential* comprehension — the kind that integrates across a whole argument. So inkflow targets that band and treats higher speeds as a knob rather than a goal. The honest benefit for ADHD is not that you read faster; it is that a fixed focal point removes the *place-keeping* load — no line tracking, no losing your position, no re-reading a paragraph because your attention drifted mid-line. See [`docs/FEATURE-SURVEY.md`](docs/FEATURE-SURVEY.md) for the evidence.

That finding also makes the hardware question easier: 250–350 WPM means 171–240 ms per update rather than the ~100 ms a 600 WPM ambition would demand, and chunking two words at 300 WPM relaxes it to 400 ms. The measurement still has to happen — but the bar it has to clear dropped by roughly 3x, for a reason that came from reading research rather than wishful engineering.

**Rewind is the feature, not a convenience.** Suppressing regressions — the backward glances that are 10–15% of normal reading time — measurably hurts comprehension, and it is the one thing RSVP inherently does. Almost nothing in the field implements a fix. That's why `rewindSentence()` walks backwards through successive presses instead of sticking.

## Building

Needs CMake ≥ 3.16, a C++17 compiler and zlib. No network access required — the test framework is vendored.

```sh
cmake -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build -j
ctest --test-dir build
```

The build also produces `rsvp_core_freestanding`, which compiles the engine with `-fno-exceptions -fno-rtti`. That target exists so the "usable on bare metal" claim is enforced by the build rather than asserted in a comment — if anything in the engine starts depending on exceptions, RTTI, or the heap, this breaks at build time instead of at flash time.

To watch a reading session rather than read about one:

```sh
./build/tools/rsvp-mk/rsvp_mk docs/FEATURE-SURVEY.md -o /tmp/book.rsvp
./build/sim/sim /tmp/book.rsvp /tmp 40 --no-png --gif /tmp/reading.gif
```

Each frame's delay is that chunk's own hold time, so it plays at the pace the panel would.

The device firmware builds with PlatformIO, not CMake:

```sh
cd firmware/reader && pio run
```

## The simulator is a test, not a viewer

It runs the shipped reading loop — the same `reader/` the firmware links — against a panel model built from the measured refresh numbers. It exits non-zero when a chunk cannot be placed, which turns a class of fault that is invisible in code review and awkward to describe over chat into a build failure.

This is worth stating plainly because it was not always true. The simulator used to reimplement the reader's screen rather than run its code, and a fault lived in that gap: the firmware took the *maximum* of per-token hold times, each of which had already been clamped up to the panel's refresh floor, so the floor won every time and every boundary pause the timing model computed was discarded. Requesting 200 WPM and requesting 400 both delivered about 300. The simulator was green throughout, because it was running different code.

A simulator that stays green while the firmware is broken is worse than no simulator, because it looks like coverage.

## Honest status

What is true today:

- The engine is complete and tested: tokenizer with boundary classification, pivot calculation, timing model, chunker, playback state machine (pause, rewind-by-sentence, fingerprinted resume points), and the `.rsvp` container. **166 test cases, 1047 assertions, plus 26 end-to-end CLI checks, 10 EPUB tests and a 5-check pipeline test — all green.**
- The reading loop is shared. The firmware and the simulator link the same `reader/`, so what the simulator clears is what the device runs. It was not always so, and the gap hid a real fault — see below.
- The simulator runs that loop against a panel model calibrated to the measured refresh numbers, streams a real 1.4 MB sidecar through the same code path the device uses, and **fails the build** on a layout fault. It can write a session as an animated GIF at the true reading pace.
- `rsvp-mk` converts text and Markdown to a `.rsvp` sidecar, and a test asserts the round trip plays back the original words in order through the real file format.
- The engine compiles clean under `-Wall -Wextra -Wpedantic -Werror -Wconversion -Wsign-conversion -Wshadow -Wold-style-cast`, and separately with exceptions and RTTI disabled.
- The firmware compiles and links for the ESP32-C3, with the shared module in the image. CI builds it on every push, so the claim is a job rather than a memory.
- **The reader reads a real book on the device.** It streams a 98,633-token sidecar off the SD card — through the same `Document` block cache the simulator exercises — and presents it for minutes at a stretch with no crash, no panic, no watchdog reset and no brownout. Recorded in [`hardware-notes/first-book-20260920.md`](hardware-notes/first-book-20260920.md).
- **The simulator predicted what the hardware did.** Same book, same opening, 330 WPM requested: the simulator said 245 WPM delivered, 4 full refreshes and a worst ghost depth of 68 partials; the device delivered 234 WPM, 4 full refreshes, and a worst ghost depth of 68 partials. Within 5% on pace, exact on both refresh figures. That is the claim `sim/` has been making since it was rebuilt, and it had never been tested against a book.
- **The panel is deterministic to three microseconds.** Across 303 partial refreshes in one reading session GxEPD2 reported between `500999` and `501002` µs — a spread of 3 µs. `docs/REFRESH-MEASUREMENTS.md` had concluded from six band heights that the partial waveform is a fixed ~501 ms and only SPI transfer scales with area, which is the finding that killed windowed partial updates and made chunking mandatory. The driver states that number, unchanged, on every frame of a real book.
- **The ghost flush behaves as designed.** Partials between full refreshes over one session: 68, 62, 61, 76. Every one came from the first tier — 60 partials, then wait for a paragraph. The 90-at-a-sentence and 120-unconditional fallbacks never fired.
- **The device can be used without documentation.** Controls sit where the hands are — speed and play/pause on the two rockers under the right thumb, book list and WiFi on the volume rocker — and the screen the panel holds while switched off is a map of them, each label drawn against the edge its button is on. The button positions were established by pressing each one and reading the serial log, because the firmware sees a resistor ladder and cannot tell geometry.
- **Books are chosen on the device**, from a list of what is on the card, and each one resumes where it was left under its own key.

What is **not** true yet, and will not be claimed until it is:

- **The WiFi transfer has never completed.** The access point comes up and the web server answers — a joining phone's captive-portal probes produce `request handler not found`, which is the server working, not failing. But no file has ever moved. The streaming upload handler, the AP holding up under a 1.4 MB transfer, and the post-upload document reload are verified by a compiler and nothing else.
- **The pacing has never been judged.** The reader now delivers the speed it is asked for, which it did not before. Whether 330 WPM in three-word chunks is comfortable to read is the one question no measurement answers.
- **Power draw is still unmeasured**, and the reason turned out to be structural: USB is also the charger. Measured over serial during a real read the cell reports *rising* — 1902 mV to 1933 mV across 768 tokens — because the measurement channel and the charging channel are the same cable. The reader now writes the reading to `/battery.csv` on the card and the transfer page can serve it back, so the run is possible; it has not been made.
- **Ghosting is unquantified.** The simulator counts partial updates between full refreshes and the reader now bounds that number, but "how ghosted is too ghosted" has been assessed by eye, on one panel, once.
- **The two full-refresh figures disagree, and this is recorded rather than resolved.** GxEPD2 reports `_Update_Full : 1689 ms`; `eink-bench` measured a 1958 ms median. They are not contradictory — the bench timed the whole operation wall-clock including power-on, SPI transfer and power-off, while `_Update_Full` is the waveform phase alone, and the 269 ms gap is the right order for the panel's quoted 100 ms power-on and 200 ms power-off. Nothing has yet measured the two independently on the same run. The timing model is calibrated against 1958 ms, because that is the one a reader actually waits.
- The panel model is a fit to six measured band heights, not a simulation of the controller. It reproduces timing, not waveforms, and it does not model ghosting as anything but a count.
- HTML and PDF ingest are not implemented. EPUB is a separate host-side step rather than something `rsvp-mk` accepts directly.
- Markdown handling is a deliberately minimal line-oriented stripper, not a real parser. It handles headings, lists, blockquotes, emphasis, inline code, links, images, fenced code, rules, and table delimiter rows; anything more exotic passes through as text.

## Prior art and license hygiene

inkflow is MIT licensed. That constrains what it can borrow, deliberately, and the rule is written down in [CONTRIBUTING.md](CONTRIBUTING.md): code may be lifted from MIT-licensed projects with attribution, while copyleft and unlicensed projects are studied for behaviour and reimplemented, never copied. Ideas and functionality are not copyrightable; expression is.

The X4 ecosystem is unusually generous — most of it is MIT. Particular thanks to [CrossPoint](https://github.com/crosspoint-reader/crosspoint-reader), the [open-x4-epaper community SDK](https://github.com/open-x4-epaper/community-sdk), [pulp-os](https://github.com/hansmrtn/pulp-os) for showing how to drive this hardware from bare metal, and [Adafruit's CircuitPython guide](https://learn.adafruit.com/circuitpython-on-the-xteink-x4-ereader/pinouts) for the canonical pinout.

### On patents

RSVP is old — the technique long predates any modern implementation of it — but Spritz Technology holds patents on specific RSVP presentation methods. inkflow's pivot calculation and timing model are implemented from published reading research and from the general description of the technique, not derived from any patented specification or from reading a competitor's implementation. This note is documentation of provenance, not legal advice. If you plan to ship a commercial product built on this, talk to a lawyer rather than trusting a README.

## License

MIT — see [LICENSE](LICENSE).
