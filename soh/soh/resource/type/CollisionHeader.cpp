#include "CollisionHeader.h"

namespace SOH {
CollisionHeaderData* CollisionHeader::GetPointer() {
    return &collisionHeaderData;
}

size_t CollisionHeader::GetPointerSize() {
    return sizeof(collisionHeaderData);
}

// The header struct's pointer fields (vtxList/polyList/surfaceTypeList/cameraDataList/waterBoxes, + each
// CamData's camPosData) are wired to these separately-allocated vectors. Report them at FIXED positions so the
// save/load sub-allocation indices line up regardless of which are empty (the savestate layer skips size-0).
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