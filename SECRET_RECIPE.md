# SECRET_RECIPE.md — Building a Self-* Coding Harness

> A transferable recipe for AI coding agents: how to build a development harness that documents, tests, and corrects itself — and, above all, how to keep it *honest* as it grows. Audience: a coding agent as capable as you, building a similar harness. This is the build order we would use knowing what we know now, which is not the order we actually used.
>
> **Provenance, stated plainly so you can weight it.** This is distilled from one repository: ~58 days of calendar time (first commit to now), built by a single human maintainer plus one agent. It is not a team's output, and not five months of multi-developer iteration. Sixty-nine architectural decision records (`D1`–`D69`) back it. The capstone — the meta-contract test layer this recipe is proudest of — shipped in the *last week* of that window, so it has days of field exposure, not months. Treat the *patterns* as well-reasoned and the *maturity claims* as young. Where this recipe says "this works," it means "we built it and it passes its tests"; it rarely means "we have run it autonomously in production a thousand times." Several of the autonomous loops below have run, in true autonomous mode, **zero to two times**: they are built and flag-gated, not battle-tested. The recipe will tell you exactly where, because a recipe about honest systems that lied about its own maturity would be worthless.

## 0. The One Idea

**Prose has no compiler.** Application code that references a missing symbol turns red instantly; a hook that's written but never wired, a doc that claims a capability that doesn't exist, a config nobody consumes — none of these turn red. They rot silently. Every durable thing in this recipe exists to give the *meta-layer* (hooks, commands, config, capability claims) the same fail-loud property that `tsc` gives application code.

The corollary, and the one most likely to mislead you if you take it as a slogan: **when a reviewer says your "self-healing" claim is overblown, prefer building until the claim is true over softening the words — but only when you can bind the claim to an assertion you can actually write today.** The aspirational claim is a forcing function; soften it reflexively and you delete the thing pulling the work forward. But there is a failure mode that is *worse* than honest softening: keeping the big claim and "satisfying" it with a shallow check (a grep that proves a function *name* exists, not that it *works*). That is softening-by-stealth, disguised as rigor. The honest rule:

> Bind every load-bearing claim to the strongest assertion you can write **now**. If the strongest you can write is a presence-grep, then your claim is only protected against *deletion*, not against *over-claiming* — so scope the claim's wording to what the grep actually proves, and file "make this a behavioral assertion" as backlog. Keep the ambition in the roadmap, not in a sentence the test can't defend.

This is the tension the whole harness is organized around, and §6 is where we admit how far our own implementation still is from the ideal.

## 1. Build Order

The honest version. We discovered most of this by getting it wrong first; the ordering front-loads what everything else turned out to depend on.

**Phase 1 — Foundation (before any "self-*" anything):**
1. **Worktree isolation + a pre-write/pre-bash hook pair.** All write sessions use a git worktree; hooks block writes/commits on the main tree. Load-bearing because it lets multiple agent sessions run without clobbering each other. Ship tiered bypass governance at the same time (every bypass logged) — you will need escape hatches and they must be auditable from day one.
2. **`jq` (or your equivalent) as a hard prerequisite.** Every safety hook hard-fails (exit 2) if it's absent, and bootstrap installs it. A hook that silently skips because a dependency is missing is worse than no hook — it's a gate everyone believes is active and isn't. (We learned this as a real incident: without `jq`, *all* hooks silently no-op'd credential detection and `--no-verify` blocking.)
3. **One structured event log + one emit helper**, sourced by every hook. Decide the canonical path once (see §8) and never let a second event store exist. Fragmented event stores are how you end up unable to answer "how many knowledge-misses have we actually had?"
4. **The hook-bijection contract test — build it alongside hook #1, not at the end.** This resolves the build-order tension other readers flagged: the *test framework's capstone* (§6) comes last, but its single highest-value member has almost no dependencies and belongs here. The moment you have two hooks and a registry file, assert the bijection (§6). It is the cheapest insurance in the whole recipe.
5. **An idempotent bootstrap** that installs the git hooks (enumerated: `pre-commit`, `prepare-commit-msg`, `pre-push` — see §6) and is tested in a throwaway `/tmp` clone, repeatedly. Bootstrap is the only thing between a fresh clone and a working harness.

