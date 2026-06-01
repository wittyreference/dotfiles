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

### `"voiceEnabled": true`

Enables voice input (push-to-talk via the keybinding in `~/.claude/keybindings.json`). Useful for narrative prompts where typing slows you down.

### `"skipDangerousModePermissionPrompt": true`

Skips the "are you sure you want dangerous mode" confirmation. Only set this if you've thought about your hook configuration — dangerous mode bypasses pre-tool-use validation.

## Statusline

### `"statusLine.command": "~/.claude/statusline.sh"`

A wrapper around `ccstatusline` that prepends a yellow warning when the CWD is outside the canonical work tree. Catches the failure mode where you start a session in `~` instead of inside a repo and don't notice for 20 minutes. Configure the canonical path via the `CANONICAL_REPO` env var or edit the script.

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
