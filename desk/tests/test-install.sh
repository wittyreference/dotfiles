#!/usr/bin/env bash
# ABOUTME: Tests desk/install.sh end to end against a scratch HOME with no tmux, mako or labwc on PATH.
# ABOUTME: Proves the dry run writes nothing, a real run installs everything, and a re-run changes nothing.
set -euo pipefail

HERE="$(cd "$(dirname "$0")" && pwd -P)"
INSTALL="$HERE/../install.sh"
fails=0
check() { if "$@"; then echo "ok   ${*:2}"; else echo "FAIL ${*:2}"; fails=$((fails + 1)); fi; }
assert_file() { [ -f "$1" ]; }
assert_grep() { grep -qF -- "$1" "$2"; }
assert_count() { [ "$(grep -cF -- "$1" "$2")" = "$3" ]; }

SCRATCH="$(mktemp -d)"
trap 'rm -rf "$SCRATCH"' EXIT
export HOME="$SCRATCH/home" XDG_CONFIG_HOME="$SCRATCH/home/.config"
export DESK_PRESENCE_FILE="$SCRATCH/run/at-test"
mkdir -p "$HOME/.config/labwc" "$HOME/.claude" "$SCRATCH/run" "$SCRATCH/bin"
# A PATH with only the basics stands in for a desk where the compositor tools are absent,
# so the installer's "apply live" branches are all skipped and must not fail the run.
export PATH="$SCRATCH/bin:/usr/bin:/bin"

# Pre-existing machine state the installer must preserve, not replace.
printf '# my bashrc\nexport FOO=1\n' > "$HOME/.bashrc"
cat > "$HOME/.claude/settings.json" <<'EOF'
{"model": "claude-fable-5-1[1m]", "permissions": {"allow": ["WebFetch"]}, "voiceEnabled": true,
 "statusLine": {"type": "command", "command": "x", "padding": 0}}
EOF
cat > "$HOME/.config/labwc/rc.xml" <<'EOF'
<?xml version="1.0"?>
<openbox_config xmlns="http://openbox.org/3.4/rc">
  <keyboard>
    <keybind key="F1">
      <action name="Execute">
        <command>wtype -k Tab</command>
      </action>
    </keybind>
  </keyboard>
</openbox_config>
EOF

echo "== dry run"
DRY_RUN=1 "$INSTALL" > "$SCRATCH/dry.out"
check assert_grep "DRY_RUN" "$SCRATCH/dry.out"
check test ! -f "$HOME/.tmux.conf"
check test ! -f "$HOME/.claude/themes/nomad.json"
check assert_grep "export FOO=1" "$HOME/.bashrc"
check assert_count "desk/bashrc-desk.sh" "$HOME/.bashrc" 0

echo "== first real run"
"$INSTALL" > "$SCRATCH/run1.out"
check test "$(grep -c . "$SCRATCH/run1.out")" -le 20
check assert_file "$HOME/.tmux.conf"
check assert_file "$HOME/.config/lxterminal/lxterminal.conf"
check assert_file "$HOME/.config/mako/config"
check assert_file "$HOME/.claude/themes/nomad.json"
check assert_file "$HOME/.config/autostart/claude-presence.desktop"
check assert_grep "Exec=touch $DESK_PRESENCE_FILE" "$HOME/.config/autostart/claude-presence.desktop"
check assert_file "$DESK_PRESENCE_FILE"
check assert_count 'key="C-A-t"' "$HOME/.config/labwc/rc.xml" 1
check assert_count 'key="C-A-n"' "$HOME/.config/labwc/rc.xml" 1
check assert_grep 'key="F1"' "$HOME/.config/labwc/rc.xml"
check python3 -c "import xml.dom.minidom,sys; xml.dom.minidom.parse(sys.argv[1])" "$HOME/.config/labwc/rc.xml"
check assert_file "$HOME/.config/labwc/rc.xml.bak-desk"
check assert_count "desk/bashrc-desk.sh" "$HOME/.bashrc" 1
check assert_grep "export FOO=1" "$HOME/.bashrc"

echo "== settings merge keeps the machine's own keys"
check python3 - "$HOME/.claude/settings.json" "$DESK_PRESENCE_FILE" <<'EOF'
import json, sys
d = json.load(open(sys.argv[1]))
assert d["model"] == "claude-fable-5-1[1m]", "model pin was lost"
assert d["permissions"] == {"allow": ["WebFetch"]}, "permissions were lost"
assert "voiceEnabled" not in d and d["voice"] == {"enabled": False}, "voice not disarmed"
assert d["prefersReducedMotion"] is True and d["spinnerTipsEnabled"] is False
assert d["env"]["CLAUDE_CODE_DISABLE_MOUSE"] == "1"
assert d["env"]["CLAUDE_CLIENT_PRESENCE_FILE"] == sys.argv[2]
assert d["theme"] == "custom:nomad" and d["tui"] == "fullscreen"
assert d["statusLine"]["refreshInterval"] == 60 and d["statusLine"]["command"] == "x"
EOF
check assert_file "$HOME/.claude/settings.json.bak-desk"

echo "== second run changes nothing"
find "$HOME" -type f ! -name '*.bak-desk' -exec md5sum {} + | sort > "$SCRATCH/sum1"
"$INSTALL" > "$SCRATCH/run2.out"
find "$HOME" -type f ! -name '*.bak-desk' -exec md5sum {} + | sort > "$SCRATCH/sum2"
check cmp -s "$SCRATCH/sum1" "$SCRATCH/sum2"
check assert_grep "0 changed" "$SCRATCH/run2.out"
check assert_count 'key="C-A-t"' "$HOME/.config/labwc/rc.xml" 1
check assert_count "desk/bashrc-desk.sh" "$HOME/.bashrc" 1

echo "== the sourced shell block is safe without wayland or mate"
check bash -c "source '$HERE/../bashrc-desk.sh' && [ \"\$EDITOR\" = nano ] && type screen-off >/dev/null"

echo "== a home with no labwc and no settings.json still installs"
export HOME="$SCRATCH/bare" XDG_CONFIG_HOME="$SCRATCH/bare/.config"
mkdir -p "$HOME"
touch "$HOME/.bashrc"
"$INSTALL" > "$SCRATCH/bare.out"
check assert_grep "keybinds skipped" "$SCRATCH/bare.out"
check python3 -c "import json,sys; d=json.load(open(sys.argv[1])); assert d['theme']=='custom:nomad'" "$HOME/.claude/settings.json"

if [ "$fails" -gt 0 ]; then echo "desk/tests: $fails FAILED"; exit 1; fi
echo "desk/tests: all passed"
