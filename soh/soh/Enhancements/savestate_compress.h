#ifndef SAVESTATE_COMPRESS_H
#define SAVESTATE_COMPRESS_H

#include <cstddef>
#include <cstdint>
#include <vector>

// Thin zlib compress/decompress over StormLib's SComp API, in its own translation unit so StormLib.h doesn't
// leak elsewhere. Shrinks the ~8.5 MiB savestate blob (roughly half zeros) to ~1-2% of its size.
namespace SaveStateCompress {

// Compress `inSize` bytes from `in`. Returns the compressed bytes, or an EMPTY vector if compression failed or
// did not shrink the data (the caller then stores it raw).
std::vector<uint8_t> Compress(const void* in, size_t inSize);

// Decompress `inSize` bytes from `in` into exactly `outSize` bytes at `out`. Returns true on success.
bool Decompress(const void* in, size_t inSize, void* out, size_t outSize);

} // namespace SaveStateCompress

#endif // SAVESTATE_COMPRESS_H
