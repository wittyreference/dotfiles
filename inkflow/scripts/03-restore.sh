#!/usr/bin/env bash
# ABOUTME: Writes a golden full-flash image back to the device and verifies it.
# ABOUTME: Checks the image against its .sha256 sidecar before writing anything.

set -uo pipefail

# No public archive of a stock Xteink X4 firmware image exists, so the golden dump from
# 01-backup.sh is the only restore path that is definitely yours. A corrupted copy of
# it is worse than no copy: written to 0x0 it takes the bootloader with it. So the
# checksum is verified before the device is touched, and a mismatch stops everything.
#
# This writes the whole 16 MB image at 0x0, which is what "back to stock" means --
# bootloader, partition table, nvs, both app slots and spiffs. To write an application
# image on its own, use ./scripts/02-flash.sh instead.
#
# Re-running is safe: the same bytes land at the same addresses and verify confirms it.
#
# Usage:  ./scripts/03-restore.sh                   newest golden image found
#         ./scripts/03-restore.sh path/to/x4-stock-golden-....bin
#         PORT=/dev/cu.usbmodem1101 ./scripts/03-restore.sh

INKFLOW_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$INKFLOW_DIR" || exit 1

FLASH_SIZE=16777216     # 16 MB, confirmed against the hardware by 00-probe.sh

# 01-backup.sh writes into hardware-notes/; the image is gitignored and the operator is
# told to copy it somewhere durable, which in practice is ~/inkflow-backups. Look in
# both, newest first, rather than making them remember which.
find_golden() {
    ls -t hardware-notes/x4-stock-golden-*.bin "$HOME"/inkflow-backups/x4-stock-golden-*.bin \
        2>/dev/null | head -1
}

IMAGE="${1:-$(find_golden)}"

if [ -z "$IMAGE" ] || [ ! -f "$IMAGE" ]; then
    cat <<MSG
No golden image found.

Looked in:
    hardware-notes/x4-stock-golden-*.bin
    $HOME/inkflow-backups/x4-stock-golden-*.bin

Take one with ./scripts/01-backup.sh, or pass a path:
    ./scripts/03-restore.sh path/to/x4-stock-golden-....bin
MSG
    exit 2
fi

# --- checksum, before anything is written -------------------------------------
SIDECAR="$IMAGE.sha256"
if [ ! -f "$SIDECAR" ]; then
    cat <<MSG
REFUSING TO RESTORE.

  $IMAGE
  no checksum sidecar at $SIDECAR

An unverified golden image is not a restore path, it is a guess. 01-backup.sh writes
the sidecar next to the image; if they got separated, put them back together.
MSG
    exit 2
fi

EXPECTED="$(awk '{print $1; exit}' "$SIDECAR")"
ACTUAL="$(shasum -a 256 "$IMAGE" | awk '{print $1}')"

if [ "$ACTUAL" != "$EXPECTED" ]; then
    cat <<MSG
REFUSING TO RESTORE -- sha256 MISMATCH.

  image:    $IMAGE
  expected: $EXPECTED
  actual:   $ACTUAL

This image is not the one that was checksummed. Writing it to 0x0 would overwrite the
bootloader with something unverified. Nothing was written to the device.

Find an intact copy before going further.

--------- PASTE THIS ---------
restore: REFUSED, sha256 mismatch
image:   $(basename "$IMAGE")
------------------------------
MSG
    exit 7
fi

SIZE="$(wc -c < "$IMAGE" | tr -d ' ')"
if [ "$SIZE" != "$FLASH_SIZE" ]; then
    cat <<MSG
REFUSING TO RESTORE.

  $IMAGE
  bytes: $SIZE, expected $FLASH_SIZE

This is not a full-flash image, and this script writes at 0x0. A partial image there
overwrites the bootloader with a fragment. To write an application image to app0, use:
    ./scripts/02-flash.sh $IMAGE
MSG
    exit 2
fi

# A full-flash dump starts with the bootloader, which carries the 0xE9 ESP image magic.
# Its absence means the dump failed rather than that the flash was blank.
MAGIC="$(dd if="$IMAGE" bs=1 count=1 2>/dev/null | od -An -tx1 | tr -d ' \n')"
if [ "$MAGIC" != "e9" ]; then
    cat <<MSG
REFUSING TO RESTORE.

  $IMAGE
  first byte: 0x${MAGIC:-??}, expected 0xe9

