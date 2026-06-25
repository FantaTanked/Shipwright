#include "CollisionHeader.h"

namespace SOH {
CollisionHeaderData* CollisionHeader::GetPointer() {
    return &collisionHeaderData;
}

size_t CollisionHeader::GetPointerSize() {
    return sizeof(collisionHeaderData);
}

// The header's pointer fields point into these separately-allocated vectors; report them so the savestate
// layer relocates them after a restart. Positions are fixed; the layer skips any that are empty.
std::vector<std::pair<void*, size_t>> CollisionHeader::GetSubAllocations() {
    return {
        { vertices.data(), vertices.size() * sizeof(Vec3s) },
        { polygons.data(), polygons.size() * sizeof(CollisionPoly) },
        { surfaceTypes.data(), surfaceTypes.size() * sizeof(SurfaceType) },
        { camData.data(), camData.size() * sizeof(CamData) },
        { camPosData.data(), camPosData.size() * sizeof(Vec3s) },
        { waterBoxes.data(), waterBoxes.size() * sizeof(WaterBox) },
    };
}
} // namespace SOH