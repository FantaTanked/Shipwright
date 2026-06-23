#ifndef SAVESTATE_COMPRESS_H
#define SAVESTATE_COMPRESS_H

#include <cstddef>
#include <cstdint>
#include <vector>

// Thin zlib compress/decompress over StormLib's SComp API, kept in its own translation unit so StormLib.h (and
// the platform headers it drags in) don't leak into savestates.cpp -- same isolation reasoning as
// savestate_filedialog. Used to shrink the savestate disk blob: it's ~8.5 MiB but roughly half zeros (the
// system + audio heaps are mostly unused at any instant), so zlib takes it to ~1-2% of its size.
namespace SaveStateCompress {

// Compress `inSize` bytes from `in`. Returns the compressed bytes, or an EMPTY vector if compression failed or
// did not shrink the data (the caller then stores it raw).
std::vector<uint8_t> Compress(const void* in, size_t inSize);

// Decompress `inSize` bytes from `in` into exactly `outSize` bytes at `out`. Returns true on success.
bool Decompress(const void* in, size_t inSize, void* out, size_t outSize);

} // namespace SaveStateCompress

#endif // SAVESTATE_COMPRESS_H
