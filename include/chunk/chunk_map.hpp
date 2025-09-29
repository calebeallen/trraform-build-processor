#pragma once

#include <vector>
#include <cstdint>

namespace ChunkMap {

    const std::vector<uint32_t>& fwd(int, size_t);
    uint32_t bwd(int, size_t);
    uint32_t plotIdToPosIdx(uint32_t);

}