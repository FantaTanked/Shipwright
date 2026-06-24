#include "soh/Enhancements/savestate_compress.h"

#include <StormLib.h>
#include <memory>

namespace SaveStateCompress {

std::vector<uint8_t> Compress(const void* in, size_t inSize) {
    // SCompCompress works on 32-bit sizes and requires the output buffer to be at least the input size.
    if (in == nullptr || inSize == 0 || inSize > 0x7FFFFFFFu) {
        return {};
    }
    // SCompCompress needs a worst-case (input-sized) scratch buffer, but the result is ~1-2%. Use a default-
    // initialized array so we don't pay an input-sized zero-fill the compressor immediately overwrites, then
    // copy only the compressed prefix into the returned vector.
    std::unique_ptr<uint8_t[]> scratch(new uint8_t[inSize]);
    int outLen = (int)inSize;
    // Level is ignored by StormLib's zlib path (it uses zlib's default); pass 0.
    int ok = SCompCompress(scratch.get(), &outLen, const_cast<void*>(in), (int)inSize, MPQ_COMPRESSION_ZLIB, 0, 0);
    if (!ok || outLen <= 0 || (size_t)outLen >= inSize) {
        return {}; // failed, or no size gain -> caller stores raw
    }
    return std::vector<uint8_t>(scratch.get(), scratch.get() + outLen);
}

bool Decompress(const void* in, size_t inSize, void* out, size_t outSize) {
    if (in == nullptr || out == nullptr || inSize == 0 || inSize > 0x7FFFFFFFu || outSize == 0 ||
        outSize > 0x7FFFFFFFu) {
        return false;
    }
    int outLen = (int)outSize;
    int ok = SCompDecompress(out, &outLen, const_cast<void*>(in), (int)inSize);
    return ok && (size_t)outLen == outSize;
}

} // namespace SaveStateCompress
