#!/usr/bin/env bash
# ABOUTME: Writes a firmware image to the app0 partition at 0x10000 and verifies it.
# ABOUTME: Never touches the bootloader, partition table, nvs or spiffs.

set -uo pipefail

# The only address this script knows is 0x10000. The bootloader at 0x0, the partition
# table at 0x8000, nvs and spiffs are all left alone, so a bad application image costs
# a reflash and nothing more -- the device still boots into the ROM loader and still
# enumerates. That is the whole reason to write app0 directly rather than going through
# `pio run -t upload`, which is free to rewrite the lot.
#
# Re-running with the same image is a no-op in effect: the same bytes land at the same
# address and the verify pass confirms it.
#
# Usage:  ./scripts/02-flash.sh                     the reader's own build output
#         ./scripts/02-flash.sh path/to/firmware.bin
#         PORT=/dev/cu.usbmodem1101 ./scripts/02-flash.sh

INKFLOW_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$INKFLOW_DIR" || exit 1

APP_OFFSET="0x10000"
DEFAULT_IMAGE="firmware/reader/.pio/build/xteink-x4/firmware.bin"
IMAGE="${1:-$DEFAULT_IMAGE}"

# --- the image ----------------------------------------------------------------
# Check the image before the tools and before the port: it is the cheapest check and
# the one the operator can act on without the device attached.
if [ ! -f "$IMAGE" ]; then
    cat <<MSG
No image at: $IMAGE

Build it first:
    ~/.inkflow-tools/pio/bin/pio run -d firmware/reader

or pass a path:
    ./scripts/02-flash.sh path/to/firmware.bin
MSG
    exit 2
fi

[ -s "$IMAGE" ] || { echo "Image is empty: $IMAGE"; exit 2; }

# An ESP application image starts with the magic byte 0xE9. Anything else -- an ELF,
# a partition table, a half-downloaded file -- would be written happily by esptool and
# then fail to boot with no clue why.
MAGIC="$(dd if="$IMAGE" bs=1 count=1 2>/dev/null | od -An -tx1 | tr -d ' \n')"
if [ "$MAGIC" != "e9" ]; then
    cat <<MSG
REFUSING TO FLASH.

  $IMAGE
  first byte: 0x${MAGIC:-??}, expected 0xe9

That is not an ESP application image. An ELF file, a partition table, or a truncated
download all look like this. Nothing was written to the device.
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

SIZE="$(wc -c < "$IMAGE" | tr -d ' ')"
SHA="$(shasum -a 256 "$IMAGE" | awk '{print $1}')"

cat <<MSG
Flash application image
  image:  $IMAGE
  bytes:  $SIZE
  sha256: $SHA
  port:   $PORT
  target: app0 at $APP_OFFSET

WRITES to the device. Leaves untouched: bootloader (0x0), partition table (0x8000),
nvs, otadata and spiffs. Your books live on the removable microSD and are not at risk.

If this image does not boot, ./scripts/03-restore.sh puts stock back.

MSG

read -r -p "Write it? [y/N] " reply
case "$reply" in [yY]*) ;; *) echo "Aborted. Nothing was written."; exit 1 ;; esac

# --after no-reset keeps the chip in the bootloader. Stock firmware drops
# USB-Serial/JTAG once it boots, so ending on a hard reset takes the connection away
# and costs a power-cycle -- including the one needed to verify what was just written.
"$ESPTOOL" --chip esp32c3 --port "$PORT" --baud 460800 --after no-reset \
    write-flash "$APP_OFFSET" "$IMAGE"
WRITE_RC=$?

if [ "$WRITE_RC" -ne 0 ]; then
    cat <<MSG

WRITE FAILED (esptool exit $WRITE_RC).

app0 may now hold a partial image, so the device may not boot into the application.
The bootloader and partition table were not addressed and are intact, so the device
still enters download mode and this script can simply be re-run.

If the port has vanished, power-cycle the device and re-run.

--------- PASTE THIS ---------
flash: FAILED (esptool $WRITE_RC)
image: $(basename "$IMAGE")
port:  $PORT
------------------------------
MSG
    exit 5
fi

echo
echo "Verifying..."
VERIFY_OUT="$("$ESPTOOL" --chip esp32c3 --port "$PORT" --after no-reset \
    verify-flash "$APP_OFFSET" "$IMAGE" 2>&1)"
VERIFY_RC=$?
printf '%s\n' "$VERIFY_OUT"

if [ "$VERIFY_RC" -ne 0 ]; then
    cat <<MSG

VERIFY FAILED (esptool exit $VERIFY_RC).

The bytes on the device do not match the image. Re-run this script; if it fails the
same way twice, the flash itself is suspect and ./scripts/03-restore.sh will tell you
whether a known-good image reads back correctly.

--------- PASTE THIS ---------
flash: WROTE BUT VERIFY FAILED
image: $(basename "$IMAGE")
sha256: $SHA
port:  $PORT
------------------------------
MSG
    exit 6
fi

cat <<MSG

Done. app0 holds the new image and the device is still in the bootloader.

Power-cycle it to run what you just wrote.

--------- PASTE THIS ---------
flash: ok
image: $(basename "$IMAGE")
bytes: $SIZE
sha256: $SHA
at:    $APP_OFFSET
------------------------------
MSG
