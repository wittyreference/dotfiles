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
say "add these to your shell, or call them by full path:"
say "    export ESPTOOL=$TOOLS/esp/bin/esptool"
say "    export ESPEFUSE=$TOOLS/esp/bin/espefuse"
say "    export PIO=$TOOLS/pio/bin/pio"
say
say "the serial port, when the device is attached and awake:"
ls -1 /dev/cu.usbmodem* 2>/dev/null || say "    (none right now -- see docs/STATUS.md on USB behaviour)"
