# ABOUTME: Shell functions for tmux session layouts, TextMate viewer bridge, and Claude Code launchers.
# ABOUTME: Source from ~/.zshrc with: [ -f ~/dotfiles/shell/functions.sh ] && source ~/dotfiles/shell/functions.sh

# ── iTerm2 tab helpers ───────────────────────────────────────────────
# Set the iTerm2 tab background color (RGB 0-255) and title.
tab-color() { echo -ne "\033]6;1;bg;red;brightness;$1\a\033]6;1;bg;green;brightness;$2\a\033]6;1;bg;blue;brightness;$3\a"; }
tab-title() { echo -ne "\033]0;$1\007"; }

# ── tmux session layouts ─────────────────────────────────────────────
# Each function attaches to an existing session if present, otherwise
# creates a fresh one with the labeled pane geometry. Tab title + color
# make it easy to spot which layout is active in iTerm2.

# factory: 1 tall left pane + 2 stacked right panes (deep royal purple)
factory() {
  tab-title "☭ factory"
  tab-color 88 28 135
  tmux attach -t factory 2>/dev/null && return
  tmux new-session -d -s factory
  tmux split-window -h -t factory
  tmux split-window -v -t factory:0.1
  tmux select-pane -t factory:0.0
  tmux attach -t factory
}

# split: 2 side-by-side vertical panes (hot magenta)
split() {
  tab-title "🪓 split"
  tab-color 217 70 239
  tmux attach -t split 2>/dev/null && return
  tmux new-session -d -s split
  tmux split-window -h -t split
  tmux set -t split pane-border-style 'fg=colour236'
  tmux set -t split pane-active-border-style 'fg=colour201,bold'
  tmux set -t split pane-border-lines heavy
  tmux set -t split window-style 'bg=colour234,fg=colour245'
  tmux set -t split window-active-style 'bg=terminal'
  tmux select-pane -t split:0.0
  tmux attach -t split
}

# splitdeux: same as split, second instance for parallel work (medium orchid)
splitdeux() {
  tab-title "✂ ️ splitdeux"
  tab-color 168 85 247
  tmux attach -t splitdeux 2>/dev/null && return
  tmux new-session -d -s splitdeux
  tmux split-window -h -t splitdeux
  tmux set -t splitdeux pane-border-style 'fg=colour236'
  tmux set -t splitdeux pane-active-border-style 'fg=colour201,bold'
  tmux set -t splitdeux pane-border-lines heavy
  tmux set -t splitdeux window-style 'bg=colour234,fg=colour245'
  tmux set -t splitdeux window-active-style 'bg=terminal'
  tmux select-pane -t splitdeux:0.0
  tmux attach -t splitdeux
}

# parallel: 4 horizontal panes stacked vertically — best on a portrait monitor
parallel() {
  tmux attach -t parallel 2>/dev/null && return
  tmux new-session -d -s parallel
  tmux split-window -v -t parallel
  tmux split-window -v -t parallel:0.0
  tmux split-window -v -t parallel:0.2
  tmux select-layout -t parallel even-vertical
  tmux set -t parallel pane-border-style 'fg=colour236'
  tmux set -t parallel pane-active-border-style 'fg=colour201,bold'
  tmux set -t parallel pane-border-lines heavy
  tmux set -t parallel window-style 'bg=colour234,fg=colour245'
  tmux set -t parallel window-active-style 'bg=terminal'
  tmux attach -t parallel
}

# cctv: 2x2 checkerboard grid (electric violet)
cctv() {
  tab-title "📺 cctv"
  tab-color 139 92 246
  tmux attach -t cctv 2>/dev/null && return
  tmux new-session -d -s cctv
  tmux split-window -h -t cctv
  tmux split-window -v -t cctv:0.1
  tmux select-pane -t cctv:0.0
  tmux split-window -v -t cctv
  tmux set -t cctv pane-border-style 'fg=colour236'
  tmux set -t cctv pane-active-border-style 'fg=colour201,bold'
  tmux set -t cctv pane-border-lines heavy
  tmux set -t cctv window-style 'bg=colour234,fg=colour245'
  tmux set -t cctv window-active-style 'bg=terminal'
  tmux select-pane -t cctv:0.0
  tmux attach -t cctv
}

