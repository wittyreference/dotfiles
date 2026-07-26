// ABOUTME: Host-side conversion -- line-oriented markdown stripping that preserves
// ABOUTME: blank lines, and assembly of a complete .rsvp sidecar image from text.

#include "convert.hpp"

#include "rsvp/format.hpp"
#include "rsvp/tokenizer.hpp"

#include <cstddef>
#include <cstdint>

namespace rsvpmk {
namespace {

bool isAlnum(char c) noexcept {
    const unsigned char b = static_cast<unsigned char>(c);
    return (b >= 'a' && b <= 'z') || (b >= 'A' && b <= 'Z') || (b >= '0' && b <= '9');
}

bool isSpaceOrTab(char c) noexcept { return c == ' ' || c == '\t'; }

/// True for a line that is only `-`, `*`, or `_` repeated three or more times.
bool isHorizontalRule(const std::string& line) noexcept {
    char marker = '\0';
    std::size_t count = 0u;
    for (const char c : line) {
        if (isSpaceOrTab(c)) {
            continue;
        }
        if (c != '-' && c != '*' && c != '_') {
            return false;
        }
        if (marker == '\0') {
            marker = c;
        } else if (c != marker) {
            return false;
        }
        ++count;
    }
    return count >= 3u;
}

/// True for a fenced-code delimiter (``` or ~~~), ignoring any info string.
bool isCodeFence(const std::string& line) noexcept {
    std::size_t i = 0u;
    while (i < line.size() && isSpaceOrTab(line[i])) {
        ++i;
    }
    if (i + 2u >= line.size()) {
        return false;
    }
    const char c = line[i];
    return (c == '`' || c == '~') && line[i + 1u] == c && line[i + 2u] == c;
}

/// Removes leading block markers: blockquote arrows, heading hashes, list bullets.
std::string stripBlockMarkers(const std::string& line) {
    std::size_t i = 0u;

    // Blockquotes nest, so consume every leading '>' layer with its padding.
    for (;;) {
        std::size_t probe = i;
        while (probe < line.size() && isSpaceOrTab(line[probe])) {
            ++probe;
        }
        if (probe < line.size() && line[probe] == '>') {
            i = probe + 1u;
            continue;
        }
        break;
    }
    while (i < line.size() && isSpaceOrTab(line[i])) {
        ++i;
    }

    // ATX heading: up to six hashes followed by whitespace.
    std::size_t hashes = i;
    while (hashes < line.size() && line[hashes] == '#') {
        ++hashes;
    }
    if (hashes > i && hashes - i <= 6u && hashes < line.size() && isSpaceOrTab(line[hashes])) {
        i = hashes;
        while (i < line.size() && isSpaceOrTab(line[i])) {
            ++i;
        }
        return line.substr(i);
    }

    // Unordered list bullet.
    if (i < line.size() && (line[i] == '-' || line[i] == '*' || line[i] == '+') &&
        i + 1u < line.size() && isSpaceOrTab(line[i + 1u])) {
        i += 2u;
        while (i < line.size() && isSpaceOrTab(line[i])) {
            ++i;
        }
        return line.substr(i);
    }

    // Ordered list marker: digits then '.' or ')' then whitespace.
    std::size_t digits = i;
    while (digits < line.size() && line[digits] >= '0' && line[digits] <= '9') {
        ++digits;
    }
    if (digits > i && digits < line.size() && (line[digits] == '.' || line[digits] == ')') &&
        digits + 1u < line.size() && isSpaceOrTab(line[digits + 1u])) {
        i = digits + 2u;
        while (i < line.size() && isSpaceOrTab(line[i])) {
            ++i;
        }
        return line.substr(i);
    }

    return line.substr(i);
}

/// Removes inline markup, keeping link text and dropping link targets and images.
std::string stripInlineMarkers(const std::string& line) {
    std::string out;
    out.reserve(line.size());

    for (std::size_t i = 0u; i < line.size();) {
        const char c = line[i];

        // Image: ![alt](target) -- dropped whole, alt text included. An image has no
        // spoken form, and its alt text mid-sentence reads as a non sequitur.
        if (c == '!' && i + 1u < line.size() && line[i + 1u] == '[') {
            const std::size_t close = line.find(']', i + 2u);
            if (close != std::string::npos && close + 1u < line.size() && line[close + 1u] == '(') {
                const std::size_t end = line.find(')', close + 2u);
                if (end != std::string::npos) {
                    i = end + 1u;
                    continue;
                }
            }
        }

        // Link: [text](target) -- keep the text, drop the target.
        if (c == '[') {
            const std::size_t close = line.find(']', i + 1u);
            if (close != std::string::npos && close + 1u < line.size() && line[close + 1u] == '(') {
                const std::size_t end = line.find(')', close + 2u);
                if (end != std::string::npos) {
                    out += stripInlineMarkers(line.substr(i + 1u, close - i - 1u));
                    i = end + 1u;
                    continue;
                }
            }
        }

        if (c == '`' || c == '*') {
            ++i;  // Emphasis and code markers are never content.
            continue;
        }

        // An underscore is only a marker at a word boundary. Dropping it
        // unconditionally would mangle snake_case identifiers appearing in prose.
        if (c == '_') {
            const bool insideWord =
                i > 0u && isAlnum(line[i - 1u]) && i + 1u < line.size() && isAlnum(line[i + 1u]);
            if (!insideWord) {
                ++i;
                continue;
            }
        }

        out += c;
        ++i;
    }
    return out;
}

}  // namespace

std::string markdownToText(const std::string& markdown) {
    std::string out;
    out.reserve(markdown.size());

    bool inFence = false;
    bool firstLine = true;
    std::size_t pos = 0u;

    for (;;) {
        std::size_t newline = markdown.find('\n', pos);
        const bool lastLine = newline == std::string::npos;
        if (lastLine) {
            newline = markdown.size();
        }

        std::string line = markdown.substr(pos, newline - pos);

        // Strip a trailing CR so CRLF input behaves like LF input.
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }

        std::string emitted;
        if (isCodeFence(line)) {
            inFence = !inFence;
        } else if (inFence || isHorizontalRule(line)) {
            // Dropped: source code and rules are not prose. An empty line is left in
            // their place, which preserves the blank-line runs that paragraph
            // detection depends on.
        } else {
            emitted = stripInlineMarkers(stripBlockMarkers(line));
        }

        if (!firstLine) {
            out += '\n';
        }
        out += emitted;
        firstLine = false;

        if (lastLine) {
            break;
        }
        pos = newline + 1u;
    }

    return out;
}

std::vector<unsigned char> buildRsvp(const std::string& text) {
    std::vector<rsvp::Token> tokens;
    rsvp::Tokenizer tokenizer(text.data(), text.size());
    rsvp::Token token{};
    while (tokenizer.next(token)) {
        tokens.push_back(token);
    }

    const std::uint32_t tokenCount = static_cast<std::uint32_t>(tokens.size());
    const std::uint32_t textLength = static_cast<std::uint32_t>(text.size());

    // The buffer comes from the allocator, which returns memory aligned for any
    // fundamental type, so the token array inside it satisfies kRsvpAlignment and
    // rsvpTokens hands back a usable pointer. The round-trip test fails loudly if
    // that ever stops holding.
    std::vector<unsigned char> out(rsvp::rsvpFileSize(tokenCount, textLength));

    std::size_t written = 0u;
    const rsvp::RsvpStatus status = rsvp::writeRsvp(tokens.data(), tokenCount, text.data(),
                                                    textLength, out.data(), out.size(), written);
    if (status != rsvp::RsvpStatus::kOk) {
        return {};
    }
    out.resize(written);
    return out;
}

}  // namespace rsvpmk
