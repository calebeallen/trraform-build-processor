#pragma once

#include <vector>
#include <cstdint>
#include <utility>

namespace Chunk {

    std::string makeIdStr(const uint64_t, const uint64_t, const bool);
    std::pair<uint64_t,uint64_t> parseIdStr(const std::string&);

    const std::vector<uint32_t>& mapFwd(int, size_t);
    uint32_t mapBwd(int, size_t);
    uint32_t plotIdToPosIdx(uint32_t);

}