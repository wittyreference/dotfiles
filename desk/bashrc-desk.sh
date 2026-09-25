# ABOUTME: Shell additions for the keyboard-only desk: a real EDITOR, screen-off/on, and the Claude presence marker.
# ABOUTME: Sourced from ~/.bashrc by desk/install.sh; safe to source on a machine without Wayland.

# ~/dotfiles/shell/functions.sh sets EDITOR to TextMate for the Mac. On the desk there is
# no mate, and an EDITOR that does not exist silently disables Claude Code's Ctrl+G plan
# editing, transcript export, and any interactive git commit. nano is on every Pi image.
if ! command -v mate >/dev/null 2>&1 && command -v nano >/dev/null 2>&1; then
    export EDITOR=nano VISUAL=nano
fi

# Claude Code skips phone pushes while this file exists. The idle blank removes it and
# resume restores it; these helpers do the same so leaving early starts pushes at once.
export DESK_PRESENCE_FILE="${DESK_PRESENCE_FILE:-/run/user/$(id -u)/at-nomad}"

# Turn the desk's displays off/on from any shell, SSH included. The Wayland socket is
# named explicitly because an SSH shell has neither variable set.
screen-off() {
    XDG_RUNTIME_DIR="${XDG_RUNTIME_DIR:-/run/user/$(id -u)}" WAYLAND_DISPLAY="${WAYLAND_DISPLAY:-wayland-0}" wlopm --off '*'
    rm -f "$DESK_PRESENCE_FILE"
}
screen-on() {
    XDG_RUNTIME_DIR="${XDG_RUNTIME_DIR:-/run/user/$(id -u)}" WAYLAND_DISPLAY="${WAYLAND_DISPLAY:-wayland-0}" wlopm --on '*'
    touch "$DESK_PRESENCE_FILE"
}