# cctv2: same as cctv, second instance (soft mauve)
cctv2() {
  tab-title "🔭 cctv2"
  tab-color 192 132 252
  tmux attach -t cctv2 2>/dev/null && return
  tmux new-session -d -s cctv2
  tmux split-window -h -t cctv2
  tmux split-window -v -t cctv2:0.1
  tmux select-pane -t cctv2:0.0
  tmux split-window -v -t cctv2
  tmux set -t cctv2 pane-border-style 'fg=colour236'
  tmux set -t cctv2 pane-active-border-style 'fg=colour201,bold'
  tmux set -t cctv2 pane-border-lines heavy
  tmux set -t cctv2 window-style 'bg=colour234,fg=colour245'
  tmux set -t cctv2 window-active-style 'bg=terminal'
  tmux select-pane -t cctv2:0.0
  tmux attach -t cctv2
}

# ── TextMate viewer bridge ───────────────────────────────────────────
# Use mate from any tmux pane. EDITOR set so git/CLI tools open in mate.
export EDITOR='mate -w'
alias m='mate'

# mview: open file with a custom tab name and no recent-files clutter.
# Usage: mview path/to/file.log "session log"
mview() {
  mate --no-recent --name "${2:-$(basename "$1")}" "$1"
}

# mpipe: pipe stdin to TextMate with a display name and syntax detection.
# Usage: git diff | mpipe "my diff" diff
#        cat output.json | mpipe "API response" json
mpipe() {
  local name="${1:-stdin}"
  local type="${2:-}"
  local flags=(--no-recent --name "$name")
  if [[ -n "$type" ]]; then
    case "$type" in
      js)   flags+=(-t source.js) ;;
      json) flags+=(-t source.json) ;;
      md)   flags+=(-t text.html.markdown) ;;
      yaml|yml) flags+=(-t source.yaml) ;;
      diff) flags+=(-t source.diff) ;;
      sh)   flags+=(-t source.shell) ;;
      html) flags+=(-t text.html) ;;
      py)   flags+=(-t source.python) ;;
      ts)   flags+=(-t source.ts) ;;
      *)    flags+=(-t "$type") ;;
    esac
  fi
  mate "${flags[@]}"
}

# mdiff: pipe git diff into TextMate with diff syntax highlighting.
# Usage: mdiff               (full diff)
#        mdiff HEAD~3        (since N commits ago)
#        mdiff origin/main   (vs upstream)
mdiff() {
  git diff "$@" | mate -t source.diff --name "diff${1:+ $1}" --no-recent
}

# ── Claude Code deep-work launcher ───────────────────────────────────
# claude-deepwork: launch Claude Code with the deep-work environment
# variables that override settings.json defaults. See docs/claude-config-rationale.md
# for why each value is set.
#
# Provider-agnostic: works with both AWS Bedrock and the Anthropic API.
# Set CLAUDE_PROVIDER to "bedrock" or "anthropic" (default: anthropic).
claude-deepwork() {
  (
    export CLAUDE_CODE_EFFORT_LEVEL=max

    if [[ "${CLAUDE_PROVIDER:-anthropic}" == "bedrock" ]]; then
      unset ANTHROPIC_API_KEY
      export CLAUDE_CODE_USE_BEDROCK=1
      export ANTHROPIC_DEFAULT_OPUS_MODEL='us.anthropic.claude-opus-4-7'
      aws sts get-caller-identity >/dev/null 2>&1 || aws sso login
    else
      export ANTHROPIC_DEFAULT_OPUS_MODEL='claude-opus-4-7'
    fi

    command claude "$@"
  )
}
