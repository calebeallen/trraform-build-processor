#pragma once

#include <cstdint>
#include <optional>

#include <opencv2/core.hpp>

namespace ColorLib {

    std::optional<cv::Mat> getColor(const size_t);
    std::optional<cv::Vec3f> getColorAsVec(const size_t);

}