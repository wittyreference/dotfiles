# Claude Code Configuration Rationale

Why each non-default setting in `claude/settings.example.json` and the `claude-deepwork` shell function is set the way it is. Update this doc when defaults shift or a new knob earns its keep.

## Model selection

### `"model": "claude-opus-4-8[1m]"`

Opus 4.8 is the latest and most capable Claude model as of mid-2026. The `[1m]` suffix opts into the 1M-token context window — needed for long sessions where the conversation history, semantic memory recall, skill index, and tool results all share the same budget. Without it, sessions that pull in even moderate codebase context start hitting the 200K cap by mid-afternoon.

### `"env": { "ANTHROPIC_DEFAULT_OPUS_MODEL": "claude-opus-4-8" }`

Pins the Opus variant at the env-var level so any tool that resolves "opus" generically (some plugins, the Agent SDK) lands on 4.8 instead of an older snapshot. The Bedrock equivalent uses the regional prefix `us.anthropic.claude-opus-4-8`; switch via `CLAUDE_PROVIDER=bedrock claude-deepwork`.

## Thinking and effort

### `"alwaysThinkingEnabled": true`

Forces extended thinking on every turn, not just when the user types "ultrathink". For deep-work sessions this is the single highest-leverage knob — the model considers more options before acting, which roughly halves the rate of "had to re-do that" cycles. Costs more tokens per turn, but cheaper than a redo.

### `"showThinkingSummaries": true`

Surfaces the model's reasoning trace in the UI. Useful for catching when the model is about to do something dumb *before* it commits to it. If you don't read the summaries, turn this off — you're paying the visual noise tax without the benefit.

### `"effortLevel": "xhigh"`

Maximum reasoning depth tier. Valid values: `low`, `medium`, `high`, `xhigh`. Pairs with `alwaysThinkingEnabled` — the latter says *whether* to think, this says *how much*. The `claude-deepwork` shell function bumps this further to `CLAUDE_CODE_EFFORT_LEVEL=max` via env var, which overrides settings.json for one-off "this session needs everything" launches.

**Why pin it explicitly even when the model defaults to `high`:** Opus 4.8 ships with `effortLevel` defaulting to `high` out of the box (4.7 was lower). If you don't set this key, you get whatever the current model decides is reasonable. Pinning `xhigh` keeps the tier stable across model bumps — when 4.9 ships and changes its default again, this config doesn't drift.

## Context management

### `"autoCompactWindow": 800000`

Triggers conversation compaction at 800K tokens (80% of the 1M window). Default is lower; raising it lets sessions run longer before the model summarizes its own history and loses fidelity. Pair with the 1M context model — without `[1m]`, this setting has nothing to bite on.

### `"autoCompactEnabled": true`

When the window threshold hits, auto-summarize prior messages instead of erroring out. Without this, long sessions just die. The compaction is lossy — anything you want preserved across compaction should live in `~/.claude/projects/<proj>/memory/` or in a plan file, not in conversation history.

### `"cleanupPeriodDays": 90`

Garbage-collect transcripts older than 90 days. Long enough to recall last quarter's decisions, short enough that `~/.claude/` doesn't balloon to gigabytes.

## Skill listing budget

### `"skillListingMaxDescChars": 384` and `"skillListingBudgetFraction": 0.01`

Caps each skill description to 384 chars and the total skill-listing system reminder to ~1% of context. Important when you have 100+ skills installed across plugins — without these caps, the skill index alone can consume 30-50K tokens per turn.

## Notifications and UX

### `"preferredNotifChannel": "terminal_bell"`

Audible bell on turn completion. For long-running tasks (test suites, multi-file refactors) it's faster than tabbing back to check.

### `"voice": { "enabled": false }`

Voice input is push-to-talk on a held Space bar, and it needs a claude.ai login plus a local microphone. On a box without a capture device the hold becomes a failing-recording loop that ends in "Voice input is failing repeatedly and has been paused". The older `voiceEnabled` key is deprecated but still honoured, so `desk/install.sh` removes it and writes the explicit off; set `enabled` to true on a machine that actually has a microphone.

### `"tui": "fullscreen"` and `"prefersReducedMotion": true`

Fullscreen rendering keeps the conversation in the terminal's alternate screen, which stops the scrollback jumping while Claude works. Reduced motion matters inside tmux 3.3: tmux cannot do synchronized output, so every animated spinner frame is a full repaint the terminal pays for, visible as flicker on a Raspberry Pi. With motion off the spinner is static and the repaints stop.

### `"spinnerTipsEnabled": false`

The rotating tips under the spinner are onboarding text. After the first week they are noise in the one place the eye rests while waiting.

### `"env": { "CLAUDE_CODE_DISABLE_MOUSE": "1" }`

On a desk with no pointer, mouse capture is dead weight, and it also stops the terminal's own text selection from working. Scrolling stays on PageUp, PageDown, Ctrl+Home and Ctrl+End.

### `"env": { "CLAUDE_CLIENT_PRESENCE_FILE": "/run/user/1000/at-nomad" }`

Claude Code skips phone pushes while this file exists, checking only for existence every few seconds. Tie it to the display: a login autostart creates it, the idle blank removes it, resume and `screen-on` restore it. Pushes then start exactly when the desk goes dark instead of whenever a heuristic guesses you left. `desk/README.md` covers the three pieces that maintain it.

### `"theme": "custom:nomad"`

A custom theme is a JSON lookup table in `~/.claude/themes/`, zero CPU, hot-reloaded on edit. The tokens it controls are the spinner symbol, the input box border and the background behind your own messages, so on a terminal whose text colour already matches the theme accent it looks like nothing happened. `claude/themes/nomad.json` uses mint against a white-text terminal for that reason.

