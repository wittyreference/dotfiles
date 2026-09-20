# The reader read a book — 2026-09-20

The reader streamed a real 98,633-token sidecar off the SD card and presented it at a
fixed focal point for minutes at a stretch, without a crash, a watchdog reset or a
brownout. Until today the longest thing it had ever displayed was a 66-token built-in
passage, and [`first-reading-20260918.md`](first-reading-20260918.md) was careful to say
so: *"No real book has been read."* That sentence is now false.

Full serial capture: [`session-20260920T172158Z.log`](session-20260920T172158Z.log).

## What came up

```
inkflow: boot
inkflow: input ok
inkflow: display ok
inkflow: sd ok
inkflow: document loaded
inkflow: agents.rsvp, 98633 tokens, streamed, resume at 0
```

Six lines, and the sixth is the one worth reading twice. `streamed` means the document is
being read through `Document`'s 256-token aligned block cache off the card — not
tokenised into the 16 KB RAM path — which is the code path the device was designed
around and the one the simulator has been exercising through `FileSource` all along.

**Two defects from the last session are confirmed fixed on hardware.** There is no
`[E][Preferences.cpp] nvs_get_str len fail: doc NOT_FOUND` line, on a genuinely empty
NVS: the `isKey()` guard works, and a benign first-run condition no longer logs at ERROR
level. And the 108-second silent card scan is gone — `document loaded` arrives promptly,
because the card now holds a `.rsvp` the scan finds immediately rather than walking every
entry to no purpose.

## The simulator was right

This is the result the whole "make the simulator authoritative" rewrite was for. The same
book, the same opening, at the same requested speed:

| | Simulator | Hardware |
|---|---|---|
| Delivered pace at 330 WPM requested | 245 WPM | **234 WPM** |
| Worst ghosting before a flush | 68 partials | **68 partials** |
| Full refreshes in the opening 200 chunks | 4 | **4** |

Within 5% on pace, exact on the ghost bound, exact on the refresh count. The simulator is
a panel model fitted to six band-height measurements, running the shipped reading loop
against a synthetic clock, on a laptop. It predicted what the hardware did.

That is the claim `sim/README.md` has been making and could not previously support.

## The panel is almost perfectly deterministic

303 partial refreshes and 8 full ones, as reported by GxEPD2:

| Operation | Observations | Range |
|---|---|---|
| Partial, 120 px band | 303 | **500 999 – 501 002 µs** |
| Full | 8 | **1 687 999 – 1 689 004 µs** |

A spread of **3 microseconds across 303 partial refreshes.** `REFRESH-MEASUREMENTS.md`
concluded from the bench that timing on this panel is deterministic — p95 equal to median
across 100 updates — and predicted a fixed ~501 ms partial waveform from six band heights.
Both hold over a real reading session.

The 1689 ms full-refresh figure reappears unchanged, and so does its disagreement with the
bench's 1958 ms median. Still recorded rather than resolved: the bench timed the whole
operation wall-clock including power-on and power-off, `_Update_Full` is the waveform
phase alone, and 269 ms is the right order for the panel's quoted 100 ms power-on and
200 ms power-off. Nothing has yet measured the two independently on the same run.

## The ghost flush behaves exactly as designed

Partial refreshes between full ones, in order: **68, 62, 61, 76**.

The reader's flush is three-tiered — 60 partials at a paragraph boundary, 90 at a
sentence, 120 unconditionally. Every flush this session came from the *first* tier: the
count passes 60 and the refresh waits for the next paragraph, which arrives within a
dozen or so chunks. The 90 and 120 fallbacks never fired. They exist for prose that does
not offer a paragraph on schedule, and this book does.

## Two things the hardware found that no test could

**USB is the charger, so the battery reading over serial is worthless.** Across 670 tokens
of sustained reading the cell reported 1902 mV rising steadily to 1932 mV. The direction
is the problem: the measurement channel and the charging channel are the same cable, so
the one number this project still lists as unknown cannot be taken while anything is
listening. Addressed by writing the reading to `/battery.csv` on the card and adding a
`/get` route to the transfer page so it can be pulled off without opening the device.

**Peak ghosting cannot be inspected on the device.** Every button handler ends in
`renderFull()` — `togglePlay` included, at `firmware/reader/src/main.cpp`. Pausing to look
at accumulated ghosting clears the accumulated ghosting. There is no freeze-frame, and the
only way to photograph the panel at its worst is to wait out the cycle: a full refresh is
visibly a flash, and the panel is most ghosted about thirty-five seconds after one ends.
Recorded in `docs/CONTROLS.md` under what is not implemented.

## The power button works, and so does the screen that had never been reached

Held for about a second, mid-read:

```
17:26:06 inkflow: at 817, 1933 mV
17:26:07 inkflow: sleeping
17:26:09 _Update_Full : 1687999
            <USB drops>
```

That is the designed sequence exactly: save the position, stop playing, draw the sleep
screen as a full refresh, arm the power pin as a wake source, sleep. The USB port
disappearing from the host is the deep sleep itself — the device is off, and e-paper holds
the last image with no power, which is the entire reason the sleep screen exists.

Worth noting what this is: `Reader::renderSleep()` was written, reviewed and committed
with nothing calling it — `enterDeepSleep()` drew `renderFull()` instead, and the commit
message said so. It was wired up and given tests this week. This is the first time either
the button or the screen has run on hardware, and both worked on the first attempt.

## What is still unproven

- **The WiFi transfer still has not completed.** It was not attempted in this leg.
- **Power draw on battery is still unmeasured.** The instrumentation to measure it exists
  now; the run has not been made.
- **The pacing has not been judged.** 234 WPM delivered in three-word chunks is what the
  device does. Whether it is comfortable to read for an hour is the question no
  measurement answers.