**Phase 2 — Pipeline and enforcement:**
6. **A development pipeline as named phases** (`architect → spec → test-gen → dev → review → docs`), each a specialized subagent the *agent invokes* — not a meta-program that drives the agent (see "What NOT to build"). Enforce TDD: test-gen writes failing tests, dev makes them pass, and a pre-write hook gates new functions on the existence of a matching test file.
7. **Domain-partitioned `CLAUDE.md` files** (§2): root small and behavioral, domain knowledge next to the code.

**Phase 3 — Memory and the documentation flywheel:**
8. **Two-tier memory** (§3): an always-in-context auto-memory index + a semantic store seeded from your own docs.
9. **The documentation flywheel** (§4): detect when code changes should trigger doc updates, write suggestions to a pending-actions file, block commits until addressed.

**Phase 4 — The self-* loops, each behind the 4-element safety template (§5), flag-gated OFF by default.**
10. Self-Documenting, Self-Learning, Self-Healing. Build them as genuine loops — *and read §5's honesty note about how little they've actually run autonomously.*

**Phase 5 — The rest of the compiler (the capstone):**
11. **The full meta-contract suite + claims ledger + local pre-push gate** (§6). The bijection test (step 4) was the beachhead; this is the rest — reference-integrity, config-consumption, and the claims ledger, which can only come now because it asserts against claims that don't exist until Phases 2–4 do.

### What NOT to build (the reversals that cost us weeks)

- **Do not build an orchestrator program.** We built a TypeScript subagent-pipeline orchestrator (`orchestrator.ts`, with a `runAgent()` loop) and separately a sequencing layer; the durable conclusion (D41) is that **the coding agent IS the orchestrator** — plan mode + a good root `CLAUDE.md` + individual phase skills handle sequencing natively. Encode the pipeline as *skills the agent invokes*, not a meta-program that drives it. (The agent already is a planner with a tool loop and a context window; a driver on top duplicates that.)
- **Do not build a queue system or HTTP status API for agent coordination.** Three separate decisions (D35, D38, D39) reached for infrastructure and concluded files-and-prompts won: file-based session status, headless task prompts over a queue, pull-based over push channels. Pull-based is correct for dev tooling.
- **Do not add adversarial-review rounds past ~3** (§7). More same-model reviewers share blind spots and inflate false confidence.
- **Do not let the meta-layer's own measurement be unwired.** Our "are we getting better?" health eval shipped **invoked by nothing** — the exact disease this recipe names. We are telling you this *because we did it*: as of this writing, `emit-harness-health.sh` is a manual script wired to no cron or hook. If you build a health metric, wire it to something that runs it (session-start or a scheduled job) the same day, or don't claim it runs.

## 2. The Root CLAUDE.md

`CLAUDE.md` is the system prompt for every session — the most consequential and most-abused file in the harness.

- **Behavioral, not encyclopedic.** Tell the agent *how to act*, not *what every subsystem contains*. We pruned ours from 239 → 154 lines (verified: `wc -l` = 154) and quality went up. Reference data belongs in on-demand files.
- **Front-load behavioral invariants.** On some model families, instructions at the top get more attention weight; even where that's uncertain, top-of-file is where a human looks first. "Use existing tools first," "minimum viable change," "think before acting," "surgical precision" go at the top.
- **A precedence ladder**, stated explicitly: hook enforcement (deterministic) > root CLAUDE.md > contextually-loaded rules > domain CLAUDE.md > commands > skills > auto-memory. Without it, the agent can't resolve the contradictions that *will* accumulate.
- **Contextually-loaded rules over always-loaded prose.** Domain invariants live in `rules/*.md` with path frontmatter so they load only when editing matching files. The always-loaded surface is your single largest recurring per-turn cost.
- **Point to a doc navigator; don't inline the docs.** One "where does X live" table the agent reads on demand beats inlining everything.