The image has the right size and checksum but no bootloader magic, so the dump it came
from did not read real flash. Nothing was written to the device.
MSG
    exit 2
fi

# --- the tools ----------------------------------------------------------------
# setup-host.sh installs esptool into a venv under ~/.inkflow-tools and deliberately
# leaves it off PATH. Honour an explicit override first, then that venv, then PATH.
ESP_BIN_DIR="$HOME/.inkflow-tools/esp/bin"
ESPTOOL="${ESPTOOL:-}"
[ -n "$ESPTOOL" ] || [ ! -x "$ESP_BIN_DIR/esptool" ] || ESPTOOL="$ESP_BIN_DIR/esptool"
[ -n "$ESPTOOL" ] || ESPTOOL="$(command -v esptool 2>/dev/null || true)"
[ -n "$ESPTOOL" ] || {
    echo "esptool not found. Looked for \$ESPTOOL, then $ESP_BIN_DIR/esptool, then PATH."
    echo "Install it with:  ./scripts/setup-host.sh"
    exit 4
}

# Same candidate set as 00-probe.sh.
PORT="${PORT:-$(ls /dev/cu.* 2>/dev/null | grep -viE 'bluetooth|debug-console' \
    | grep -iE 'usbmodem|usbserial|wchusbserial|slab' | head -1)}"
[ -n "$PORT" ] || { echo "No serial port found. Run ./scripts/00-probe.sh first."; exit 3; }

cat <<MSG
Restore stock firmware
  image:  $IMAGE
  bytes:  $SIZE
  sha256: $ACTUAL   (matches $(basename "$SIDECAR"))
  port:   $PORT
  target: whole flash from 0x0

WRITES THE ENTIRE FLASH. Bootloader, partition table, nvs, both app slots and spiffs
are all replaced. Anything stored in nvs by custom firmware is gone. Your books live on
the removable microSD and are not at risk.

This takes about a minute at 460800 baud over native USB.

MSG

read -r -p "Restore it? [y/N] " reply
case "$reply" in [yY]*) ;; *) echo "Aborted. Nothing was written."; exit 1 ;; esac

START="$(date -u +%Y-%m-%dT%H:%M:%SZ)"

# --after no-reset keeps the chip in the bootloader. Stock firmware drops
# USB-Serial/JTAG once it boots, so ending on a hard reset takes the connection away
# before the verify pass can run and costs a power-cycle.
"$ESPTOOL" --chip esp32c3 --port "$PORT" --baud 460800 --after no-reset \
    write-flash "0x00000" "$IMAGE"
WRITE_RC=$?

if [ "$WRITE_RC" -ne 0 ]; then
    cat <<MSG

RESTORE WRITE FAILED (esptool exit $WRITE_RC).

The flash is in an unknown state and the device may not boot. It should still enter
download mode over USB, so re-run this script -- the image is verified and intact.

If the port has vanished, power-cycle the device and re-run.

--------- PASTE THIS ---------
restore: FAILED (esptool $WRITE_RC)
image:   $(basename "$IMAGE")
port:    $PORT
------------------------------
MSG
    exit 5
fi

echo
echo "Verifying the whole image..."
VERIFY_OUT="$("$ESPTOOL" --chip esp32c3 --port "$PORT" --baud 460800 --after no-reset \
    verify-flash "0x00000" "$IMAGE" 2>&1)"
VERIFY_RC=$?
printf '%s\n' "$VERIFY_OUT"

END="$(date -u +%Y-%m-%dT%H:%M:%SZ)"

if [ "$VERIFY_RC" -ne 0 ]; then
    cat <<MSG

VERIFY FAILED (esptool exit $VERIFY_RC).

The bytes on the device do not match the golden image. Re-run this script; if it fails
the same way twice the flash hardware is suspect, and that is worth knowing before
trusting this unit with anything.

--------- PASTE THIS ---------
restore: WROTE BUT VERIFY FAILED
image:   $(basename "$IMAGE")
sha256:  $ACTUAL
port:    $PORT
------------------------------
MSG
    exit 6
fi

cat <<MSG

Done. The device holds stock firmware again and is still in the bootloader.

Power-cycle it to boot stock. Remember that stock drops USB-Serial/JTAG once it boots,
so the port will disappear until the next power-up.

--------- PASTE THIS ---------
restore: ok
image:   $(basename "$IMAGE")
bytes:   $SIZE
sha256:  $ACTUAL
started: $START
ended:   $END
------------------------------
MSG
