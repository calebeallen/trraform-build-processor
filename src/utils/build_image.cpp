#include <vector>
#include <span>
#include <cstdint>
#include <limits>
#include <optional>
#include <cmath>
#include <iostream>

#include <opencv2/core.hpp>
#include <opencv2/imgcodecs.hpp>

#include "utils/build_image.hpp"
#include "config/config.hpp"
#include "utils/color_lib.hpp"
#include "utils/utils.hpp"

static const cv::Vec3f UP{0.0f, 1.0f, 0.0f};

static const cv::Vec3f FACE_NORMALS[6]{
    { 0.0f,  1.0f,  0.0f},
    { 0.0f, -1.0f,  0.0f},
    {-1.0f,  0.0f,  0.0f},
    { 1.0f,  0.0f,  0.0f},
    { 0.0f,  0.0f,  1.0f},
    { 0.0f,  0.0f, -1.0f}
};

static const size_t FACE_IDX[6][4] = {
    {3, 2, 6, 7},
    {0, 4, 5, 1},
    {0, 3, 7, 4},
    {1, 5, 6, 2},
    {4, 7, 6, 5},
    {0, 1, 2, 3}
};

static const cv::Vec4f VERT_OFFSET[8] = {
    {0.0f, 0.0f, 0.0f, 0.0f},
    {1.0f, 0.0f, 0.0f, 0.0f},
    {1.0f, 1.0f, 0.0f, 0.0f},
    {0.0f, 1.0f, 0.0f, 0.0f},
    {0.0f, 0.0f, 1.0f, 0.0f},
    {1.0f, 0.0f, 1.0f, 0.0f},
    {1.0f, 1.0f, 1.0f, 0.0f},
    {0.0f, 1.0f, 1.0f, 0.0f}
};

static void rasterize(
    cv::Mat& rgb, 
    std::vector<float>& zbuf, 
    const cv::Vec3f& a, 
    const cv::Vec3f& b, 
    const cv::Vec3f& c,
    const cv::Vec3b& color
) {

    // compute face bounds
    int minX = std::max(0, (int)std::floor(std::min({a[0], b[0], c[0]})));
    int maxX = std::min(static_cast<int>(BuildImage::IMG_WIDTH - 1), (int)std::ceil (std::max({a[0], b[0], c[0]})));
    int minY = std::max(0, (int)std::floor(std::min({a[1], b[1], c[1]})));
    int maxY = std::min(static_cast<int>(BuildImage::IMG_HEIGHT - 1), (int)std::ceil (std::max({a[1], b[1], c[1]})));

    // precompute edge function denominator (area * 2)
    float denom = (b[1] - c[1]) * (a[0] - c[0]) + (c[0] - b[0]) * (a[1] - c[1]);
    if (std::fabs(denom) < 1e-8f) 
        return;

    // rasterize
    for (int y = minY; y <= maxY; ++y) {
        for (int x = minX; x <= maxX; ++x) {

            // barycentric coordinates
            float w0 = ( (b[1] - c[1]) * (x - c[0]) + (c[0] - b[0]) * (y - c[1]) ) / denom;
            float w1 = ( (c[1] - a[1]) * (x - c[0]) + (a[0] - c[0]) * (y - c[1]) ) / denom;
            float w2 = 1.0f - w0 - w1;

            // bounds test
            if (w0 < 0.f || w1 < 0.f || w2 < 0.f)
                continue;

            // interpolated depth
            float z = w0 * a[2] + w1 * b[2] + w2 * c[2];
            int idx = y * BuildImage::IMG_WIDTH + x;

            if (z >= 0 && z < zbuf[idx]) {
                zbuf[idx] = z;
                auto& px = rgb.at<cv::Vec3b>(y, x);
                px = color; // white in BGR
            }
        }
    }
}

static void vmin(cv::Vec3f& a, const cv::Vec4f& b) {
    for (int i = 0; i < 3; ++i) 
        a[i] = std::min(a[i], b[i]);
}

static void vmax(cv::Vec3f& a, const cv::Vec4f& b) {
    for (int i = 0; i < 3; ++i) 
        a[i] = std::max(a[i], b[i]);
}

