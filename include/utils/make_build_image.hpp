#pragma once

#include <span>
#include <vector>
#include <cstdint>

namespace BuildImage {

    std::vector<uint8_t> make(const std::span<uint16_t>&);  

}
