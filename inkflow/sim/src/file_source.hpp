// ABOUTME: A ByteSource over a host file, so the streaming sidecar path the device runs
// ABOUTME: on SD can be exercised on a laptop against the same real .rsvp file.

#pragma once

#include <cstdint>
#include <cstdio>

#include "reader/source.hpp"

namespace sim {

/// Reads a `.rsvp` sidecar off the host filesystem.
class FileSource : public reader::ByteSource {
public:
    explicit FileSource(const char* path) {
        file_ = std::fopen(path, "rb");
        if (file_ == nullptr) {
            return;
        }
        std::fseek(file_, 0, SEEK_END);
        const long end = std::ftell(file_);
        size_ = end > 0 ? static_cast<uint32_t>(end) : 0u;
        std::fseek(file_, 0, SEEK_SET);
    }

    ~FileSource() { close(); }

    FileSource(const FileSource&) = delete;
    FileSource& operator=(const FileSource&) = delete;

    bool valid() const override { return file_ != nullptr; }
    uint32_t size() const override { return size_; }

    bool seek(uint32_t position) override {
        return file_ != nullptr && std::fseek(file_, static_cast<long>(position), SEEK_SET) == 0;
    }

    size_t read(uint8_t* out, size_t n) override {
        return file_ != nullptr ? std::fread(out, 1, n, file_) : 0u;
    }

    void close() override {
        if (file_ != nullptr) {
            std::fclose(file_);
            file_ = nullptr;
        }
    }

private:
    std::FILE* file_ = nullptr;
    uint32_t size_ = 0;
};

}  // namespace sim
