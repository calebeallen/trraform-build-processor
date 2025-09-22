#pragma once

#include <vector>
#include <cstdint>
#include <cmath>

namespace BuildImage {

    inline constexpr size_t IMG_WIDTH = 1024;
    inline constexpr size_t IMG_HEIGHT = 1024;

    inline constexpr float CAMERA_THETA = M_PI*0.25f;
    inline constexpr float CAMERA_PHI = M_PI*0.5f;
    inline constexpr float CAMERA_R_SCALAR = 2.5f;

    inline constexpr float FOV = 70.0f;
    inline constexpr float NEAR = 1.0f;
    inline constexpr float FAR = 100.0f;

    inline constexpr float LIGHT_INTENSITY = 1.8f;

    std::vector<std::uint8_t> make(const std::vector<std::uint16_t>&);  

}
