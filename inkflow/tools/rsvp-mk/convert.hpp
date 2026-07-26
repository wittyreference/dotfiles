// ABOUTME: Host-side conversion helpers -- markdown to plain prose, and assembly of
// ABOUTME: a complete .rsvp sidecar buffer from text.

#ifndef RSVPMK_CONVERT_HPP
#define RSVPMK_CONVERT_HPP

#include <string>
#include <vector>

namespace rsvpmk {

/// Reduces markdown to the prose a reader actually wants flashed at them.
///
/// Deliberately minimal and line-oriented, not a real markdown parser. It strips
/// the markers that would otherwise be read aloud as words -- heading hashes, list
/// bullets, blockquote arrows, emphasis runs, inline backticks -- keeps link text
/// while dropping URLs, and discards fenced code blocks, images, and horizontal
/// rules entirely. Reading a URL or a block of source one word at a time is noise.
///
/// **Blank lines are preserved.** This is load-bearing rather than incidental:
/// paragraph detection in the tokenizer keys off a blank line, so collapsing them
/// would silently remove every paragraph pause in the document.
///
/// This lives in the host tool rather than in rsvp-core because it allocates
/// freely and the engine may not.
std::string markdownToText(const std::string& markdown);

/// Tokenises `text` and returns a complete `.rsvp` file image.
///
/// Returns an empty vector only if serialisation fails, which for a
/// correctly-sized buffer cannot happen -- the size is computed from the same
/// function the writer validates against.
std::vector<unsigned char> buildRsvp(const std::string& text);

}  // namespace rsvpmk

#endif  // RSVPMK_CONVERT_HPP
