# Platform capability matrix — Xteink X4

Research snapshot: **2026-07-26**. Every row carries a confidence rating and a source. Anything I could not verify against a primary source is labelled `UNVERIFIED` rather than smoothed over — the point of this document is to be trustworthy about its own gaps.

A warning about the sources, because it shaped this research: a large slice of the "Xteink guide" web (`pocketink.io`, `joshualowcock.com`, `creativegamelife.com`) is SEO/AI-generated content of uneven reliability. One ecosystem survey documents content farms inventing a firmware called "Crossing" with fabricated version numbers. Primary sources — GitHub, Adafruit, Espressif, CNX-Software, Liliputing, Hacker News — are preferred throughout, and secondary-only claims are marked.

---

## 1. The device

| Property | Value | Confidence |
|---|---|---|
| SoC | **Espressif ESP32-C3**, single-core **RISC-V @160 MHz** | High |
| RAM | **400 KB SRAM, no PSRAM** (~380 KB usable) | High |
| Flash | **16 MB SPI** | High |
| Display | 4.26" mono E Ink, **800×480**, ~219 PPI | High |
| Panel | `GDEQ0426T82` (Good Display) | High |
| EPD controller | **SSD1677**, SPI | High |
| Storage | **No usable internal storage — microSD only** | High |
| Input | 7 buttons: power + 6 nav. **No touch, no front light** | High |
| Battery | 650 mAh, ~14 days at 1–3 h/day (stock page-turn reading) | High |
| Size / weight | 114 × 69 × 5.9 mm / 74 g | High |
| Connectivity | 2.4 GHz Wi-Fi + BT (ESP32-C3 radio), USB-C | High |
| Price | ~$69 | High |
| Debug | **USB Serial/JTAG exposed on the USB-C port** | High |

