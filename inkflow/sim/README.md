# Simulator

Renders the reader's screens to PNG on a desktop, so layout can be looked at instead of
read aloud off a device.

```sh
c++ -std=c++17 -O1 -Ivendor -I../core/include src/main.cpp src/png.cpp ../core/src/*.cpp -lz -o sim
./sim [text-file] [output-dir] [frames]
```

Add `-DSIM_LANDSCAPE` to render the 800x480 landscape layout instead of 480x800 portrait.

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

Chunking, pivot selection and hold timing come from `rsvp-core` itself, not a copy. What
differs from the device is only the panel: no refresh latency, no ghosting, no partial
update. Those need hardware, which is what `bench/eink-bench` is for.

**The layout constants are duplicated from `firmware/reader/src/config.h`.** If they
drift, the simulator will confidently show a layout the device does not produce, which
is worse than having no simulator. Change them together.
