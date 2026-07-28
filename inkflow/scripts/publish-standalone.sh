#!/usr/bin/env bash
# ABOUTME: Extracts inkflow/ into a standalone history and pushes it to its own repo.
# ABOUTME: Re-runnable -- syncs later work without rewriting what is already published.

set -uo pipefail

# inkflow currently lives as a subdirectory of the dotfiles repo, because that is the
# repo the authoring environment can write to. This script republishes it as a
# standalone repository with inkflow/ contents at the root, preserving commit history.
#
# `git subtree split` is deterministic for unchanged history, so re-running after more
# work produces the same commits plus the new ones -- the push fast-forwards rather
# than rewriting. Safe to run repeatedly.
#
# Usage:  ./scripts/publish-standalone.sh
#         TARGET=https://github.com/you/other.git ./scripts/publish-standalone.sh

TARGET="${TARGET:-https://github.com/wittyreference/inkflow.git}"
PREFIX="inkflow"
TMP_BRANCH="inkflow-standalone-$$"

REPO_ROOT="$(git rev-parse --show-toplevel 2>/dev/null)" || {
    echo "Not a git checkout. Run this from inside the dotfiles clone."
    exit 1
}
cd "$REPO_ROOT" || exit 1

[ -d "$PREFIX" ] || { echo "No $PREFIX/ directory here -- wrong repo?"; exit 1; }

if [ -n "$(git status --porcelain)" ]; then
    echo "Working tree is dirty. Commit or stash first -- subtree split needs a clean tree."
    git status --short | sed 's/^/  /'
    exit 1
fi

cleanup() { git branch -D "$TMP_BRANCH" >/dev/null 2>&1 || true; }
trap cleanup EXIT

echo "Extracting $PREFIX/ into a standalone history..."
git subtree split --prefix="$PREFIX" -b "$TMP_BRANCH" >/dev/null || {
    echo "subtree split failed."
    exit 1
}

COMMITS="$(git rev-list --count "$TMP_BRANCH")"
echo "  $COMMITS commits, $PREFIX/ contents at the root"
echo
echo "Pushing to $TARGET (branch: main)"
echo

if git push "$TARGET" "$TMP_BRANCH:main"; then
    echo
    echo "Published. $TARGET now carries $COMMITS commits."
    exit 0
fi

cat <<MSG

PUSH FAILED. The likely causes, in order:

  1. The repository does not exist yet. Create an EMPTY one (no README, no
     .gitignore, no license) at https://github.com/new named "inkflow", then
     re-run this script.

  2. The target was created with a README, so the histories are unrelated.
     Re-run with:
         git push --force $TARGET $TMP_BRANCH:main
     That discards only GitHub's auto-generated initial commit.

  3. This machine has no push credentials for the repository. A read-only
     deploy key produces "the key you are authenticating with has been marked
     as read only" -- deploy keys are also single-repo, so one issued for
     another repo can never work here. Use a token over HTTPS, or run
     'gh auth login' and try again.

Nothing was changed locally.
MSG
exit 1
