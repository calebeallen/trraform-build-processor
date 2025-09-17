#pragma once

#include <string>

#include <opencv2/core.hpp>

namespace Utils {

    cv::Vec3f idxToVec3(const int idx, const int bs);
    cv::Vec4f idxToVec4(const int idx, const int bs);
    void loadENV(const std::string& path);

}