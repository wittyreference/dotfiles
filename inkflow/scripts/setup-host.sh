#!/usr/bin/env bash
# ABOUTME: Installs the host toolchain needed to build, flash and simulate inkflow.
# ABOUTME: Everything lands in venvs under ~/.inkflow-tools so nothing touches system python.

set -uo pipefail

TOOLS="$HOME/.inkflow-tools"
mkdir -p "$TOOLS"

say() { printf '%s\n' "$*"; }

say "inkflow host toolchain -> $TOOLS"
say

if [ ! -x "$TOOLS/esp/bin/esptool" ]; then
    say "installing esptool..."
    python3 -m venv "$TOOLS/esp" && "$TOOLS/esp/bin/pip" install -q esptool
fi
say "esptool:    $("$TOOLS/esp/bin/esptool" version 2>&1 | head -1)"

if [ ! -x "$TOOLS/pio/bin/pio" ]; then
    say "installing platformio (this takes a few minutes the first time)..."
    python3 -m venv "$TOOLS/pio" && "$TOOLS/pio/bin/pip" install -q platformio
fi
say "platformio: $("$TOOLS/pio/bin/pio" --version 2>&1)"

say
say "nothing to add to PATH: the operator scripts look in $TOOLS themselves."
say "set these only to point at a build of your own somewhere else:"
say "    export ESPTOOL=$TOOLS/esp/bin/esptool"
say "    export ESPEFUSE=$TOOLS/esp/bin/espefuse"
say "    export PIO=$TOOLS/pio/bin/pio"
say
say "the serial port, when the device is attached and awake:"
# Same candidate set as 00-probe.sh -- a board with a USB-UART bridge enumerates as
# cu.usbserial*, cu.wchusbserial* or cu.SLAB_* rather than cu.usbmodem*, and reporting
# "none" while one is attached sends the operator hunting for the wrong fault.
PORTS="$(ls -1 /dev/cu.* 2>/dev/null | grep -viE 'bluetooth|debug-console' \
    | grep -iE 'usbmodem|usbserial|wchusbserial|slab')"
if [ -n "$PORTS" ]; then say "$PORTS"
else say "    (none right now -- see docs/STATUS.md on USB behaviour)"; fi
say
say "next:  ./scripts/00-probe.sh"
