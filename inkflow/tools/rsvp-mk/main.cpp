// ABOUTME: rsvp-mk entry point -- reads a text or markdown file and writes the .rsvp
// ABOUTME: sidecar a device streams during playback, reporting what it produced.

#include "convert.hpp"

#include "rsvp/format.hpp"
#include "rsvp/player.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

namespace {

constexpr std::uint16_t kDefaultWpm = 300u;

void usage() {
    std::fputs(
        "rsvp-mk -- build an .rsvp sidecar from a text or markdown file\n"
        "\n"
        "usage: rsvp-mk <input> [-o <output>] [--wpm <n>] [--raw]\n"
        "\n"
        "  -o <output>  output path (default: <input> with a .rsvp extension)\n"
        "  --wpm <n>    words per minute for the reading-time estimate (default 300);\n"
        "               does not affect the file, which stores no durations so speed\n"
        "               stays adjustable on the device\n"
        "  --raw        treat the input as plain text, skipping markdown stripping\n",
        stderr);
}

/// Reads a whole file. Returns false and leaves `out` untouched on any failure.
bool readFile(const char* path, std::string& out) {
    std::FILE* file = std::fopen(path, "rb");
    if (file == nullptr) {
        return false;
    }

    std::string contents;
    char buffer[65536];
    for (;;) {
        const std::size_t got = std::fread(buffer, 1u, sizeof(buffer), file);
        contents.append(buffer, got);
        if (got < sizeof(buffer)) {
            break;
        }
    }
    const bool failed = std::ferror(file) != 0;
    std::fclose(file);

    if (failed) {
        return false;
    }
    out = contents;
    return true;
}

bool writeFile(const char* path, const std::vector<unsigned char>& bytes) {
    std::FILE* file = std::fopen(path, "wb");
    if (file == nullptr) {
        return false;
    }
    const std::size_t wrote =
        bytes.empty() ? 0u : std::fwrite(bytes.data(), 1u, bytes.size(), file);
    const bool ok = wrote == bytes.size() && std::ferror(file) == 0;
    return std::fclose(file) == 0 && ok;
}

/// Replaces a trailing extension with `.rsvp`, or appends one if there is none.
std::string defaultOutputPath(const std::string& input) {
    const std::size_t slash = input.find_last_of('/');
    const std::size_t dot = input.find_last_of('.');
    const bool hasExtension =
        dot != std::string::npos && (slash == std::string::npos || dot > slash);
    return (hasExtension ? input.substr(0u, dot) : input) + ".rsvp";
}

bool looksLikeMarkdown(const std::string& path) {
    const std::size_t dot = path.find_last_of('.');
    if (dot == std::string::npos) {
        return false;
    }
    const std::string ext = path.substr(dot);
    return ext == ".md" || ext == ".markdown" || ext == ".mdown";
}

}  // namespace

int main(int argc, char** argv) {
    const char* inputPath = nullptr;
    std::string outputPath;
    std::uint16_t wpm = kDefaultWpm;
    bool raw = false;

    for (int i = 1; i < argc; ++i) {
        const char* arg = argv[i];
        if (std::strcmp(arg, "-h") == 0 || std::strcmp(arg, "--help") == 0) {
            usage();
            return 0;
        }
        if (std::strcmp(arg, "--raw") == 0) {
            raw = true;
        } else if (std::strcmp(arg, "-o") == 0) {
            if (++i >= argc) {
                std::fputs("rsvp-mk: -o needs a path\n", stderr);
                return 2;
            }
            outputPath = argv[i];
        } else if (std::strcmp(arg, "--wpm") == 0) {
            if (++i >= argc) {
                std::fputs("rsvp-mk: --wpm needs a number\n", stderr);
                return 2;
            }
            const long parsed = std::strtol(argv[i], nullptr, 10);
            if (parsed < 1 || parsed > 2000) {
                std::fputs("rsvp-mk: --wpm must be between 1 and 2000\n", stderr);
                return 2;
            }
            wpm = static_cast<std::uint16_t>(parsed);
        } else if (arg[0] == '-' && arg[1] != '\0') {
            std::fprintf(stderr, "rsvp-mk: unknown option %s\n", arg);
            return 2;
        } else if (inputPath == nullptr) {
            inputPath = arg;
        } else {
            std::fputs("rsvp-mk: more than one input given\n", stderr);
            return 2;
        }
    }

    if (inputPath == nullptr) {
        usage();
        return 2;
    }

    std::string source;
    if (!readFile(inputPath, source)) {
        std::fprintf(stderr, "rsvp-mk: cannot read %s\n", inputPath);
        return 1;
    }

    const bool stripMarkdown = !raw && looksLikeMarkdown(inputPath);
    const std::string text = stripMarkdown ? rsvpmk::markdownToText(source) : source;

    const std::vector<unsigned char> file = rsvpmk::buildRsvp(text);
    if (file.empty()) {
        std::fputs("rsvp-mk: failed to build the sidecar\n", stderr);
        return 1;
    }

    if (outputPath.empty()) {
        outputPath = defaultOutputPath(inputPath);
    }
    if (!writeFile(outputPath.c_str(), file)) {
        std::fprintf(stderr, "rsvp-mk: cannot write %s\n", outputPath.c_str());
        return 1;
    }

    rsvp::RsvpHeader header{};
    if (rsvp::readRsvpHeader(file.data(), file.size(), header) != rsvp::RsvpStatus::kOk) {
        std::fputs("rsvp-mk: wrote a file the reader rejects -- this is a bug\n", stderr);
        return 1;
    }

    // Report a reading-time estimate from the real timing model rather than a
    // word-count division: pauses and length bonuses make the honest figure
    // noticeably longer than words/wpm, and quoting the naive number would set the
    // wrong expectation.
    rsvp::TimingConfig config{};
    config.wpm = wpm;
    config.rampTokens = 0u;

    const rsvp::Token* tokens = rsvp::rsvpTokens(file.data(), header);
    std::uint32_t totalMs = 0u;
    if (tokens != nullptr) {
        rsvp::Player player(tokens, header.tokenCount, config);
        totalMs = player.remainingMs();
    }

    std::printf("%s -> %s\n", inputPath, outputPath.c_str());
    std::printf("  tokens    %u\n", header.tokenCount);
    std::printf("  text      %u bytes\n", header.textLength);
    std::printf("  file      %zu bytes\n", file.size());
    if (stripMarkdown) {
        std::printf("  markdown  stripped\n");
    }
    std::printf("  at %u wpm  %um %02us\n", wpm, totalMs / 60000u, (totalMs / 1000u) % 60u);

    return 0;
}
