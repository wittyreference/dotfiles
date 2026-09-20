// ABOUTME: A seekable byte source, so the streaming document path is the same code on
// ABOUTME: an SD card and on a host filesystem.

#pragma once

#include <cstddef>
#include <cstdint>

namespace reader {

/// Random-access reads over something that holds a `.rsvp` sidecar.
///
/// The sidecar exists to be seeked into rather than loaded: the test volume is 98,633
/// tokens over a 1.4 MB file against 400 KB of SRAM. Abstracting the source is what lets
/// that exact path -- aligned block cache and all -- run under test on a host instead of
/// only on hardware where nobody can observe it.
class ByteSource {
public:
    virtual bool valid() const = 0;
    virtual uint32_t size() const = 0;
    virtual bool seek(uint32_t position) = 0;
    /// Reads up to `n` bytes. Returns how many were read.
    virtual size_t read(uint8_t* out, size_t n) = 0;
    virtual void close() = 0;

protected:
    ~ByteSource() = default;
};

}  // namespace reader
