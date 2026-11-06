#pragma once
#include <cstdint>

namespace pocket {
// Replacement may briefly use a smaller working margin, but only when the old
// editor PCM can be freed at commit. Never borrow against a synth-owned asset.
inline bool canStageSample(unsigned freeBytes,unsigned requested,unsigned reclaimable=0) {
    return uint64_t(freeBytes)>=uint64_t(requested)+16384
        &&uint64_t(freeBytes)+reclaimable>=uint64_t(requested)+32768;
}
}
