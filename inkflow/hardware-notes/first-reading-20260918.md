# The reader ran — 2026-09-18

The RSVP reader presented words at a fixed focal point on the device. That had never
happened before, and it is the claim the README had refused to make since the project
started.

## What was observed

Bring-up, over USB serial. The firmware builds with `ARDUINO_USB_CDC_ON_BOOT=1` and holds
USB up after boot, so this is readable directly rather than off the panel:

```
inkflow: boot
inkflow: input ok
inkflow: display ok
inkflow: sd ok
inkflow: document loaded
inkflow: built-in, 66 tokens, in RAM, resume at 0
```

Then, on pressing Confirm, 24 partial refreshes and 4 full ones — the whole 66-token
built-in passage played through and stopped cleanly at the end. No crash, no panic, no
watchdog reset, no brownout. The panel showed the passage and advanced through it.

## The measurement that predicted itself

Every partial update logged by GxEPD2:

```
_Update_Part : 501000
```

**501.000 ms, every frame.** `REFRESH-MEASUREMENTS.md` concluded from the bench data that
the panel's partial waveform is a fixed ~501 ms and only the SPI transfer scales with area
— which is why windowed partial updates were abandoned as a lever and why chunking became
mandatory. That number was inferred from six band-height measurements. Here it is, stated
by the driver, unchanged, on every refresh of a real reading session.

## A second full-refresh figure, which needs reconciling

GxEPD2 reports `_Update_Full : 1689000`, i.e. 1689 ms. `eink-bench` measured a median of
**1958 ms** for a full refresh.

These are not contradictory — the bench timed the whole operation wall-clock, including
power-on, SPI transfer and power-off, while `_Update_Full` is the waveform phase alone.
The gap is 269 ms, which is the right order for the panel's quoted 100 ms power-on and
200 ms power-off. Recorded rather than resolved: nothing has yet measured the two
independently on the same run, and the timing model is calibrated against the 1958 ms
figure, which is the one a reader actually waits.

## You do not need to power-cycle a custom build

`esptool --port <port> --after hard-reset chip-id` resets the chip into the freshly written
app, and **USB survives it** on firmware built with `ARDUINO_USB_CDC_ON_BOOT=1`.

The bring-up notes warn that any esptool command ending in a hard reset takes the USB
connection away. That is true of **stock**, which drops USB-Serial/JTAG once the
application boots. It is not true of ours. So the flash loop needs no hands on the device
at all:

```sh
./scripts/02-flash.sh path/to/firmware.bin     # writes app0, stays in bootloader
esptool --port "$PORT" --after hard-reset chip-id   # boots it
```

**Do not hold the serial port open while doing this.** esptool and a reader on the same
port produce `device reports readiness to read but returned no data (device disconnected or
multiple access on port?)`, and the chip comes back up in download mode
(`boot:0x7 DOWNLOAD`) rather than running the app. Close the capture, reset, reopen.

## Two defects the hardware found and the tests could not

**The SD scan takes 108 seconds and says nothing.** The NVS line above is stamped
`[108516]` — milliseconds since boot. `loadDocument()` walks the card root with
`openNextFile()` looking for a `.rsvp` and then a `.txt`; the test card had neither, so it
scanned every entry before falling back. Nothing is printed during the scan and
`renderFull()` is not reached until after it, so for nearly two minutes the device is
indistinguishable from a hang. It was very nearly diagnosed as one.

The simulator cannot catch this. It has no card, and document selection is the one part of
the reading path that is genuinely platform-specific.

**A benign condition is logged at ERROR level.**

```
[E][Preferences.cpp:483] getString(): nvs_get_str len fail: doc NOT_FOUND
```

That is NVS reporting no saved reading position, which is correct on a first run. Logging
it as an error is how a real fault becomes hard to spot later.

## What is still unproven

- **The WiFi transfer path has never completed on hardware.** The access point comes up and
  the web server answers — captive-portal probes from a joining phone produced
  `_handleRequest(): request handler not found`, which is the server working, not failing.
  But no file has been transferred, so the streaming upload handler, the AP holding up
  under a 1.4 MB transfer, and the post-upload document reload remain untested outside a
  compiler.
- **No real book has been read.** 66 tokens of built-in passage is not a book.
- **Power draw is still unmeasured.** This run was on USB.
- **Pacing has not been judged.** The reader now delivers the speed it is asked for, which
  it did not before, but whether 330 WPM in three-word chunks is comfortable is the one
  question no measurement answers.

## Note on the host machine

Uploading to the device's access point from this Mac is blocked by its VPN: a host route
pins `192.168.4.1` into `utun7`, so packets to the reader go down the tunnel. Three
tunnels were up. Joining from a phone sidesteps it entirely and needs no privileges, which
is the path to prefer rather than editing a VPN's routing table for a file copy.
