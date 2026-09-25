#!/usr/bin/env bash
# ABOUTME: Installs the keyboard-only desk config: tmux, lxterminal, mako, labwc keys, Claude Code theme and settings.
# ABOUTME: Idempotent and phone-operable. Re-run freely, DRY_RUN=1 previews, and every failure names its file.

# The desk is a Linux box driven from a USB keyboard with no mouse, on Wayland (labwc),
# with Claude Code living in a tmux session. Everything here was verified on a Raspberry
# Pi 4 running Raspberry Pi OS Bookworm; see README.md for what is machine-specific.
set -euo pipefail

HERE="$(cd "$(dirname "$0")" && pwd -P)"
CFG="${XDG_CONFIG_HOME:-$HOME/.config}"
CLAUDE_DIR="${CLAUDE_CONFIG_DIR:-$HOME/.claude}"
# Claude Code mutes phone pushes while this file exists. The name is shared with the
# swayidle unit and the shell helpers, so override all three together or none.
PRESENCE="${DESK_PRESENCE_FILE:-/run/user/$(id -u)/at-nomad}"
DRY="${DRY_RUN:-}"

changed=(); kept=(); deferred=()
die() { echo "desk/install: $*" >&2; exit 1; }

[ -d "$HERE" ] || die "cannot find my own directory"
for f in tmux.conf lxterminal.conf mako.conf bashrc-desk.sh "../claude/themes/nomad.json"; do
    [ -f "$HERE/$f" ] || die "missing $HERE/$f, the checkout is incomplete"
done
command -v python3 >/dev/null || die "python3 is required for the JSON and XML merges"

# put <src> <dest>: copy when different. The first differing install keeps a one-time
# backup of whatever was there, so the pre-desk file is never lost to a re-run.
put() {
    local src="$1" dest="$2"
    if [ -f "$dest" ] && cmp -s "$src" "$dest"; then kept+=("$dest"); return; fi
    if [ -n "$DRY" ]; then changed+=("$dest (would write)"); return; fi
    mkdir -p "$(dirname "$dest")"
    [ -f "$dest" ] && cp -n "$dest" "$dest.bak-desk"
    cp "$src" "$dest"
    changed+=("$dest")
}

# ---- tmux ---------------------------------------------------------------------------
put "$HERE/tmux.conf" "$HOME/.tmux.conf"
if [ -z "$DRY" ] && command -v tmux >/dev/null && tmux ls >/dev/null 2>&1; then
    tmux source-file "$HOME/.tmux.conf" && deferred+=("tmux: reloaded; true colour applies to clients attached from now on")
fi

# ---- lxterminal ---------------------------------------------------------------------
# lxterminal reads its config once per process, so a running window keeps the old font
# and colours. A window opened with `lxterminal --no-remote` starts a fresh process.
put "$HERE/lxterminal.conf" "$CFG/lxterminal/lxterminal.conf"
deferred+=("lxterminal: open a new window with 'lxterminal --no-remote' to see the font and colours")

# ---- mako ---------------------------------------------------------------------------
put "$HERE/mako.conf" "$CFG/mako/config"
if [ -z "$DRY" ] && command -v makoctl >/dev/null; then
    makoctl reload >/dev/null 2>&1 && deferred+=("mako: reloaded") || deferred+=("mako: not running, config applies at next login")
fi

# ---- labwc keybinds -----------------------------------------------------------------
# Ctrl+Alt+T opens a terminal and Ctrl+Alt+N clears notifications. On a mouse-less desk
# the first is a recovery path: without it a dead terminal has no way back. The two
# binds are inserted once, before </keyboard>, and the file is checked to still parse.
RC="$CFG/labwc/rc.xml"
if [ -f "$RC" ]; then
    if grep -q 'key="C-A-t"' "$RC" && grep -q 'key="C-A-n"' "$RC"; then
        kept+=("$RC (keybinds present)")
    elif [ -n "$DRY" ]; then
        changed+=("$RC (would add C-A-t and C-A-n keybinds)")
    else
        cp -n "$RC" "$RC.bak-desk"
        python3 - "$RC" <<'EOF'
import sys, xml.dom.minidom
path = sys.argv[1]
text = open(path).read()
binds = ""
if 'key="C-A-t"' not in text:
    binds += '    <keybind key="C-A-t">\n      <action name="Execute" command="lxterminal"/>\n    </keybind>\n'
if 'key="C-A-n"' not in text:
    binds += '    <keybind key="C-A-n">\n      <action name="Execute" command="makoctl dismiss -a"/>\n    </keybind>\n'
if "</keyboard>" not in text:
    sys.exit("rc.xml has no <keyboard> section; add the keybinds by hand")
