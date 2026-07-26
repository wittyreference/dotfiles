# Feature survey — RSVP readers and the Xteink X4 ecosystem

Research snapshot: **2026-07-26**.

Two purposes. First, to find out what already exists so inkflow builds on it instead of rediscovering it. Second, to record the **license boundary** for each project, because inkflow is MIT and intends to be upstreamable into CrossPoint — a single copyleft-derived function would make that impossible.

## The rule this survey follows

**Ideas and functionality are not copyrightable. Expression is.** So everything below records *what* a project does and how it behaves, never how its code is written. For MIT-licensed projects that restraint isn't required, but the notes are written the same way regardless, so the boundary is never ambiguous. See [CONTRIBUTING.md](../CONTRIBUTING.md) for the enforceable version.

---

## Part A — What the research actually says

This section comes first because it reframes everything after it, and because most RSVP projects surveyed below don't mention it at all.

RSVP's premise is that eliminating saccades — the small jumps your eye makes across a line — should make reading faster and less tiring. **The evidence does not support the strong version of that claim.**

| Finding | Implication for inkflow |
|---|---|
| No significant comprehension difference vs. normal reading at **250, 300, and 350 WPM**. Above that, comprehension drops significantly ([PLOS One](https://journals.plos.org/plosone/article?id=10.1371%2Fjournal.pone.0153786)) | **The useful band is ~250–350 WPM, not 600+.** Design for that band and treat higher speeds as a knob, not a goal |
| Suppressing **regressions** — backward glances to re-read — measurably hurts literal comprehension. Regressions are 10–15% of normal reading time ([ScienceDirect](https://www.sciencedirect.com/science/article/abs/pii/S0747563214007663)) | **Rewind is not a convenience feature. It is the mitigation for RSVP's best-documented failure mode.** This is the strongest single justification for anything in this project |
| Suppressing parafoveal processing (previewing the next word peripherally) also hurts comprehension | Multi-word chunks partially restore this, which makes chunking a comprehension feature and not only a refresh-rate trick |
| RSVP may *increase* cognitive load rather than reduce it | Don't claim reduced fatigue. The honest claim is different — see below |
| At 600+ WPM, degradation is worst on **inferential** questions — the comprehension that integrates across long spans ([researchgate](https://www.researchgate.net/publication/328925418)) | High-WPM RSVP is worst at exactly the reading most worth doing. Cap the default well below it |

### Two consequences worth stating plainly

**1. The honest pitch for ADHD is not speed.** It is that a fixed focal point removes the *place-keeping* load — no line tracking, no losing your position, no re-reading the same paragraph because your attention drifted mid-line. That is an attention-and-executive-function benefit, and it is compatible with reading at an ordinary 300 WPM. Claiming speed gains would be overclaiming against the literature.

**2. This rescues the e-ink feasibility question.** The [platform matrix](PLATFORM-MATRIX.md) flags panel refresh latency as the project's central unknown, sized against a 600–900 WPM ambition. If the useful band is 250–350 WPM, the requirement is **171–240 ms per update** — comfortably more forgiving, and plausibly within reach of a windowed partial update on an SSD1677. Chunking two words at 300 WPM relaxes it to 400 ms, which is almost certainly achievable.

The measurement in `bench/eink-bench` still has to happen. But the target it has to hit just got roughly 3x easier, for a reason that comes from reading research rather than from wishful engineering.

---

## Part B — Open-source RSVP implementations

| Project | License | ★ | Stack | WPM | Chunking | ORP | Pause | **Rewind** | Resume | Notable |
|---|---|---:|---|---|:-:|:-:|:-:|:-:|:-:|---|
| [pasky/speedread](https://github.com/pasky/speedread) | **MIT** | 1300 | terminal | ±10% live | ○ | ● | ● | ○ | ○ | **Pause shows surrounding context.** Most-starred RSVP tool anywhere |
| [karpushchenko/koreader-rsvp-plugin](https://github.com/karpushchenko/koreader-rsvp-plugin) ("FastReader") | **MIT** | 34 | Lua / KOReader | 50–1000 | ● **1–10 words** | ● crosshair | ● | **○ undocumented** | ● per-page | Closest prior art. Fixed-width widget so words don't jump. Known limits: some PDFs have no extractable text; slows at high word counts |
| [sami-29/speeedy](https://github.com/sami-29/speeedy) | **MIT** | 41 | TS / Lit / PWA | 100–1600 | ○ | ● adjustable pivot offset | ● | ○ | ● | **Comprehension quiz to set baseline WPM.** OpenDyslexic, Irlen overlays, RTL, click/ambient audio, streaks |
| [the-happy-hippo/sprits-it](https://github.com/the-happy-hippo/sprits-it) | — | — | web | ● | ○ | ● | ● | **● rewind** | ○ | Mobile-browser focused, night mode |
| [syniuhin/Readily](https://github.com/syniuhin/Readily) | — | — | Android | ● | ○ | ● | ● | ? | ● | Inspired by Spritzer |
| [n-ivkovic/tspreed](https://github.com/n-ivkovic/tspreed) | **GPL-3.0** | 104 | POSIX shell | 1–60000 | ○ | ● `--focus` | ● | ○ | ○ | `--length-vary` scales timing by word length |
| [inattendu/dashreader](https://github.com/inattendu/dashreader) | — | — | plugin | ● | ○ | ● | ● | ? | ? | RSVP plugin |
| Streamer (Calibre plugin) | — | — | Calibre | ● | ? | ? | ● | ? | ? | [MobileRead thread](https://www.mobileread.com/forums/showthread.php?p=4578319) |
| Kindle "Word Runner" · Reedy | proprietary | — | — | ● | ● | ● | ● | ● | ● | The commercial baselines users compare against |

`?` means the README doesn't say. `○` means absent or undocumented.

### Pivot and timing algorithms, compared concretely

READMEs don't document these, so I read the source of the two MIT implementations. (Deliberately *not* the copyleft ones — see the rule at the top.) The numbers turn out to be the most useful thing in this survey.

**Pivot banding — inkflow matches the field's most-used implementation exactly.**

| Word length (chars) | [speedread](https://github.com/pasky/speedread) (MIT, 1.3k★) | [speeedy](https://github.com/sami-29/speeedy) (MIT) | **inkflow** |
|---|:-:|:-:|:-:|
| 1 | 0 | 0 | **0** |
| 2–5 | 1 | 1 | **1** |
| 6–9 | 2 | 2 | **2** |
| 10–13 | 3 | 3 | **3** |
| 14+ | 4 | **3** (capped) | **4** |

speedread's table is `(0,0,1,1,1,1,2,2,2,2,3,3,3,3)[len]`, with 4 for `len > 13` — **identical to ours**, arrived at independently from published research rather than by copying. speeedy is the outlier in capping at 3, though it lets the user nudge the pivot to 4.

**Timing weights — inkflow sits at the gentle end of a genuinely wide spread.**

| | speedread | speeedy | **inkflow** |
|---|:-:|:-:|:-:|
| Base | `0.9 × 60/wpm` | `60000/wpm` | **`60000/wpm`** |
| Sentence `.?!` | **×3.0** | ×2.0 | **×2.0** |
| Clause `,;:` | ×2.0 | ×1.4 | **×1.5** |
| Paragraph | — | separate multiplier | **×2.6** |
| Word length | `0.04 × √len` | ≥8 chars → ×0.75 speed | **+3% per char over 5 (linear)** |
| Short words | — | **×1.3 speed** (faster) | **no reduction** |
| Numerals | — | — | **×1.3** |
| Multi-word chunk | flat ×1.2 | **× chunkSize** | not yet implemented |
| Global min / max hold | first word only, 0.2 s | **none** | **`minHoldMs` / `maxHoldMs`** |
| Speed ramp | — | — | **yes, restarts on resume** |

Four things fall out of this:

1. **Our length model is the most aggressive of the three, and probably wrong.** speedread uses `√len` — diminishing returns — while we add a flat 3% per character indefinitely. At 20 characters we grant +45%; speedread grants roughly +9%. Square-root scaling is the more plausible shape and I'd change ours to match, except that changing a timing weight without measuring it just swaps one guess for another. **Filed as a real open question, not a bug.**
2. **We deliberately differ on short words.** speeedy *accelerates* them by 1.3×; we explicitly refuse to shorten below base. That divergence is correct for our target and wrong for theirs: on e-paper a sub-refresh-interval hold cannot physically be drawn. Worth recording that the reasoning is hardware, not taste.
3. **Chunk timing is unsettled in the field.** speeedy scales linearly with chunk size, speedread applies a flat ×1.2 regardless. These give wildly different results at 3 words. When we implement chunking, speeedy's is the defensible one.
4. **Nobody else has a global refresh floor**, because nobody else targets e-ink. `minHoldMs` remains genuinely ours.

### The gap in the field

**Almost nothing implements rewind.** Of everything surveyed, only `sprits-it` clearly documents it, and the closest prior art — the MIT KOReader plugin — does not mention rewind at all. Meanwhile:

- The KOReader feature request ([#13891](https://github.com/koreader/koreader/issues/13891), closed as a duplicate of [#13206](https://github.com/koreader/koreader/issues/13206)) asks explicitly for "pause/play and **rewind** capabilities," and the requester cites **ADHD** as their reason for wanting RSVP at all.
- The research in Part A identifies suppressed regression as a *measured* cause of comprehension loss.

So the single feature most requested by users, and most strongly indicated by the literature, is the one the field has largely skipped. That is inkflow's clearest contribution, and it's why `rewindSentence()` walks backwards through successive presses rather than sticking at the current sentence.

**Timing models are crude across the board.** `speedread` — the most popular implementation by an order of magnitude — states in its own TODO: *"Better word timing! Instead of just pausing longer at commas and full-stops, distribute time better."* `tspreed` offers word-length variation as an opt-in flag. Nobody surveyed documents sentence-versus-paragraph pause weighting, numeral handling, or a session-start speed ramp. inkflow's timing model is more developed than the field's, which was not the expected finding.

### Ideas worth taking

| Idea | From | Verdict |
|---|---|---|
| **Pause reveals surrounding context** | speedread (MIT) | **Take it.** Pausing is when you've lost the thread; showing the sentence you're inside is a cheaper fix than rewinding, and complements it |
| **Configurable chunk size, 1–10 words** | KOReader plugin (MIT) | **Take it.** Already the plan's refresh-rate lever; Part A makes it a comprehension feature too |
| **Fixed-width widget so text doesn't jump** | KOReader plugin (MIT) | **Take it.** On e-ink a shifting layout also means a larger dirty region and a slower update |
| **Comprehension quiz to set a baseline WPM** | speeedy (MIT) | **Strong idea, defer.** It directly addresses "is my chosen speed actually working?", which the research says is the real question. Needs a UI we don't have |
| **Adjustable pivot offset** | speeedy (MIT) | Take it as a config field; cheap, and the banded ORP heuristic is not sacred |
| **Dyslexia-friendly font option** | speeedy (MIT) | Take it later — the X4 supports custom SD fonts, so this is a font-file question, not an engine one |
| **Live speed adjust (±10%)** | speedread (MIT) | Take it. The X4 has 6 nav buttons; two of them should be speed |
| **Progress / position indicator** | KOReader plugin (MIT) | Already planned as `remainingMs`, framed as time-left-in-chapter |
| Irlen tinted overlays | speeedy (MIT) | **Skip.** Meaningless on a 1-bit monochrome panel |
| Click / ambient audio pacing | speeedy (MIT) | **Skip.** No audio output on the X4 |
| Streaks, goals, daily charts | speeedy (MIT) | **Skip.** Gamification aimed at a different problem, and 380 KB of RAM is not where this belongs |

---

## Part C — The Xteink X4 reader ecosystem

Feature comparison lives in [PLATFORM-MATRIX.md](PLATFORM-MATRIX.md) §3. This section records only the **reuse boundary**, per project.

### Tier 1 — MIT, code may be copied with attribution

`crosspoint-reader/crosspoint-reader` · `open-x4-epaper/community-sdk` · `open-x4-epaper/sample-firmware` · `CidVonHighwind/xteink-x4-sample` · `bigbag/papyrix-reader` · `Josh-writes/microslate-firmware` · `hansmrtn/pulp-os` · `dcherrera/CrossLuaReader` · `zakerytclarke/crosspoint-reader-apps` · `yattsu/biscuit` · `penk/X4Term` · `maddiedreese/xteink-terminal` · `maddiedreese/xteink-tamagotchi` · `trilwu/crosspet` · `Xatpy/send-to-x4` · `bigbag/epub-to-xtc-converter` · `thirteen37/calibre-xtc` · `varo6/xtcjs` · `tazua/cbz2xtc` · `bigbag/papyrix-flasher` · `crosspoint-reader/crosspoint-tools` · `crosspoint-reader/xteink-flasher` · `marginalia-os/marginalia-firmware` · `shakogegia/xtlibre` · `jtvargas/crosspoint-app`

Two additions confirmed by direct license lookup, both of which change plans:

**`psychoplath9450/SUMI` is MIT** — 174 stars, C, last pushed 2026-05-30. The survey previously listed its license as unstated. Its sandboxed Lua 5.4 app model is the zero-flash trial route for inkflow, and it is now confirmed legally clean to build against and lift from.

**`uxjulia/crossink-simulator` is MIT** — C++, SDL2, 117 commits, actively pushed. **This is the desktop simulator I was about to write from scratch.** It compiles X4-family firmware natively and renders the e-paper panel in an SDL2 window, maps the physical buttons to keys (arrows, Return, Escape, P for power, S for sleep), maps a host directory onto the device's `/books/` SD path, and backs HTTP with the host's `curl` so sync actually works. It is tightly coupled to CrossInk and warns that upstream CrossPoint interface changes break it — so it is not a drop-in — but as MIT C++ it is both a working reference and directly liftable. Finding this is the single best argument for having done this reading before writing more code.

Most relevant otherwise: **CrossPoint** is the upstream target and the source of the EPUB pipeline, fonts, input, and power management we don't intend to rewrite. **pulp-os** is the reference for driving this exact chip bare-metal (`no_std` + Embassy) — Rust, so not directly reusable in C++, but its approach to the SSD1677 is informative and legally liftable.

**`jonmooreai/Crosspoint-Emulator` is confirmed to have no license** (42 stars, C++, last pushed 2026-02-11 and stale since). It stays in Tier 3 and we cannot use it — which matters much less now that an MIT simulator exists.

### Tier 2 — Copyleft. Study behaviour, reimplement independently

| Project | License | Why it's interesting anyway |
|---|---|---|
| `azw413/TernOS` | GPL-2.0 | **Ships a desktop simulator** — the thing inkflow most needs and cannot take. Studying that it works, and roughly how it's layered, is exactly the conceptual inspiration this survey is for |
| `HookedBehemoth/TrustyReader` | GPL-2.0 | Second Rust reader; another data point on `no_std` viability |
| `chazeon/xtctool` | GPL-3.0 | XTC tooling |
| `lakafior/XTEink-Web-Font-Maker` | GPL-3.0 | Font → XTEink binary conversion; relevant if we ship a dyslexia-friendly font |
| `LowFlowIO/x4m` | GPL-3.0 | Device file manager |
| `icannotttt/crosspoint-chinesetype` | AGPL-3.0 | CJK typography |
| **KOReader** | AGPL-3.0 | The most mature reader UX in existence. A deep well of ideas — and note its RSVP *plugin* is separately MIT, so the plugin is Tier 1 even though the host is not |
| `n-ivkovic/tspreed` | GPL-3.0 | Word-length timing variation (Part B) |

### Tier 3 — No license. Stricter than copyleft, not looser

`jonmooreai/Crosspoint-Emulator` · `CrazyCoder/cr2xt` · `meta-boy/hojo` · `sunwoods/Xteink-X4`

"No license" means all rights reserved. The most common mistake here is assuming the opposite.

Two notes. `Crosspoint-Emulator`'s value to us is purely that **its existence proves a desktop emulator for this device is achievable** — that's a fact, not expression. And `sunwoods/Xteink-X4` is hardware documentation: pinouts, board photos, and register addresses are *data*, uncopyrightable, and safe to use. We already do, in the platform matrix.

### Tier 4 — Unclear, resolve before use

`ngxson/pluspoint-reader` (NOASSERTION) · `uxjulia/crossink-fonts` and `uxjulia/crossink-dictionaries` (both no license) · the CrossInk forks below

### Correction: the CrossInk situation

An earlier draft of this survey described CrossInk as "~648 stars, one of the most popular alternative firmwares." **That was wrong and I'm striking it.** The figure came from a secondary source this project had already flagged as possibly AI-generated, plus an HN comment that gave a conflicting repo path. I propagated it without checking.

What's actually verifiable: **`uxjulia/CrossInk` does not exist as a public repository.** Searching every repo named `crossink` on GitHub returns ten results, none of them the upstream firmware. What survives is a ring of satellites and third-party forks — `samfoy/CrossInk` describes itself as a *"Standalone fork of uxjulia/CrossInk"*, alongside `at689/CrossInked`, `alpzoloto-sudo/Crossink`, `ProfessorRGB/ChromadyneCrossink`, `Sparkadium/crossink-almanac`, `MimiGapa/crossink-stats-forge` — all created June–July 2026, all at 0 stars.

The most likely reading is that the upstream repo was deleted or made private recently and the forks outlived it. Either way, **there is no CrossInk repo to study, no star count to cite, and no license to resolve.** The Bionic Reading angle is still the closest prior art conceptually, and `uxjulia` is still worth talking to — but through the surviving forks or directly, not through a repo that isn't there.

This is the second time an unreliable secondary source has cost something in this project. The lesson is already in the platform matrix's source warning; it now has a concrete example attached.

---

## Part D — What inkflow does differently

Not novelty for its own sake; these are the gaps the survey actually found.

1. **Rewind-by-sentence that walks backwards.** The most-requested and least-implemented feature, and the one the research most supports. Repeated presses step through successive sentences rather than sticking.
2. **A timing model with real structure** — separate clause, sentence, and paragraph weights taken as the strongest match rather than compounded, a one-sided length bonus, numeral handling, and a session-start speed ramp. The field's models are noticeably cruder, including the most popular one by its own admission.
3. **A hardware refresh floor as a first-class concept.** `TimingConfig::minHoldMs` exists because on e-paper the panel, not the reader, sets the maximum speed. No surveyed project models this, because none of them target e-ink.
4. **Host-side precompute into a versioned binary sidecar.** Everything surveyed parses at read time, which is affordable on a phone or laptop and not on 380 KB of SRAM.
5. **A portable engine rather than an application.** Every project surveyed is welded to its platform — a shell script, a browser, an Android app, a Lua plugin. `rsvp-core` is a dependency-free library precisely so it can be hosted by CrossPoint, a simulator, or something nobody has built yet.
6. **Honesty about comprehension.** Not one surveyed project mentions the research in Part A. inkflow's README states the useful WPM band and does not claim speed gains.

---

## Upstream landscape

**CrossPoint has zero RSVP issues.** A search of the repo returns nothing — nobody has asked for this. So an upstream PR would be introducing an idea, not joining a conversation, which makes opening an issue *before* building the integration considerably more important than it looked.

**KOReader [#13206](https://github.com/koreader/koreader/issues/13206)** — open since 3 February 2025, labelled *Enhancement* and *User plugin available*. The requester wants word-by-word display, an optional focus indicator, adjustable WPM, adjustable font size and word count, and EPUB/plain-text support, citing Moon+ Reader as the model. **No maintainer objections are recorded** — no pushback on comprehension, refresh rate, or core-versus-plugin. The *User plugin available* label is the whole maintainer position: there's a community plugin, use that. [#13891](https://github.com/koreader/koreader/issues/13891) was closed as a duplicate of it.

So the field has no documented technical objection to RSVP. That is mildly reassuring and also means nobody has seriously stress-tested the idea in public.

## Gaps closed since the first draft

- Pivot and timing algorithms for both MIT implementations — read and tabulated above.
- SUMI, crossink-simulator, and Crosspoint-Emulator licenses — resolved.
- KOReader #13206 and the CrossPoint upstream picture — read.
- The CrossInk claim — checked, found wrong, corrected.

## Gaps still open

- **Licenses marked `—`** (`sprits-it`, `Readily`, `dashreader`, Streamer) remain unconfirmed. Treat as all-rights-reserved. They're low priority: we aren't lifting from any of them.
- **Copyleft timing internals stay unread on purpose** (`tspreed` and others). Not a gap to close — that's the rule working.
- **Panel dimensions conflict and need resolving on hardware.** `crossink-simulator`'s README gives the X4 as 792×1040 portrait and the X3 as 792×528 landscape. The [platform matrix](PLATFORM-MATRIX.md) has the X4 at 800×480 from Adafruit, corroborated independently by the arithmetic (800×480 over 4.26" ≈ 219 PPI, which matches every review). These cannot both be right. Chunk-width limits depend on it, so it needs a direct check — trivial once the device is in hand.
- **No project has been run.** Still the biggest gap, and still needs hardware. Flashing Papyrix, SUMI, microslate, pulp-os, and TernOS and *using* them for twenty minutes each will teach more than every README combined.
- **`speeedy`'s comprehension quiz** — still the feature I'd most like to understand before deferring it, given Part A makes "is this speed actually working for me?" the central question.
- **Whether our linear length bonus should become square-root**, per the comparison above. Needs measurement, not a coin flip.
