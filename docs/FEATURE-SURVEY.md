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
| [karpushchenko/koreader-rsvp-plugin](https://github.com/karpushchenko/koreader-rsvp-plugin) ("FastReader") | **MIT** | 34 | Lua / KOReader | 50–1000 | ● **1–10 words** | ● crosshair | ● | **○ undocumented** | ● per-page | Closest prior art. Fixed-width widget so words don't jump |
| [sami-29/speeedy](https://github.com/sami-29/speeedy) | **MIT** | 41 | TS / Lit / PWA | 100–1600 | ○ | ● adjustable pivot offset | ● | ○ | ● | **Comprehension quiz to set baseline WPM.** OpenDyslexic, Irlen overlays, RTL, click/ambient audio, streaks |
| [the-happy-hippo/sprits-it](https://github.com/the-happy-hippo/sprits-it) | — | — | web | ● | ○ | ● | ● | **● rewind** | ○ | Mobile-browser focused, night mode |
| [syniuhin/Readily](https://github.com/syniuhin/Readily) | — | — | Android | ● | ○ | ● | ● | ? | ● | Inspired by Spritzer |
| [n-ivkovic/tspreed](https://github.com/n-ivkovic/tspreed) | **GPL-3.0** | 104 | POSIX shell | 1–60000 | ○ | ● `--focus` | ● | ○ | ○ | `--length-vary` scales timing by word length |
| [inattendu/dashreader](https://github.com/inattendu/dashreader) | — | — | plugin | ● | ○ | ● | ● | ? | ? | RSVP plugin |
| Streamer (Calibre plugin) | — | — | Calibre | ● | ? | ? | ● | ? | ? | [MobileRead thread](https://www.mobileread.com/forums/showthread.php?p=4578319) |
| Kindle "Word Runner" · Reedy | proprietary | — | — | ● | ● | ● | ● | ● | ● | The commercial baselines users compare against |

`?` means the README doesn't say. `○` means absent or undocumented.

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

Most relevant: **CrossPoint** is the upstream target and the source of the EPUB pipeline, fonts, input, and power management we don't intend to rewrite. **pulp-os** is the reference for driving this exact chip bare-metal (`no_std` + Embassy) — Rust, so not directly reusable in C++, but its approach to the SSD1677 is informative and legally liftable.

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

`ngxson/pluspoint-reader` (NOASSERTION) · `psychoplath9450/SUMI` (unstated) · `uxjulia/CrossInk` (unstated)

**CrossInk deserves a flag.** At ~648 stars it's one of the most popular alternative firmwares, and its focus is *typography and Bionic Reading* — making it the closest thing in this ecosystem to inkflow's problem space. Bionic Reading bolds word-openings to guide the eye, which is a different answer to the same question RSVP asks. Its license is unstated in what I could reach, so it's study-only for now, and its author (`uxjulia`) is the most relevant person in the ecosystem to talk to.

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

## Gaps in this survey

- **Timing algorithm specifics are undocumented across the field.** Every README describes *that* it varies timing, none say *how*. Those details live in source I have deliberately not read for the copyleft projects, and have not yet read for the MIT ones. inkflow's model was derived from published reading research instead, which is the outcome the license rule was meant to produce.
- **Licenses marked `—`** (`sprits-it`, `Readily`, `dashreader`, Streamer) are unconfirmed; treat them as Tier 3 until checked.
- **KOReader issue #13206** — the primary RSVP discussion thread, and where maintainer objections would live — has not been read. Worth doing before proposing anything upstream.
- **No project was actually run.** This is a documentation survey. The plan calls for flashing Papyrix, SUMI, microslate, pulp-os, and TernOS onto the X4 and *using* them, because twenty minutes of use tells you what a README cannot. That needs the device.
- **`speeedy`'s comprehension quiz** is the one feature I'd most like to understand properly before dismissing or deferring it.
