# SECRET_RECIPE.md — Build a Self-Maintaining Coding Harness

> **To the coding agent reading this:** This is a build manual, not an essay. You are capable of writing the code; what you need from me is *exactly what to build, what it must do, and how to prove it works*. So this document specifies artifacts, their required behavior, the non-obvious correctness rules, the wiring, and a verification gate per phase. Write the implementations yourself — but meet every MUST.
>
> **How to run this:** In plan mode, read the whole document, then produce a plan that is "execute the phases in order, stopping to verify each Acceptance Gate." Phases 1–6 are the core harness; 7–8 are the substrate and commands that make it pay off daily; 9 is optional. (If you want the smallest useful thing first, build the Minimum Viable Harness subset called out near the end, then return for the rest.) Then execute with max thinking. Do not advance past a red gate. Do not skip phases. Do not "improve" the architecture mid-build — the ordering and the omissions are deliberate, and the surprising ones are flagged.
>
> **End state:** a harness that tests its own wiring, documents itself as code changes, remembers across sessions, and fails loudly when any of that breaks — on top of which you (or the human) build whatever you want.

## What you are building

```
<repo>/
  .claude/
    settings.json            # hook registrations (the source of "what's wired")
    hooks/
      _meta-mode.sh          # shared: resolve repo root + state paths
      _emit-event.sh         # shared: append a structured event
      pre-write-validate.sh  # PreToolUse(Write|Edit): block bad writes
      pre-bash-validate.sh   # PreToolUse(Bash): block dangerous commands
      post-write.sh          # PostToolUse(Write|Edit): track touched files
      post-bash.sh           # PostToolUse(Bash): log; attribute commits
      flywheel-doc-check.sh  # detect code→doc drift, write suggestions
      pre-push               # git hook: run the meta-contract gate
      prepare-commit-msg     # git hook: inject flywheel trailers
    hook-registry.json       # canonical catalog of every hook + trigger
    claims.json              # load-bearing doc claims bound to assertions
    rules/                   # path-scoped instruction fragments
    logs/events.jsonl        # the single event log
    pending-actions.json     # open doc-update suggestions (the commit gate)
  __tests__/meta/            # the meta-contract test suite
  scripts/
    run-meta-tests.js        # the `npm run test:meta` runner
    bootstrap.sh             # idempotent setup; installs git hooks; seeds memory
    memory.js / seed-memory.js  # semantic memory store + seeder
  CLAUDE.md                  # the always-loaded behavioral system prompt
  package.json
```

Build order is bottom-up: the spine everything depends on, then the first hooks *with the compiler that keeps them honest*, then bootstrap, the system prompt, memory, the docs flywheel + claims ledger. Optional autonomous loops last.

## The Invariants

Seven rules. If you violate them the rest collapses, and they are precisely the rules a capable coding agent breaks on instinct. Most of the build is one of these made executable.

