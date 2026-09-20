#!/usr/bin/env bash
# ABOUTME: Drives one reading session on the device -- build, flash, watch the serial log.
# ABOUTME: Composes the existing numbered scripts rather than reimplementing them.

set -uo pipefail

# The legs of a device session, one subcommand each:
#
#   ./scripts/04-read-session.sh build     pio run, print size and sha256
#   ./scripts/04-read-session.sh flash     02-flash.sh, then boot the app
#   ./scripts/04-read-session.sh transfer  print the phone-sized transfer instructions
#   ./scripts/04-read-session.sh watch     capture the serial log to hardware-notes/
#   ./scripts/04-read-session.sh publish   push-results.sh
#
# The usual order is build, flash, watch (in one window), transfer (read off another),
# and publish at the end. `watch` is the one that runs long: leave it going for the whole
# read so pacing, refresh counts and battery land in a single timeline.

SELF="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)/$(basename "${BASH_SOURCE[0]}")"
INKFLOW_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$INKFLOW_DIR" || exit 1

IMAGE="firmware/reader/.pio/build/xteink-x4/firmware.bin"
PIO="${PIO:-$HOME/.inkflow-tools/pio/bin/pio}"
[ -x "$PIO" ] || PIO="$(command -v pio 2>/dev/null || true)"

ESP_BIN_DIR="$HOME/.inkflow-tools/esp/bin"
ESPTOOL="${ESPTOOL:-}"
[ -n "$ESPTOOL" ] || [ ! -x "$ESP_BIN_DIR/esptool" ] || ESPTOOL="$ESP_BIN_DIR/esptool"
[ -n "$ESPTOOL" ] || ESPTOOL="$(command -v esptool 2>/dev/null || true)"

# Same candidate set as 00-probe.sh and 02-flash.sh.
find_port() {
    ls /dev/cu.* 2>/dev/null | grep -viE 'bluetooth|debug-console' \
        | grep -iE 'usbmodem|usbserial|wchusbserial|slab' | head -1
}

usage() {
    sed -n '8,18p' "$SELF" | sed 's/^# \{0,1\}//'
    exit 2
}

# --- build --------------------------------------------------------------------
do_build() {
    [ -n "$PIO" ] || {
        echo "pio not found. Install it with:  ./scripts/setup-host.sh"
        exit 4
    }
    "$PIO" run -d firmware/reader || exit 5

    [ -f "$IMAGE" ] || { echo "Build reported success but $IMAGE is missing."; exit 5; }
    local size sha
    size="$(wc -c < "$IMAGE" | tr -d ' ')"
    sha="$(shasum -a 256 "$IMAGE" | awk '{print $1}')"

    cat <<MSG

--------- PASTE THIS ---------
build: ok
bytes: $size
sha:   ${sha:0:16}...
------------------------------
MSG
}

# --- flash --------------------------------------------------------------------
do_flash() {
    local port
    port="${PORT:-$(find_port)}"
    [ -n "$port" ] || { echo "No serial port found. Run ./scripts/00-probe.sh first."; exit 3; }

    # Close any capture before writing. esptool and a reader on the same port produce
    # "device reports readiness to read but returned no data", and the chip comes back up
    # in download mode (boot:0x7 DOWNLOAD) rather than running the app. The failure looks
    # like a dead flash and is not one.
    if command -v lsof >/dev/null 2>&1 && lsof "$port" >/dev/null 2>&1; then
        cat <<MSG
REFUSING TO FLASH.

  $port is open in another process.

esptool and a serial reader on the same port produce "device reports readiness to read
but returned no data", and the device comes back up in download mode instead of running
the application. Close the "watch" window, then re-run this.

Holding it open:
$(lsof "$port" 2>/dev/null | tail -n +2 | awk '{print "  " $1 " (pid " $2 ")"}')
MSG
        exit 6
    fi

    PORT="$port" ./scripts/02-flash.sh "$IMAGE" || exit $?

    [ -n "$ESPTOOL" ] || { echo "esptool not found; power-cycle the device by hand."; exit 4; }

    cat <<MSG

Booting the application.

02-flash.sh finishes with --after no-reset, which leaves the chip in the ROM bootloader.
That rule exists for STOCK firmware, which drops USB-Serial/JTAG the moment it boots. Ours
does not: it builds with ARDUINO_USB_CDC_ON_BOOT=1 and holds USB up across a hard reset,
so this needs no hands on the device.
MSG
    "$ESPTOOL" --port "$port" --after hard-reset chip-id >/dev/null 2>&1

    cat <<MSG

--------- PASTE THIS ---------
flash: ok, booted
port:  $port
------------------------------

Now:  ./scripts/04-read-session.sh watch
MSG
}

