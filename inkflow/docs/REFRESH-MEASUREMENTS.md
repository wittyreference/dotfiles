# E-paper refresh measurements — Xteink X4

Measured 2026-09-18 on an Xteink X4: ESP32-C3 driving an SSD1677 / GDEQ0426T82 at
40 MHz SPI, via `bench/eink-bench`. Twelve samples per configuration with the first two
discarded as warm-up; the sustained run is 100 iterations. Raw data in
[`../hardware-notes/eink-bench-20260918.csv`](../hardware-notes/eink-bench-20260918.csv).

**No numbers like these appear to exist publicly for this device.** Every performance
claim this project made before today was arithmetic. These are observation.

## Results

| Configuration | Median | Words/min at 1 word | at 2 words | at 3 words |
|---|---:|---:|---:|---:|
| Full refresh | **1958 ms** | 31 | 61 | 92 |
| Partial, 40 px band | **524 ms** | 115 | 229 | **344** |
| Partial, 80 px band | 530 ms | 113 | 226 | 340 |
| Partial, 120 px band | **542 ms** | 111 | 221 | **332** |
| Partial, 200 px band | 560 ms | 107 | 214 | 321 |
| Partial, 400 px band | 608 ms | 99 | 197 | 296 |
| Partial, 800 px (full screen) | 704 ms | 85 | 170 | 256 |

## The hypothesis that died

This project bet on **windowed partial updates**: RSVP only changes one word, so
redrawing a narrow band should be dramatically cheaper than the full screen, and that
was framed as "the key bet" in the plan.

**It is not.** A 40 px band takes 524 ms and a full-screen 800 px partial takes 704 ms —
a **20x area difference for 34% more time**.

The driver's own debug output says why. Every partial update logs:

```
_Update_Part : 501000
```

A fixed **~501 ms** regardless of window size. That is the panel's partial-refresh
waveform, and it is constant. Only the SPI transfer scales with area, adding 23 ms for a
40 px band and 203 ms for the full screen.

So shrinking the update region buys almost nothing. **Chunk size is the only real
lever**, which was the secondary hypothesis and is now the primary one.

## What this means for RSVP

Comprehension research puts the useful band at **250–350 WPM** — it holds against normal
reading there and degrades above. Against a 542 ms floor:

- **One word per update: 111 WPM.** Less than half the useful minimum. **Classic
  single-word RSVP is not possible on this hardware.**
- **Two words: 221 WPM.** Still short.
- **Three words: 332 WPM.** Comfortably inside the band.

**Chunking is a requirement, not an optimisation.** The engine already treats chunk size
as a first-class lever, which turns out to have been the right call for the wrong
reason — it was justified by refresh economics, and the real justification is that
nothing else works.

There is a pleasing consequence. Three-word chunks also restore some parafoveal preview,
which the same research identifies as something single-word RSVP destroys. The
constraint the hardware imposes pushes toward the design the literature prefers.

## Risks retired

**SPI bus contention is a non-issue.** SD and EPD share the bus, and the plan flagged
this as needing "explicit arbitration design". Measured: 542.6 ms with concurrent SD
reads versus 542.0 ms without. **0.6 ms, about 0.1%.** No arbitration design needed.

**Timing is extremely stable.** Across 100 sustained updates: median 541.997 ms, p95
541.999 ms, max 542.783 ms. A spread under 1 ms. Pacing can be treated as deterministic,
and the `Samples` p95 tracking exists to catch a tail that turns out not to be there.

## Full refresh is for ghosting only

At 1958 ms a full refresh is 3.6x a partial and far too slow to advance a word. It has
exactly one job: clearing accumulated ghosting. The timing model already inserts pauses
at sentence and paragraph boundaries, and that is where a flush belongs — a 2-second
clear on a natural beat reads as intentional, mid-phrase it reads as a fault.

## What this run did NOT establish

**Power draw is unmeasured.** The device was on USB throughout, so battery voltage was
pinned near 2047 mV start and end. The number in the CSV is real but meaningless for
power: nothing was discharging. Establishing the cost of sustained refresh needs a run on
battery, and that is the remaining open risk — RSVP refreshes roughly 2x/second against
normal e-reading's once per 30 seconds, on a 650 mAh cell.

**Ghosting is unquantified.** There is no sensor for it. The sustained run performs 100
windowed partials without an intervening full refresh, so the panel state afterwards is
the evidence, assessed by eye.

**Only one refresh mode was tested.** GxEPD2's standard partial update was measured. Any
faster A2-style waveform the SSD1677 might expose would need driving the controller's LUT
registers directly, which was not attempted.
