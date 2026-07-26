#!/usr/bin/env bash
# ABOUTME: Commits everything in hardware-notes/ and pushes it to the shared branch,
# ABOUTME: so device results reach the repo without anyone copying text by hand.

set -uo pipefail

# The operator has no agent session locally, so results have to travel via the repo
# rather than via a chat window. This is the return leg: run a probe, push what it
# found, and it is readable from anywhere.
#
# Never pushes to main. Never commits the flash image -- 16 MB of device-specific
# firmware is gitignored on purpose.

cd "$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)" || exit 1
REPO_ROOT="$(git rev-parse --show-toplevel 2>/dev/null)" || {
    echo "Not a git checkout -- nothing to push."
    echo "Re-run the scripts from a clone rather than a downloaded copy."
    exit 1
}
cd "$REPO_ROOT" || exit 1

BRANCH="$(git rev-parse --abbrev-ref HEAD)"
if [ "$BRANCH" = "main" ] || [ "$BRANCH" = "master" ]; then
    echo "Refusing to commit results onto $BRANCH."
    echo "Check out the working branch first:"
    echo "    git checkout claude/xteink-x4-rsvp-reader-212m18"
    exit 1
fi

NOTES="inkflow/hardware-notes"
if [ -z "$(git status --porcelain -- "$NOTES" 2>/dev/null)" ]; then
    echo "No new results in $NOTES -- nothing to push."
    exit 0
fi

echo "Results to publish:"
git status --porcelain -- "$NOTES" | sed 's/^/  /'
echo

# Identity: only set it if the machine has none, and keep it local to this clone so
# nothing leaks into the operator's global git config.
git config user.email >/dev/null 2>&1 || git config user.email "inkflow-operator@localhost"
git config user.name  >/dev/null 2>&1 || git config user.name  "inkflow operator"

git add -- "$NOTES" || exit 1
git commit -q -m "Add hardware probe results from $(date -u +%Y-%m-%dT%H:%MZ)

Device output captured by inkflow/scripts on the machine with the X4
attached. The flash image itself is gitignored; only reports and
manifests are committed." || {
    echo "Nothing committed."
    exit 0
}

echo "Committed. Syncing with the remote..."

# Someone else may have pushed to this branch in the meantime; rebase rather than
# fail, since these are additive report files and will not conflict.
git pull --rebase --quiet origin "$BRANCH" 2>/dev/null || true

for attempt in 1 2 3; do
    if git push --quiet origin "$BRANCH"; then
        echo
        echo "Pushed to origin/$BRANCH."
        echo "Results are now readable from the repo -- no copying needed."
        exit 0
    fi
    echo "Push attempt $attempt failed; retrying..."
    sleep $(( attempt * 3 ))
    git pull --rebase --quiet origin "$BRANCH" 2>/dev/null || true
done

cat <<MSG

PUSH FAILED after three attempts.

The results are committed locally but did not reach the remote. Most likely this
machine has no push credentials for the repo. Either:

  * authenticate and re-run this script:   gh auth login   (then re-run)
  * or fall back to pasting the digest the probe printed.

Nothing is lost either way -- the reports are in $NOTES.
MSG
exit 1
