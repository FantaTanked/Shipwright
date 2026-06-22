#include "Path.h"

namespace SOH {
PathData* Path::GetPointer() {
    return pathData.data();
}

size_t Path::GetPointerSize() {
    return pathData.size() * sizeof(PathData);
}

// Each pathData[k].points is wired to paths[k].data() -- one separate vector per path.
std::vector<std::pair<void*, size_t>> Path::GetSubAllocations() {
    std::vector<std::pair<void*, size_t>> subs;
    subs.reserve(paths.size());
    for (auto& p : paths) {
        subs.emplace_back(p.data(), p.size() * sizeof(Vec3s));
    }
    return subs;
}
} // namespace SOH
