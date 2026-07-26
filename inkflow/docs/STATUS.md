# Status and handoff

Last updated: **2026-07-26**. Written so a fresh session — or a fresh person — can pick this up without re-deriving anything.

## Read these first, in order

1. [`README.md`](../README.md) — what inkflow is, and its **Honest status** section, which is the contract for what is and isn't true yet.
2. [`CONTRIBUTING.md`](../CONTRIBUTING.md) — TDD discipline, the engine's hard constraints, and the license boundary. Read before borrowing any code.
3. [`docs/PLATFORM-MATRIX.md`](PLATFORM-MATRIX.md) — the X4 as a target: hardware, flashing, firmware comparison.
4. [`docs/FEATURE-SURVEY.md`](FEATURE-SURVEY.md) — what other RSVP readers do, what the comprehension research says, and per-project reuse rights.

## What exists

`rsvp-core` is complete for the engine layer and fully tested — **84 test cases, 386 assertions, 22 end-to-end CLI checks, all green.** Built strictly test-first.

| Piece | File | State |
|---|---|---|
| Tokenizer + boundary classification | `core/src/tokenizer.cpp` | Done |
| ORP pivot | `core/src/orp.cpp` | Done. Banding matches `pasky/speedread` exactly |
| Timing model | `core/src/timing.cpp` | Done. Boundary pauses, length bonus, numerals, WPM ramp, refresh floor |
| Playback state machine | `core/src/player.cpp` | Done. Pause, rewind-by-sentence, fingerprinted resume |
| `.rsvp` container | `core/src/format.cpp` | Done. Versioned, CRC-checked, forward-compatible, alignment-safe |
| Host converter | `tools/rsvp-mk/` | Working for TXT and Markdown |
| Desktop simulator | `sim/` | **Not started** |
| E-ink benchmark | `bench/eink-bench/` | **Not started** — needs hardware |

```sh
cmake -B build -DCMAKE_BUILD_TYPE=Debug && cmake --build build -j
ctest --test-dir build --output-on-failure
./build/tools/rsvp-mk/rsvp_mk docs/PLATFORM-MATRIX.md --wpm 400
```

## What has never happened

**Nothing has run on an Xteink X4.** Not once. Every words-per-minute number in this repo is arithmetic, not measurement. `TimingConfig::minHoldMs` defaults to 0 because nobody has measured what it should be. No one has watched this render a single word.

That is the entire reason the next phase needs hardware.

## Next phase — on a machine with the device attached

### Phase 0: safety, in this order

**1. Determine locked vs unlocked.** Connect a **data** USB-C cable (charge-only cables are the most common time-waster here) and look for the serial port. On macOS:

```sh
ls /dev/cu.usbmodem*
```

A port appearing means **unlocked** — the ESP32-C3's USB Serial/JTAG is native CDC-ACM, so macOS needs no driver. Nothing appearing, after trying another cable and another port, means **locked**: stop, and read the locked-unit section of the platform matrix before doing anything else. A locked unit can be permanently stranded.

**2. Golden image, before touching anything.** No public archive of a stock X4 image exists, so this dump is the only recovery path that is definitely yours.

```sh
pipx install esptool     # or: pip install esptool
PORT=$(ls /dev/cu.usbmodem* | head -1)
esptool --chip esp32c3 --port "$PORT" --baud 460800 read-flash 0 16M x4-stock-golden.bin
shasum -a 256 x4-stock-golden.bin | tee x4-stock-golden.bin.sha256
```

Takes roughly 25 minutes. **Copy it off the machine** — a backup that lives only next to the thing it backs up is not a backup. Books are never at risk either way; they live on the removable microSD and firmware only touches internal flash.

**3. Verify the restore path works** before relying on it. A backup you have never restored is a hypothesis.

**4. First light.** Build and flash `open-x4-epaper/sample-firmware` unmodified, via PlatformIO. Nothing at stake, and it proves the whole toolchain.

### Phase 1: resolve the open hardware questions

Cheap to answer with the device, impossible without it:

- **Panel dimensions.** This repo says 800×480 (Adafruit, corroborated by the 219 PPI arithmetic). `crossink-simulator` says 792×1040 portrait. Both cannot be right, and chunk-width limits depend on the answer.
- **Secure boot / flash encryption.** Currently *inferred* off from behaviour, never confirmed. `espefuse.py summary` settles it.
- **The locking mechanism** — eFuse versus a firmware-level USB disable. Nobody has published a test. Only matters if a locked unit turns up.

### Phase 2: `eink-bench` — the measurement that decides the project

Build from `open-x4-epaper/sample-firmware`, write results to SD as CSV. Measure:

- Full refresh, partial refresh, and any fast/A2-style mono waveform the SSD1677 exposes.
- **Windowed partial updates** — redrawing only an ~800×120 centre strip. This is the key bet: RSVP only needs to change one word.
- Ghosting accumulation: how many windowed partials before contrast degrades.
- Refresh latency with and without concurrent SD reads — **SD and EPD share the SPI bus.**
- **Sustained current draw**, and projected battery life per configuration. Normal e-reading refreshes once per 30 s; RSVP refreshes several times per second on a 650 mAh cell.

The target is more forgiving than it first appeared: the comprehension research puts the useful band at **250–350 WPM**, i.e. **171–240 ms per update**, and two-word chunks at 300 WPM relax it to 400 ms.

Measurements become named calibration constants in `TimingConfig`, each citing the bench run that produced it. **These numbers do not exist anywhere publicly — publishing them is a contribution to the ecosystem regardless of whether inkflow ships.**

### Phase 3: simulator

Study [`uxjulia/crossink-simulator`](https://github.com/uxjulia/crossink-simulator) first — **MIT, C++, SDL2**. It compiles X4-family firmware natively, renders the panel in an SDL2 window, maps buttons to keys, and maps a host directory onto the device's `/books/` path. It's coupled to CrossInk so it isn't a drop-in, but it is the reference, and it is liftable.

### Phase 4: device integration

SUMI Lua app first (**MIT**, no flash risk, real panel), then the CrossPoint RSVP mode. **Open a CrossPoint issue before building the integration** — the repo has zero RSVP issues, so this introduces the idea rather than joining a discussion.

## Open questions worth holding

- **Should the length bonus be square-root instead of linear?** `speedread` uses `0.04 × √len`; we add 3% per character indefinitely, which is far more aggressive on long words. Square root is the more plausible shape, but changing it without measurement swaps one guess for another.
- **Chunk timing** is unsettled in the field — `speeedy` scales linearly with chunk size, `speedread` applies a flat ×1.2. Decide before implementing chunking; `speeedy`'s is the defensible one.
- **`speeedy`'s comprehension quiz** for setting a baseline WPM. Given the research, "is this speed actually working for me?" is the real question.

## Repository situation

Everything currently lives on branch `claude/xteink-x4-rsvp-reader-212m18` of `wittyreference/dotfiles`, under `inkflow/`. A standalone history — `inkflow/` contents at the repo root — is staged on the same remote as branch **`inkflow-main`**.

To land it in its own repo from a machine with working credentials:

```sh
git clone -q --bare https://github.com/wittyreference/dotfiles.git d
git -C d push https://github.com/wittyreference/inkflow.git inkflow-main:main
rm -rf d
```

Add `--force` if the target repo was created with a README. This was blocked in the originating environment by a git-proxy repo allowlist, not by anything wrong with the history.
