# Simulator

Renders the reader's screens on a desktop, so layout and pacing can be looked at instead
of read aloud off a device.

```sh
cmake -B build -DCMAKE_BUILD_TYPE=Debug && cmake --build build
./build/sim/sim [input] [output-dir] [frames] [--portrait] [--wpm n] [--no-png] [--gif path]
```

`input` is a `.rsvp` sidecar — streamed, exactly as the device reads one off the card —
or a `.txt` small enough to tokenise into RAM; with neither it plays a built-in passage.
`frames` is how many chunks to present, 0 for the whole document. Landscape is what the
firmware ships, so landscape is what you get: `--portrait` renders the rejected layout,
which must still overflow.

## Watching a session instead of describing one

`--gif path` writes the session as an animated GIF:

```sh
./build/sim/sim book.rsvp . 24 --no-png --gif reading.gif
```

Every frame is held for the reading loop's own `holdMs`, refresh time included, so the
animation runs at the pace the device would. That is the whole point of it. Pacing is the
one thing about this reader nobody has been able to confirm without power-cycling a
device and narrating the panel, and an animation at any other speed would be worse than
none — it would look like evidence. GIF counts delay in hundredths of a second and most
viewers clamp anything under about 2cs to a default rate, but a hold is around 54cs, so
nothing here is near that floor.

Each frame carries the whole screen rather than a diff of the reading band. At one bit
per pixel the file is small either way — 24 frames of 800x480 comes to 68 KB — and
a frame that shows the whole screen is a frame that can be trusted.

## It is a test, not a viewer

The simulator **exits non-zero** if any frame overflows the screen, and says why. That
turns a class of fault which is invisible in code review and awkward to describe over
chat into a build failure.

It earned that on its first run. Portrait at 20 characters per chunk overran the right
edge by 88 px, because a chunk is positioned by its *pivot* — which sits near the start
of the first word — so nearly the whole chunk extends rightward. The usable budget is
the screen width minus the focal column, 290 px of the 480, and a budget computed as
though the chunk were centred is simply wrong. That is what moved the reader to
landscape.

## Why it is pixel-faithful

`canvas.hpp` walks glyph bitmaps exactly as Adafruit_GFX does — packed MSB-first,
positioned by `xOffset`/`yOffset` from the cursor baseline, advanced by `xAdvance` —
over the same vendored font data the firmware compiles in. Reimplemented rather than
linked because the library is entangled with Arduino headers, and the part that matters
is forty lines of bit-walking.

Chunking, pivot selection and hold timing come from `rsvp-core`, and the reading loop
itself from `rsvp-reader`; the firmware links the same two libraries rather than a copy
of either. `panel.hpp` supplies what is left: it charges the measured cost of every full
and partial refresh and counts the partials since the last flush, from the least-squares
fit over `hardware-notes/eink-bench-20260918.csv`.

**What that model cannot do is surprise you.** It reproduces timings that were measured
once, on one panel, at one temperature, and it draws no ghosting it is not told to count.
Real hardware is what `bench/eink-bench` is for.
