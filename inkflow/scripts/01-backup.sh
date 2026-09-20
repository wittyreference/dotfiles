#!/usr/bin/env bash
# ABOUTME: Takes the golden 16 MB flash dump from an attached X4 and checksums it.
# ABOUTME: Read-only against the device; the dump is the only recovery path that exists.

set -uo pipefail

# No public archive of a stock Xteink X4 firmware image exists anywhere. This dump is
# therefore the only restore path that is definitely yours, and it must be taken
# before anything writes to the device.
#
# Read-only: `read-flash` only reads. Nothing here erases or writes.
#
# Usage:  ./scripts/01-backup.sh
#         PORT=/dev/cu.usbmodem1101 ./scripts/01-backup.sh

INKFLOW_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$INKFLOW_DIR" || exit 1

# Publish the manifest on the way out, whatever happened. The image itself stays
# local and gitignored; only the checksum and metadata travel.
publish() {
    [ "${NO_PUSH:-0}" = "1" ] && return 0
    [ -x "$INKFLOW_DIR/scripts/push-results.sh" ] || return 0
    echo
    "$INKFLOW_DIR/scripts/push-results.sh" || true
}
trap 'publish' EXIT

OUT_DIR="hardware-notes"
mkdir -p "$OUT_DIR"
STAMP="$(date -u +%Y%m%dT%H%M%SZ)"
IMAGE="$OUT_DIR/x4-stock-golden-$STAMP.bin"
MANIFEST="$OUT_DIR/backup-$STAMP.md"

# setup-host.sh installs esptool into a venv under ~/.inkflow-tools and deliberately
# leaves it off PATH, so `command -v esptool` finds nothing on a correctly set up
# machine. Honour an explicit override first, then that venv, then PATH.
ESP_BIN_DIR="$HOME/.inkflow-tools/esp/bin"
ESPTOOL="${ESPTOOL:-}"
[ -n "$ESPTOOL" ] || [ ! -x "$ESP_BIN_DIR/esptool" ] || ESPTOOL="$ESP_BIN_DIR/esptool"
[ -n "$ESPTOOL" ] || ESPTOOL="$(command -v esptool 2>/dev/null || true)"
[ -n "$ESPTOOL" ] || {
    echo "esptool not found. Looked for \$ESPTOOL, then $ESP_BIN_DIR/esptool, then PATH."
    echo "Install it with:  ./scripts/setup-host.sh"
    exit 4
}

# Same candidate set as 00-probe.sh: a board with a USB-UART bridge appears as
# cu.usbserial*, cu.wchusbserial* or cu.SLAB_* rather than cu.usbmodem*, and a port
# the probe can find must not be invisible here.
PORT="${PORT:-$(ls /dev/cu.* 2>/dev/null | grep -viE 'bluetooth|debug-console' \
    | grep -iE 'usbmodem|usbserial|wchusbserial|slab' | head -1)}"
[ -n "$PORT" ] || { echo "No serial port found. Run ./scripts/00-probe.sh first."; exit 3; }

cat <<MSG
Golden flash backup
  port:   $PORT
  output: $IMAGE
  size:   16 MB, about a minute at 460800 baud over native USB

This only reads from the device. Leave it plugged in and don't let the Mac sleep.

MSG

read -r -p "Start? [y/N] " reply
case "$reply" in [yY]*) ;; *) echo "Aborted."; exit 1 ;; esac

START="$(date -u +%Y-%m-%dT%H:%M:%SZ)"

# esptool v5 spells this `read-flash` and its option values with hyphens; v4 used
# underscores for both. Try the modern form and fall back only if it actually failed --
# getting this wrong would run the dump twice.
#
# --after no-reset keeps the chip in the bootloader. The stock firmware drops
# USB-Serial/JTAG once it boots, so ending on a hard reset takes the connection away
# and costs a power-cycle before anything else can be run.
if ! "$ESPTOOL" --chip esp32c3 --port "$PORT" --baud 460800 --after no-reset \
        read-flash 0 0x1000000 "$IMAGE"; then
    echo
    echo "read-flash failed; retrying with the older read_flash spelling..."
    "$ESPTOOL" --chip esp32c3 --port "$PORT" --baud 460800 --after no_reset \
        read_flash 0 0x1000000 "$IMAGE"
fi

END="$(date -u +%Y-%m-%dT%H:%M:%SZ)"

if [ ! -s "$IMAGE" ]; then
    echo
    echo "FAILED: no image was produced. Nothing on the device was changed."
    exit 5
fi

SIZE="$(wc -c < "$IMAGE" | tr -d ' ')"
SHA="$(shasum -a 256 "$IMAGE" | awk '{print $1}')"
printf '%s  %s\n' "$SHA" "$(basename "$IMAGE")" > "$IMAGE.sha256"

cat > "$MANIFEST" <<EOF
# Golden flash backup -- $STAMP

The only known-good restore path for this specific unit. No public archive of a
stock X4 image exists, so this file cannot be re-obtained if lost.

\`\`\`
file:    $(basename "$IMAGE")
bytes:   $SIZE
sha256:  $SHA
port:    $PORT
started: $START
ended:   $END
esptool: $("$ESPTOOL" version 2>&1 | head -1)
\`\`\`

The image itself is gitignored -- 16 MB of device-specific firmware does not belong
in the repository. This manifest is committed so the checksum survives.

## Restore

\`\`\`sh
./scripts/03-restore.sh $(basename "$IMAGE")
\`\`\`

That checks the image against its \`.sha256\` sidecar before it writes anything. The
equivalent by hand, if the script is unavailable:

\`\`\`sh
esptool --chip esp32c3 --port $PORT --baud 460800 --after no-reset write-flash 0x0 $(basename "$IMAGE")
esptool --chip esp32c3 --port $PORT --after no-reset verify-flash 0x0 $(basename "$IMAGE")
\`\`\`
EOF

cat <<MSG

Done.

  $IMAGE
  $SIZE bytes
  sha256 $SHA

NOW COPY THE .bin OFF THIS MACHINE. A backup that lives only next to the thing it
backs up is not a backup. It is gitignored on purpose -- put it somewhere durable
yourself.

--------- PASTE THIS ---------
backup: ok
bytes:  $SIZE
sha256: $SHA
------------------------------

Manifest: $MANIFEST
MSG
