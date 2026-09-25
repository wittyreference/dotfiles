# desk — a keyboard-only Linux desk for long Claude Code sessions

The config for a box driven from a USB keyboard with no mouse, on Wayland (labwc), with
Claude Code living in a tmux session and results often read from a phone. It came out of
a study of 23 sessions and 28 days of transcripts on a Raspberry Pi 4: the felt costs were
recovering context and reviewing, not waiting, and the fixes were configuration, not new
software. Nothing here adds a daemon, a per-prompt hook, or a resident process.

## What it installs

| Piece | Lands at | Why |
|---|---|---|
| `tmux.conf` | `~/.tmux.conf` | Bell flags light a window's tab when Claude rings; true colour; passthrough and extended keys per the Claude Code docs; 20k history; a green status bar |
| `lxterminal.conf` | `~/.config/lxterminal/lxterminal.conf` | DejaVu Sans Mono 16, soft white on near-black, ANSI green kept for accents, no bell, menubar hidden |
| `mako.conf` | `~/.config/mako/config` | Notifications as a corner card for 8 s instead of a red flash over the transcript |
| labwc keybinds | inserted into `~/.config/labwc/rc.xml` | Ctrl+Alt+T opens a terminal (with no mouse, a dead terminal otherwise has no way back); Ctrl+Alt+N dismisses notifications |
| `bashrc-desk.sh` | sourced from `~/.bashrc` | `EDITOR=nano` where TextMate is absent (an EDITOR that does not exist silently kills Ctrl+G plan editing); `screen-off` / `screen-on` |
| `../claude/themes/nomad.json` | `~/.claude/themes/nomad.json` | Mint accent and input border, green success, a faintly green background behind your own messages |
| settings merge | `~/.claude/settings.json` | Reduced motion (tmux cannot do synchronized output, so every spinner frame is a repaint), mouse capture off, voice disarmed, fullscreen TUI, statusline every 60 s, presence file |
| `claude-presence.desktop` | `~/.config/autostart/` | Touches the presence marker at login so phone pushes stay quiet while you are at the desk |

## Install

```bash
gh repo clone wittyreference/dotfiles ~/dotfiles   # if not already there
DRY_RUN=1 ~/dotfiles/desk/install.sh                # see what would change
~/dotfiles/desk/install.sh                          # do it; safe to re-run
```

The last lines of the output say what still needs a hand: a fresh `lxterminal --no-remote`
window for the font, a new shell for EDITOR, and one Claude Code restart for the mouse
and theme settings. Files that differed get a one-time `.bak-desk` copy beside them.

## The presence marker

Claude Code skips phone pushes while `CLAUDE_CLIENT_PRESENCE_FILE` exists. Three things
keep it honest: the login autostart creates it, `screen-off` removes it, and an idle
blank should remove it too. That idle half is not here because it belongs to whoever owns
the display: on the Nomad it is `deploy/screen-blank.desktop` in the elmer repo, whose
swayidle command removes the marker on timeout and restores it on resume. The default
path is `/run/user/<uid>/at-nomad`; set `DESK_PRESENCE_FILE` before installing to change
it, and change the idle unit to match.

## Machine-specific, on purpose

- The Wayland socket is assumed to be `wayland-0` and the runtime dir `/run/user/<uid>`.
  Both can be overridden in the environment before calling `screen-off`.
- The keybinds are labwc's `rc.xml` syntax. Another compositor gets the deferred note in
  the digest and nothing else.
- lxterminal is the terminal because it is what Raspberry Pi OS ships and it is CPU
  rendered. foot is the documented alternative if Shift+Enter or a real terminal bell
  matter; kitty, alacritty and wezterm want a GPU the Pi 4 does not have spare.
- Not included, by decision: fzf, zellij, starship, Nerd Fonts, Notification hooks and
  `verbose: false`. Each is a process, a spawn per prompt, or a font download whose only
  consumer is a separator glyph.

## Test

```bash
~/dotfiles/desk/tests/test-install.sh
```

Runs the installer against a scratch HOME with no tmux, mako or labwc on PATH. Proves the
dry run writes nothing, the real run installs and merges without clobbering the model pin
or permissions, the labwc file still parses with exactly one copy of each keybind, and a
second run changes no file.