# --- transfer -----------------------------------------------------------------
do_transfer() {
    local book="${1:-$HOME/inkflow-books/agents.rsvp}"
    local size="unknown size"
    [ ! -f "$book" ] || size="$(( $(wc -c < "$book") / 1024 )) KB"

    # Instructions rather than an upload, because the hard part is not the HTTP request --
    # it is keeping a client on a network with no internet for long enough to make one.
    # Both macOS and iOS leave such a network for a remembered one that has internet, and
    # they do it without saying so. Every failed attempt so far has been that, and the
    # device logged the association each time.
    cat <<MSG
Transfer a book.

Either a phone or this Mac will do. An earlier note said the Mac could not work because
its VPN pinned 192.168.4.1 into a tunnel; that was a one-off state and is not true in
general. On 2026-09-20 this Mac associated with the reader and was issued 192.168.4.2 by
the device's own DHCP server. What defeats BOTH is the same thing, below.

  1. On the reader, press RIGHT. Serial will say: inkflow: ap inkflow up at 192.168.4.1
  2. TURN CELLULAR DATA OFF on the phone. This is not optional and it is the thing that
     has failed every previous attempt: a phone that can still reach the internet will
     leave a network that cannot, silently, and the upload goes out over cellular to an
     address that does not exist there.
  3. On the phone, join wifi network   inkflow   password   inkflow-reader
  4. CHECK THE SERIAL LOG for:          inkflow: ap clients 0 -> 1
     That line is the only trustworthy account of whether the phone is on the network.
     The panel and the phone's own settings screen will both claim success either way.
  5. Open                              http://192.168.4.1
  6. Upload                            $(basename "$book")   ($size)
  7. On the reader, press RIGHT again.

The page will say either "Uploaded" with a size, or "Upload failed" with a reason. It
used to say "Uploaded" no matter what happened, so if you see a failure that is the page
working. Paste whichever it says.

This has never completed on hardware. Three attempts on 2026-09-20 all failed the same
way and none of them was the device's fault: the access point, its DHCP server and the
association all work, but no client stayed on the network long enough to make a single
HTTP request. Both macOS and iOS prefer a remembered network that has internet.

From a Mac, put the reader's network at the top of the list first, and put it back after:

    networksetup -addpreferredwirelessnetworkatindex en0 inkflow 0 WPA2 inkflow-reader

The serial log from "watch" is the evidence -- keep it running throughout. inkflow: ap
clients is the line that matters.

Book on this machine:
  $book
MSG
    [ -f "$book" ] || echo "  (not found -- convert one with ./build/tools/rsvp-mk/rsvp_mk)"
}

# --- watch --------------------------------------------------------------------
do_watch() {
    local port
    port="${PORT:-$(find_port)}"
    [ -n "$port" ] || { echo "No serial port found. Is the device attached?"; exit 3; }

    local stamp log
    stamp="$(date -u +%Y%m%dT%H%M%SZ)"
    log="hardware-notes/session-$stamp.log"

    cat <<MSG
Capturing $port at 115200 to:
  $log

Timestamped, so pacing, refresh counts and battery all land in one timeline. Leave this
running for the whole read. Ctrl-C to stop -- and stop it before flashing again.

MSG

    # stdbuf keeps the timestamper from holding lines in a pipe buffer, so the log is
    # readable while it is still being written rather than only after Ctrl-C.
    ( stty -f "$port" raw 115200 2>/dev/null || stty -F "$port" raw 115200 2>/dev/null ) || true
    cat "$port" \
        | while IFS= read -r line; do
              printf '%s %s\n' "$(date -u +%H:%M:%S)" "$line"
          done \
        | tee "$log"

    cat <<MSG

--------- PASTE THIS ---------
session log: $log
lines:       $(wc -l < "$log" | tr -d ' ')
boot lines:  $(grep -c '^..:..:.. inkflow:' "$log" 2>/dev/null || echo 0)
last:        $(tail -1 "$log" 2>/dev/null)
------------------------------

Publish it with:  ./scripts/04-read-session.sh publish
MSG
}

# --- publish ------------------------------------------------------------------
do_publish() {
    ./scripts/push-results.sh "$@"
}

case "${1:-}" in
    build)    shift; do_build "$@" ;;
    flash)    shift; do_flash "$@" ;;
    transfer) shift; do_transfer "$@" ;;
    watch)    shift; do_watch "$@" ;;
    publish)  shift; do_publish "$@" ;;
    *)        usage ;;
esac