## 3. Memory Configuration

Two tiers, deliberately separate.

**Tier 1 — Auto-memory (always in context).** A `MEMORY.md` index loaded every session + one file per fact. Frontmatter: `name`, `description` (drives recall relevance), `type` ∈ {`user`, `feedback`, `project`, `reference`}, optional `last_reviewed: YYYY-MM-DD`. The `feedback` type is the highest-value — corrections *with the why* are how the harness stops repeating mistakes. The index is the per-turn cost; one line per memory, fact in the file.

**Tier 2 — Semantic store (queried on demand).** A SQLite DB (better-sqlite3) with TF-IDF + cosine search over your own docs. Seed it *from files you already maintain* (operational-gotchas, design decisions, domain CLAUDE.md), chunked on paragraph boundaries with frontmatter/headings/templates filtered out, deduped by a `UNIQUE(namespace, content)` constraint so re-seeding is idempotent. Schema and seeding are in the appendix. The seed runs at bootstrap so a fresh clone starts with the team's accumulated knowledge, not an empty store.

**Hygiene as a loop.** `last_reviewed` feeds an oldest-first review; a cron nags at >90 days. When a memory becomes contributor-agnostic (true for anyone, not just the current human), *promote* it into a project-scoped `rules/*.md` or a domain CLAUDE.md. Memory is a staging area; durable general knowledge graduates into the repo.

**The part that makes it work:** seed the semantic store from the *same* canonical docs the flywheel promotes into. A promoted learning becomes both a doc edit and a re-seedable memory, so the two knowledge bases reinforce instead of drifting apart.

## 4. The Documentation Flywheel

Turns "code changed" into "doc updated" without relying on memory. Four parts:

**(a) Detection.** A hook aggregates changed files from four sources — uncommitted (`git status`), commits since session start (`git log --since`), session-tracked files (a `.sessions/<id>.files` list the post-write hook maintains), and unresolved validation-failure patterns. It maps changed paths to the docs that describe them (a change under `functions/voice/` → `functions/voice/CLAUDE.md`) and **skips suggestions whose target doesn't exist** (never nag about deleted files). Debounce it and exclude its own outputs, or it re-fires on itself.

**(b) Capture + gate.** Suggestions land in `pending-actions.json` (shape in appendix). A pre-bash hook **blocks commits while it's non-empty** (bypass logged). This is the forcing function against doc debt.

**(c) Causal attribution — built, but barely exercised; calibrate your expectations.** A `prepare-commit-msg` hook injects a `Flywheel-Suggestion-Id:` trailer when staged files match a pending suggestion; a `post-commit` hook reads it and emits a `commit_attributed` event, with a content-hash fallback for commits that paraphrase without the exact trailer. This is the design that *makes the loop measurable in principle*. Honesty: in our repo it has fired **2 times against 120 emitted suggestions**. The mechanism is real and correct; the *number is not yet trendable*. Build it for the property, but don't claim a measurement you don't have the n for.

**(d) The learnings cycle (capture → promote → clear).** Discoveries go into `learnings.md` immediately; stable ones get promoted to permanent docs; a pre-write hook **blocks clearing `learnings.md` unless `learnings-archive.md` was just updated** — you can't delete a learning without archiving it. This stops capture from becoming a black hole.

## 5. The 4-Element Safety Template (for every autonomous loop)

Every self-* loop that *mutates* anything follows the same structure. This is what lets you ship autonomy without recklessness:

1. **Off-by-default feature flag at the entry point** (`AUTONOMOUS_REMEDIATION_ENABLED`, `FLYWHEEL_AUTONOMOUS_PR`). Opt-in, per-operator, per-session. Production-safe by default.
2. **A pre-flight check immediately before the mutation** — `gh auth status` before a PR; a TOCTOU re-fetch-and-compare before changing a webhook; a marker-block requirement before patching a doc. Check reality *at the moment of action*.
3. **A reversal step paired with the mutation** — delete the branch on PR failure; `rollback()` after a failed re-validation; ledger writeback only on success. Every forward action defines its undo in the same place.
4. **A structured event at every checkpoint**, joinable by a stable id and queryable. If you can't observe the loop end-to-end, you can't claim it's closed.

