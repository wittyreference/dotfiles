#!/usr/bin/env python3
# ABOUTME: Tests for epub-to-text, run against real EPUBs built with zipfile and the
# ABOUTME: real script in a subprocess -- no stubs, no import-time monkeying.

"""Exercises epub-to-text end to end.

Every case writes a genuine EPUB to a temporary directory -- a stored `mimetype`,
`META-INF/container.xml`, an OPF with a manifest and a spine, and XHTML documents --
and runs the shipped script over it. That is the only way to defend spine ordering
and the CLI's exit codes, both of which live outside any single function.

Run directly (`python3 test_epub_to_text.py`) or through ctest as `epub_to_text_tests`.
Standard library only: the project builds with no network, and a test dependency
would end that.
"""

import os
import subprocess
import sys
import tempfile
import unittest
import zipfile

SCRIPT = os.path.join(os.path.dirname(os.path.abspath(__file__)), os.pardir,
                      "epub-to-text.py")

CONTAINER = """<?xml version="1.0" encoding="utf-8"?>
<container version="1.0" xmlns="urn:oasis:names:tc:opendocument:xmlns:container">
  <rootfiles>
    <rootfile full-path="OEBPS/content.opf" media-type="application/oebps-package+xml"/>
  </rootfiles>
</container>
"""

XHTML = """<?xml version="1.0" encoding="utf-8"?>
<!DOCTYPE html>
<html xmlns="http://www.w3.org/1999/xhtml">
<head><title>{title}</title><style>p {{ margin: 0; }}</style></head>
<body>
{body}
</body>
</html>
"""


def build_epub(path, chapters, spine, manifest_order=None, omit=(),
               title="The Panel Refreshes"):
    """Writes a real EPUB at `path`.

    `chapters` is a list of (item_id, href, body_markup). `spine` is the list of item
    ids in reading order. `manifest_order` reorders the manifest independently of the
    spine, which is what makes a spine-ordering test mean something. Ids in `omit` are
    declared in the manifest but their file is left out of the archive, modelling a
    damaged book.
    """
    by_id = {item_id: (href, body) for item_id, href, body in chapters}
    manifest_ids = manifest_order if manifest_order is not None else [c[0] for c in chapters]

    items = "\n".join(
        f'    <item id="{i}" href="{by_id[i][0]}" media-type="application/xhtml+xml"/>'
        for i in manifest_ids
    )
    itemrefs = "\n".join(f'    <itemref idref="{i}"/>' for i in spine)
    opf = f"""<?xml version="1.0" encoding="utf-8"?>
<package xmlns="http://www.idpf.org/2007/opf" version="3.0" unique-identifier="bookid">
  <metadata xmlns:dc="http://purl.org/dc/elements/1.1/">
    <dc:title>{title}</dc:title>
    <dc:identifier id="bookid">urn:uuid:inkflow-test</dc:identifier>
  </metadata>
  <manifest>
{items}
  </manifest>
  <spine>
{itemrefs}
  </spine>
</package>
"""

    with zipfile.ZipFile(path, "w", zipfile.ZIP_DEFLATED) as z:
        # The spec wants `mimetype` first and uncompressed. Readers that check will
        # reject the file otherwise, so the fixture honours it to stay a real EPUB.
        z.writestr(zipfile.ZipInfo("mimetype"), "application/epub+zip",
                   compress_type=zipfile.ZIP_STORED)
        z.writestr("META-INF/container.xml", CONTAINER)
        z.writestr("OEBPS/content.opf", opf)
        for item_id, href, body in chapters:
            if item_id in omit:
                continue
            z.writestr(f"OEBPS/{href}", XHTML.format(title=item_id, body=body))
    return path


def paragraphs(*lines):
    return "\n".join(f"<p>{line}</p>" for line in lines)


