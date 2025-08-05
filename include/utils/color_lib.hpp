#pragma once

#include <cstdint>
#include <opencv2/core.hpp>

namespace ColorLib {

    const cv::Mat getColor(const size_t);
    const cv::Vec3f getColorAsVec(const size_t);

}