**Two boundaries that matter more than the flags.** First, the autonomous mutator **never deletes and never touches credentials** — enforce it with a hard-coded allow-list of action types, not discipline. Second, and stronger: keep the mutator **architecturally incapable of touching the shell.** In our repo the thing that shells out to `gh`/`git` lives in the agent-tooling layer; the MCP API-wrapper layer has *zero* `child_process` calls and physically cannot deploy or run a CLI. This contains blast radius even if a flag is misconfigured — structure beats configuration.

**The honesty note this section exists for.** These loops are *built and flag-gated*, and in true autonomous mode they have run, in our repo: Self-Healing config remediation **0 times** (`remediation_executed` events: 0 — the second gate flag has never been enabled in recorded history), autonomous PR **0 times**, attribution **2 times**. By default the system **detects drift and prompts a human to fix it** — which is exactly what our shipped `ONBOARDING.md` says: "it doesn't fix things on its own; the human (or the next session) does the fixing." So: build the loops to the template, keep the ambitious names, *and describe them as "detect-and-prompt by default, autonomous-capable behind flags, lightly exercised."* That sentence is the difference between this recipe being true and being the thing it warns against.

## 6. The Test Plan — Meta-Contract Tests

The compiler. Application code gets normal TDD. The *meta-layer* gets contract tests asserting wiring:

- **Hook bijection** (build first, per §1). A canonical `hook-registry.json` lists every hook with its trigger. The test asserts, bidirectionally: every non-helper hook on disk is registered, every registry entry resolves to a real file, every `git-hook` entry is referenced by a bootstrap installer (catches "registered but never installed"), and every `settings`-triggered hook is actually in settings. One direction is a blind spot; you need both.
- **Reference integrity.** Commands/skills reference real files; event-type emitters exist; config keys have consumers.
- **A claims ledger — and a frank account of what it does and doesn't do.** `claims.json` binds each load-bearing capability claim to an assertion (shape in appendix). When the assertion's mechanism is removed, the test goes red. **Critical limitation, because we hit it ourselves:** every assertion in our shipped ledger is `assertion_kind: "grep"` — a presence check. A presence-grep catches a claim whose *symbol was deleted*; it does **not** catch a claim that has *outrun its code* (our "Self-Healing exists" claim is grep-green while the loop has run zero times). So the ledger as we built it is a **deletion-guard, not an over-claim detector** — and our own ledger still contains the weak one-word token (`ABOUTME`, which appears in 1,200+ files) that we'll tell you in the same breath to avoid. Do better than we did: bind to distinctive multi-word tokens, and where you possibly can, make the assertion *behavioral* — invoke the mechanism and assert an outcome — even if you can only afford it for one or two marquee claims. A behavioral assertion on your single most-important claim is worth more than grep coverage on ten.
- **A local pre-push gate** runs the suite. Make it **soft at first** (warn + exit 0) for a grace period, then flip to blocking — a gate that cries wolf on day one gets disabled. Provide a logged bypass. **Curate gated tests by allowlist, not glob**: a non-contract scanner that makes the gate red on day one kills it.
- **Run enforcement locally if CI can't be trusted — and assume it can't until proven.** Our org disabled GitHub Actions org-wide; workflows registered as "active" and never fired. A local git hook was the *only* enforcement primitive that actually executed. If your CI genuinely runs and supports required server-side checks, prefer that — a server-side check beats a client-side hook a contributor can `rm`. Know which world you're in before you choose.
- **Anti-unfalsifiability.** Design at least one metric that can report *regression*. We treat a *low* knowledge-miss-capture count as an alarm (the observer may be broken), not a pass. Caveat in the same breath: this only helps if the metric is *wired to run* — see §1's confession about our unwired health eval.

