# inkflow

An RSVP reader for e-ink devices, targeting the [Xteink X4](docs/PLATFORM-MATRIX.md).

RSVP — rapid serial visual presentation — displays text one word or short chunk at a time at a fixed screen position. Your eye stops moving: no line tracking, no saccades, no losing your place. For some readers, and specifically for readers with ADHD, removing the eye-movement and place-keeping overhead is the difference between reading and re-reading the same paragraph six times.

**Status: early. The engine works and is tested; nothing runs on hardware yet.** See [Honest status](#honest-status) — that section is the contract, and it will not overstate what exists.

## What's here

| Component | What it is | State |
|---|---|---|
| `core/` | **`rsvp-core`** — the portable engine: tokenizer, pivot calculation, timing model, playback state machine, `.rsvp` container. C++17, no dependencies, no dynamic allocation, no I/O | Done and tested |
| `tools/rsvp-mk/` | Host-side converter: text / markdown → a compact `.rsvp` sidecar | Working for TXT and Markdown. EPUB, HTML, and PDF not started |
| `bench/eink-bench/` | On-device harness measuring real SSD1677 refresh latency and power draw | Not started |
| `sim/` | Desktop simulator for tuning speed and chunking without hardware | Not started |
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

- *The screen is monochrome, so the pivot cannot be a red letter.* Every RSVP reader since Spritz marks the recognition point by colouring one character. With 1-bit mono that's unavailable, so inkflow uses static vertical guide marks above and below the focal column instead — drawn once and never redrawn, which means they cost zero refresh time.
- *800 px of width at a legible size fits about 10–14 characters.* Chunking two or three words together is the main lever for higher words-per-minute on a slow panel, but it collides with legibility on long words. So chunking is width-constrained, not word-count-constrained.

**The open question is physics, not access.** Flashing the X4 is a solved problem. Whether the panel can redraw fast enough is not, and nobody in this ecosystem has published the numbers. RSVP at 300 WPM needs a word every 200 ms; normal e-reading refreshes once every 30 seconds. That is a completely different duty cycle on a 650 mAh battery, and it is why `eink-bench` exists and why `TimingConfig::minHoldMs` is a first-class field rather than an afterthought. Measurements will land here as named calibration constants citing the run that produced them.

**Speed is not the point, and the research is clear about it.** Comprehension holds against normal reading at 250–350 WPM and drops significantly above that, with the worst damage to *inferential* comprehension — the kind that integrates across a whole argument. So inkflow targets that band and treats higher speeds as a knob rather than a goal. The honest benefit for ADHD is not that you read faster; it is that a fixed focal point removes the *place-keeping* load — no line tracking, no losing your position, no re-reading a paragraph because your attention drifted mid-line. See [`docs/FEATURE-SURVEY.md`](docs/FEATURE-SURVEY.md) for the evidence.

That finding also makes the hardware question easier: 250–350 WPM means 171–240 ms per update rather than the ~100 ms a 600 WPM ambition would demand, and chunking two words at 300 WPM relaxes it to 400 ms. The measurement still has to happen — but the bar it has to clear dropped by roughly 3x, for a reason that came from reading research rather than wishful engineering.

**Rewind is the feature, not a convenience.** Suppressing regressions — the backward glances that are 10–15% of normal reading time — measurably hurts comprehension, and it is the one thing RSVP inherently does. Almost nothing in the field implements a fix. That's why `rewindSentence()` walks backwards through successive presses instead of sticking.

## Building

Needs CMake ≥ 3.16 and a C++17 compiler. No network access required — the test framework is vendored.

```sh
cmake -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build -j
./build/core/rsvp_core_tests
```

The build also produces `rsvp_core_freestanding`, which compiles the engine with `-fno-exceptions -fno-rtti`. That target exists so the "usable on bare metal" claim is enforced by the build rather than asserted in a comment — if anything in the engine starts depending on exceptions, RTTI, or the heap, this breaks at build time instead of at flash time.

## Honest status

What is true today:

- The engine is complete and tested: tokenizer with boundary classification, pivot calculation, timing model, playback state machine (pause, rewind-by-sentence, fingerprinted resume points), and the `.rsvp` container. **84 test cases, 386 assertions, plus 22 end-to-end CLI checks — all green.**
- `rsvp-mk` converts text and Markdown to a `.rsvp` sidecar, and a test asserts the round trip plays back the original words in order through the real file format.
- The engine compiles clean under `-Wall -Wextra -Wpedantic -Werror -Wconversion -Wsign-conversion -Wshadow -Wold-style-cast`, and separately with exceptions and RTTI disabled.

What is **not** true yet, and will not be claimed until it is:

- **Nothing has run on an Xteink X4. No firmware, no integration, not once.**
- **No refresh-latency or power-draw measurements exist.** Every statement in this repo about achievable WPM is arithmetic, not observation. `TimingConfig::minHoldMs` currently defaults to 0 because nobody has measured what it should be.
- There is no simulator, so nobody has *watched* this render a word.
- EPUB, HTML, and PDF ingest are not implemented — only plain text and Markdown.
- Markdown handling is a deliberately minimal line-oriented stripper, not a real parser. It handles headings, lists, blockquotes, emphasis, inline code, links, images, fenced code, and rules; anything more exotic passes through as text.

## Prior art and license hygiene

inkflow is MIT licensed. That constrains what it can borrow, deliberately, and the rule is written down in [CONTRIBUTING.md](CONTRIBUTING.md): code may be lifted from MIT-licensed projects with attribution, while copyleft and unlicensed projects are studied for behaviour and reimplemented, never copied. Ideas and functionality are not copyrightable; expression is.

The X4 ecosystem is unusually generous — most of it is MIT. Particular thanks to [CrossPoint](https://github.com/crosspoint-reader/crosspoint-reader), the [open-x4-epaper community SDK](https://github.com/open-x4-epaper/community-sdk), [pulp-os](https://github.com/hansmrtn/pulp-os) for showing how to drive this hardware from bare metal, and [Adafruit's CircuitPython guide](https://learn.adafruit.com/circuitpython-on-the-xteink-x4-ereader/pinouts) for the canonical pinout.

### On patents

RSVP is old — the technique long predates any modern implementation of it — but Spritz Technology holds patents on specific RSVP presentation methods. inkflow's pivot calculation and timing model are implemented from published reading research and from the general description of the technique, not derived from any patented specification or from reading a competitor's implementation. This note is documentation of provenance, not legal advice. If you plan to ship a commercial product built on this, talk to a lawyer rather than trusting a README.

## License

MIT — see [LICENSE](LICENSE).
