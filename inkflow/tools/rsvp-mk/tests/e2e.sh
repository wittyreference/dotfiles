#!/usr/bin/env bash
# ABOUTME: End-to-end test for the shipped rsvp-mk binary -- converts real files on
# ABOUTME: disk and checks the outputs, exit codes, and determinism of the CLI itself.

set -euo pipefail

RSVP_MK="${1:?usage: e2e.sh <path-to-rsvp-mk>}"
[ -x "$RSVP_MK" ] || { echo "FAIL: $RSVP_MK is not executable"; exit 1; }

WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT

pass=0
check() {
    if [ "$2" = "yes" ]; then
        echo "  ok   $1"
        pass=$((pass + 1))
    else
        echo "  FAIL $1"
        exit 1
    fi
}

# A document with markdown that must be stripped and prose that must survive.
cat > "$WORK/book.md" <<'DOC'
# Chapter One

The panel refreshes slowly. See [the notes](https://example.com/notes) for detail.

- a bullet point
- another one

```cpp
int main() { return 0; }
```

It was a caf&eacute;, not a laboratory. About 180 milliseconds, **maybe** less.
DOC

echo "e2e: rsvp-mk"

# --- conversion succeeds and produces a well-formed file ---------------------
# `set -e` has to come off to observe the exit status: left on, a non-zero status
# aborts the script before the check can run, so the check would always pass.
set +e
"$RSVP_MK" "$WORK/book.md" > "$WORK/stdout.txt" 2> "$WORK/stderr.txt"
convert_status=$?
set -e
check "exits 0 on a valid input" "$([ "$convert_status" -eq 0 ] && echo yes || echo no)"
check "derives the default .rsvp output path" "$([ -f "$WORK/book.rsvp" ] && echo yes || echo no)"

magic="$(head -c 4 "$WORK/book.rsvp")"
check "output starts with the RSVP magic" "$([ "$magic" = "RSVP" ] && echo yes || echo no)"

size="$(wc -c < "$WORK/book.rsvp" | tr -d ' ')"
check "output is larger than a bare header" "$([ "$size" -gt 32 ] && echo yes || echo no)"

check "reports a token count" \
    "$(grep -q 'tokens' "$WORK/stdout.txt" && echo yes || echo no)"
check "reports that markdown was stripped" \
    "$(grep -q 'markdown  stripped' "$WORK/stdout.txt" && echo yes || echo no)"
check "writes nothing to stderr on success" \
    "$([ ! -s "$WORK/stderr.txt" ] && echo yes || echo no)"

# --- markdown really was stripped -------------------------------------------
# The text blob is stored verbatim, so grep the output file directly: a URL or a
# line of source surviving into the token stream is the failure this catches.
check "link target is absent from the sidecar" \
    "$(grep -qa 'example.com' "$WORK/book.rsvp" && echo no || echo yes)"
check "code block content is absent from the sidecar" \
    "$(grep -qa 'int main' "$WORK/book.rsvp" && echo no || echo yes)"
check "link text survives in the sidecar" \
    "$(grep -qa 'the notes' "$WORK/book.rsvp" && echo yes || echo no)"

# --- determinism -------------------------------------------------------------
# Same input must give byte-identical output, or resume points and document ids
# stop being stable across a re-convert.
"$RSVP_MK" "$WORK/book.md" -o "$WORK/again.rsvp" > /dev/null
check "output is byte-identical on a second run" \
    "$(cmp -s "$WORK/book.rsvp" "$WORK/again.rsvp" && echo yes || echo no)"

# --- --raw changes the result for markdown input ----------------------------
"$RSVP_MK" "$WORK/book.md" --raw -o "$WORK/raw.rsvp" > /dev/null
check "--raw produces a different file than stripped" \
    "$(cmp -s "$WORK/book.rsvp" "$WORK/raw.rsvp" && echo no || echo yes)"
check "--raw keeps the link target" \
    "$(grep -qa 'example.com' "$WORK/raw.rsvp" && echo yes || echo no)"

# --- plain text needs no stripping ------------------------------------------
printf 'One two three. Four five six.\n' > "$WORK/plain.txt"
"$RSVP_MK" "$WORK/plain.txt" > "$WORK/plain.out"
check "plain text converts" "$([ -f "$WORK/plain.rsvp" ] && echo yes || echo no)"
check "plain text is not reported as stripped" \
    "$(grep -q 'markdown' "$WORK/plain.out" && echo no || echo yes)"

# --- failure modes -----------------------------------------------------------
set +e
"$RSVP_MK" "$WORK/does-not-exist.txt" > /dev/null 2> "$WORK/missing.txt"
missing_status=$?
"$RSVP_MK" > /dev/null 2>&1
noargs_status=$?
"$RSVP_MK" --nonsense "$WORK/plain.txt" > /dev/null 2>&1
badopt_status=$?
"$RSVP_MK" "$WORK/plain.txt" --wpm 0 > /dev/null 2>&1
badwpm_status=$?
"$RSVP_MK" --help > /dev/null 2>&1
help_status=$?
set -e

check "missing input fails" "$([ "$missing_status" -ne 0 ] && echo yes || echo no)"
check "missing input explains itself on stderr" \
    "$(grep -q 'cannot read' "$WORK/missing.txt" && echo yes || echo no)"
check "no arguments fails" "$([ "$noargs_status" -ne 0 ] && echo yes || echo no)"
check "unknown option fails" "$([ "$badopt_status" -ne 0 ] && echo yes || echo no)"
check "out-of-range --wpm fails" "$([ "$badwpm_status" -ne 0 ] && echo yes || echo no)"
check "--help exits 0" "$([ "$help_status" -eq 0 ] && echo yes || echo no)"

# --- refuses to overwrite its own input --------------------------------------
# `rsvp-mk book.rsvp` resolves its default output to book.rsvp, which is the input.
# Left unguarded the input is truncated before it is read back, destroying the only
# copy of the source the user had.
cp "$WORK/plain.txt" "$WORK/self.rsvp"
self_before="$(cksum < "$WORK/self.rsvp")"
set +e
"$RSVP_MK" "$WORK/self.rsvp" > /dev/null 2> "$WORK/self.err"
self_status=$?
"$RSVP_MK" "$WORK/plain.txt" -o "$WORK/./plain.txt" > /dev/null 2> "$WORK/alias.err"
alias_status=$?
set -e

check "refuses to write over its own input" "$([ "$self_status" -eq 2 ] && echo yes || echo no)"
check "leaves the input untouched when it refuses" \
    "$([ "$self_before" = "$(cksum < "$WORK/self.rsvp")" ] && echo yes || echo no)"
check "explains the refusal on stderr" \
    "$(grep -q 'would overwrite' "$WORK/self.err" && echo yes || echo no)"
check "recognises the input reached by a different spelling of its path" \
    "$([ "$alias_status" -eq 2 ] && echo yes || echo no)"

# --- an empty input is valid, not an error ----------------------------------
: > "$WORK/empty.txt"
"$RSVP_MK" "$WORK/empty.txt" > /dev/null
empty_size="$(wc -c < "$WORK/empty.rsvp" | tr -d ' ')"
check "empty input yields a bare 32-byte header" \
    "$([ "$empty_size" -eq 32 ] && echo yes || echo no)"

echo "e2e: $pass checks passed"