class EpubToTextTest(unittest.TestCase):
    def setUp(self):
        self._tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self._tmp.cleanup)
        self.dir = self._tmp.name

    def run_tool(self, epub, *args):
        return subprocess.run([sys.executable, SCRIPT, epub, *args],
                              capture_output=True, text=True)

    def convert(self, chapters, spine, *args, **kwargs):
        """Builds an EPUB, converts it, and returns (result, extracted text or None)."""
        epub = build_epub(os.path.join(self.dir, "book.epub"), chapters, spine, **kwargs)
        result = self.run_tool(epub, *args)
        out = os.path.join(self.dir, "book.txt")
        if not os.path.exists(out):
            return result, None
        with open(out, encoding="utf-8") as f:
            return result, f.read()

    def test_spine_order_is_reading_order(self):
        """The spine is the reading order; the manifest is only a catalogue.

        The manifest here lists the chapters backwards, so a walk of the manifest
        instead of the spine would emit them backwards and this would catch it.
        """
        chapters = [
            ("ch1", "ch1.xhtml", paragraphs("Alpha opens the book.")),
            ("ch2", "ch2.xhtml", paragraphs("Beta closes the book.")),
        ]
        result, text = self.convert(chapters, ["ch1", "ch2"], "--skip-short", "0",
                                    manifest_order=["ch2", "ch1"])
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(result.stderr, "")
        self.assertIn("Alpha opens the book.", text)
        self.assertIn("Beta closes the book.", text)
        self.assertLess(text.index("Alpha"), text.index("Beta"),
                        f"spine order not preserved: {text!r}")

    def test_blank_lines_separate_paragraphs(self):
        """Blank lines are load-bearing, not cosmetic.

        The tokenizer keys its paragraph pauses off blank lines, so collapsing them
        flattens the timing model into an undifferentiated stream, silently.
        """
        chapters = [("ch1", "ch1.xhtml",
                     paragraphs("First paragraph.", "Second paragraph.",
                                "Third paragraph."))]
        result, text = self.convert(chapters, ["ch1"], "--skip-short", "0")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(
            text, "First paragraph.\n\nSecond paragraph.\n\nThird paragraph.")

    def test_blank_line_survives_between_spine_documents(self):
        chapters = [
            ("ch1", "ch1.xhtml", paragraphs("End of one.")),
            ("ch2", "ch2.xhtml", paragraphs("Start of two.")),
        ]
        result, text = self.convert(chapters, ["ch1", "ch2"], "--skip-short", "0")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(text, "End of one.\n\nStart of two.")

    def test_typography_is_folded_to_ascii(self):
        """The device fonts carry no curly quotes, dashes or ellipsis.

        A glyph the font lacks renders as a blank box in the middle of a word, so
        each of these has to arrive as ASCII or the page is unreadable.
        """
        body = paragraphs(
            "“Wait,” she said—quietly. It’s 3–4 "
            "minutes… no break, co­operate."
        )
        result, text = self.convert([("ch1", "ch1.xhtml", body)], ["ch1"],
                                    "--skip-short", "0")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertTrue(text.isascii(), f"non-ASCII survived: {text!r}")
        self.assertEqual(
            text,
            '"Wait," she said -- quietly. It\'s 3-4 minutes... no break, cooperate.')

    def test_html_entities_are_decoded_before_substitution(self):
        """Entities resolve first, so `&mdash;` reaches the substitution table as a
        character rather than passing through as the literal word.

        The table folds punctuation the device fonts lack; accented letters are not
        in it, so an e-acute arrives unchanged. That is the boundary as it stands.
        """
        body = paragraphs("It was a caf&eacute;, not a laboratory.",
                          "Wait&mdash;stop. 5 &lt; 6 &amp; 7.")
        result, text = self.convert([("ch1", "ch1.xhtml", body)], ["ch1"],
                                    "--skip-short", "0")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(
            text,
            "It was a café, not a laboratory.\n\nWait -- stop. 5 < 6 & 7.")

    def test_skip_short_drops_furniture_but_keeps_a_chapter(self):
        """Covers and colophons are a few words that would open the book mid-fragment."""
        chapter_text = ("The panel settles at its own pace, and the words arrive one "
                        "group at a time, which is the whole point of the exercise, "
                        "and it runs on for long enough that nobody could mistake it "
                        "for a title page or a colophon or any other piece of the "
                        "furniture that opens a book.")
        self.assertGreater(len(chapter_text), 200)
        chapters = [
            ("cover", "cover.xhtml", paragraphs("Cover")),
            ("ch1", "ch1.xhtml", paragraphs(chapter_text)),
        ]

        result, text = self.convert(chapters, ["cover", "ch1"], "--skip-short", "200")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertNotIn("Cover", text)
        self.assertIn(chapter_text, text)

        # The same book with the threshold off keeps the cover, which proves the flag
        # did the dropping rather than some unrelated part of the pipeline.
        result, text = self.convert(chapters, ["cover", "ch1"], "--skip-short", "0")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("Cover", text)

    def test_spine_member_missing_from_the_archive_is_reported(self):
        """A damaged book loses a chapter either way; saying so is the difference
        between a known hole and a silent one. The rest still extracts, so the
        output remains usable."""
        chapters = [
            ("ch1", "ch1.xhtml", paragraphs("Alpha opens the book.")),
            ("ghost", "ghost.xhtml", paragraphs("Never written to the archive.")),
            ("ch2", "ch2.xhtml", paragraphs("Beta closes the book.")),
        ]
        result, text = self.convert(chapters, ["ch1", "ghost", "ch2"],
                                    "--skip-short", "0", omit={"ghost"})
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("ghost.xhtml", result.stderr)
        self.assertIn("Alpha opens the book.", text)
        self.assertIn("Beta closes the book.", text)
        self.assertNotIn("Never written", text)

    def test_exits_one_when_there_is_no_readable_text(self):
        chapters = [("ch1", "ch1.xhtml", "<p></p>")]
        result, text = self.convert(chapters, ["ch1"], "--skip-short", "0")
        self.assertEqual(result.returncode, 1)
        self.assertIn("no readable text found", result.stderr)
        self.assertIsNone(text, "an empty extraction must not leave an output file")

    def test_output_lands_beside_the_epub_and_reports_the_title(self):
        chapters = [("ch1", "ch1.xhtml", paragraphs("Alpha opens the book."))]
        result, text = self.convert(chapters, ["ch1"], "--skip-short", "0")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(text, "Alpha opens the book.")
        self.assertIn("The Panel Refreshes", result.stdout)
        self.assertIn(os.path.join(self.dir, "book.txt"), result.stdout)

    def test_output_flag_overrides_the_default_path(self):
        epub = build_epub(os.path.join(self.dir, "book.epub"),
                          [("ch1", "ch1.xhtml", paragraphs("Alpha opens the book."))],
                          ["ch1"])
        chosen = os.path.join(self.dir, "elsewhere.txt")
        result = self.run_tool(epub, "-o", chosen, "--skip-short", "0")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(result.stderr, "")
        self.assertFalse(os.path.exists(os.path.join(self.dir, "book.txt")))
        with open(chosen, encoding="utf-8") as f:
            self.assertEqual(f.read(), "Alpha opens the book.")


if __name__ == "__main__":
    unittest.main(verbosity=2)
