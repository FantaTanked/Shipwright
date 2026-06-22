#include "SkeletonLimb.h"

namespace SOH {
SkeletonLimbData* SkeletonLimb::GetPointer() {
    return &limbData;
}

size_t SkeletonLimb::GetPointerSize() {
    switch (limbType) {
        case LimbType::Standard:
            return sizeof(limbData.standardLimb);
        case LimbType::LOD:
            return sizeof(limbData.lodLimb);
        case LimbType::Skin:
            return sizeof(limbData.skinLimb);
        case LimbType::Curve:
            return sizeof(limbData.skelCurveLimb);
        case LimbType::Invalid:
        case LimbType::Legacy:
        default:
            return 0;
    }
}

// Only Skin (deformable-mesh) limbs have separate sub-allocations: the SkinLimbModif array, and each modif's
// skinVertices / limbTransformations arrays. Standard/LOD/Curve limbs keep everything in the embedded struct.
std::vector<std::pair<void*, size_t>> SkeletonLimb::GetSubAllocations() {
    std::vector<std::pair<void*, size_t>> subs;
    if (limbType != LimbType::Skin) {
        return subs;
    }
    subs.emplace_back(skinLimbModifArray.data(), skinLimbModifArray.size() * sizeof(SkinLimbModif));
    for (auto& v : skinLimbModifVertexArrays) {
        subs.emplace_back(v.data(), v.size() * sizeof(SkinVertex));
    }
    for (auto& t : skinLimbModifTransformationArrays) {
        subs.emplace_back(t.data(), t.size() * sizeof(SkinTransformation));
    }
    return subs;
}
} // namespace SOH
