# Panel recovery — 2026-09-18

The panel that stopped updating at the end of the bring-up session **recovers on a proper
power-cycle**. Nothing had to be changed to get it back.

## What was done

1. `scripts/00-probe.sh` — read-only. The device answered: ESP32-C3 (QFN32) rev v0.4,
   16 MB flash, manufacturer `0x85`, and all seven relevant eFuses still unburned. So the
   part was never in doubt; only the display was.
2. `open-x4-epaper/sample-firmware` re-cloned and built. It is not vendored here and was
   not on the machine, so this step has to be repeated by anyone retracing it. It needs
   `git submodule update --init --recursive` — the `open-x4-sdk` submodule is not fetched
   by a plain shallow clone, and without it PlatformIO fails on a symlink it cannot create.
3. Flashed to app0 at `0x10000` with `scripts/02-flash.sh`. Bootloader, partition table,
   nvs and spiffs untouched. `Verification successful (digest matched)`.
4. Power-cycled. **The sample screen drew.**

## What this rules in and out

The freeze was **transient**. It was not a dead controller and not a bricked device.

`docs/STATUS.md` named two suspects: `eink-bench` ending in `display.hibernate()` combined
with a 2 ms reset pulse against the SSD1677 guide's specified 10 ms, and a flat battery
after an evening of sustained refreshing and WiFi. This run does not distinguish between
them — a power-cycle clears both. What it does establish is that neither leaves permanent
damage, which is the thing that mattered.

The 2 ms reset duration is still worth raising to 10 ms on general principle, since the
guide is explicit that waking from hibernate requires a hardware reset via RST and the
reader inherits the same value. It is a one-line change and there is now a known-good
baseline to test it against. Not done here, because changing a variable while diagnosing
another one is how the last session lost its bisect.

## The process lesson, applied

The previous session flashed three successively more complex firmwares without confirming
each one rendered, which left it ambiguous which change broke things. This time the
known-good sample was confirmed drawing **before** anything else was written. That is the
order to keep.

## State at the end of this note

`app0` holds the inkflow reader firmware — 880,720 bytes, sha256
`d5b931564bbe0f7fbea23fe701c1c792c1c28d80191687d0d6679ce034ad5318`, written and verified.
It has **not been power-cycled**, so it has not yet run: `--after no-reset` deliberately
leaves the device in the ROM bootloader, and the reader does not start until power is
cycled.

Stock is restorable at any time with `scripts/03-restore.sh`, which verifies the golden
image's sha256 before writing rather than after.
