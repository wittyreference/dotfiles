# inkflow reader firmware

The RSVP reader itself, running on an Xteink X4.

## Reading material

Drop a `.rsvp` sidecar or a `.txt` file at the root of the microSD card. The firmware
prefers the first `.rsvp` it finds and falls back to the first `.txt`. With no card, or
neither on it, it plays a short built-in passage so the device is never a blank screen.

A `.txt` is tokenised into RAM, and that path is capped at **16 KB of text, 2000
tokens**. The device has 400 KB of SRAM with no PSRAM, and the framebuffer alone costs
48 KB. A `.rsvp` sidecar is streamed from the card instead and is not bound by those
caps — it is the path a whole book takes.

## Controls

| Button | Action |
|---|---|
| **Confirm** | Play / pause |
| **Left** | Rewind to the start of the sentence |
| **Up / Down** | Speed up / down, 30 WPM per press |
| **Right** | WiFi transfer mode — serve the upload page over the device's own AP |
| **Back** | Redraw (also clears ghosting); leaves transfer mode |

**Rewind is the feature, not a convenience.** Suppressing the backward glance is the one
thing RSVP inherently does to a reader, and it measurably costs comprehension. Pressing
Left again while already at a sentence start steps into the previous sentence, so
repeated presses walk backwards rather than sticking.

## Why three words at a time

Measured on hardware: a partial refresh costs **~542 ms regardless of how much of the
screen it covers**, because the panel's waveform is a fixed ~501 ms and only the SPI
transfer scales.

| Words per update | Resulting speed |
|---|---|
| 1 | 111 WPM — less than half the speed at which comprehension holds |
| 2 | 221 WPM |
| **3** | **332 WPM — inside the 250–350 band** |

Classic single-word RSVP is not possible on this panel. See
[`../../docs/REFRESH-MEASUREMENTS.md`](../../docs/REFRESH-MEASUREMENTS.md).

## Why the pivot is two ticks and not a red letter

Every RSVP reader since Spritz marks the optimal recognition point by colouring one
character. A 1-bit panel cannot. So two static guide marks bracket the focal column, and
each chunk is positioned so its pivot character lands between them.

They sit *outside* the band that gets redrawn, so they are drawn once and cost nothing
per word — which on a panel this slow is the difference between a design and a nice idea.

## Building

```sh
pio run                     # build
pio run -t upload           # or flash app0 directly with esptool
```

`build_unflags = -std=gnu++11` is load-bearing: rsvp-core is C++17, and under the
Arduino-ESP32 default a `constexpr` function must be a single return statement, so the
engine will not compile without it.

Flashing writes **only app0** at `0x10000`. The bootloader and partition table are never
touched, so a golden backup restores stock with a single `write-flash`.
