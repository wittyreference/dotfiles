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

command -v esptool >/dev/null 2>&1 || {
    echo "esptool not installed.  pipx install esptool"
    exit 4
}

PORT="${PORT:-$(ls /dev/cu.usbmodem* 2>/dev/null | head -1)}"
[ -n "$PORT" ] || { echo "No /dev/cu.usbmodem* found. Run ./scripts/00-probe.sh first."; exit 3; }

cat <<MSG
Golden flash backup
  port:   $PORT
  output: $IMAGE
  size:   16 MB, roughly 25 minutes

This only reads from the device. Leave it plugged in and don't let the Mac sleep.

MSG

read -r -p "Start? [y/N] " reply
case "$reply" in [yY]*) ;; *) echo "Aborted."; exit 1 ;; esac

START="$(date -u +%Y-%m-%dT%H:%M:%SZ)"

# esptool v5 spells this `read-flash`, v4 spells it `read_flash`. Try the modern form
# and fall back only if it actually failed -- getting this wrong would run a
# 25-minute dump twice.
if ! esptool --chip esp32c3 --port "$PORT" --baud 460800 read-flash 0 0x1000000 "$IMAGE"; then
    echo
    echo "read-flash failed; retrying with the older read_flash spelling..."
    esptool --chip esp32c3 --port "$PORT" --baud 460800 read_flash 0 0x1000000 "$IMAGE"
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
esptool: $(esptool version 2>&1 | head -1)
\`\`\`

The image itself is gitignored -- 16 MB of device-specific firmware does not belong
in the repository. This manifest is committed so the checksum survives.

## Restore

\`\`\`sh
esptool --chip esp32c3 --port $PORT --baud 460800 write-flash 0x0 $(basename "$IMAGE")
esptool --chip esp32c3 --port $PORT verify-flash 0x0 $(basename "$IMAGE")
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
