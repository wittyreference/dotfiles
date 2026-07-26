# Contributing to inkflow

## License hygiene — read this before borrowing code

inkflow is MIT, and it is meant to be upstreamable into [CrossPoint](https://github.com/crosspoint-reader/crosspoint-reader), which is also MIT. A single copyleft-derived function would make that impossible and would silently relicense the project. So the boundary is explicit.

**Ideas and functionality are not copyrightable. Expression is.** That distinction is the whole rule.

| Source license | What you may do |
|---|---|
| MIT, BSD, Apache-2.0, public domain | **Copy code**, with attribution in the file and in the README |
| GPL, LGPL, AGPL (any version) | **Study behaviour. Reimplement independently.** No code, no structure, no naming |
| No license file at all | **Same as copyleft, and stricter.** "No license" means all rights reserved — it is more restrictive than GPL, not less |
| `NOASSERTION` / unclear | Resolve it before using anything. Ask the author |

When studying a project you cannot copy from:

- Record **what** it does and how it feels to use — features, defaults, UX decisions, what its issue tracker shows it got wrong and fixed.
- Do **not** transcribe code. Do not carry over file organisation, function decomposition, naming, or comment structure. Those are expression.
- When you need a specific algorithm, get it from a **published paper, spec, or datasheet** — the SSD1677 datasheet, the reading-research literature — not from reading someone's implementation of it.
- Credit the inspiration in `docs/FEATURE-SURVEY.md`. It costs nothing and it is the honest record.

**The tripwire:** if a note you are taking starts describing *how* code is written rather than *what* it does, stop. You have crossed from studying into copying.

Specific projects in this ecosystem that are off-limits for code: `TernOS` and `TrustyReader` (GPL-2.0), `xtctool` and `XTEink-Web-Font-Maker` and `x4m` (GPL-3.0), `crosspoint-chinesetype` (AGPL-3.0), KOReader (AGPL-3.0), and `Crosspoint-Emulator`, `cr2xt`, `hojo`, and `sunwoods/Xteink-X4` (no license). Hardware facts from that last one — pinouts, board photos, register addresses — are data, not expression, and are fine to use.

## Test-driven development, not negotiable

Every behavioural change follows the cycle:

1. Write a failing test that defines the desired behaviour.
2. **Run it and confirm it fails, for the reason you expect.** A test that fails because it does not compile has not told you anything yet.
3. Write the minimum code to make it pass.
4. Run it again and confirm it passes.
5. Refactor while green.

A build-configuration failure is not a red test. Get to the point where the test *runs* and fails on behaviour before writing implementation.

Test output must be pristine. If an error is expected, capture and assert on it rather than letting it print.

There are no mock modes anywhere in this project. Real data, real files, real hardware.

## Code conventions

- **Every source file opens with two `ABOUTME:` comment lines** describing what it does. Greppable, and it means `head -2` on any file tells you why it exists.
- **Comments explain *why*, not *what*.** If the comment restates the code, delete it. The comments worth writing are the ones recording a decision, a constraint, or a trap — the things a reader cannot recover from the code.
- Match the style of surrounding code, even where it differs from an external standard. Consistency within a file beats global consistency.
- Smallest reasonable change. Don't bundle unrelated fixes.
- No names like `improved`, `new`, `enhanced`, `v2`. What's new today is old tomorrow.
- Commit in coherent atomic units, imperative mood. Don't wait until everything is done.
- **Never `--no-verify`.** If a hook fails, fix the underlying issue.

## The engine has hard constraints

`core/` ships to an ESP32-C3 with 400 KB of SRAM, no PSRAM, and no FPU. So inside `core/`:

- **No dynamic allocation.** No `new`, no `malloc`, no `std::vector`, `std::string`, or any container that allocates. The caller owns all buffers.
- **No exceptions, no RTTI.** Enforced by the `rsvp_core_freestanding` build target, which compiles the same sources with `-fno-exceptions -fno-rtti`. If you break it, the build tells you.
- **No floating point.** The chip has no FPU; floats become soft-float calls in the playback hot loop. Use integer arithmetic with percentages.
- **No I/O and no platform headers.** The engine computes; the platform layer reads and draws.
- Warnings are errors, including `-Wconversion` and `-Wsign-conversion`. They have already caught one real truncation bug. Don't disable them; fix the type.

Tests may allocate freely — they run on a host.

## Claims discipline

Don't write a capability claim in the present tense until a test defends it. Prose has no compiler, so "the reader supports X" in a README rots silently the moment X breaks. If something is built but unproven, say "capable of" and note that it has not been exercised. If it does not exist, it belongs in a roadmap, not in a description.

The README's **Honest status** section is the contract for this. Keep it accurate even when that is unflattering — especially then.
