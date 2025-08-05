#pragma once

#include <opencv2/core.hpp>

namespace Utils {

    inline cv::Vec3f idxToVec3(const int idx, const int bs) {
        const int bs2 = bs * bs;
        return cv::Vec3f{
            float(idx % bs),
            float(idx / bs2),
            float((idx % bs2) / bs)
        };
    };

    inline cv::Vec4f idxToVec4(const int idx, const int bs) {
        const int bs2 = bs * bs;
        return cv::Vec4f{
            float(idx % bs),
            float(idx / bs2),
            float((idx % bs2) / bs),
            1.0
        };
    };

}