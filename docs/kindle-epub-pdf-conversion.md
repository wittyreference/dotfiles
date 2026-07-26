# Getting EPUB / PDF copies of Kindle books you've licensed

A field guide for pulling open-format copies of books bought as Amazon Kindle
licenses, for personal use on a Mac. Written mid-2026; this area moves fast and
Amazon actively works against it, so treat version numbers as "check before you
trust" and re-read the linked FAQ if something doesn't line up.

## Read this first: two different things, one legal snag

The job splits into two operations that get conflated:

1. **Format conversion** — turning a file you already have into EPUB or PDF.
   This is legal and boring. [Calibre](https://calibre-ebook.com/) does it in
   one click.
2. **DRM removal** — stripping Amazon's access control off the file so it *can*
   be converted. This is the whole ballgame, and it's the part with legal
   friction.

Almost every "how to convert Kindle books" guide online is really a DRM-removal
guide with a conversion step bolted on. So the honest question isn't "how do I
convert" — it's "am I willing to circumvent DRM, and can I even still get at the
files."

## The legal reality (you asked not to break laws — so here it is straight)

- **You bought a license, not the book.** Amazon's Terms of Use explicitly
  forbid bypassing the DRM.
- **US law (DMCA §1201)** prohibits circumventing an access-control measure
  *even on content you own, even for personal use*. The Copyright Office's 2024
  triennial rulemaking (in force through Oct 28, 2027) declined — again — to
  create a general personal-use / format-shifting exemption for e-books. The
  **only** e-book carve-out is narrow: DRM that blocks screen readers and other
  **assistive technology for people with print disabilities**.
- So the underlying *copy* might well be fair use, but the **act of removing the
  DRM is separately unlawful** under §1201 unless you fall under that
  accessibility exemption. Enforcement against individuals doing personal
  format-shifting is effectively nonexistent, and the tools are open-source and
  freely available — but "never prosecuted" is not the same as "legal," and you
  asked for the real answer.
- **Outside the US** it's rarely better: the UK, EU member states, Canada, and
  Australia all implement anti-circumvention rules that prohibit breaking a
  "digital lock" regardless of whether the private copy would otherwise be
  allowed.

**Bottom line:** converting a *DRM-free* file is unambiguously fine. Removing
Amazon's DRM to get there is legally gray-to-red in the US and most of the West.
Decide that part for yourself; the sections below lay out both the clean path
and the community path.

## Amazon changed the board in Feb 2025 — this breaks most old guides

On **26 February 2025** Amazon removed the **"Download & Transfer via USB"**
option from *Manage Your Content and Devices* on the website. That option used
to hand you a downloadable `.azw3` file tied to a Kindle's serial number — it
was the cornerstone of nearly every DeDRM tutorial (including the popular
`joelhooks/kindle-dedrm` macOS toolchain, whose documented download step is now
defunct).

What that means today: **the only remaining local sources of the file are the
Kindle desktop app and a physical Kindle e-reader.** Any guide that opens with
"go to Manage Your Content and click Download & Transfer" is pre-2025 and will
leave you stuck at step one.

There's also a broader hardening trend: books distributed after roughly
mid-2025 increasingly use per-account keys held on the device, and are widely
reported as effectively unrecoverable by the current tools. **Test your method
on one older book before assuming your whole library will convert.**

## Path A — the clean, no-circumvention route (recommended where it fits)

No DRM to remove means no legal snag. In order of usefulness:

1. **Books that shipped DRM-free.** DRM is the publisher's choice, not
   universal — a chunk of Kindle titles (much of Tor, many technical and indie
   books) have none. Calibre converts those to EPUB/PDF with zero circumvention.
   The catch post-Feb-2025 is simply *getting the file* onto disk (see Path B's
   sourcing notes), but once you have it, conversion is legal and trivial.
2. **Buy the open format directly for future reads.** For anything you care
   about keeping, buy the EPUB from a store that sells it DRM-free:
   - Publisher-direct stores that are DRM-free by policy — e.g.
     [O'Reilly](https://www.oreilly.com/), [No Starch](https://nostarch.com/),
     [Manning](https://www.manning.com/), [Verso](https://www.versobooks.com/).
   - [Standard Ebooks](https://standardebooks.org/) and
     [Project Gutenberg](https://www.gutenberg.org/) for public-domain titles —
     free, meticulously produced EPUBs.
   - [Smashwords](https://www.smashwords.com/) for indie titles, DRM-free.
   This is the only path that's both legal *and* durable — it sidesteps Amazon
   entirely.
3. **Accessibility exemption.** If DRM is blocking a screen reader or other
   assistive tech for a print disability, that's the one personal-use case US
   law actually permits.
4. **Libraries.** [Libby / OverDrive](https://libbyapp.com/) for borrowing.
   Reading, not owning — but no DRM crime and no cost.

## Path B — the community technical route (DRM circumvention; your call)

This is what the blog posts, forum threads, and GitHub repos actually describe.
The maintained toolchain today:

| Component | What / where |
|---|---|
| **Calibre** | The e-book manager that does the conversion. [calibre-ebook.com](https://calibre-ebook.com/) |
| **DeDRM plugin** | Use the **maintained fork: [`noDRM/DeDRM_tools`](https://github.com/noDRM/DeDRM_tools)**. The original `apprenticeharper/DeDRM_tools` is no longer maintained. |
| **KFX Input plugin** | jhowell's plugin, needed for Amazon's newer **KFX** format. Install from Calibre's plugin menu or the [MobileRead thread](https://www.mobileread.com/forums/showthread.php?t=291290). |

**How it fits together (macOS):**

1. Install Calibre, then add both the DeDRM plugin and the KFX Input plugin
   (*Preferences → Plugins → Load plugin from file* for DeDRM; *Get new plugins*
   for KFX Input). Restart Calibre.
2. Get the file onto disk. Two sources survive the Feb-2025 change:
   - **A physical Kindle e-reader.** Register its **serial number** in the
     DeDRM plugin config (*Customize plugin → eInk Kindle*). The serial is the
     decryption key for content that device downloaded. Older devices/firmware
     are the reliable case.
   - **An older Kindle for Mac.** The noDRM FAQ recommends **version 1.17 or
     earlier** — v1.19+ switched to KFX, which is a worse conversion source and
     a moving DRM target. Disable auto-update, or the app will silently upgrade
     itself out of the supported range. Downloaded books land in
     `~/Library/Containers/.../My Kindle Content` (path varies by app version).
3. Import the file into Calibre. **DeDRM only acts on import** — it strips DRM
   as the book enters the library. If import succeeds without a DRM error,
   you're through the hard part.
4. Right-click → *Convert books* → EPUB or PDF. (For fixed-layout / Print
   Replica titles, PDF preserves layout; EPUB reflows and may mangle it.)

**Where this breaks:**

- **KFX + newest apps.** The current app versions and post-mid-2025 books are
  the failure zone. Amazon has re-broken KFX DeDRM before and the noDRM FAQ
  openly warns it may again. Version-pinning advice you find (a specific Kindle
  for PC 2.x paired with a specific key-extractor build) is brittle and often
  Windows-only — verify against the noDRM FAQ rather than a vendor blog.
- **Apple Silicon.** Running the Windows Kindle app under Wine is unreliable
  (Wine's wow64 mode is incompatible with Kindle for PC's DRM library), which
  is why the device-serial route is more dependable on modern Macs.
- **Paid one-click tools** (Epubor, Aura, etc.) exist and automate this, but
  they perform the same DRM circumvention — paying for the tool doesn't change
  the legal status, so they buy you convenience, not legitimacy.

## iOS: not a viable source

Your local copies on the iPhone aren't a practical extraction point. The Kindle
iOS app stores books inside its encrypted app sandbox; iOS gives you no
supported way to pull those files out. Every workable method routes through the
Mac (desktop app) or a physical Kindle. Treat the phone as a reading endpoint,
not a source.

## Where the Claude toolset actually helps

Claude can't drive Calibre's GUI or the Kindle app on your Mac, and it can't
remove DRM for you. What it's genuinely good for here:

- **Research and triage** (this document), and re-checking the noDRM FAQ when a
  step stops working.
- **Batch conversion** of files you already hold, via Calibre's `ebook-convert`
  CLI. This operates on files on disk and removes no DRM, so it's clean. Drop
  this in `shell/functions.sh` if it earns its keep:

  ```bash
  # kindle-convert: batch-convert every ebook under a dir to EPUB (or $2) using
  # Calibre's CLI. Operates on files you already have; it does NOT remove DRM —
  # point it at DRM-free files. Usage: kindle-convert <dir> [epub|pdf|azw3]
  kindle-convert() {
    local src="${1:?usage: kindle-convert <dir> [epub|pdf|azw3]}"
    local fmt="${2:-epub}"
    local cli="/Applications/calibre.app/Contents/MacOS/ebook-convert"
    [ -x "$cli" ] || { echo "ebook-convert not found — install Calibre"; return 1; }
    find "$src" -type f \( -iname '*.azw3' -o -iname '*.mobi' -o -iname '*.kfx' -o -iname '*.epub' \) -print0 |
      while IFS= read -r -d '' f; do
        local out="${f%.*}.${fmt}"
        echo "→ $out"
        "$cli" "$f" "$out" || echo "  failed: $f"
      done
  }
  ```

- **Backups.** Once you have open-format files, Claude can help wire up a synced
  backup (the whole point of wanting portable copies is not losing them again).

## Sources

- Amazon removing "Download & Transfer via USB", Feb 26 2025 —
  [Good e-Reader](https://goodereader.com/blog/kindle/amazon-removing-download-and-transfer-on-the-kindle-feb-26th),
  [The Verge/Vice coverage](https://www.vice.com/en/article/amazon-is-killing-your-ability-to-download-kindle-books-next-week/),
  [Consumer Rights Wiki](https://consumerrights.wiki/w/Amazon_Kindle_removes_download_feature_of_purchased_books)
- DeDRM toolchain — [`noDRM/DeDRM_tools`](https://github.com/noDRM/DeDRM_tools),
  [FAQ](https://github.com/noDRM/DeDRM_tools/blob/master/FAQs.md)
- macOS-specific toolchain (note: its download step predates Feb 2025) —
  [`joelhooks/kindle-dedrm`](https://github.com/joelhooks/kindle-dedrm)
- KFX Input plugin — [jhowell's MobileRead thread](https://www.mobileread.com/forums/showthread.php?t=291290)
- US law — [US Copyright Office, 2024 §1201 rulemaking](https://www.copyright.gov/1201/2024/),
  [Federal Register final rule](https://www.federalregister.gov/documents/2024/10/28/2024-24563/exemption-to-prohibition-on-circumvention-of-copyright-protection-systems-for-access-control)
- DRM-free & public-domain sources —
  [Standard Ebooks](https://standardebooks.org/),
  [Project Gutenberg](https://www.gutenberg.org/)