Hold every meta-test to the bar you hold the code: prove it red-on-defect (break the wiring, watch it fail) and green-on-restore. A contract test you've never seen fail is itself unwired.

## 7. How to Review (and how not to)

For a significant decision, use **exactly three roles**: an advocate and a critic in *isolation* (neither sees the other), then an arbiter on a *different model* that verifies contested claims with tools (fetch URLs, read code, run counts) rather than synthesizing opinions. The research is consistent: accuracy improves to ~round 2–3 then *drops*; same-model reviewers have correlated errors; extended debate converges on shared wrong answers. If the result is unsatisfying, the fix is better tools / a different model family / narrower scope — **not more rounds.** (This recipe was itself put through that review; §5 and §6's honesty notes are findings the critic and the aspirational-auditor forced. The process works — it caught me overclaiming.)

Pair it with a standing adversarial review *of the harness itself* on a fixed schedule — personas that attack closed-loop claims, hook bypassability, context cost, onboarding friction, aspirational-vs-real language, and drift. Findings that survive verification become backlog; the rest is documented noise.

## 8. Meta-Mode: the single-developer / shipped split

One structural trick that paid off: a `.meta/` directory (gitignored, or a symlink to a separate workshop repo) that, **when present**, reroutes all session state — events, learnings, pending-actions, plans, the memory DB — out of the shipped tree; when absent (the default for anyone who clones), everything routes to `.claude/`. The maintainer accumulates messy working state without polluting what new users clone, using the *same* hooks. Detect it once in a sourced helper that sets the path env vars (`CLAUDE_LOGS_DIR` etc.) **before** the emit helper picks its path; every hook and the memory store inherit the routing. Never special-case it per-hook. (One residue we haven't cleaned: two writers to `pending-actions.json` disagree on shape — a raw array vs `{actions:[]}`. It works but the dual-writer contract is still inconsistent; unify yours from the start.)

## 9. The Minimum Viable Harness

The full recipe is a lot of surface (28 hooks, two memory tiers, a ledger, an event bus, a 4-element template), and it earned its place *at our scale* — a repo with many services, upstream platform repos, and an autonomous agent doing the bulk of commits. **Do not build all of it on day one.** Apply §10's smell test honestly and start with the subset that is true-of-itself immediately:

1. **One append-only `events.jsonl` + one emit helper.** Fragmentation is the enemy; get this right first.
2. **The hook-bijection contract test, the moment you have two hooks.** Highest value-per-line in the entire recipe.
3. **Dependency hard-fail** (no silent-skip).
4. **One *behavioral* smoke test per load-bearing claim** — actually invoke the mechanism and assert an outcome, even if you can only afford one or two — in preference to a grep ledger across all of them.

Add a memory tier, the self-* loops, the `.meta/` split, the claims ledger, and the 4-element template **only when a specific, named failure forces each one.** That subset is ~80% of the value at a fraction of the surface, and — unlike the full kit — it is honest on day one.

## 10. The Smell Tests (carry these the whole way)

- "Would a senior engineer say this is overcomplicated?" If yes, simplify. We deleted more than we kept.
- "Is this solving a real problem or gold-plating?" Can you name the specific failure each piece prevents? If not, cut it.
- "If the maintainer stopped tending this, what's the half-life before it's misleading?" Pieces with real enforcement (hooks, contract tests) survive neglect; pieces that depend on discipline (hand-updated registries, hand-run evals) rot. Push everything you can toward enforcement.
- "Does the language outrun the implementation?" Grep your own docs for "autonomous," "self-*," "closes the loop." For each, find the code *and check how often it has actually run*. If the code exists but has run zero times, your doc should say "capable of," not "does." That last clause is the one we failed first and fixed last.

---

## Appendix — Artifact Skeletons

A recipe about falsifiable systems should be falsifiable, so here are the load-bearing data shapes, verbatim from the source repo. With these you can recreate the artifacts; without them the sections above are just prose.

### A1. Host assumptions (portability)

This recipe assumes Claude Code; the nouns below are its primitives. Map them to your host's equivalents before building:

| Recipe primitive | Claude Code form | Abstract capability you need |
|---|---|---|
| Hooks | `settings.json` events: `PreToolUse`/`PostToolUse`/`SessionStart`/`PreCompact` with `Write\|Edit\|Bash\|Skill` matchers | Run a script before/after the agent's file-write, shell, and session-lifecycle actions, with the ability to block (exit 2) |
| Git hooks | `pre-commit`, `prepare-commit-msg`, `pre-push` installed by bootstrap | Same, at git's lifecycle points |
| Skills/commands | `.claude/skills/**`, `.claude/commands/*.md` | Named, on-demand procedures the agent invokes |
| Contextual rules | `.claude/rules/*.md` with path frontmatter | Load extra instructions only when editing matching files |
| The orchestrator | plan mode + the agent's own tool loop | A planner that sequences multi-step work natively (do not build your own) |
| Deps | `jq`, `better-sqlite3`, `git`, `gh` | JSON in shell; embedded SQL DB; git; a PR CLI |

If your host lacks the blocking-hook primitive, most of the enforcement in this recipe is not reproducible — that is the one hard dependency.

### A2. `events.jsonl` + the `emit_event` contract

One append-only file per canonical path (§8). The helper, sourced by every hook:

```bash
# emit_event TYPE [PAYLOAD_JSON]
emit_event() {
  local event_type="$1" payload="${2:-{}}"
  command -v jq >/dev/null || return 0          # degrade silently, never block on missing jq here
  local log_dir="${CLAUDE_LOGS_DIR:-${PROJECT_ROOT:-.}/.claude/logs}"   # set by the meta-mode helper FIRST
  mkdir -p "$log_dir"
  local line
  line="$(jq -nc --arg ts "$(date -u +%Y-%m-%dT%H:%M:%SZ)" \
                 --arg type "$event_type" \
                 --arg sid "${EMIT_SESSION_ID:-unknown}" \
                 --argjson payload "$payload" \
                 '{timestamp:$ts, event_type:$type, session_id:$sid} + $payload')"
  [ -n "$line" ] && printf '%s\n' "$line" >> "$log_dir/events.jsonl"
}
```
One JSONL line looks like: `{"timestamp":"2026-06-10T00:00:00Z","event_type":"commit_attributed","session_id":"abc","suggestion_id":"...","attribution_method":"trailer"}`. The load-bearing guard: resolve `CLAUDE_LOGS_DIR` in a sourced meta-mode helper *before* this runs, so events never scatter.

### A3. `hook-registry.json` (the bijection's source of truth)

```json
{
  "trigger_types": {
    "settings": "Registered in settings.json under a hooks.<Event> matcher",
    "settings-local": "Registered in settings.local.json (per-developer, gitignored)",
    "invoked-by-hook": "Called by another hook; 'invoked_by' names the caller",
    "command-invoked": "Called by a slash command; 'invoked_by' names it",
    "git-hook": "Installed into .git/hooks/ by bootstrap; 'installed_as' names the path"
  },
  "hooks": [
    {"file":"pre-write-validate.sh","trigger":"settings","event":"PreToolUse","matcher":"Write|Edit","purpose":"Credential detection, ABOUTME enforcement, meta-mode isolation, pipeline gate."},
    {"file":"pre-commit-data-classification.sh","trigger":"git-hook","installed_as":".git/hooks/pre-commit","purpose":"Block commits containing secrets."},
    {"file":"post-commit-attribution.sh","trigger":"invoked-by-hook","invoked_by":"post-bash.sh","purpose":"Match commits to flywheel suggestions via trailer or content-SHA."}
  ]
}
```
Convention: helper libraries whose names begin with `_` (e.g. `_meta-mode.sh`) are sourced, not entry points, and are excluded from the bijection. The test reads `.claude/hooks/*.sh`, the registry, and `settings.json`, and asserts all four conditions in §6.

### A4. `claims.json` (one full claim) + the test's limitation

```json
{
  "id": "self-healing-system-property",
  "source_doc": "ONBOARDING.md",
  "text_token": "Self-Healing",
  "claim": "Self-Healing exists as a system property — validation failures can attempt autonomous remediation.",
  "status": "backed",
  "assertion": "validation.ts resolves autoHeal default from CLAUDE_SELF_HEAL env",
  "assertion_kind": "grep",
  "assertion_file": "agents/mcp-servers/twilio/src/tools/validation.ts",
  "assertion_pattern": "selfHealDefault"
}
```
The test, per claim: (1) `text_token` still appears in `source_doc` (claim wasn't silently deleted), and (2) `assertion_pattern` appears in `assertion_file` (mechanism exists). **This exact object is the cautionary tale of §6**: `text_token: "Self-Healing"` is fine, but `assertion_kind: "grep"` means it only proves `selfHealDefault` *exists*, not that remediation has ever *run* (it hasn't — 0 events). Ship `assertion_kind: "behavioral"` for your marquee claims if you can; we hadn't yet.

### A5. Memory schema (Tier 2) + seeding

```sql
CREATE TABLE IF NOT EXISTS memories (
  id INTEGER PRIMARY KEY AUTOINCREMENT,
  namespace TEXT NOT NULL,            -- gotchas | project | feedback | reference
  content   TEXT NOT NULL,
  source    TEXT DEFAULT '',          -- provenance, for incremental re-seed
  metadata  TEXT DEFAULT '{}',
  tokens    TEXT DEFAULT '',          -- pre-tokenized content, for TF-IDF scoring
  created_at TEXT DEFAULT (datetime('now')),
  updated_at TEXT DEFAULT (datetime('now'))
);
CREATE UNIQUE INDEX IF NOT EXISTS idx_ns_content ON memories(namespace, content);  -- idempotent re-seed
```
TF-IDF lives in application code, not SQL: tokenize (lowercase, strip punctuation, drop stopwords, Porter-stem) into the `tokens` column at write time; at query time compute IDF over candidate rows + query terms and rank by cosine similarity, returning top-k above a min score. **Seeding is a script that walks existing docs** (operational-gotchas, design decisions, domain CLAUDE.md), chunks on blank lines, filters frontmatter/headings/templates, and bulk-inserts with `INSERT OR IGNORE` — there is no separate committed corpus directory; the docs you already maintain *are* the corpus.

### A6. `pending-actions.json` (flywheel capture)

```json
{ "actions": [
  { "timestamp": "2026-06-10T00:00:00Z",
    "target": "functions/voice/CLAUDE.md",
    "reason": "voice handler changed; doc may be stale",
    "suggestionId": "uuid-v4",
    "contentSha256": "sha-of-reason-for-fallback-matching" }
] }
```
Lifecycle: the flywheel hook appends (deduped — re-add a file target only if its mtime is newer than the last suggestion); the pre-bash hook blocks commits while `actions` is non-empty; `prepare-commit-msg` injects a `Flywheel-Suggestion-Id:` trailer for staged files matching a `target`; `post-commit` reads the trailer (or content-hash-matches the `reason`) and emits `commit_attributed`. Pick the array-vs-object shape once and use one writer — we have two writers that disagree and it's a latent bug (§8).

---

*A note on this document's own honesty: an earlier draft called the self-* loops "genuine autonomous loops" and sold the attribution mechanism as a measurement you can trend. A four-reviewer adversarial pass — the same kind §7 prescribes — counted the actual events (0 remediations, 2 attributions) and caught the draft being less honest than the repo's own onboarding doc. Every maturity claim above was then rewritten to match the event counts, and this appendix was added because the reviewers (rightly) called a prose-only recipe about falsifiable systems unfalsifiable. That is the recipe working on itself: the patterns are sound, the numbers are real, and where the two disagreed, the numbers won.*
