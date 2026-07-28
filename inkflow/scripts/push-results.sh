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

RESULTS_BRANCH="${RESULTS_BRANCH:-hw-results}"
BRANCH="$(git rev-parse --abbrev-ref HEAD)"

# Device results never land directly on the default branch -- they go to a results
# branch that can be reviewed and merged like anything else. Switching automatically
# rather than refusing, because the operator should not have to think about branch
# hygiene while standing at a bench with hardware plugged in.
if [ "$BRANCH" = "main" ] || [ "$BRANCH" = "master" ]; then
    echo "On $BRANCH -- moving results to the '$RESULTS_BRANCH' branch."
    if git show-ref --verify --quiet "refs/heads/$RESULTS_BRANCH"; then
        git checkout -q "$RESULTS_BRANCH" || exit 1
    elif git ls-remote --exit-code --heads origin "$RESULTS_BRANCH" >/dev/null 2>&1; then
        git fetch -q origin "$RESULTS_BRANCH" || true
        git checkout -q -b "$RESULTS_BRANCH" "origin/$RESULTS_BRANCH" || exit 1
    else
        git checkout -q -b "$RESULTS_BRANCH" || exit 1
    fi
    BRANCH="$RESULTS_BRANCH"
    echo
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
    # -u sets upstream tracking. Without it a freshly created results branch has no
    # remote configured, and the operator's next plain `git pull` fails with "no
    # tracking information" -- on a branch they never chose, because this script put
    # them there.
    if git push --quiet -u origin "$BRANCH"; then
        echo
        echo "Pushed to origin/$BRANCH."
        echo "Results are now readable from the repo -- no copying needed."
        echo
        echo "You are now on '$BRANCH'. Back to the default branch with:"
        echo "    git checkout main && git pull"
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
