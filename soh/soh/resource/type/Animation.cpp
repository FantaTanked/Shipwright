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

// The animation header's data fields point into these separately-allocated vectors; report them so the
// savestate layer relocates them after a restart. Positions are fixed; the layer skips any that are empty.
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