Sources: [Adafruit pinouts](https://learn.adafruit.com/circuitpython-on-the-xteink-x4-ereader/pinouts) · [Liliputing](https://liliputing.com/this-69-ereader-is-designed-to-stick-to-the-back-of-your-phone/) · [Notebookcheck](https://www.notebookcheck.com/Xteink-X4-Dieser-kompakte-E-Reader-passt-dank-MagSafe-auch-ans-iPhone-und-kann-nicht-nur-E-Books-anzeigen.1176787.0.html) · [Good e-Reader](https://goodereader.com/blog/electronic-readers/review-of-the-xteink-x4-e-reader)

### Pinout (verified, and independently corroborated)

```
EPD:   SCK=GPIO8   MOSI=GPIO10  CS=GPIO21  DC=GPIO4  RST=GPIO5  BUSY=GPIO6
SD:    CS=GPIO12   MISO=GPIO7   -- shares the SPI bus with the EPD
Btns:  power=GPIO3 (direct);  6 nav buttons via resistor-ladder ADC on GPIO1 + GPIO2
Power: battery voltage divider on GPIO0 (ADC);  charge-detect on GPIO20
```

Source: [Adafruit](https://learn.adafruit.com/circuitpython-on-the-xteink-x4-ereader/pinouts), corroborated by [sunwoods/Xteink-X4](https://github.com/sunwoods/Xteink-X4) (which confirmed its pins empirically by getting the panel to refresh).

**Two facts here drive the whole architecture.** 380 KB of RAM with no PSRAM means no on-device book parser and no full-page buffers to spare. And the SD card sharing the SPI bus with the display means streaming text while refreshing the panel needs deliberate arbitration, not optimism.

### Partition layout

```
nvs       data nvs      0x9000    0x5000
otadata   data ota      0xe000    0x2000
app0      app  ota_0    0x10000   0x640000
app1      app  ota_1    0x650000  0x640000
spiffs    data spiffs   0xc90000  0x360000
coredump  data coredump 0xFF0000  0x10000
```

Standard dual-OTA ESP-IDF layout. `0x640000` = 6,553,600 bytes — which matches *exactly* the free space the stock firmware's OTA client reports, so stock uses the same app-partition geometry. Source: [partitions.csv](https://raw.githubusercontent.com/crosspoint-reader/crosspoint-reader/develop/partitions.csv), [crosspoint issue #1918](https://github.com/crosspoint-reader/crosspoint-reader/issues/1918).

---

## 2. Access and flashing capability

This is the section that determines whether this project is possible at all.

| Capability | Status | Evidence | Confidence |
|---|---|---|---|
| Flash unsigned code over USB | **Yes** — plain `esptool --chip esp32c3` | [Adafruit](https://learn.adafruit.com/circuitpython-on-the-xteink-x4-ereader/install-circuitpython) | High |
| Bootloader-entry ritual needed | **No.** Adafruit: *"You do not need to put the Xteink X4 eReader into bootloader mode"* | [Adafruit](https://learn.adafruit.com/circuitpython-on-the-xteink-x4-ereader/install-circuitpython) | High |
| Full 16 MB read/write/verify | **Yes** | [aimindseye docs](https://github.com/aimindseye/xteink-x4/blob/main/docs/backup-and-restore.md) | High |
| Secure boot enabled | **No** (inferred) | Plaintext dumps restore; unsigned third-party binaries boot; CircuitPython + Rust + 7 firmwares all run. ESP32-C3 Secure Boot v2 would reject every one | **Med** — no source states it literally |
| Flash encryption enabled | **No** (same inference) | as above | **Med** |
| Stock OTA verifies signatures | **No.** The community unlocker DNS-spoofs `api-prod.xteink.cn` over **plain HTTP** and stock installs the substituted image | [unlocker-tool](https://github.com/crosspoint-reader/crosspoint-tools/tree/master/unlocker-tool) · [issue #1918](https://github.com/crosspoint-reader/crosspoint-reader/issues/1918) · [HN](https://news.ycombinator.com/item?id=48048564) | High |
| ROM bootloader always reachable | **Yes** — USB Serial/JTAG, the hardware escape hatch | [Adafruit](https://learn.adafruit.com/circuitpython-on-the-xteink-x4-ereader/install-circuitpython) | High |
| SD-card `update.bin` path | Yes via OEM loader (hold power + Up), but **unreliable from X4 stock** | [pocketink FAQ](https://pocketink.io/firmware/faq/) — secondary | Med |
| Public archive of stock firmware images | **No** — none found, with checksums or otherwise | [aimindseye stock-firmware.md](https://github.com/aimindseye/xteink-x4/blob/main/docs/stock-firmware.md) explicitly hosts none | Med (absence of evidence) |
| Vendor SDK / API docs / dev program | **None** | — | High |
| Third-party code on *stock* firmware | **None.** Data only: EPUB/TXT/XTC files and BMP/JPG/PNG wallpapers | [Liliputing](https://liliputing.com/this-69-ereader-is-designed-to-stick-to-the-back-of-your-phone/) | High |

### The locked-unit trap

Some units ship with USB flashing disabled. This is the one genuine risk to the hardware.

| | Unlocked unit | Locked unit |
|---|---|---|
| Enumerates as USB serial | Yes | **No** |
| Can take a full flash backup | Yes | **No** |
| Flash route | `esptool` over USB | OTA/SD only |
| Recovery from a bad flash | ROM bootloader — always | **Only if the installed firmware has OTA** |
| Brick risk | ~Zero | **Real and potentially permanent** |

Locking clusters in grey-market channels — Taobao highest risk, then AliExpress. Units from xteink.com and Amazon are sold unlocked as **"Developer Edition."** Xteink's stated justification was crashes, screen damage, and warranty cost.

Named casualties on locked units: Papyrix removed OTA support and left an owner unable to reflash; CrossPet left one owner permanently stuck. CrossLuaReader's README shouts the warning in capitals.

**The mechanism is UNVERIFIED.** One secondary source asserts a burned eFuse disabling the USB-serial route and itself hedges that this "isn't vendor-confirmed and may vary by batch." No `espefuse summary` dump, teardown, or vendor statement confirms eFuse versus a firmware-level USB-CDC disable — and the two are behaviourally distinguishable, so this is a testable question nobody has published an answer to.

Sources: [crosspoint README](https://github.com/crosspoint-reader/crosspoint-reader) · [Good e-Reader](https://goodereader.com/blog/electronic-readers/xteink-is-going-to-block-custom-software) · [Liliputing](https://liliputing.com/xteink-blocks-installation-of-custom-firmware-on-some-ereaders-launches-a-new-android-powered-model/) · [pocketink buying guide](https://pocketink.io/blog/xteink-locked-or-unlocked-buying-guide/) (secondary)

### How to tell which one you have (2 minutes)

Power on, connect a **data** USB-C cable, open the serial-port picker in Chrome/Edge. An `Espressif USB/JTAG/serial` entry means unlocked. Nothing — after trying another cable, another port, and another browser — means locked. There is no packaging or serial-number indicator.

### The golden image is step one, unconditionally

```sh
esptool --chip esp32c3 --port <PORT> --baud 460800 read-flash 0 16M x4-stock-golden.bin
sha256sum x4-stock-golden.bin | tee x4-stock-golden.bin.sha256
```

~25 minutes. Since no public stock image archive exists, this dump is the only recovery path that is definitely yours. Store it off the device and verify a restore *before* relying on it. Books are never at risk — they live on the removable microSD; firmware only touches internal flash.

---

## 3. Firmware capability matrix

The comparison MC asked for. Columns are the capabilities that matter for hosting an RSVP reader.

Legend: ● full · ◐ partial · ○ none · ? unverified

| Firmware | License | ★ | Lang | EPUB | TXT/MD | Other fmt | 3rd-party apps | OTA | Sim | Fonts | Sync | As RSVP host |
|---|---|---:|---|:-:|:-:|---|:-:|:-:|:-:|:-:|---|---|
| **crosspoint-reader** | **MIT** | 6488 | C/C++ | ● 2/3 + CSS | ● | XTC | ○ | ● | ○ | ● SD | KOReader, OPDS, WebDAV | **Best.** Parsing, fonts, input, power all solved. Active daily. Upstream target |
| **papyrix-reader** | MIT | 427 | C | ● | ● | FB2 | ○ | **○ removed** | ○ | ● | — | Good code to learn from; **no OTA is disqualifying for locked units** |
| **SUMI** | **MIT** | 174 | C + Lua | ● | ● | — | **● Lua 5.4 sandbox** | ? | ○ | ● | StarDict dict | **Zero-flash trial target.** 46-fn API, 40 KB VM cap. License confirmed MIT |
| **CrossLuaReader** | MIT | 35 | C + Lua | ◐ | ● | ZIP/XML/JSON | **● SD plugins** | ? | ○ | ● | — | Alternative plugin host. ~640 KB flash, ~380 KB RAM budget |
| **microslate-firmware** | MIT | 121 | C | ◐ | ● | — | ○ | ? | ○ | ◐ | — | Deliberately minimal — a clean reference for "how little is enough" |
| **pulp-os** | MIT | 122 | **Rust** | ◐ | ● | — | ○ | ? | ○ | ◐ | — | **The `no_std` + Embassy reference for this exact chip.** MIT, so liftable |
| **TernOS** | **GPL-2.0** | 107 | Rust | ○ | ◐ | PalmOS apps | ● PalmOS | ? | **● desktop sim** | ◐ | — | Ideas only. Ships the simulator we need and can't take |
| **TrustyReader** | **GPL-2.0** | 18 | Rust | ◐ | ● | — | ○ | ? | ○ | ◐ | — | Ideas only |
| ~~**CrossInk**~~ | — | — | C | — | — | — | — | — | — | — | — | ⚠️ **Upstream repo does not exist publicly.** See correction below |
| **pluspoint-reader** | NOASSERTION | 28 | C | ◐ | ● | — | ○ | ? | ○ | ◐ | — | Resolve license before touching |
| **biscuit** | MIT | ~360 | C | ○ | ○ | — | ◐ | ? | ○ | — | — | Dashboard/games — device-as-platform, not reading-first |
| **Stock (XTOS)** | closed | — | C (ESP-IDF) | ● | ● | XTC/XTG/XTH | **○** | ● | ○ | ◐ | Wi-Fi transfer | Unusable — no third-party code, period |

`?` in the OTA column is load-bearing: for a locked unit, **OTA support is the difference between reversible and permanent.** It's unverified for most of these because nobody documents it, which is itself a finding.

> **Correction — CrossInk.** An earlier version of this table listed CrossInk at ~648 stars as one of the most popular alternative firmwares. That was wrong. The figure came from a secondary source this document already warns about, and I propagated it without checking. **`uxjulia/CrossInk` does not exist as a public repository** — a GitHub-wide search for repos named `crossink` returns ten results, none of them the upstream firmware. What remains is satellites (`crossink-fonts`, `crossink-dictionaries`, `crossink-simulator`) and third-party forks (`samfoy/CrossInk`, which calls itself a *"Standalone fork of uxjulia/CrossInk"*, plus `at689/CrossInked`, `alpzoloto-sudo/Crossink`, `ProfessorRGB/ChromadyneCrossink`), all created June–July 2026 at 0 stars. The upstream was most likely deleted or made private and the forks outlived it. See [`FEATURE-SURVEY.md`](FEATURE-SURVEY.md) for the full note.

### Non-reader firmware — precedent that arbitrary apps work

`penk/X4Term` (VT100 terminal, C++) · `maddiedreese/xteink-terminal` (MIT, Wi-Fi terminal + tmux bridge) · `ngxson/esphome-component-xteink` (Home Assistant integration) · `maddiedreese/xteink-tamagotchi` (MIT) · `trilwu/crosspet` (MIT, virtual pet). These matter as existence proofs: the device will happily run something that is not a page-based reader.

---

## 4. Toolchain and SDK options

| Option | License | What it is | Verdict for us |
|---|---|---|---|
| `open-x4-epaper/community-sdk` | MIT | Reusable PlatformIO libs (`libs/`: display, graphics, hardware) + `tools/`. Consumed as a git submodule | **Primary SDK** |
| `open-x4-epaper/sample-firmware` | MIT | Minimal GxEPD2 hello-world | **Skeleton for `eink-bench`** |
| `CidVonHighwind/xteink-x4-sample` | MIT | The original sample that seeded the ecosystem | Reference |
| PlatformIO + Arduino-ESP32 / ESP-IDF | — | `riscv32-esp-elf` GCC | **Our toolchain** |
| CircuitPython (Adafruit) | — | Python on the X4, with the canonical pinout guide | Fast prototyping / pinout truth |
| Rust `no_std` (`riscv32imc-unknown-none-elf`, Embassy) | — | Proven by pulp-os and TernOS | Viable, but see below |
| **`uxjulia/crossink-simulator`** | **MIT** | **SDL2 desktop simulator, C++, 117 commits.** Compiles firmware natively, renders the panel in an SDL2 window, maps buttons to keys, maps a host dir onto `/books/`, backs HTTP with the host's `curl` | **Use this.** Tightly coupled to CrossInk so not a drop-in, but MIT and the reference we'd otherwise have written |
| `jonmooreai/Crosspoint-Emulator` | **NO LICENSE** (confirmed) | Desktop emulator, 42★, stale since 2026-02-11 | **Cannot use.** Matters less now that an MIT simulator exists |

**Why C++17 and not Rust,** since Rust is genuinely well-supported here: the end target is an upstream PR into CrossPoint, which is C/C++ on PlatformIO, and community-sdk is C++. A Rust contribution to that codebase is a non-starter. The upstream distribution path is worth more than the language preference. `pulp-os` remains the reference for how to drive this hardware well, and being MIT we can lift from it even while re-expressing in C++.

---

## 5. Host-side tooling and file formats

| Tool | License | Language | Purpose |
|---|---|---|---|
| `bigbag/epub-to-xtc-converter` | MIT | JS | EPUB → XTC |
| `thirteen37/calibre-xtc` | MIT | Python | Calibre plugin |
| `chazeon/xtctool` | **GPL-3.0** | Python | XTC tooling — ideas only |
| `CrazyCoder/cr2xt` | **no license** | PowerShell | CoolReader-based converter — ideas only |
| `varo6/xtcjs` · `tazua/cbz2xtc` | MIT | TS / Python | Manga CBZ → XTC |
| `lakafior/XTEink-Web-Font-Maker` | **GPL-3.0** | — | Font → XTEink binary — ideas only |
| `Xatpy/send-to-x4` | MIT | — | **Browser extension: push web articles to device** |
| `shakogegia/xtlibre` | MIT | TS | Self-hosted companion |
| `crosspoint-reader/xteink-flasher` | MIT (archived) | TS/Next.js | Web flasher, superseded by crosspointreader.com |
| `bigbag/papyrix-flasher` | MIT | Go | CLI flasher with auto-detect |

### On the XTC format

XTC/XTG/XTH/XTCH is the stock **pre-rendered bitmap page** container: magic `XTG\0`/`XTH\0`/`XTC\0`/`XTCH`, 22-byte headers for XTG/XTH and 56-byte for XTC/XTCH, a 16-byte-per-page index table, little-endian. XTG is 1bpp row-major MSB-first; XTH is 2bpp as two bit-planes, column-major, right-to-left, with a non-linear grey LUT.

**Treat this spec with suspicion.** [The gist documenting it](https://gist.github.com/CrazyCoder/b125f26d6987c0620058249f59f1327d) says of itself: *"generated by AI, there may be issues with the specific details."* Validate against real files before depending on any of it.

We probably don't need it. XTC is designed for pre-rendered *pages*, and RSVP doesn't have pages — it has a token stream. Our own `.rsvp` sidecar format is a better fit and avoids inheriting an unverified spec.

---

## 6. Platform alternatives

Ranked by documentation quality. The first three run the same CrossPoint codebase, so our code ports nearly for free.

| Device | SoC | Why it's a good fallback | Trade-off |
|---|---|---|---|
| **M5Stack PaperS3** | ESP32-S3, **8 MB PSRAM**, dual-core, touch | Best swap. Vendor schematics + official SDK, CrossPoint already ports to it. **PSRAM removes our #1 constraint** | Bigger, pricier, not pocketable |
| **LilyGo T5 ePaper S3** | ESP32-S3 | Open schematics, large PlatformIO community, existing port | Dev board, not a product |
| **Xteink X3** | ESP32-C3 | Same platform, sharper 259 PPI | Proprietary pogo-pin charging; SD-flash behaves differently |
| **Xteink X4 Pro** | presumed ESP32-family | Touch + front light, CrossPoint support announced (July 2026) | **SoC UNVERIFIED.** Wait for teardowns |
| **PineNote** | Rockchip RK3566, mainline-ish Linux | Fully open by design, 10.3" | Different scale, price, and effort entirely |
| **Kobo + KOReader** | Various ARM Linux | Decade-old modding scene, mature reader | AGPL-3.0 — incompatible with our MIT plan |
| ⚠️ **Xteink S4** | Android 11, no GMS | — | **Avoid.** Different platform, no CrossPoint, no community |

---

## 7. Community

Activity as of 2026-07-26 is **high and accelerating**. CrossPoint went 5,581★ (26 June) → 6,488★ (26 July), roughly +30/day, with 1.3k forks and 1,070 commits on `develop`. The X4 Pro launching 21 July with announced CrossPoint support is pulling in new contributors.

| Where | What | Confidence |
|---|---|---|
| Discord "Xteink eReader Community" — `discord.gg/2cdKUbWRE8` | ~3,303 members. The invite comes from community-sdk's own README | High (invite) / Med (member count) |
| GitHub orgs `crosspoint-reader`, `open-x4-epaper` | The real technical centre of gravity | High |
| Hacker News [48021901](https://news.ycombinator.com/item?id=48021901), [48048564](https://news.ycombinator.com/item?id=48048564) | Launch + unlocker discussion | High |
| `r/xteinkereader` | Cited by one secondary source; **I could not confirm this subreddit exists** | **Low** |

People worth knowing: **Dave Allie** (`@daveallie`) — CrossPoint creator/maintainer, and the person to talk to about upstreaming. **CrazyCoder** — the XTC format spec and `cr2xt`; the closest thing to a format reverse-engineer of record. **OvermindDL1** — the OTA unlocker. **uxjulia** — author of the MIT `crossink-simulator` (the SDL desktop simulator), `auto-epub-optimizer`, and the CrossInk font and dictionary repos. Their CrossInk firmware, which focused on typography and Bionic Reading, is no longer public — but the simulator is, and it is the most directly useful artifact anyone in this ecosystem has published for our purposes. **hansmrtn** — pulp-os. **azw413** — TernOS. **psychoplath9450** — SUMI. **aimindseye** — the community wiki.

Xteink publicly **partnered** with the CrossPoint project on 2026-06-21 after initially moving to block custom firmware — the vendor stopped fighting the modding scene and started referral-partnering with it. (Med/High confidence; the partnership is reported by CNX-Software and one ecosystem survey.)

---

## 8. What this means for inkflow

1. **The device is not the obstacle.** No secure boot, an always-reachable ROM bootloader, a published pinout, an MIT reference firmware with daily commits, a community SDK, and a cooperative vendor. Writing custom code for this thing is a solved problem.
2. **RAM is the obstacle.** 380 KB with no PSRAM shapes every decision, and it's why book parsing happens on a laptop rather than on the device.
3. **The open question is physics, not access.** Nobody in this ecosystem has published SSD1677 refresh-latency numbers for windowed partial updates, or power draw at sustained high refresh rates. RSVP at 300 WPM needs a word every 200 ms, and normal e-reading refreshes once every 30 seconds — a completely different duty cycle on a 650 mAh battery. **Measuring this is the first real engineering task, and the numbers are a contribution to the ecosystem regardless of whether inkflow ships.**

   **Update after the feature survey:** the reading-comprehension literature puts the useful RSVP band at **250–350 WPM**, not the 600–900 this section was originally sized against — comprehension drops significantly above ~350. That means the requirement is **171–240 ms per update**, and two-word chunks at 300 WPM relax it to 400 ms. The measurement is still required, but the bar dropped by roughly 3x. See [`FEATURE-SURVEY.md`](FEATURE-SURVEY.md) Part A.
4. **The reuse story is excellent and mostly permissive.** The large majority of the ecosystem is MIT. Only a handful of projects are copyleft or unlicensed, and those are read-for-ideas only — see `FEATURE-SURVEY.md` for the per-project boundary.

---

## Unverified — the honest gap list

- Secure boot / flash encryption are *inferred* off, never stated. Confirm with `espefuse summary` on a real unit.
- The locking mechanism: eFuse versus firmware-level USB-CDC disable. Distinguishable by test; nobody has published one.
- Flash chip and PMIC part numbers; whether the ESP32-C3 is a bare QFN or a WROOM-type module.
- No FCC filing found for the base X4 (only `2BTR9-X4PRO` for the Pro, per a secondary source).
- Latest stock firmware version — 3.1.0 is the only number referenced anywhere, from the AI-generated gist. Low confidence.
- OTA support in most community firmwares — unverified, and it's the field that determines reversibility on locked units.
- Whether `r/xteinkereader` exists.
- **No measured SSD1677 refresh or power numbers exist anywhere.** This is the gap inkflow's `eink-bench` is built to close.
- **Panel dimensions are in conflict and need checking on hardware.** This document says 800×480, from Adafruit and corroborated by arithmetic — 800×480 across 4.26" is ~219 PPI, matching every review. But `crossink-simulator`'s README gives the X4 as **792×1040 portrait** (and the X3 as 792×528 landscape). Both cannot be right. It matters, because the usable width caps how many characters fit in a chunk at a legible size. Resolving it takes ten seconds with the device in hand.
