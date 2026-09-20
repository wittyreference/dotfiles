// ABOUTME: A reader::ByteSource over an Arduino File, so the sidecar streaming path the
// ABOUTME: device runs off microSD is the same code the host tests run off a file.

#pragma once

#include <FS.h>
#include <SD.h>

#include "reader/source.hpp"

/// Reads a `.rsvp` sidecar from the card.
///
/// Held open for the life of the document rather than reopened per read: the token cache
/// crosses a block boundary every 256 tokens, and an open-seek-close around each of those
/// would put the card's mount cost in the reading path.
class SdSource : public reader::ByteSource {
public:
    /// Opens `path` on the card, replacing whatever was open before.
    bool open(const char* path) {
        close();
        file_ = SD.open(path, FILE_READ);
        return static_cast<bool>(file_);
    }

    bool valid() const override { return static_cast<bool>(file_); }
    uint32_t size() const override { return static_cast<uint32_t>(file_.size()); }

    bool seek(uint32_t position) override { return file_ && file_.seek(position); }

    /// Reads up to `n` bytes, reporting a short read as zero rather than as a count.
    ///
    /// The guard is not redundant: Arduino's File::read returns -1 on a closed file, which
    /// as a size_t is SIZE_MAX, and a caller adding that to an offset would wrap.
    size_t read(uint8_t* out, size_t n) override { return file_ ? file_.read(out, n) : 0u; }

    void close() override {
        if (file_) {
            file_.close();
        }
    }

private:
    File file_;
};
