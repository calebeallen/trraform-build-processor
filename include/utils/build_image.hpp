#pragma once

#include <span>
#include <vector>
#include <cstdint>

namespace BuildImage {

    inline constexpr size_t IMG_WIDTH = 1024;
    inline constexpr size_t IMG_HEIGHT = 1024;
    inline constexpr float CAMERA_THETA = 0;
    inline constexpr float CAMERA_PHI = 0;
    inline constexpr float CAMERA_R_SCALAR = 0;
    inline constexpr float FOV = 70;
    inline constexpr float NEAR = 1;
    inline constexpr float FAR = 100;

    inline constexpr float LIGHT_INTENSITY = 1.8f;

    std::vector<uint8_t> make(const std::span<uint16_t>&);  

}
