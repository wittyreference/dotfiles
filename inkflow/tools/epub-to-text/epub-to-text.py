#!/usr/bin/env python3
# ABOUTME: Extracts reading text from an EPUB in spine order, for feeding to rsvp-mk.
# ABOUTME: Python rather than C++ because EPUB is a zip of XHTML and both are stdlib here.

"""Turn an EPUB into the plain prose an RSVP reader can play.

Deliberately not a full EPUB renderer. It walks the spine so chapters come out in
reading order, strips markup, and preserves paragraph breaks -- which are load-bearing,
because the tokenizer keys its paragraph pauses off blank lines.

Lives in Python rather than in rsvp-mk because an EPUB is a zip of XHTML, and Python's
standard library reads both. Doing it in C++ would mean vendoring a zip library, an
inflate implementation and an XML parser for a tool that only ever runs on a laptop.
"""

import argparse
import html
import posixpath
import re
import sys
import zipfile
from xml.etree import ElementTree as ET

OPF_NS = {"o": "http://www.idpf.org/2007/opf"}
CONTAINER_NS = "{urn:oasis:names:tc:opendocument:xmlns:container}"
DC_TITLE = "{http://purl.org/dc/elements/1.1/}title"

# Closing tags that end a block, and therefore a paragraph.
BLOCK_END = re.compile(r"</(p|div|h[1-6]|li|blockquote|section|tr|td)\s*>", re.I)

# Typographic characters the device's fonts do not carry, mapped to ASCII. A missing
# glyph renders as a blank box mid-word, which is worse than a plain quote.
SUBSTITUTIONS = {
    " ": " ", "’": "'", "‘": "'", "“": '"', "”": '"',
    "—": " -- ", "–": "-", "…": "...", "­": "",
}


def html_to_text(markup: str) -> str:
    """Reduce one XHTML document to prose, keeping paragraph breaks."""
    markup = re.sub(r"(?is)<(script|style|head)[^>]*>.*?</\1>", " ", markup)
    markup = BLOCK_END.sub("\n\n", markup)
    markup = re.sub(r"(?i)<br\s*/?>", "\n", markup)
    markup = re.sub(r"<[^>]+>", "", markup)
    markup = html.unescape(markup)
    for src, dst in SUBSTITUTIONS.items():
        markup = markup.replace(src, dst)
    markup = re.sub(r"[ \t]+", " ", markup)
    lines = [line.strip() for line in markup.split("\n")]
    return re.sub(r"\n{3,}", "\n\n", "\n".join(lines)).strip()


def extract(path: str, skip_short: int) -> tuple[str, str]:
    """Returns (title, text) for the EPUB at `path`."""
    with zipfile.ZipFile(path) as z:
        container = ET.fromstring(z.read("META-INF/container.xml"))
        rootfile = container.find(f".//{CONTAINER_NS}rootfile")
        if rootfile is None:
            raise SystemExit("epub-to-text: no rootfile in container.xml")
        opf_path = rootfile.get("full-path")
        base = posixpath.dirname(opf_path)

        opf = ET.fromstring(z.read(opf_path))
        title_el = opf.find(f".//{DC_TITLE}")
        title = (title_el.text or "untitled").strip() if title_el is not None else "untitled"

        manifest = {i.get("id"): i.get("href") for i in opf.findall(".//o:manifest/o:item", OPF_NS)}
        spine = [it.get("idref") for it in opf.findall(".//o:spine/o:itemref", OPF_NS)]

        parts = []
        for idref in spine:
            href = manifest.get(idref)
            if not href:
                continue
            member = posixpath.normpath(posixpath.join(base, href)) if base else href
            try:
                raw = z.read(member).decode("utf-8", "replace")
            except KeyError:
                continue
            text = html_to_text(raw)
            # Covers, title pages and colophons are a few words of furniture that would
            # otherwise open the book with a stray fragment.
            if len(text) >= skip_short:
                parts.append(text)

    return title, re.sub(r"\n{3,}", "\n\n", "\n\n".join(parts))


def main() -> int:
    ap = argparse.ArgumentParser(description="Extract reading text from an EPUB.")
    ap.add_argument("epub")
    ap.add_argument("-o", "--output", help="output .txt (default: alongside the epub)")
    ap.add_argument("--skip-short", type=int, default=200, metavar="N",
                    help="drop spine documents under N characters (default 200)")
    args = ap.parse_args()

    title, text = extract(args.epub, args.skip_short)
    if not text:
        print("epub-to-text: no readable text found", file=sys.stderr)
        return 1

    out = args.output or re.sub(r"\.epub$", ".txt", args.epub, flags=re.I)
    with open(out, "w", encoding="utf-8") as f:
        f.write(text)

    words = len(text.split())
    print(f"{title}")
    print(f"  {out}")
    print(f"  {len(text):,} chars, {words:,} words")
    # 3 words per update at the measured 542ms panel floor.
    print(f"  ~{words / 3 * 0.542 / 60:.0f} min at 3 words/update")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
