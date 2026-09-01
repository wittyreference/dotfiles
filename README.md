# dotfiles

Personal config snapshots for shell, tmux, and Claude Code. Curated for portability — anything machine-specific (API keys, marketplace SIDs, plugin enablements) is omitted on purpose.

## What's in here

| Path | Purpose |
|---|---|
| `shell/functions.sh` | tmux session layouts (`factory`, `split`, `cctv`, `parallel`, …), TextMate viewer bridge (`mview`, `mpipe`, `mdiff`), `claude-deepwork` launcher |
| `claude/settings.example.json` | Sanitized `~/.claude/settings.json` — model, thinking, context window, statusline reference |
| `claude/statusline.sh` | `ccstatusline` wrapper that warns when CWD is outside the canonical work tree |
| `claude/ccstatusline-lite.json` | Portable 2-line statusline layout (no project-specific custom-command segments) |
| `docs/claude-config-rationale.md` | Why each non-default setting is set the way it is |
| `CLAUDE.md` | Working agreement and coding standards (relationship framing, TDD, comment policy) |
| `brainstorm.md` / `twilio-brainstorm.md` | Brainstorming prompt templates for new prototypes |
| `LLM-PROMPTS.md` | PM concept-development prompts — idea → concept.md → PR-FAQ → PRD → tickets |

## Setup

```bash
# 1. Clone (anywhere, but ~/dotfiles is conventional)
gh repo clone wittyreference/dotfiles ~/dotfiles

# 2. Source the shell functions from your ~/.zshrc
echo '[ -f ~/dotfiles/shell/functions.sh ] && source ~/dotfiles/shell/functions.sh' >> ~/.zshrc

# 3. Install ccstatusline itself — the wrapper is not the tool
#    Pure JS, no native binary, so aarch64 (Raspberry Pi) needs nothing special.
npm install -g ccstatusline

# 4. Install Claude statusline wrapper
cp ~/dotfiles/claude/statusline.sh ~/.claude/statusline.sh
chmod +x ~/.claude/statusline.sh

# 5. Install ccstatusline layout
mkdir -p ~/.config/ccstatusline
cp ~/dotfiles/claude/ccstatusline-lite.json ~/.config/ccstatusline/settings.json

# 6. Merge desired keys from settings.example.json into ~/.claude/settings.json
#    (don't overwrite — your existing file has plugin enablements you want to keep)
#    Two statusline-specific edits, both silent when wrong:
#      - without the `statusLine` key there is no error, just no statusline
#      - set CANONICAL_REPO to THIS machine's primary tree, or the ⚠ fires every session
#    "statusLine": {
#      "type": "command",
#      "command": "CANONICAL_REPO=$HOME/your-primary-repo ~/.claude/statusline.sh",
#      "padding": 0
#    }

# 7. Verify the whole chain without restarting Claude
#    The wrapper reads the same JSON Claude Code sends it on stdin.
echo "{\"cwd\":\"$PWD\",\"model\":{\"display_name\":\"Opus\"},\"workspace\":{\"current_dir\":\"$PWD\"},\"cost\":{\"total_cost_usd\":0,\"total_duration_ms\":0,\"total_lines_added\":0,\"total_lines_removed\":0}}" \
  | CANONICAL_REPO=$HOME/your-primary-repo ~/.claude/statusline.sh
```

Step 7 prints the ⚠ line until you replace `your-primary-repo` — that *is* the check working.
Settings reload on save, so step 6 takes effect without restarting Claude. If the line stays blank anyway,
the folder is probably untrusted — `claude --debug` logs `Status line command skipped: workspace trust not accepted`.

## Reading order for a new machine

1. `docs/claude-config-rationale.md` — understand the *why* before copying values.
2. `claude/settings.example.json` — pick the keys that match your workflow.
3. `shell/functions.sh` — source it, then run `factory` or `split` to see the tmux layouts.
4. `CLAUDE.md` — read the relationship framing and TDD policy; this is what every Claude Code session in a repo without its own CLAUDE.md will fall back to.

## Conventions

- **No secrets, no marketplace SIDs, no machine-specific paths in committed files.** The repo is public.
- **`ABOUTME:` comments** on every shell file (lines 1-2). Makes it greppable.
- **Comments explain *why*, not *what*.** If the comment restates the code, delete it.
- **One logical change per commit.** Squash-merge upstream.

## Attribution

The CLAUDE.md relationship framing and coding-standards section started from Harper Reed's dotfiles ([github.com/harperreed/dotfiles](https://github.com/harperreed/dotfiles/blob/master/.claude/CLAUDE.md)) and has drifted since. Worth crediting either way.