1. **Prose has no compiler — so test wiring, not structure.** A hook written but never registered, a doc claiming a feature that doesn't exist, a config nobody reads: none throw an error; they rot silently. Every test in this harness asserts something is *connected*, not that it's *shaped right*.
2. **Never resolve paths from `cwd` — and the *right* anchor differs by artifact type.** `cwd`/`$PWD` betray you the instant code runs from a worktree or subdirectory; this is the single most expensive mistake in the build. But the correct replacement is not the same everywhere: **a sourced hook must anchor to its own file location** (`"$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"`, then walk up to the repo root), because a hook can be *invoked* with a `cwd` deep inside `.claude/` — and `git rev-parse --show-toplevel` run from there will still resolve the root but a naive `"$ROOT/.claude/logs"` then doubles to `.claude/.claude/logs`. **A test runner or standalone script, by contrast, should anchor to `git rev-parse --show-toplevel`** (it's invoked from the repo, and must find the *worktree's* root, which `BASH_SOURCE` can't give it). Rule of thumb: *sourced-into-a-hook → `BASH_SOURCE`; run-as-a-program → `git rev-parse`.* Either way, guard against a doubled `.claude/.claude/` prefix.
3. **A safety check that fails *open* is worse than no check.** If a hook's dependency (e.g. `jq`) is missing, it must **block (exit non-zero)**, never silently pass. A gate everyone believes is active but isn't is the worst state.
4. **Do not build an orchestrator.** You are already the orchestrator — you have a plan, a tool loop, a context window. Encode each pipeline phase as a *skill/command you invoke*, never a program that "drives" the agent. The instinct to meta-program the agent is the mistake; a driver program on top of an agent gets no use and earns deletion.
5. **Every autonomous mutation is opt-in (off by default), re-checks reality immediately before acting, defines its own undo, and emits an event.** If you can't write the undo, you may not write the mutation.
6. **A doc claim is a liability until a test defends it.** Either bind "the harness does X" to an assertion that goes red when X breaks, or don't write it in present tense. Aspiration goes in a roadmap, not in prose a test can't back.
7. **Verify a claim against live state before acting on it — recorded interpretations drift.** A finding, an audit item, a subagent's report, a doc's own description: each is a *snapshot*, and the world moved since it was written. Before you act, re-check the claim against current reality with a command (grep the live tree, re-run the count, read the file), and the more dramatic the claim — "it's broken," "delete this," "there's nothing here" — the more it must be *reproduced* first, because that's exactly the claim that does damage if it's wrong. A proxy (a name match, a directory listing, a zero-count grep) is not the criterion it stands in for. And read a load-bearing instruction file *whole*, never a prefix: a partial read leaves you acting as if you'd seen it, which is worse than not having read it.

---

## The Host Contract (read before Phase 2 — the things you cannot infer)

Everything below assumes a host that runs scripts around the agent's tool calls. This recipe targets **Claude Code**; on a different host, map these primitives first (see Host Assumptions at the end). Four facts about the host are *contracts* — guess them and you build broken hooks, so they're stated literally.

**1. How a hook receives input.** A tool hook is invoked with a JSON object on **stdin**. The fields you parse:
```json
{ "session_id":"abc123", "tool_name":"Write",
  "tool_input":{ "file_path":"/abs/path.js", "content":"<Write>", "new_string":"<Edit>", "command":"<Bash>" } }
```
A pre-write hook reads content via `jq -r '.tool_input.content // .tool_input.new_string // empty'` and the path via `.tool_input.file_path`; a pre-bash hook reads `.tool_input.command`; session id is `.session_id` (pass it to `emit_event` as `CLAUDE_SESSION_ID`).

**2. How a hook blocks.** The exit code is the signal: **`exit 2` = block and feed stderr back to the model** (use this for a safety violation — the model sees your message and self-corrects); `exit 0` = allow. Other non-zero codes surface as errors but may *not* block — so a safety hook that uses `exit 1` is fail-OPEN (Invariant #3 violated invisibly).

**3. How hooks are registered.** In `settings.json` under the host's event keys, with a command that resolves the hook via the git root *and guards against firing in unrelated repos*. Use this wrapper shape — the `|| exit 0` guards are load-bearing (without them the hook fires on every tool call in every repo the user ever opens):
```
bash -c 'ROOT=$(git rev-parse --show-toplevel 2>/dev/null) || exit 0; [ -d "$ROOT/.claude/hooks" ] || exit 0; "$ROOT/.claude/hooks/pre-write-validate.sh"'
```
Your bijection test (Phase 2c) parses hook filenames out of these strings, so keep the `.claude/hooks/<name>.sh` substring intact.

**4. The concrete tools this recipe assumes** (swap for equivalents, but the choice ripples): test runner = **Jest** (the worktree trap in Phase 3 is specifically Jest's `testPathIgnorePatterns`); embedded store = **better-sqlite3** (a native addon — its build toolchain must exist at bootstrap); PR CLI (Phase 7 only) = **gh**. Pick differently and the *shapes* below still hold, but the worktree workaround and the seeder API differ.

---

## Phase 1 — The Spine: state-path resolver + event log

**Goal:** one helper that decides *where* state lives, and one that appends structured events. Everything downstream sources these. Build them first or you retrofit them painfully.

**Build `.claude/hooks/_meta-mode.sh`** — a sourced helper (not an entry point) that every other hook sources first. It MUST:
- Set `PROJECT_ROOT` by anchoring to **its own file location**, not `cwd` and not `git rev-parse` (Invariant #2 — this is a *sourced* artifact): resolve the helper's directory via `"$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"` and walk up to the repo root (e.g. two levels up from `.claude/hooks/`). Respect an already-set `PROJECT_ROOT` if a caller exported one. Anchoring a sourced hook via `git rev-parse` is the doubled-`.claude/.claude/logs` orphan bug waiting to happen — do not.
- Choose a state directory: if `$PROJECT_ROOT/.meta/` exists, use it; otherwise use `$PROJECT_ROOT/.claude/`. Export `CLAUDE_STATE_DIR`, and derived paths `CLAUDE_LOGS_DIR`, `CLAUDE_PENDING_ACTIONS`, `CLAUDE_LEARNINGS` beneath it. Guard against a doubled `.claude/.claude/` prefix in the result.
- Why the `.meta/` split: it lets the maintainer keep private, messy session state out of what a cloner sees, using the *same hooks*. A cloner has no `.meta/`, so everything routes to `.claude/`. Decide this now; bolting it on later means touching every hook.

**Build `.claude/hooks/_emit-event.sh`** — defines an `emit_event TYPE [PAYLOAD_JSON]` function. It MUST:
- Append exactly one JSON object per line to `$CLAUDE_LOGS_DIR/events.jsonl` (create the dir if needed).
- Produce this envelope, merging any payload keys in:
  ```json
  {"timestamp":"<UTC ISO-8601>","event_type":"<TYPE>","session_id":"<id or 'unknown'>", ...payload}
  ```
- Build the line with `jq` (so payloads are always valid JSON). If `jq` is absent, **drop the event silently and return success** — this helper is non-safety, so it must never crash the hook that called it. (Contrast Invariant #3: *safety* hooks fail closed; this *observability* helper fails open. Know which kind each hook is.)

**Why one log, one helper:** fragmented event stores are how you end up unable to answer "how many times has loop X actually fired?" — the exact question that later tells you whether your harness works. One file, one writer function, forever.

### Acceptance Gate 1
Source both helpers, emit a test event, read it back:
```bash
cd "$(git rev-parse --show-toplevel)"
( source .claude/hooks/_meta-mode.sh && source .claude/hooks/_emit-event.sh \
  && emit_event test_event '{"phase":1}' && tail -1 "$CLAUDE_LOGS_DIR/events.jsonl" )
```
**Pass when ALL hold** (each is something that, if broken, must fail this gate — not a smoke check):
- The last log line is valid JSON (`jq -e .`) containing all four envelope keys: `timestamp`, `event_type` (=`test_event`), `session_id`, and the merged `phase` (=1). A missing key means the envelope is wrong — don't accept "a line got written."
- `CLAUDE_LOGS_DIR` resolved under the repo root with **no doubled `.claude/.claude/`** segment (echo it and check) — the orphan-path bug only shows when you look.
- **Both routing branches work:** run the source step once with a `.meta/` dir present and once without; confirm the log lands under `.meta/logs` then `.claude/logs` respectively. The no-`.meta/` branch is the one a cloner hits and the one you'd otherwise never exercise.
- If `jq` is missing, install it first (Invariant #3) — do not build safety hooks on a machine without it.

---

## Phase 2 — First hooks + the compiler that keeps them honest

**Goal:** a few real safety hooks, registered with the host, *and the meta-contract test that proves every hook is wired.* Build the test alongside the first hook — not later. It is the cheapest insurance in the harness and the embodiment of Invariant #1.

### 2a. The hooks (PreToolUse / PostToolUse)
Build these as scripts under `.claude/hooks/`, each sourcing `_meta-mode.sh` then `_emit-event.sh` first. The host (see Host Assumptions at the end) invokes them with the tool-call payload as JSON on stdin; a hook signals "block" by exiting non-zero with a message on stderr.

- **`pre-write-validate.sh`** (runs before Write/Edit). MUST block (`exit 2`) when the proposed content contains a hardcoded secret AND the target is a real source file. MUST hard-fail if `jq` is missing (Invariant #3 — this is a *safety* hook). SHOULD enforce any house rule you want guaranteed (e.g. a 2-line `ABOUTME` header on source files) — what you put here is enforced, not hoped for.
  - *Secret classes to detect (minimum):* cloud keys (`AKIA…`, Google `AIza…`), provider tokens matching your stack's prefixes, PEM private-key headers (`-----BEGIN … PRIVATE KEY-----`), generic high-entropy `assign = "long-base64ish"`, and any account/auth identifiers specific to your platform. Maintain the patterns as data (a config file), not inline regex, so they're extensible without editing the hook.
  - *The source-vs-exempt classifier (define as globs, both this hook and the flywheel use it):* treat as **source** (enforce) anything under your code dirs; treat as **exempt** docs (`**/*.md`), test fixtures (`**/*fixture*`, `**/__tests__/**` sample data), lockfiles, `.env*` examples, and the harness's own state files (`.claude/**/*.json`, `.meta/**/*.json`) — the latter hold content hashes and ids that read as high-entropy and would otherwise self-trip the generic detector. A secret-shaped string in a fixture, doc, or harness state file must NOT block, or the hook trains people to bypass it.
- **`pre-bash-validate.sh`** (runs before Bash). MUST block destructive/unsafe commands you never want run unguarded: `git commit --no-verify`, force-push to your default branch, reads of credential files. Provide a documented bypass env var per check (see the bypass-governance note below) and emit a `bypass_used` event whenever one fires.
- **`post-write.sh`** (after Write/Edit). Append the touched file path to a session file list (`$CLAUDE_STATE_DIR/.sessions/<session_id>.files`) — the docs flywheel (Phase 6) reads this. Emit an event.
- **`post-bash.sh`** (after Bash). Log the command class; on a detected `git commit`, trigger commit attribution (Phase 6).

**Register them** in `.claude/settings.json` under the host's hook-event keys (PreToolUse/PostToolUse with `Write|Edit` and `Bash` matchers). The registration command MUST resolve the hook path via the git root, not a relative path, so it fires identically from any worktree or subdir (Invariant #2).

**Bypass governance (build this with the hooks, not after):** every block needs a documented escape hatch, tiered. Tier-1 bypasses (those that disable a core safety boundary) require the human's explicit say-so and you should never self-authorize them; Tier-2 bypasses (quality gates) an agent may use autonomously but every use emits a `bypass_used` event. Keep the tier list in one place. The point: escape hatches must exist (or people rip the hooks out) and must be auditable (or they erode silently).

### 2b. `hook-registry.json` — the source of truth
A JSON catalog: a `trigger_types` map describing each way a hook can be wired, and a `hooks` array with one entry per hook. Each entry MUST carry enough to verify its wiring. Use exactly these shapes:

```json
{
  "trigger_types": {
    "settings": "Registered in settings.json under a hooks.<Event> matcher",
    "settings-local": "Registered in a gitignored local settings file (per-developer)",
    "invoked-by-hook": "Called by another hook; 'invoked_by' names the caller",
    "command-invoked": "Called by a slash command; 'invoked_by' names it",
    "git-hook": "Installed into .git/hooks/ by bootstrap; 'installed_as' names the path"
  },
  "hooks": [
    {"file":"pre-write-validate.sh","trigger":"settings","event":"PreToolUse","matcher":"Write|Edit","purpose":"Block secrets + enforce house rules"},
    {"file":"prepare-commit-msg","trigger":"git-hook","installed_as":".git/hooks/prepare-commit-msg","purpose":"Inject flywheel trailers"},
    {"file":"post-commit-attribution.sh","trigger":"invoked-by-hook","invoked_by":"post-bash.sh","purpose":"Attribute commits to suggestions"}
  ]
}
```
Convention: helper libraries whose names start with `_` (e.g. `_meta-mode.sh`) are sourced, not entry points, and are **excluded** from the registry and the bijection below. (The three entries above are *illustrative of the shape* — your registry at Phase 2 lists only the hooks that exist now; add the `git-hook`/`invoked-by-hook` entries when you build those files in Phases 4/6, so the registry never references a file that doesn't yet exist.)

### 2c. The hook-bijection test — the compiler
Write a test that reads `.claude/hooks/*.sh`, `hook-registry.json`, and `settings.json`, and asserts **all four** of these, **bidirectionally**. (It lives in the Phase-3 meta suite, but you need a *minimal way to run this one test now* to clear Gate 2 — a bare `<test-runner> <thisfile>` invocation is enough; the full curated runner comes in Phase 3.)
1. Every non-`_` hook file on disk has a registry entry. (Catches "wrote a hook, never wired it.")
2. Every registry entry resolves to a file that exists. (Catches "deleted/renamed a hook, left a dangling claim.")
3. Every entry with `trigger: settings`/`settings-local` actually appears in the corresponding settings file. (Catches "registered but the host never calls it.")
4. Every entry with `trigger: git-hook` is actually referenced by `bootstrap.sh`'s install step. (Catches the "registered + claims to be installed, but bootstrap never installs it" failure — a git-hook that is wired in the registry yet dead on every real clone.)
One direction alone is a blind spot; you need both "disk → registry" and "registry → disk + reachable."

### Acceptance Gate 2
1. **Safety hooks, per-guard:** for each guard (secret, `--no-verify`, force-push, cred-read), trigger with a payload that SHOULD block → confirm `exit 2` + stderr message; trigger the benign counterpart → confirm `exit 0`. Test the *exclusion* too: a secret in a doc/test fixture must NOT block. Confirm a fired bypass emits a `bypass_used` event.
2. **Prove the bijection test red-on-defect** (one orphan check is not enough). Induce each defect for the trigger types that *exist at this phase* — watch the named failure, repair, watch green:
   - add an unregistered `.claude/hooks/orphan.sh` → red naming `orphan.sh` (disk→registry);
   - add a registry entry pointing at a missing file → red naming the dangling entry (registry→disk);
   - mark a `settings` hook but remove it from `settings.json` → red (registered-but-unreachable).
   The fourth direction — `git-hook` entry not referenced by a bootstrap installer — can't be induced until bootstrap and a git-hook exist, so its red-on-defect proof is **deferred to Gate 4**. Build the check now; prove it then.
**Pass when:** every safety guard blocks-on-bad / passes-on-good / honors-exclusions, AND each bijection defect runnable at this phase produces a distinct red that names the offending artifact, each going green on repair. (The git-hook direction is proven at Gate 4.) A bijection test you have never seen fail has blind spots — Invariant #1 applies to your tests too.

---

## Phase 3 — The meta-contract suite + the local gate

**Goal:** a single command (`npm run test:meta`) that runs all your wiring tests fast, and a git `pre-push` hook that runs it so dead wiring can't be pushed.

### 3a. The runner (`scripts/run-meta-tests.js`, wired to `npm run test:meta`)
A small runner that executes your meta tests (the bijection test from Phase 2 plus the ones added in later phases) and any hook *behavioral* shell tests, reporting one pass/fail summary. It MUST:
- **Anchor to the git root**, and if your test framework excludes worktree paths by default, override that. *This is the trap that will cost you a day if you skip it:* a stock test runner config often ignores nested/worktree directories, so running tests from inside a worktree silently matches **zero files** and reports green. Your runner MUST resolve the test root from `git rev-parse --show-toplevel` and force the framework to look there (e.g. pass an explicit root/config rather than relying on auto-discovery). Verify by count: the runner must assert it actually executed N>0 tests, never treat "0 tests found" as success.
- **Curate which shell/hook tests gate by an explicit allowlist, not a glob.** A glob over your hooks' test directory will eventually pick up a non-contract scanner that goes red for unrelated reasons and someone disables the whole gate. List the contract tests by name; when you exclude one, leave a one-line reason in the runner so the omission is visible, not silent.
- Be **fast** (target seconds). The gate runs on every push; a slow gate gets bypassed.

### 3b. The gate (`.claude/hooks/pre-push`, installed by bootstrap)
A git pre-push hook that runs `npm run test:meta`. It MUST:
- **Ship soft first.** Default to: on failure, print a loud warning and **exit 0** (allow the push). Gate on an env var (e.g. `META_GATE_SOFT=0`) to flip to **blocking** (exit non-zero) once you've watched it stay green in real use for a week or two. A gate that cries wolf on day one — before you know its false-positive rate — gets disabled, and then you have nothing. Soft-launch buys credibility.
- Provide a logged, one-shot bypass (`SKIP_META_TESTS=true`) that emits a `bypass_used` event.
- Print, on failure, the *specific* failing contract and the fix (e.g. "hook `X` on disk but absent from hook-registry.json — add an entry"). A bare assertion dump teaches nobody; a newcomer who trips this must be able to self-rescue.

**CI note (decide which world you're in):** if your host CI genuinely runs and supports required server-side checks, prefer that — a server check beats a client hook a contributor can delete. But verify CI actually fires before depending on it; some orgs disable it silently (workflows show "active" and never run), in which case a local git hook is your *only* enforcement primitive. Don't assume; check.

### Acceptance Gate 3
```bash
npm run test:meta            # from repo root AND from a fresh worktree
```
**Pass when ALL hold:**
- The runner reports N>0 tests and all green from **both** the main checkout and a fresh worktree (Invariant #2 in the runner).
- **Force zero-discovery and confirm it ERRORS, not passes:** point the runner at an empty/wrong test dir (or temporarily move the meta tests) → the runner must fail loudly ("0 tests found"), never report green. This is the silent-green trap; prove the guard fires, don't trust it.
- Break one wiring fact (unregister a hook) → `test:meta` goes red naming it.
- A `git push` with the soft gate prints the warning but succeeds; with `META_GATE_SOFT=0` it blocks. A stray *non-contract* failing test must not be on the gated allowlist (confirm the allowlist, not a glob, decides what gates).

---

## Phase 4 — Bootstrap: clone → working harness

**Goal:** one idempotent script that takes a fresh clone to a working harness, including installing the git hooks. Bootstrap is the only thing between a new user and the whole system; treat it as a first-class product and test it in a throwaway clone repeatedly.

**Build `scripts/bootstrap.sh`.** It MUST:
- Be **idempotent** — running it twice changes nothing the second time, and a second run must exit clean. (Test this explicitly; it's an easy gate to add.)
- **Verify prerequisites and fail loudly** on missing ones it can't install (Invariant #3): `jq`, `git`, your runtime, the test framework.
- **Install the git hooks** by copying/symlinking `.claude/hooks/pre-push` and `.claude/hooks/prepare-commit-msg` into `.git/hooks/`. **This install step is load-bearing and MUST be referenced by the bijection test (Phase 2c, check #4):** a git-hook that is registered but not installed by bootstrap is "wired" in the registry and dead on every real clone. Bind the registry claim to the bootstrap reality with a test, and use exactly ONE authoritative installer (if you ship two bootstrap paths, both must install every git-hook, or the bijection check must target the one a real clone runs).
- **Seed the semantic memory store** (Phase 5) from the repo's docs so a fresh clone starts knowledgeable, not empty.
- Print a clear "you're ready / here's what's next" summary.

**Then build a clone test** (`scripts/fresh-install-validation.sh` or similar) that copies the repo to a temp dir, runs bootstrap there, and asserts: install succeeded, hooks landed in `.git/hooks/`, `test:meta` runs green, and a second bootstrap run is a no-op. This is your single highest-value adoption test — it's the literal experience of the friend you're writing this for.

### Acceptance Gate 4
Run the clone test against a `/tmp` copy of the repo. **Pass when ALL hold** (assert function, not just presence):
- Bootstrap exits clean on a fresh clone; the git hooks are present in `.git/hooks/` AND **executable AND on the path a real commit/push invokes** (a present-but-non-executable or mis-pathed hook is the dead-hook failure — `git push` in the clone must actually trigger the gate).
- `test:meta` is green in the clone.
- **Memory was actually seeded:** the store has >0 rows after bootstrap (an empty seed that "succeeds" is a false-green — assert the row count).
- **Idempotency by content, not self-report:** a second bootstrap run changes nothing — verify by hashing the tracked tree before/after, not by trusting a "no changes" message.
- **Prereqs fail loud:** run bootstrap with `jq` removed from PATH → it must exit non-zero with a clear message, never proceed.
- **The deferred 4th bijection direction (from Gate 2), now provable:** remove a `git-hook` entry from the bootstrap installer's list (or rename it) → the bijection test goes red naming the un-installed hook; restore → green. This is the "registered but never installed" defect, and only now (bootstrap + a git-hook both exist) can it be induced.
- The main repo is untouched, and seeded artifacts (the memory DB, logs) are gitignored so the clone stays clean.

---

## Phase 5 — The system prompt (CLAUDE.md) and memory

**Goal:** the always-loaded behavioral prompt, plus a two-tier memory so the harness remembers across sessions.

### 5a. `CLAUDE.md` — behavioral, not encyclopedic
This file loads into every session; it is your most consequential and most-abused surface. It MUST:
- Be **behavioral, not a catalog.** Tell the agent *how to act*, not *what every subsystem contains*. Keep it short — target ~150 lines; quality goes *up* when you cut it, not down. Reference data lives in on-demand files, not here. (A capable agent's instinct is to write an exhaustive CLAUDE.md; resist it. Long context here is paid on every single turn.)
- **Front-load the behavioral invariants** (use existing tools before building new ones; minimum-viable change; state assumptions before acting; surgical edits only). Top-of-file gets the most attention weight on some model families and is where a reader looks first regardless.
- State an explicit **precedence ladder** so conflicts resolve deterministically: hook enforcement (can't be argued with) > root CLAUDE.md > path-scoped rules > domain CLAUDE.md > commands > skills > memory. Conflicts *will* accumulate; without a ladder the agent has no way to resolve them.
- Use **path-scoped rule fragments** (`.claude/rules/*.md` with a path-glob in frontmatter) for domain-specific invariants, so they load only when editing matching files. The always-loaded surface is your largest recurring per-turn cost; push everything conditional out of the root.
- **Point to a doc navigator** (a "where does X live" table the agent reads on demand) rather than inlining docs.

### 5b. Two-tier memory
**Tier 1 — auto-memory (always in context).** A `MEMORY.md` index loaded each session plus one file per fact. Each fact file has frontmatter: `name`, `description` (this is what's matched for relevance, so write it well), `type` ∈ {`user`, `feedback`, `project`, `reference`}, optional `last_reviewed: YYYY-MM-DD`. The **`feedback`** type — corrections *with the reason why* — is the highest-value: it's how the harness stops repeating a mistake. The index is the per-turn cost, so one line per memory; the content goes in the file, never the index.

**Tier 2 — semantic store (queried on demand).** A small local store (an embedded SQL DB is plenty) holding chunks of *your own docs*, searchable by relevance. It MUST:
- **Seed from docs you already maintain** — operational gotchas, design decisions, domain CLAUDE.md files. There is **no separate "corpus" to hand-author**; the docs are the corpus. *Chunker rule:* split on blank-line (paragraph) boundaries; drop chunks that are pure YAML frontmatter, a lone heading, an HTML comment, or a template placeholder; drop chunks below a minimum length (~a sentence — e.g. <40 chars) so noise doesn't dilute relevance. (Don't overthink search: keyword/TF-IDF relevance over your own markdown is enough — you do **not** need embeddings or a vector DB to start. The instinct to reach for a heavyweight vector store is premature; skip it until a real need forces it.)
- **Dedupe idempotently** so re-seeding on every bootstrap is safe — a uniqueness constraint on `(namespace, content)` and insert-or-ignore. Namespaces: `gotchas`, `project`, `feedback`, `reference`.
- Be **seeded by bootstrap** (Phase 4) so a clone starts knowledgeable.

**Memory hygiene as a loop, not a chore.** The `last_reviewed` field feeds an oldest-first review; nag when entries pass ~90 days. When a memory becomes true-for-anyone (not just the current human), **promote** it out of personal memory into a project-scoped `rules/*.md` or a domain `CLAUDE.md`. Memory is a staging area; durable general knowledge graduates into the repo (and then gets re-seeded into Tier 2 from there — closing the loop with 5c).

**The connection that makes it pay off:** seed Tier 2 from the same canonical docs the flywheel (Phase 6) promotes *into*. A promoted learning becomes both a doc edit and a re-seedable memory, so the two knowledge bases reinforce each other instead of drifting apart.

### Acceptance Gate 5
- Confirm `CLAUDE.md` loads, is under your line budget, and contains the precedence ladder; confirm a path-scoped rule is **absent** when editing a non-matching file and **present** when editing a matching one.
- **Two-sided retrieval (not one cherry-picked hit):** seed the store, then (a) query a term from a known gotcha doc → that doc's chunk is the *top* hit; (b) query an unrelated nonsense term → it does NOT return that chunk (or returns nothing above threshold). A store that returns the same chunk for every query passes a one-sided check and is useless; prove relevance discriminates.
- **Idempotency by hash:** re-seed and confirm the store is byte-identical (hash the rows), not merely "same size."
**Pass when:** a known query's expected doc is the top hit AND an unrelated query misses, re-seeding is hash-identical, and the path-scoped rule loads conditionally.

---

## Phase 6 — The documentation flywheel + claims ledger (self-documenting)

**Goal:** code changes generate doc-update suggestions automatically, commits get linked to the suggestions they satisfy, and load-bearing doc claims are bound to tests so they can't drift from reality. This is "self-documenting" made real — and the claims ledger is Invariant #6 made executable.

### 6a. Detection + the commit gate (`flywheel-doc-check.sh`)
A hook (or post-write step) that detects when changed code should trigger a doc update. It MUST:
- Aggregate changed files from multiple sources: uncommitted (`git status`), commits since session start (`git log --since`), and the session-touched list `post-write.sh` maintains.
- Map changed paths to the docs that describe them via simple rules, defined as **data you can extend** (a list of `path-glob → target-doc` pairs). Seed it with the obvious general rule so it works in a fresh repo before you have domain dirs: **`<dir>/** → <dir>/CLAUDE.md`** (any change under a directory suggests updating that directory's `CLAUDE.md`), plus `.claude/hooks/** → CLAUDE.md` (hook changes may affect the system prompt). Add domain-specific pairs as the codebase grows. (For Gate 6, this general rule means touching *any* tracked dir with a `CLAUDE.md` produces a suggestion — so the gate is runnable on day one.)
- **Skip any suggestion whose target file doesn't exist** — otherwise it nags forever about deleted/renamed docs.
- Debounce, and **exclude its own outputs** (`pending-actions.json`, the session files) from the changed set, or it re-fires on itself.
- Write suggestions to `$CLAUDE_PENDING_ACTIONS`. **Pick ONE shape and use one writer** — two writers that disagree (a raw array vs an object) is a latent bug that surfaces only when both fire. Use this object shape:
  ```json
  { "actions": [
    { "timestamp":"<UTC ISO-8601>",
      "target":"functions/voice/CLAUDE.md",
      "reason":"voice handler changed; doc may be stale",
      "suggestionId":"<uuid>",
      "contentSha256":"<sha256 of reason, for fallback matching>" }
  ] }
  ```

**The commit gate:** `pre-bash-validate.sh` (Phase 2) MUST block `git commit` while `actions` is non-empty (with a logged bypass). This is the forcing function: you cannot ship with undrained doc debt without explicitly opting out. Entries are removed when their target is updated/attributed.

### 6b. Causal attribution (the part most people skip — and the part that makes the loop *measurable*)
Two git hooks close the loop from suggestion → commit → event:
- **`prepare-commit-msg`**: when staged files match a pending suggestion's `target`, inject a trailer `Flywheel-Suggestion-Id: <uuid>` into the commit message (cap at ~5, idempotent).
- **`post-commit-attribution.sh`** (invoked by `post-bash.sh` on commit): read the trailer and emit a `commit_attributed` event linking suggestion → commit. Include a **content-hash fallback**: if no trailer but the commit body matches the suggestion's `contentSha256`/reason, attribute anyway — so commits authored outside the agent still close the loop.

**Honest expectation-setting (so you don't over-claim per Invariant #6):** this mechanism is what makes "doc updates traceable to a suggestion" a *number you can trend* — but only once it's fired enough times to trend. Build it for the property; report it as "instrumented," not "proven," until the events accumulate. Don't write "the harness measures its own doc-drift closure" in a README until the count supports it.

### 6c. The learnings cycle (capture → promote → clear)
Session discoveries go into `learnings.md` immediately. Stable ones get **promoted** into permanent docs (design decisions, domain CLAUDE.md, rules) — and from there re-seeded into Tier-2 memory (5b). A pre-write hook MUST **block clearing `learnings.md` unless `learnings-archive.md` was just updated** — you cannot delete a learning without archiving it. This stops "capture" from being a black hole.

### 6d. The claims ledger (`claims.json`) — Invariant #6, executable
A ledger binding each load-bearing capability claim in your README/onboarding to an assertion a test checks. Two assertion kinds; **at least your single most-important claim MUST be `behavioral`** (the rest may be `grep` while you build up). Shapes:
```json
// behavioral: the test RUNS the mechanism and asserts an outcome. The strong form.
{ "id":"commit-gate-blocks-on-pending-docs",
  "source_doc":"README.md",
  "text_token":"blocks commits while documentation is pending",   // distinctive multi-word phrase
  "claim":"An open doc-update suggestion blocks the next commit.",
  "status":"backed",
  "assertion_kind":"behavioral",
  "assertion":"with a non-empty pending-actions.json, the pre-bash hook on a `git commit` exits 2",
  "assertion_test":"__tests__/.../claims/commit-gate.test.js" }   // a real test that performs the action

// grep: the test only checks a string is present. Proves the symbol EXISTS, not that it WORKS.
{ "id":"aboutme-header-enforced",
  "source_doc":"README.md",
  "text_token":"every source file carries an ABOUTME header",
  "claim":"The pre-write hook enforces a 2-line ABOUTME header.",
  "status":"backed",
  "assertion_kind":"grep",
  "assertion_file":".claude/hooks/pre-write-validate.sh",
  "assertion_pattern":"ABOUTME" }
```
A meta test asserts, per claim: (1) the `text_token` still appears in `source_doc` (the claim wasn't silently deleted); and (2) the assertion holds — for `behavioral`, the named `assertion_test` passes (it actually exercises the mechanism); for `grep`, `assertion_pattern` appears in `assertion_file`.

**The trap to avoid:** a `grep`-kind assertion only proves a *symbol exists*. It catches a claim whose code was *deleted*, but NOT a claim that has *outrun its code*: backing a "self-healing" claim with a grep for a function name leaves the test green while the feature has never run, a hollow claim that passes. Two rules. **(a) Use distinctive multi-word `text_token`s**; a one-word token like `ABOUTME` that appears in hundreds of files proves nothing. **(b) Make the marquee claims `behavioral`** — invoke the mechanism and assert an outcome, even if only for your one or two most important claims. A behavioral assertion on your single most-important claim is worth more than grep coverage on ten. If the strongest assertion you can write today is a grep, *scope the claim's wording* to what the grep proves and put the rest on the roadmap (Invariant #6).

### Acceptance Gate 6 (induce every defect — do not smoke-test)
- **Flywheel:** touch a mapped source file → confirm a suggestion lands in `pending-actions.json` with the shape above AND `git commit` is now blocked; drain it → confirm commit allowed again.
- **Attribution, both paths independently:** (a) commit with the injected trailer → `commit_attributed` event with method `trailer`; (b) commit a paraphrase WITHOUT the trailer → `commit_attributed` via the content-hash fallback. The fallback is the valuable half; prove it fires on its own.
- **Claims test, red-on-defect in both directions:** for your `behavioral` marquee claim — (1) delete its `text_token` from the doc → red; (2) **break the actual mechanism** (e.g. make the pre-bash gate not block) → the `assertion_test` goes red *while the text_token still present* — proving the test catches a claim that has outrun its code, not just a deleted string. Confirm the two reds are independent.
- **The behavioral requirement is a gate condition, not advice:** confirm at least one claim has `assertion_kind:"behavioral"` and that its `assertion_test` actually performs the action (not a grep). A 100%-grep ledger fails this gate.
**Pass when:** the commit gate blocks-and-releases, *both* attribution paths emit independently, and breaking the marquee claim's *mechanism* (not just its text) turns the suite red.

---

## Phase 7 — The Durable Substrate (what actually gets used most)

> Of everything in the harness, this gets used the most. In a mature harness, memory recall fires more often than any command or lifecycle action and returns a relevant hit nearly every time — the cross-session memory of *what was decided and why* is the highest-traffic capability. Build this substrate well; it's what makes every future session start informed instead of cold.

Four artifacts, each a place where knowledge that would otherwise evaporate gets persisted and made re-findable:

**7a. A referenceable decisions log (`DESIGN_DECISIONS.md`).** One append-only file, one entry per architectural decision, each with a stable ID (`D1`, `D2`, …) and a fixed template: Context / Decision / Rationale / Alternatives Considered / Consequences / Status. Decisions are never deleted — only marked `Superseded by Dxx`. This is the spine the agent greps when it asks "why is it this way?"; it's also a prime seed source for Tier-2 memory (Phase 5). MUST: a new decision is a new entry, never an edit to an old one; a changelog table maps date→ID→summary.

**7b. Promoted-learnings flow.** Session discoveries land in `learnings.md` (Phase 6c); the ones that prove durable get *promoted* — copied into `DESIGN_DECISIONS.md`, a domain `CLAUDE.md`, or a path-scoped rule — and then re-seeded into Tier-2 memory. The promotion is the moment provisional knowledge becomes canonical. MUST: promotion is a real move (the learning leaves `learnings.md` for a permanent home), guarded by the archive-before-clear hook so nothing is lost in transit.

**7c. Pre-compaction summaries.** Long agent sessions hit context compaction; without intervention, the working state (what was being done, what was decided this session, open threads) is lost. A `PreCompact` hook MUST capture a structured snapshot — branch, modified files, active plan, pending actions, and a short session summary — to the state dir AND persist a summary into Tier-2 memory, so a post-compaction (or next) session can recover its bearings by recall instead of re-reading the whole transcript. (Build exactly one hook for this; don't invent a separate "post-compact" hook to pair with it — a single `PreCompact` capture is sufficient, and a doc that references a non-existent paired hook is itself the dead-wiring this harness exists to prevent.)

**7d. Archived plan files.** When the agent plans (in plan mode), that plan is reasoning worth keeping. On session end, archive the active plan to a dated location with metadata (branch, session id). Future sessions — and `DESIGN_DECISIONS` entries — reference them. MUST: archival is automatic (a session-end/Stop hook), deduped by content hash so re-archiving the same plan is a no-op.

**The loop that ties 7a–7d to Phase 5:** everything persisted here is a *seed source* for Tier-2 memory. Decisions, promoted learnings, and compaction summaries all get chunked into the semantic store, which is why recall stays productive: the store is fed by the substrate the harness writes as a byproduct of working. Persistence + seeding + recall is one loop, not four features.

### Acceptance Gate 7
- Add a decision to `DESIGN_DECISIONS.md`, re-seed memory, and recall a phrase from it → it comes back as a hit (proves the decisions-log → memory loop).
- Trigger the pre-compaction capture (simulate the `PreCompact` event) → confirm a snapshot file exists with branch/files/plan AND a summary chunk is recallable from memory.
- End a session with an active plan → confirm it's archived with metadata, and a second archive of the same plan is a no-op (content-hash dedup).
**Pass when:** a written decision is recall-able after re-seed, a compaction snapshot is captured and recall-able, and plan archival is automatic + idempotent.

---

## Phase 8 — The Commands & Workflows Worth Building

> The review commands earn the most use of any named capability, and parallel subagent fan-out (explore / general-purpose / plan agents) is the harness's actual superpower — it dwarfs single-command use. The point most builders miss: **formalize the recurring fan-outs as saved workflow scripts instead of hand-spawning agents every time.** A fan-out you run more than twice should be a script, not a ritual.

**8a. The `*-review` pattern (the highest-value named capability — build the pattern, then its instances).** Every review command is the same shape, and it's the shape worth internalizing: **fan out to N *isolated* reviewers with distinct lenses → synthesize with a verifier that has tools.** The invariants that make it work (don't violate them):
- **Isolation.** Reviewers must not see each other's output, or they converge and you get one opinion N times. Spawn them in parallel from the same brief, not in sequence.
- **Distinct lenses, not duplicate reviewers.** Same-model reviewers share blind spots; the value comes from *different attack angles* (advocate vs critic; or personas like security/onboarding/drift), not more of the same.
- **A tool-grounded synthesizer.** The final stage doesn't just average opinions — it *verifies contested claims* (fetch the URL, read the code, run the count). Opinion-only synthesis inherits the reviewers' blind spots; tool-grounded synthesis breaks past them. Run it on a *different model* where you can, for partial error decorrelation.
- **Three roles, not five.** Accuracy peaks at 2–3 independent passes and *drops* after (extended same-model debate converges on shared wrong answers). If results disappoint, give the synthesizer better tools or a narrower scope — never add rounds.
- **A calibration pass at synthesis (report-only, never a gate).** After the synthesizer forms its verdict, have it answer two questions *about that verdict* and render them for the reader: (1) the specific calls it is least confident in, ranked by impact-if-wrong, each with the one check that would resolve it; and (2) a premortem — the biggest thing the requester likely doesn't realize, answered from the model's outside vantage (it read every lens; the human holds context it lacks). This surfaces the known-unknowns and frame-errors a clean-looking report otherwise buries. Keep it report-only — it informs, it never blocks — and make it artifact-specific: a generic "I might be missing edge cases" is a failed pass, worse than none, because it manufactures the *appearance* of calibration.

The instances worth building (they're all the pattern above — build the ones you'll use):
- **`/adversarial-review`** — a decision ("should we do X?"): advocate + critic in isolation → arbiter that verifies. Use it on this recipe itself: a tool-grounded arbiter that counts the event log will catch any claim that outruns the evidence.
- **`/harness-review`** — the harness attacking *itself*: personas for closed-loop claims, hook bypassability, context cost, onboarding friction, aspirational-vs-real language, drift. Run on a schedule; survived findings become backlog.
- **`/uber-review`** (broad code review across many personas) and **`/hostile-review`** (adversarial "what embarrasses us in front of users") — heavier fan-outs of the same pattern for pre-ship gauntlets.
- **`/review`** — the single-reviewer pipeline phase (see 8d), for routine change review where the full fleet is overkill.
The discipline: pick the review weight to match the stakes, but every one of them is *isolated-lenses → tool-grounded synthesis*.

**8b. Author the review fleets as Workflows, not ad-hoc spawns.** If your host supports deterministic multi-agent orchestration (Claude Code's `Workflow`), the review fleets are the *textbook* use: a fixed fan-out (N isolated reviewers) → a synthesis/arbiter stage. A saved workflow script makes them repeatable, parameterizable, and cheap to re-run, instead of re-orchestrating agents by hand every time. MUST: any fan-out you run more than twice becomes a script. Good first candidates beyond the reviews: a docs-accuracy sweep, a multi-file audit, a parallel-explore-then-map of an unfamiliar subsystem.

**8c. The lifecycle commands (daily-driver ergonomics).** These encode the multi-step rituals so they're done right every time:
- **`/wrap-up`** — end-of-session: flush learnings, surface pending doc actions, update the decisions log, archive the plan (drives Phase 7's substrate).
- **`/ship`** — the full land sequence: commit → PR → CI/checks → merge → exit the worktree cleanly. Separates intent from `/commit` (save) and `/push` (share). Worth building because the worktree-cleanup footguns (exiting before branch delete, orphaned worktrees) are exactly the kind of error a command prevents and a human repeats.
- **`/recall`** — surface accumulated memory/decisions for a topic before starting work. This is the substrate's primary read path and the harness's highest-traffic action; make it one keystroke.
- **`/session-crons`** — register the recurring maintenance checks (Phase 7 hygiene nags, drift checks) as **session-start checks** so upkeep doesn't depend on anyone remembering. Resist the instinct to wire these as persistent/scheduled "cron" tasks: on this host durable scheduled tasks fire unreliably (session-scoped and idle-only), so the check that runs at the *start of a session* is the one that actually executes. If you built a `/durable-crons` first, this replaces it.

**Merge discipline (once more than a trickle of changes land at once).** The bottleneck on throughput is a single reviewer/approver, and append-only "hub" files that nearly every change edits — a decisions log, a registry, a doc index/navigator — conflict pairwise even when the real changes are unrelated. Two rules keep a burst of work from becoming a self-inflicted merge jam: cap how many changes are in flight at once to what one approver can actually clear, and **serialize edits to hub files** — land a hub-file change on its own and branch the next work off it, rather than editing the same append-only file from several concurrent branches. Rebase only the branches that truly conflict; a branch that is merely behind but still merges cleanly should be left alone (rebasing it only re-arms its review).

**8d. The development pipeline (how the human actually builds *on* the harness).** Everything above maintains the harness; this is the assembly line for building features with it. Encode each stage as a command the agent invokes — **never** a program that drives the agent (Invariant #4). The stages, in order:

`idea → /architect → /spec → /test-gen → /dev → /review → /docs → /ship`

- **`/architect`** — turn a rough idea into a design: surface the relevant existing code, choose the pattern, name the trade-offs, write or update a `DESIGN_DECISIONS` entry. The "look at what exists before building new" step that prevents reinvention.
- **`/spec`** — a precise, testable specification of the change.
- **`/test-gen`** — the **TDD red phase**: write the failing tests *first*, from the spec. This is the load-bearing stage — it's what makes "tested" structural rather than aspirational.
- **`/dev`** — the **green phase**: implement until the `/test-gen` tests pass; nothing more. (A pre-write hook from Phase 2 gates new source files on the existence of a matching test, so skipping `/test-gen` is *blocked*, not merely discouraged — the pipeline is enforced, not suggested.)
- **`/review`** — single-reviewer code review (escalate to the 8a fleet for high-stakes changes).
- **`/docs`** — update the docs the change touches (the Phase-6 flywheel will also have flagged them).
- **`/ship`** — land it (8c).

Two rules make the pipeline real rather than decorative: **(1) it's gated** — the test-before-code hook means an agent that tries to jump straight to `/dev` on a new function hits a block, so the discipline survives a hurried session; **(2) it's not mandatory for everything** — bug fixes, doc edits, and one-line changes skip it (forcing the full pipeline on a typo is the kind of ceremony that gets a harness resented). Reserve it for net-new functionality. The guardrail against the AI-agent instinct here: a capable agent *wants* to write the implementation immediately because it can; `/test-gen` first is the forcing function that turns that speed into tested speed instead of plausible-looking-but-unverified speed.

**Caution:** scope bypass/observability logging tightly. An emitter that fires on every *check* of a bypass var (rather than every actual *use*) buries the signal you most need — how often a gate is genuinely overridden — under tens of thousands of no-op events. Log the override, not the check.

### Acceptance Gate 8
- Run `/adversarial-review` on a toy decision → confirm three isolated outputs (advocate, critic, arbiter) land, the arbiter cites at least one tool-verified fact, and the advocate/critic prompts did not see each other.
- If the host supports workflows: re-run one review as a saved workflow script and confirm identical structure with no manual agent-spawning.
- Run `/wrap-up` → confirm it touched the Phase-7 substrate (learnings flushed, plan archived, pending actions surfaced).
**Pass when:** the review command produces genuinely isolated roles with a tool-grounded arbiter, at least one recurring fan-out exists as a re-runnable workflow, and `/wrap-up` demonstrably feeds the durable substrate.

---

## Phase 9 — Optional: autonomous loops (only when a real need forces them)

Everything through Phase 6 is *detect-and-prompt*: the harness notices drift and asks the human (or the next session) to act. That is the right default and is genuinely useful on its own. **Autonomous action — the harness fixing things without a human — is a separate, optional step you should not take until a specific repeated toil forces it.** When you do, every autonomous mutation MUST follow the 4-element safety template (Invariant #5):

1. **Off-by-default flag at the entry point.** Autonomy is opt-in, per-operator, per-session. The default path stays detect-and-prompt.
2. **A pre-flight check immediately before the mutation** — re-verify the precondition at the moment of action (e.g. `gh auth status` before opening a PR; re-fetch-and-compare before changing a remote resource). State can change between decision and action.
3. **A reversal paired with the mutation** — define the undo in the same place as the do (delete the branch on PR failure; roll back on re-validation failure; write back only on success). If you can't write the undo, you may not write the mutation.
4. **A structured event at every checkpoint**, joinable by a stable id, so the loop is observable end-to-end. If you can't observe it, you can't claim it's closed.

**Two boundaries stronger than the flags:** (a) the autonomous mutator never deletes and never touches credentials — enforce with a hard-coded allow-list of action types, not discipline; (b) keep the mutator **architecturally incapable of touching the shell** — the component that shells out to `git`/`gh` lives in your tooling layer, and your API/data layer has zero ability to spawn processes. Structure contains blast radius even when a flag is misconfigured.

**Be honest about maturity (Invariant #6).** "Built and flag-gated" is not "battle-tested." Until an autonomous loop has run many times in anger, describe it as "capable of," not "does." A loop that has executed zero times is a roadmap item, not a feature — even if all the code exists.

### Acceptance Gate 9 (only if you build this phase)
- **Flag off:** the loop detects-and-prompts and performs **no** mutation. Confirm by checking no mutation event fires.
- **Flag on, in a sandbox:** pre-flight runs; the mutation happens; an induced failure triggers the reversal cleanly; every step emitted a joinable event.
- **Pre-flight efficacy (not just presence):** flip the precondition to false right before the action → confirm the loop **aborts**, doesn't mutate. A pre-flight that's never seen abort is unproven.
- **The structural boundaries hold:** attempt a *disallowed* action type (delete / credential touch) with the flag ON → confirm it's refused by the allow-list, not merely discouraged; and statically confirm the data/API layer cannot reach the shell (no process-spawn path exists from it).
**Pass when:** flag-off mutates nothing, flag-on mutates-and-undoes, pre-flight actually aborts on a falsified precondition, a disallowed action is structurally refused, and the whole sequence is reconstructable from events alone.

---

## Host Assumptions & Portability

This recipe assumes **Claude Code**. The mechanisms are portable, but the nouns are host-specific. Before building, map each primitive to your host's equivalent; if your host lacks the **blocking pre-tool hook**, most of the enforcement here is not reproducible — that is the one hard dependency.

| Recipe primitive | Claude Code form | The capability you actually need |
|---|---|---|
| Tool hooks | `settings.json` events `PreToolUse`/`PostToolUse`/`SessionStart`/`SessionEnd`(or `Stop`)/`PreCompact`, matchers `Write\|Edit\|Bash\|Skill` | Run a script before/after the agent's file-write, shell, and session-lifecycle actions (start, end, and pre-compaction), with the ability to **block** (non-zero exit). `SessionEnd`/`Stop` is what Phase 7d's plan-archive and `/wrap-up` hang off. |
| Git hooks | `pre-push`, `prepare-commit-msg`, `pre-commit` installed by bootstrap | Same enforcement at git's lifecycle points |
| Pipeline phases / skills | `.claude/skills/**`, `.claude/commands/*.md` the agent invokes | Named, on-demand procedures — **not** a program that drives the agent |
| Path-scoped rules | `.claude/rules/*.md` with a path glob in frontmatter | Inject extra instructions only when editing matching files |
| Always-loaded prompt | `CLAUDE.md` | A per-session system prompt you control |
| Plan mode | Claude Code plan mode | A read-only planning pass before execution |
| Tooling deps | `jq`, `git`, an embedded SQL lib, a PR CLI | JSON-in-shell; git; a tiny local store; (optional) a PR tool for Phase 7 |

If a primitive has no host equivalent, either find the nearest analog or drop the feature that depends on it — and say so in your docs (Invariant #6), rather than claiming a capability the host can't deliver.

## Minimum Viable Harness (where to stop if you want 80% for 10%)

You do not need all nine phases to get most of the value. The full kit earns its place at scale (many services, an agent doing the bulk of commits); a smaller project should stop early and add the rest only when a *named* failure forces each piece. The minimum that is genuinely worth having, in order:

1. **Phase 1** — the event log + path resolver. (Cheap, and everything else assumes it.)
2. **Phase 2's bijection test + one or two safety hooks.** The bijection test is the single highest value-per-line artifact in this document; build it the moment you have two hooks.
3. **Phase 3's `test:meta` + soft pre-push gate**, and **Phase 4 bootstrap** so it survives a clone.
4. **One behavioral claim test** (Phase 6d) on your single most important capability — in preference to a broad grep ledger.

Stop there until something hurts. Add memory (Phase 5), the docs flywheel (6), the durable substrate (7), the commands and workflows (8), and autonomous loops (9) when, and only when, a specific recurring pain names the need. Apply the smell test at every step: *can you name the exact failure this piece prevents?* If not, don't build it yet.

## The Smell Tests (carry these the whole way)

- **"Would a senior engineer call this overcomplicated?"** If yes, simplify. You will delete more than you keep, and that's the system working.
- **"Can I name the specific failure this piece prevents?"** If not, cut it. Most meta-tooling is ceremony that makes the builder *feel* rigorous.
- **"If the maintainer vanished, what's the half-life before this misleads someone?"** Pieces with real enforcement (hooks, contract tests) survive neglect; pieces that rely on discipline (hand-updated registries, hand-run evals) rot. Push everything toward enforcement.
- **"Does the language outrun the implementation?"** Grep your own docs for "autonomous," "self-*," "closes the loop." For each, find the code *and check how many times it has actually run.* If it exists but has run zero times, the doc says "capable of," not "does." This is the easiest invariant to violate and the last one anyone notices — make the claims ledger (6d) enforce it for you.

---

*Build it in order. Stop at every red gate. The harness that results will tell you, loudly, the moment any part of it stops being true — which is the only property that makes a self-maintaining system actually maintain itself.*
