// ABOUTME: Minimal greyscale PNG writer for the simulator, using zlib for the one
// ABOUTME: compressed stream a PNG requires. Enough to look at, nothing more.

#include "canvas.hpp"

#include <zlib.h>

#include <cstdio>
#include <vector>

namespace sim {
namespace {

void be32(std::vector<uint8_t>& out, uint32_t v) {
    out.push_back(uint8_t(v >> 24));
    out.push_back(uint8_t(v >> 16));
    out.push_back(uint8_t(v >> 8));
    out.push_back(uint8_t(v));
}

void chunk(std::vector<uint8_t>& out, const char* type, const std::vector<uint8_t>& data) {
    be32(out, uint32_t(data.size()));
    const size_t start = out.size();
    out.insert(out.end(), type, type + 4);
    out.insert(out.end(), data.begin(), data.end());
    const uLong c = crc32(crc32(0L, Z_NULL, 0), out.data() + start,
                          uInt(out.size() - start));
    be32(out, uint32_t(c));
}

}  // namespace

bool Canvas::writePng(const char* path) const {
    // Each scanline is prefixed with a filter byte; 0 means "no filter", which keeps
    // this writer to the minimum a decoder will accept.
    std::vector<uint8_t> raw;
    raw.reserve(size_t(h_) * (size_t(w_) + 1));
    for (int y = 0; y < h_; ++y) {
        raw.push_back(0);
        raw.insert(raw.end(), px_ + size_t(y) * size_t(w_), px_ + size_t(y + 1) * size_t(w_));
    }

    uLongf bound = compressBound(uLong(raw.size()));
    std::vector<uint8_t> deflated(bound);
    if (compress2(deflated.data(), &bound, raw.data(), uLong(raw.size()), 9) != Z_OK) {
        return false;
    }
    deflated.resize(bound);

    std::vector<uint8_t> png = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};
    std::vector<uint8_t> ihdr;
    be32(ihdr, uint32_t(w_));
    be32(ihdr, uint32_t(h_));
    ihdr.push_back(8);  // 8 bits per sample
    ihdr.push_back(0);  // greyscale
    ihdr.push_back(0);  // deflate
    ihdr.push_back(0);  // adaptive filtering
    ihdr.push_back(0);  // no interlace
    chunk(png, "IHDR", ihdr);
    chunk(png, "IDAT", deflated);
    chunk(png, "IEND", {});

    std::FILE* f = std::fopen(path, "wb");
    if (f == nullptr) {
        return false;
    }
    const bool ok = std::fwrite(png.data(), 1, png.size(), f) == png.size();
    std::fclose(f);
    return ok;
}

}  // namespace sim
