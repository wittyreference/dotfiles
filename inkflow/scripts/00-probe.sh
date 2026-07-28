#!/usr/bin/env bash
# ABOUTME: Read-only probe of an attached Xteink X4 -- port, chip, flash and eFuse state.
# ABOUTME: Writes a full report to a file and prints a short digest for pasting into chat.

set -uo pipefail

# STRICTLY READ-ONLY. Every esptool subcommand here reads; none erase, write, or burn
# an eFuse. Safe on an unknown device, which is the point -- this runs before the
# backup so the backup is taken with knowledge rather than hope.
#
# Usage:  ./scripts/00-probe.sh            auto-detect the port
#         PORT=/dev/cu.usbmodem1101 ./scripts/00-probe.sh

INKFLOW_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$INKFLOW_DIR" || exit 1

OUT_DIR="hardware-notes"
mkdir -p "$OUT_DIR"
STAMP="$(date -u +%Y%m%dT%H%M%SZ)"
REPORT="$OUT_DIR/probe-$STAMP.md"
EFUSE_TMP="$(mktemp)"

# Publish on every exit path, including the early ones. "No port found" is a result
# worth having in the repo, not just a reason to stop -- and the whole point of this
# setup is that nobody transcribes output by hand.
publish() {
    [ "${NO_PUSH:-0}" = "1" ] && return 0
    [ -x "$INKFLOW_DIR/scripts/push-results.sh" ] || return 0
    echo
    "$INKFLOW_DIR/scripts/push-results.sh" || true
}
trap 'rm -f "$EFUSE_TMP"; publish' EXIT

log() { printf '%s\n' "$*" >> "$REPORT"; }

# esptool v5 hyphenated its subcommands; v4 used underscores. Try both rather than
# pinning a version the operator may not have.
esp() {
    local sub="$1"; shift
    esptool "$sub" "$@" 2>&1 || esptool "${sub//-/_}" "$@" 2>&1
}
efuse_tool() {
    if command -v espefuse >/dev/null 2>&1; then espefuse "$@";
    elif command -v espefuse.py >/dev/null 2>&1; then espefuse.py "$@";
    else return 127; fi
}

echo "inkflow probe -- read-only, nothing is written to the device"
echo

: > "$REPORT"
log "# X4 probe -- $STAMP"
log ""
log '```'
log "host: $(uname -srm)"
command -v sw_vers >/dev/null 2>&1 && log "macos: $(sw_vers -productVersion 2>/dev/null)"
log '```'

# --- port --------------------------------------------------------------------
# The ESP32-C3's native USB enumerates as cu.usbmodem*, but a board with a USB-UART
# bridge would appear as cu.usbserial*, cu.wchusbserial*, or cu.SLAB_*. Look at every
# candidate rather than assuming, and exclude the Bluetooth pseudo-port that is always
# present on a Mac and never what anyone wants.
list_candidate_ports() {
    ls /dev/cu.* 2>/dev/null | grep -viE 'bluetooth|debug-console' || true
}

PORT="${PORT:-}"
if [ -z "$PORT" ]; then
    PORT="$(list_candidate_ports | grep -iE 'usbmodem|usbserial|wchusbserial|slab' | head -1)"
fi

if [ -z "$PORT" ]; then
    log ""
    log "## Result: NO SERIAL PORT"
    log ""
    log "No usable \`/dev/cu.*\` serial device present."
    log ""
    log "### All /dev/cu.* entries"
    log ""
    log '```'
    if [ -n "$(list_candidate_ports)" ]; then
        list_candidate_ports >> "$REPORT"
    else
        log "(none besides Bluetooth)"
    fi
    log '```'
    log ""
    # Full tree, not a keyword grep. The point is to see whether *anything* new appears
    # when the device is plugged in -- a narrow filter can't distinguish "device absent"
    # from "device present but not matching my guess at its name".
    log "### Full USB device tree"
    log ""
    log '```'
    if command -v system_profiler >/dev/null 2>&1; then
        system_profiler SPUSBDataType 2>/dev/null >> "$REPORT"
    fi
    log '```'
    log ""
    log "### Non-Apple USB vendors seen"
    log ""
    log '```'
    if command -v ioreg >/dev/null 2>&1; then
        ioreg -p IOUSB -w0 -l 2>/dev/null \
            | grep -iE '"(USB Vendor Name|USB Product Name|idVendor|idProduct)"' \
            | sed 's/^[[:space:]]*//' >> "$REPORT" || true
    fi
    log '```'

    cat <<'MSG'
NO SERIAL PORT FOUND.

