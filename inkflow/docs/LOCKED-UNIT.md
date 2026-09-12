# Working with a USB-locked X4

MC's unit is locked. This is the playbook for that case, which the rest of the docs
assumed away. **Conclusion up front: a locked unit can still be a development target**,
but the route and the safety rules are different, and one of them is load-bearing.

## Confirming the diagnosis

A locked unit **never enumerates as a USB serial port**. Everything else can look fine:

| Check | Our unit |
|---|---|
| Cable carries data | Yes — an iPhone enumerated on it |
| Mac USB works | Yes — three healthy buses |
| Device powered and running | Yes — showing the stock onboarding screen |
| Serial port appears | **No** |

That combination is the locked signature. Source:
[PocketInk FAQ](https://pocketink.io/firmware/faq/) — *"If the device never appears over
USB, treat it as locked."* Locked units cluster in AliExpress and other third-party
channels; xteink.com stock is usually unlocked.

## What is permanently lost

**The golden flash backup.** `esptool read-flash` runs over USB, which is exactly the
route the lock blocks. There is no way to take a stock image off this unit, and no
public archive of one exists. **This unit has no restore point and never will.**

**USB flashing, forever.** The unlocker does not restore it — *"you get custom firmware
but the USB port stays locked."*

Every decision below is shaped by having no USB safety net.

## Getting custom firmware on: the one-time unlock

The [CrossPoint OTA Unlocker](https://crosspointreader.com/unlocker) makes the host
machine a Wi-Fi hotspot, DNS-spoofs the Xteink update server, and serves community
firmware when the device's own *Check for Updates* runs. Stock accepts it because
Xteink's OTA images carry no signature the device verifies.

**Only CrossPoint is a safe target.** This is not a preference:

| Firmware | On a locked unit |
|---|---|
| **CrossPoint** | Safe — has OTA *and* in-app SD System Update, so there is always a way back |
| CrossInk | Nominally safe; its upstream repo is not publicly available (see FEATURE-SURVEY) |
| Papyrix | **Removed OTA. No way back.** |
| CrossPet | **An owner reports being permanently stuck on it** |
| Microreader, AvesO3, inx | Bricking risk |

Put a stock `update.bin` on the SD card first as the only rollback. Note the X3
button-combo recovery *"doesn't reliably engage on a stock X4"* — on a locked X4 the
OTA Unlocker is the only way in.

## After that: the iteration loop

Once CrossPoint is installed, custom firmware goes on **via SD card**, no USB:

1. Build a `.bin`
2. Copy it to the SD card root as `update.bin`
3. On device: **Settings → System → SD Card Firmware Update**, pick the file

No signature or checksum verification is documented anywhere on this path, and it is
the documented route for locked units.

**The build does not need to happen on the operator's machine.** A full ESP32-C3
toolchain installs and builds in the authoring environment — verified, a 247 KB
`firmware.bin` produced in 70 seconds via PlatformIO with `board = esp32-c3-devkitm-1`.
So the loop is:

```
author writes code  ->  author builds .bin  ->  operator copies to SD  ->
operator picks it in the menu  ->  device writes results to SD  ->  results committed
```

The operator does file copies and menu taps. No toolchain, no USB, no agent on their
machine.

## The rule that matters most

**Any firmware flashed to this unit must itself provide a way back.**

There is no USB escape hatch. If an image is flashed that lacks an SD-update path, the
device runs that image forever. That rules out shipping `eink-bench` as standalone
firmware — a bench build with no menu would strand the device on a benchmark.

**So the bench must be a CrossPoint fork with the bench as a mode, not a standalone
image.** The SD System Update menu then stays reachable at all times.

This is a better design regardless: the RSVP reader was always going to live inside
CrossPoint, so the bench sharing that host means the measurement runs in the same
environment the product will.

## What the unlocker cannot do

Serving our own firmware through the unlocker is possible but not worth it: it requires
hosting a catalog JSON at a public HTTPS URL (`firmware_url`, `firmware_sha256`, `size`,
`id`, `channel`, `name`, `version`, `released_at`, `supported_devices`) **and** forking
the unlocker to patch a hard-coded catalog URL in
`crates/unlocker-core/src/catalog.rs`. The SD path achieves the same thing with a file
copy. Use the unlocker once, to get CrossPoint on, and never again.

## Open question

Whether CrossPoint's SD System Update accepts arbitrary images is documented only by
absence — no source describes verification on that path, and it is the official route
for locked units. **It has not been tested by us.** The first custom build we send is
the test, and per the rule above it must be a CrossPoint fork so a rejection or a bad
image is recoverable.