// light-weight renderer
std::vector<std::uint8_t> BuildImage::make(const std::vector<std::uint16_t>& buildData) {

    const float aspect = static_cast<float>(IMG_WIDTH) / IMG_HEIGHT;
    const int bs = buildData[1];
    const float fmin = std::numeric_limits<float>::lowest();
    const float fmax = std::numeric_limits<float>::max();

    // get points & bounds
    cv::Vec3f min(fmax,fmax,fmax), max(fmin,fmin,fmin);
    std::vector<cv::Vec4f> points;
    std::vector<cv::Vec3f> colors;

    const size_t bdLen = buildData.size();
    points.reserve(bdLen - 2);
    colors.reserve(bdLen - 2);

    std::optional<cv::Vec3f> col;

    // buffers
    std::vector<float> zbuf(IMG_WIDTH * IMG_HEIGHT, 1.0f);
    cv::Mat rgb(IMG_HEIGHT, IMG_WIDTH, CV_8UC3);
    rgb.setTo(cv::Vec3b(27, 24, 24));

    if (bdLen - 2 == 0) {
        std::vector<uint8_t> png;
        cv::imencode(".png", rgb, png, {cv::IMWRITE_PNG_COMPRESSION, 3});
        return png;
    }

    int idx = 0;
    for (size_t i = 2; i < bdLen; ++i) {
        if (buildData[i] & 1) {
            col = ColorLib::getColorAsVec(buildData[i] >> 1);
            if (col) {
                cv::Vec4f u(Utils::idxToVec4(idx, bs));
                vmin(min, u);
                vmax(max, u);
                points.emplace_back(u);
                colors.emplace_back(*col);
            }
            ++idx;
        } else {
            int repeat = buildData[i] >> 1;
            if (col) {
                for(int j = 0; j < repeat; ++j){
                    cv::Vec4f u(Utils::idxToVec4(idx + j, bs));
                    vmin(min, u);
                    vmax(max, u);
                    points.emplace_back(u); 
                    colors.emplace_back(*col);
                }
            }
            idx += repeat;
        }
    }

    for(int i = 0; i < 3; ++i)
        max[i] += 1;

    float scale = cv::norm(max - min); 

    // create view matrix
    cv::Vec3f campos(
        scale * std::sin(CAMERA_PHI) * std::cos(CAMERA_THETA),
        scale * std::cos(CAMERA_PHI),
        scale * std::sin(CAMERA_PHI) * std::sin(CAMERA_THETA)
    );

    cv::Vec3f f = cv::normalize(-campos); // forward
    cv::Vec3f s = cv::normalize(f.cross(UP)); // right
    cv::Vec3f u = s.cross(f); // up
    cv::Matx44f view(
        s[0],  s[1],  s[2], -s.dot(campos),
        u[0],  u[1],  u[2], -u.dot(campos),
       -f[0], -f[1], -f[2],  f.dot(campos),
        0.0f,  0.0f,  0.0f,           1.0f
    );
    
    // create projection matrix
    float g = 1.f / std::tan(FOV * M_PI / 90.0f);
    cv::Matx44f proj = cv::Matx44f::zeros();
    proj(0,0) = g / aspect;
    proj(1,1) = g;
    proj(2,2) = (FAR + NEAR) / (NEAR - FAR);
    proj(2,3) = (2.f * FAR * NEAR) / (NEAR - FAR);
    proj(3,2) = -1.f;
    
    // apply view matrix
    proj = proj * view;

    // light vector
    cv::Vec3f light(cv::normalize(cv::Vec3f(
        static_cast<float>(std::cos(43.0 * M_PI / 180.0)),
        static_cast<float>(std::cos(45.0 * M_PI / 180.0)),
        static_cast<float>(std::cos(47.0 * M_PI / 180.0))
    )));
    light *= LIGHT_INTENSITY * 0xFF;

    cv::Vec3f target(min);
    target += max;
    target *= 0.5f;

    float projMinX = fmax, projMaxX = fmin;
    float projMinY = fmax, projMaxY = fmin;

    for (size_t i = 0; i < points.size(); ++i) {
        auto& p = points[i];
        p[0] -= target[0];
        p[1] -= target[1];
        p[2] -= target[2];

        for (size_t j = 0; j < 8; ++j) {
            cv::Vec4f u = p + VERT_OFFSET[j];
            u = proj * u;

            float x = (u[0] / u[3] * 0.5f);
            float y = (u[1] / u[3] * 0.5f);

            projMinX = std::min(projMinX, x);
            projMaxX = std::max(projMaxX, x);
            projMinY = std::min(projMinY, y);
            projMaxY = std::max(projMaxY, y);
        }
    }

    float dx = (projMinX + projMaxX) * 0.5f;
    float dy = (projMinY + projMaxY) * 0.5f;
    
    // draw faces for each voxel
    for (size_t i = 0; i < points.size(); ++i) {

        auto& p = points[i];
        
        // project vertices to screen space
        cv::Vec3f v[8];
        for (size_t j = 0; j < 8; ++j) {
            cv::Vec4f u = p + VERT_OFFSET[j];
            u = proj * u;
            v[j] = cv::Vec3f(
                (u[0] / u[3] * 0.5f + 0.5f - dx) * BuildImage::IMG_WIDTH,
                (u[1] / u[3] * 0.5f + 0.5f - dy) * BuildImage::IMG_HEIGHT,
                0.5f * u[2] / u[3]+ 0.5f
            );
        }

        // rasterize
        for (size_t j = 0; j < 6; ++j) {
            // apply lighting
            const auto& cf = colors[i];
            float l = light[j/2];
            cv::Vec3b c(
                cv::saturate_cast<uint8_t>(std::abs(cf[2] * l)),
                cv::saturate_cast<uint8_t>(std::abs(cf[1] * l)),
                cv::saturate_cast<uint8_t>(std::abs(cf[0] * l))
            );

            size_t i0 = FACE_IDX[j][0];
            size_t i1 = FACE_IDX[j][1];
            size_t i2 = FACE_IDX[j][2];
            size_t i3 = FACE_IDX[j][3];
            
            rasterize(rgb, zbuf, v[i0], v[i1], v[i2], c);
            rasterize(rgb, zbuf, v[i0], v[i2], v[i3], c);
        }
    }

    // png encoding
    std::vector<uint8_t> png;
    cv::imencode(".png", rgb, png, {cv::IMWRITE_PNG_COMPRESSION, 3}); // 0=none, 9=max
    return png;
}