text = text.replace("</keyboard>", binds + "  </keyboard>", 1)
xml.dom.minidom.parseString(text)
open(path, "w").write(text)
EOF
        changed+=("$RC (C-A-t, C-A-n)")
        if command -v labwc >/dev/null && pgrep -x labwc >/dev/null; then
            labwc -r && deferred+=("labwc: reconfigured, the keybinds work now")
        fi
    fi
else
    deferred+=("labwc: no $RC here, keybinds skipped")
fi

# ---- shell --------------------------------------------------------------------------
LINE='[ -f ~/dotfiles/desk/bashrc-desk.sh ] && source ~/dotfiles/desk/bashrc-desk.sh'
if grep -qF 'desk/bashrc-desk.sh' "$HOME/.bashrc" 2>/dev/null; then
    kept+=("$HOME/.bashrc (sources bashrc-desk.sh)")
elif [ -n "$DRY" ]; then
    changed+=("$HOME/.bashrc (would add the source line)")
else
    printf '\n# keyboard-only desk: EDITOR, screen-off/on and the Claude presence marker\n%s\n' "$LINE" >> "$HOME/.bashrc"
    changed+=("$HOME/.bashrc (sources bashrc-desk.sh)")
fi

# ---- Claude Code --------------------------------------------------------------------
put "$HERE/../claude/themes/nomad.json" "$CLAUDE_DIR/themes/nomad.json"

# Settings are merged, never replaced: the model pin, permissions and plugin state on a
# machine are its own. Only the desk keys are set, and the deprecated voiceEnabled is
# removed because Claude Code still honours it and would arm push-to-talk on a box
# with no microphone.
SETTINGS="$CLAUDE_DIR/settings.json"
if [ -z "$DRY" ]; then
    mkdir -p "$CLAUDE_DIR"
    [ -f "$SETTINGS" ] && cp -n "$SETTINGS" "$SETTINGS.bak-desk"
fi
# The merge runs in both modes and only writes when not DRY, so the preview reports the
# same verdict the real run would.
merged="$(python3 - "$SETTINGS" "$PRESENCE" "$DRY" <<'EOF'
import json, os, sys
path, presence, dry = sys.argv[1], sys.argv[2], sys.argv[3]
d = json.load(open(path)) if os.path.exists(path) else {}
before = json.dumps(d, sort_keys=True)
d["prefersReducedMotion"] = True
d["spinnerTipsEnabled"] = False
d["tui"] = "fullscreen"
d["theme"] = "custom:nomad"
d["preferredNotifChannel"] = "terminal_bell"
env = d.setdefault("env", {})
env["CLAUDE_CODE_DISABLE_MOUSE"] = "1"
env["CLAUDE_CLIENT_PRESENCE_FILE"] = presence
d.pop("voiceEnabled", None)
d["voice"] = {"enabled": False}
if isinstance(d.get("statusLine"), dict):
    d["statusLine"]["refreshInterval"] = 60
if json.dumps(d, sort_keys=True) != before:
    if not dry:
        with open(path, "w") as f:
            json.dump(d, f, indent=2)
            f.write("\n")
    print("changed")
else:
    print("kept")
EOF
)"
if [ "$merged" = "changed" ]; then
    changed+=("$SETTINGS (desk keys${DRY:+, would merge})")
else
    kept+=("$SETTINGS")
fi

# ---- presence marker at login ---------------------------------------------------------
AUTOSTART="$CFG/autostart/claude-presence.desktop"
tmp="$(mktemp)"
printf '[Desktop Entry]\nType=Application\nName=Claude presence marker\nComment=At the desk after login, so Claude Code phone pushes stay quiet until the idle blank or screen-off clears it\nExec=touch %s\n' "$PRESENCE" > "$tmp"
put "$tmp" "$AUTOSTART"
rm -f "$tmp"
if [ -z "$DRY" ] && [ -d "$(dirname "$PRESENCE")" ]; then
    touch "$PRESENCE" && deferred+=("presence: marker set at $PRESENCE (you are at the desk)")
fi

# ---- digest ---------------------------------------------------------------------------
# Short enough to paste from a phone; the detail is the files themselves.
[ -n "$DRY" ] && echo "desk/install: DRY_RUN, nothing written"
echo "desk/install: ${#changed[@]} changed, ${#kept[@]} already current"
for c in "${changed[@]}"; do echo "  + $c"; done
for d in "${deferred[@]}"; do echo "  ! $d"; done
echo "  ! shell: open a new shell (or 'source ~/.bashrc') for EDITOR and screen-off/on"
echo "  ! Claude Code: restart it once for the mouse and theme settings; then /theme shows Nomad"
