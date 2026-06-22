#include "Animation.h"

namespace SOH {
AnimationData* Animation::GetPointer() {
    return &animationData;
}

size_t Animation::GetPointerSize() {
    switch (type) {
        case AnimationType::Normal:
            return sizeof(animationData.animationHeader);
        case AnimationType::Link:
            return sizeof(animationData.linkAnimationHeader);
        case AnimationType::Curve:
            return sizeof(animationData.transformUpdateIndex);
        case AnimationType::Legacy:
        default:
            return 0;
    }
}

// AnimationHeader.frameData/jointIndices (Normal) and TransformUpdateIndex.refIndex/transformData/copyValues
// (Curve) point into these separately-allocated vectors. Fixed positions; empties are skipped by the savestate
// layer. (Link/Player animations are the separate PlayerAnimation resource type, a Pattern-1 main payload.)
std::vector<std::pair<void*, size_t>> Animation::GetSubAllocations() {
    return {
        { rotationValues.data(), rotationValues.size() * sizeof(uint16_t) },
        { rotationIndices.data(), rotationIndices.size() * sizeof(RotationIndex) },
        { refIndexArr.data(), refIndexArr.size() * sizeof(uint8_t) },
        { transformDataArr.data(), transformDataArr.size() * sizeof(TransformData) },
        { copyValuesArr.data(), copyValuesArr.size() * sizeof(int16_t) },
    };
}
} // namespace SOH