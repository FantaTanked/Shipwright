#include "Scene.h"

namespace SOH {
void* Scene::GetPointer() {
    // Scene is a special type that requries C++ processing. As such, we return nothing.
    return nullptr;
}

size_t Scene::GetPointerSize() {
    return 0;
}

// A Scene has no single payload; its data lives in command sub-objects whose pointers the game caches. Report
// each command's payload plus its sub-allocations so they relocate after a restart; command order is stable.
std::vector<std::pair<void*, size_t>> Scene::GetSubAllocations() {
    std::vector<std::pair<void*, size_t>> subs;
    for (auto& cmd : commands) {
        if (cmd == nullptr) {
            continue;
        }
        void* p = cmd->GetRawPointer();
        if (p != nullptr) {
            subs.emplace_back(p, cmd->GetPointerSize());
        }
        for (auto& s : cmd->GetSubAllocations()) {
            subs.push_back(s);
        }
    }
    return subs;
}
} // namespace SOH
