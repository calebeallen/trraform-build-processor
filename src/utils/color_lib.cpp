#include <cstdint>
#include <vector>
#include <optional>

#include <opencv2/core.hpp>

#include "utils/color_lib.hpp"
#include "config/config.hpp"

static cv::Vec3f lerp(const cv::Vec3f& a, const cv::Vec3f& b, const float alpha) {

    cv::Vec3f c;
    cv::addWeighted(a, alpha, b, 1.0 - alpha, 0.0, c);
    return c;

}   

std::optional<cv::Mat> ColorLib::getColor(const size_t idx) {

    constexpr int OFFSET = VARS::PLOT_COUNT + 1;

    static const cv::Mat palette = []() {

        constexpr int HPB = 8;
        constexpr int HUES = HPB*6;
        constexpr int GS = 25;
        constexpr int GS2 = GS*GS;

        cv::Vec3f white(1.0, 1.0, 1.0);
        cv::Vec3f black(0.0, 0.0, 0.0);
        
        std::vector<cv::Vec3f> palette;
        
        // greyscale
        for (int i = 0; i < GS2; ++i)
            palette.emplace_back(lerp(white, black, static_cast<float>(i) / GS2));
        
        // other colors
        int c1i = 0, c2i = 0;
        for (int i = 0; i < 6; ++i) {
            cv::Vec3f c1(0.0f, 0.0f, 0.0f), c2(0.0f, 0.0f, 0.0f);
            c1[c1i % 3] = 1.0f;
            c2[c2i % 3] = 1.0f;

            if (i & 1) {
                ++c1i;
                c1[c1i % 3] = 1.0f;
            } else {
                ++c2i;
                c2[c2i % 3] = 1.0f;
            }

            for (int h = 0; h < HPB; ++h) {
                const cv::Vec3f base = lerp(c1, c2, static_cast<float>(h) / HPB);
                for (int s = 0; s < GS; ++s)
                    for (int r = 0; r < GS; ++r) {
                        const float x = static_cast<float>(r + 1) / (GS + 2);
                        const float y = static_cast<float>(s) / GS;
                        palette.emplace_back(lerp(lerp(base, white, x), black, y));
                    }
            }

        }

        cv::Mat m(static_cast<int>(palette.size()), 3, CV_32F);        // rows×3, own storage
        std::memcpy(m.data, palette.data(), palette.size() * sizeof(cv::Vec3f));
        return m;    
    }();

    if(idx < OFFSET)
        return std::nullopt;

    return palette.row(idx - OFFSET);

}

std::optional<cv::Vec3f> ColorLib::getColorAsVec(const size_t idx) {

    const auto& m = getColor(idx);
    if (!m)
        return std::nullopt;

    return m->at<cv::Vec3f>(0, 0);

}