### `"statusLine.refreshInterval": 60`

The ccstatusline wrapper costs about a second of node start per render on a Pi 4. The `block-timer` segment shows whole minutes, so a 60 s tick loses nothing visible and halves the idle cost of a 30 s one.

### `"skipDangerousModePermissionPrompt": true`

Skips the "are you sure you want dangerous mode" confirmation. Only set this if you've thought about your hook configuration — dangerous mode bypasses pre-tool-use validation.

## Statusline

### `"statusLine.command": "CANONICAL_REPO=$HOME/your-primary-repo ~/.claude/statusline.sh"`

A wrapper around `ccstatusline` that prepends a yellow warning when the CWD is outside the canonical work tree. Catches the failure mode where you start a session in `~` instead of inside a repo and don't notice for 20 minutes.

The chain is four separate pieces — the npm package, the wrapper, the layout, and the settings key — and three of them fail quietly. In the order you hit them on a fresh machine:

**`ccstatusline` is its own install; the wrapper is not the tool.** `statusline.sh` prefers a global `ccstatusline`, falls back to a pinned fnm path, and last resorts to `npx -y ccstatusline@latest`. That final fallback is the trap precisely because it *works*: nothing errors, but the package is re-resolved from the registry on every render. On a Raspberry Pi 4 the lag is plainly visible; on a fast laptop you can pay it for months without noticing. `npm i -g ccstatusline` removes it from the path. The package is pure JS (`bin: dist/ccstatusline.js`, `engines: node >=14`), so there is no native-binary story on aarch64.

**`CANONICAL_REPO` defaults to a path that exists on one machine.** Unset, the script falls back to `$HOME/workspaces/internal-factory`. Anywhere that directory is absent, no CWD can ever match it, so the ⚠ line fires in *every* session — which teaches you to ignore the warning, the exact inverse of what it is for. Set it per machine in the settings command string; the `VAR=value cmd` prefix works because Claude Code runs the statusline command through a shell.

**A missing `statusLine` key is indistinguishable from a working setup that renders nothing.** Wrapper installed, layout installed, binary on PATH, no key in `~/.claude/settings.json`: no error, no statusline. Adding the key does *not* need a restart — Claude Code reloads settings on save and re-runs a changed `command` immediately, skipping its usual 300ms debounce. The other silent blank is workspace trust: `statusLine` runs a shell command, so it is gated by the same trust rule as hooks in settings files, and until the folder is trusted `claude --debug` logs `Status line command skipped: workspace trust not accepted`.

One knob the lite layout wants that the example settings don't set: `refreshInterval`. Updates are event-driven by default, so the `block-timer` and `session-cost` segments freeze whenever the main session goes idle — waiting on background subagents, most visibly. `"refreshInterval": 30` re-runs the command on a timer as well; size it off your slowest-changing segment rather than reflexively picking a small number — `block-timer` renders whole minutes, so a 30-second tick is already twice as often as the display can show. It is off by default here because every tick is a process spawn, which is also why the `npx` fallback above matters: Claude Code cancels an in-flight statusline script when the next update arrives, so a slow one can be killed before it ever prints.

Because none of the three announce themselves, verify by hand rather than by restarting and squinting — the wrapper takes the same JSON on stdin that Claude Code sends it. `cwd` drives the ⚠ check, and the context-bar segment needs a real `transcript_path` to have anything to measure (omit it and the segment simply doesn't render, which is not a fault).

## Deep-work launcher

### `claude-deepwork()` shell function

Wraps `claude` with the env vars that override settings.json for sessions where you want the absolute-max-effort defaults regardless of what's pinned. Specifically:

- `CLAUDE_CODE_EFFORT_LEVEL=max` — bumps past `xhigh` to the highest tier
- `ANTHROPIC_DEFAULT_OPUS_MODEL` — pins the Opus variant explicitly
- `CLAUDE_CODE_USE_BEDROCK=1` (when `CLAUDE_PROVIDER=bedrock`) — routes through Bedrock for SSO-authenticated workloads

Run subshelled `(...)` so the env vars don't leak into the parent shell after the session ends.

## Troubleshooting: "I bumped the model but it reverts on restart"

Claude Code reads settings from multiple layers in precedence order. On macOS managed devices (corporate laptops with MDM) the highest-precedence layer lives at `/Library/Application Support/ClaudeCode/managed-settings.json`, deployed by IT and owned by root. When that file pins `model` or `env.ANTHROPIC_DEFAULT_OPUS_MODEL`, **user-scope settings.json values are ignored on restart**.

Default precedence is `first-wins` — managed beats user. Symptoms: `/model` switches the model live in the current session, but the next launch reverts to the managed value. Fixes:

- **Live override only:** run `/model <id>` after launch. Doesn't persist.
- **Per-session env override:** `ANTHROPIC_DEFAULT_OPUS_MODEL=<id> claude`. The `claude-deepwork` shell function does this. Wins because env vars beat both managed and user settings for variables.
- **Durable fix:** ask IT to either (a) update the managed pin, or (b) set `"parentSettingsBehavior": "merge"` in `managed-settings.json` so user settings can override individual keys.

Diagnose with: `cat /Library/Application\ Support/ClaudeCode/managed-settings.json` (read-only is fine, no sudo needed).

## What's intentionally NOT in the example settings

- **`enabledPlugins`**: plugin enablement is per-machine and per-project. Listing them in the example would imply they're required.
- **`extraKnownMarketplaces`**: same reason — your marketplace list is your business.
- **`permissions.allow` beyond `WebFetch`**: tool-by-tool allow-listing belongs in project-scope `.claude/settings.json`, not user-scope.
- **Hooks**: hook config is project-specific. User-scope hooks are a footgun.