Work through these in order. Most of the time it is one of the first three,
not a locked device.

  1. Is the X4 actually connected, and is it POWERED ON? An off device
     enumerates nothing.
  2. Cable. Charge-only USB-C cables are visually identical to data cables
     and are the single most common cause. Borrow a cable you have used for
     data transfer, not one that came with a power bank.
  3. Direct to the Mac -- no hub, no dongle, no monitor passthrough. Then try
     the other port.
  4. Unplug everything else USB, plug in only the X4, and re-run. The report
     now dumps the full USB tree, so we can see whether the device appears at
     all -- even unrecognised.

Only once all four are ruled out is "locked" the likely answer. A locked unit
cannot be backed up over USB and can be permanently stranded by the wrong
firmware -- see docs/PLATFORM-MATRIX.md section 2 before flashing anything.

--------- PASTE THIS ---------
probe: NO PORT FOUND
MSG
    printf 'usb non-apple: %s\n' \
        "$(ioreg -p IOUSB -w0 -l 2>/dev/null | grep -i '"USB Vendor Name"' \
           | grep -vi apple | sed 's/.*= *//' | tr -d '"' | tr '\n' ',' | sed 's/,$//')"
    printf 'host: %s\n' "$(sw_vers -productVersion 2>/dev/null || uname -sr)"
    echo "------------------------------"
    echo
    echo "Full report: $REPORT"
    exit 3
fi

echo "Port: $PORT"
log ""
log "## Port"
log ""
log "\`$PORT\`"

if ! command -v esptool >/dev/null 2>&1; then
    log ""
    log "**esptool not installed** -- chip, flash and eFuse sections skipped."
    cat <<MSG

esptool is not installed. Install it and re-run:
    pipx install esptool        # or: pip3 install esptool

--------- PASTE THIS ---------
probe: port $PORT found, esptool MISSING
------------------------------

Full report: $REPORT
MSG
    exit 4
fi

log ""
log "## esptool"
log ""
log '```'
log "$(esptool version 2>&1 | head -1)"
log '```'

# --- chip / flash / mac -------------------------------------------------------
echo "Reading chip id, flash id, MAC..."
CHIP_OUT="$(esp chip-id --port "$PORT")"
FLASH_OUT="$(esp flash-id --port "$PORT")"
MAC_OUT="$(esp read-mac --port "$PORT")"

for section in "chip-id:$CHIP_OUT" "flash-id:$FLASH_OUT" "read-mac:$MAC_OUT"; do
    log ""
    log "### ${section%%:*}"
    log ""
    log '```'
    printf '%s\n' "${section#*:}" >> "$REPORT"
    log '```'
done

# --- eFuses -------------------------------------------------------------------
# The valuable part. Settles three things this project has only ever inferred:
# whether secure boot is on, whether flash encryption is on, and -- if a unit turns
# out locked -- whether the lock is a burned eFuse or a firmware-level USB disable.
# Nobody has published this for the X4. `summary` is read-only.
echo "Reading eFuse summary..."
if efuse_tool --port "$PORT" summary > "$EFUSE_TMP" 2>&1; then :; fi
log ""
log "### eFuse summary"
log ""
log '```'
cat "$EFUSE_TMP" >> "$REPORT"
log '```'

# --- digest -------------------------------------------------------------------
field() {
    # First matching eFuse line, trimmed. Absent field prints "not reported".
    local name="$1" line
    line="$(grep -m1 -E "^[[:space:]]*${name}[[:space:]]" "$EFUSE_TMP" 2>/dev/null)"
    if [ -z "$line" ]; then printf 'not reported'; else
        printf '%s' "$(printf '%s' "$line" | sed -E 's/[[:space:]]+/ /g' | cut -c1-72)"
    fi
}

{
    echo
    echo "--------- PASTE THIS ---------"
    echo "port:  $PORT"
    printf 'chip:  %s\n' "$(printf '%s' "$CHIP_OUT" | grep -m1 -iE '^chip is' || echo '?')"
    printf 'flash: %s\n' "$(printf '%s' "$FLASH_OUT" | grep -m1 -iE 'detected flash size' || echo '?')"
    printf 'mfr:   %s\n' "$(printf '%s' "$FLASH_OUT" | grep -m1 -iE 'manufacturer' || echo '?')"
    echo "-- eFuses that matter --"
    for f in SECURE_BOOT_EN SPI_BOOT_CRYPT_CNT DIS_USB_JTAG DIS_USB_SERIAL_JTAG \
             DIS_DOWNLOAD_MODE DIS_DIRECT_BOOT DIS_FORCE_DOWNLOAD; do
        printf '%s\n' "$(field "$f")"
    done
    echo "------------------------------"
    echo
    echo "Full report: $REPORT"
    echo
    echo "Next: ./scripts/01-backup.sh    # golden flash dump, ~25 min"
} | tee -a /dev/null
