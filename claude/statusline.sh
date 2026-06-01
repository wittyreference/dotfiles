#!/bin/bash
# ABOUTME: ccstatusline wrapper that warns when CWD is outside a canonical workspace.
# ABOUTME: Set CANONICAL_REPO env var (or edit below) to point at your primary work tree.

INPUT=$(cat)
CWD=$(echo "$INPUT" | python3 -c "import sys,json; print(json.load(sys.stdin).get('cwd',''))" 2>/dev/null)

# Resolve to real path so symlinks/worktrees compare correctly.
REAL_CWD="$(cd "$CWD" 2>/dev/null && pwd -P || echo "$CWD")"
CANONICAL="${CANONICAL_REPO:-$HOME/workspaces/internal-factory}"

# Treat the canonical repo and any git worktree under it as "in canonical".
IN_CANONICAL=false
if [ -d "$CANONICAL" ]; then
    REAL_CANONICAL="$(cd "$CANONICAL" && pwd -P 2>/dev/null)"
    case "$REAL_CWD" in
        "$REAL_CANONICAL"*) IN_CANONICAL=true ;;
    esac
fi

# Prepend a yellow warning line when working outside the canonical repo.
if [ "$IN_CANONICAL" = "false" ] && [ -n "$CWD" ]; then
    SHORT_CWD=$(echo "$CWD" | sed "s|$HOME|~|")
    echo -e "\033[33m⚠ $SHORT_CWD\033[0m"
fi

# Prefer global ccstatusline; fall back to a known fnm install; last resort npx.
if command -v ccstatusline >/dev/null 2>&1; then
    echo "$INPUT" | ccstatusline 2>/dev/null
elif [ -x "$HOME/.local/share/fnm/node-versions/v22.22.2/installation/bin/ccstatusline" ]; then
    echo "$INPUT" | "$HOME/.local/share/fnm/node-versions/v22.22.2/installation/bin/ccstatusline" 2>/dev/null
else
    echo "$INPUT" | npx -y ccstatusline@latest 2>/dev/null
fi
