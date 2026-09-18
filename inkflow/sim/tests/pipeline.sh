#!/usr/bin/env bash
# ABOUTME: End-to-end gate -- converts a real document with rsvp-mk, then plays the
# ABOUTME: sidecar through the reading loop and asserts the layout and ghosting hold up.

set -uo pipefail

RSVP_MK="${1:?usage: pipeline.sh <rsvp_mk> <sim> <source-document>}"
SIM="${2:?missing sim binary}"
SOURCE="${3:?missing source document}"

WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT

FAILURES=0
check() {
    local what="$1" got="$2" want="$3"
    if [ "$got" = "$want" ]; then
        printf '  ok    %s\n' "$what"
    else
        printf '  FAIL  %s (got %s, wanted %s)\n' "$what" "$got" "$want"
        FAILURES=$((FAILURES + 1))
    fi
}

echo "pipeline: $(basename "$SOURCE") -> .rsvp -> reading loop"

# The reader consumes a sidecar, not raw Markdown. Converting here rather than pointing
# the simulator at a .md is the difference between testing the product and testing a
# fixture: raw Markdown contains link targets that are single 60-character "words", wider
# than the panel, which the chunker correctly presents alone and which correctly overflow.
"$RSVP_MK" "$SOURCE" -o "$WORK/book.rsvp" > "$WORK/mk.log" 2>&1
check "rsvp-mk converts the document" "$?" "0"

if [ ! -s "$WORK/book.rsvp" ]; then
    echo "  FAIL  sidecar is empty or missing"
    exit 1
fi

# The shipped layout must fit. This is the regression that moved the reader to landscape.
"$SIM" "$WORK/book.rsvp" "$WORK" 0 --no-png > "$WORK/landscape.log" 2>&1
check "landscape layout fits the whole document" "$?" "0"

# And the rejected one must still not fit. Without this the landscape pass proves only
# that the check is lenient, not that it works.
"$SIM" "$WORK/book.rsvp" "$WORK" 0 --portrait --no-png > "$WORK/portrait.log" 2>&1
check "portrait layout still overflows" "$?" "1"

# Ghosting must actually clear. The flush needs both a partial count and a paragraph
# boundary, so a document with long paragraph-free stretches can starve it indefinitely.
WORST="$(grep -o 'worst ghosting [0-9]*' "$WORK/landscape.log" | grep -o '[0-9]*')"
if [ -z "$WORST" ]; then
    echo "  FAIL  simulator did not report ghosting"
    FAILURES=$((FAILURES + 1))
elif [ "$WORST" -gt 200 ]; then
    echo "  FAIL  ghosting reached $WORST partial updates without a full refresh"
    FAILURES=$((FAILURES + 1))
else
    printf '  ok    ghosting cleared, worst was %s partial updates deep\n' "$WORST"
fi

# Speed control has to move the pace. A reader that ignores its own buttons is worse than
# one without them, because it looks like it is responding.
slow_wpm="$("$SIM" "$WORK/book.rsvp" "$WORK" 300 --wpm 200 --no-png 2>/dev/null \
    | grep -o 'delivered [0-9]*' | grep -o '[0-9]*')"
fast_wpm="$("$SIM" "$WORK/book.rsvp" "$WORK" 300 --wpm 500 --no-png 2>/dev/null \
    | grep -o 'delivered [0-9]*' | grep -o '[0-9]*')"
if [ -n "$slow_wpm" ] && [ -n "$fast_wpm" ] && [ "$slow_wpm" -lt "$fast_wpm" ]; then
    printf '  ok    speed control moves the pace (%s wpm vs %s wpm)\n' "$slow_wpm" "$fast_wpm"
else
    printf '  FAIL  speed control inert (slow=%s fast=%s)\n' "${slow_wpm:-?}" "${fast_wpm:-?}"
    FAILURES=$((FAILURES + 1))
fi

if [ "$FAILURES" -ne 0 ]; then
    echo "pipeline: $FAILURES failed"
    exit 1
fi
echo "pipeline: all checks passed"
