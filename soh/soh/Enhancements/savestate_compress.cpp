#include "soh/Enhancements/savestate_compress.h"

#include <StormLib.h>

namespace SaveStateCompress {

std::vector<uint8_t> Compress(const void* in, size_t inSize) {
    // SCompCompress works on 32-bit sizes and requires the output buffer to be at least the input size.
    if (in == nullptr || inSize == 0 || inSize > 0x7FFFFFFFu) {
        return {};
    }
    std::vector<uint8_t> out(inSize);
    int outLen = (int)inSize;
    // Level is ignored by StormLib's zlib path (it uses zlib's default); pass 0.
    int ok = SCompCompress(out.data(), &outLen, const_cast<void*>(in), (int)inSize, MPQ_COMPRESSION_ZLIB, 0, 0);
    if (!ok || outLen <= 0 || (size_t)outLen >= inSize) {
        return {}; // failed, or no size gain -> caller stores raw
    }
    out.resize((size_t)outLen);
    return out;
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
