#include <vector>
#include <span>
#include <cstdint>
#include <limits>

#include <opencv2/core.hpp>
#include <opencv2/imgcodecs.hpp>

#include "build_image.hpp"
#include "color_lib.hpp"
#include "utils.hpp"

static const cv::Vec3f UP{0.0f, 1.0f, 0.0f};

static const cv::Vec3f FACE_NORMALS[6]{
    { 0.0f,  1.0f,  0.0f},
    { 0.0f, -1.0f,  0.0f},
    {-1.0f,  0.0f,  0.0f},
    { 1.0f,  0.0f,  0.0f},
    { 0.0f,  0.0f,  1.0f},
    { 0.0f,  0.0f, -1.0f}
};

static const cv::Vec4f FACE_VERTS[6][4]{
    { {0.0f,1.0f,0.0f,1.0f}, {1.0f,1.0f,0.0f,1.0f}, {1.0f,1.0f,1.0f,1.0f}, {0.0f,1.0f,1.0f,1.0f} },
    { {0.0f,0.0f,0.0f,1.0f}, {0.0f,0.0f,1.0f,1.0f}, {1.0f,0.0f,1.0f,1.0f}, {1.0f,0.0f,0.0f,1.0f} },
    { {0.0f,0.0f,0.0f,1.0f}, {0.0f,1.0f,0.0f,1.0f}, {0.0f,1.0f,1.0f,1.0f}, {0.0f,0.0f,1.0f,1.0f} },
    { {1.0f,0.0f,0.0f,1.0f}, {1.0f,0.0f,1.0f,1.0f}, {1.0f,1.0f,1.0f,1.0f}, {1.0f,1.0f,0.0f,1.0f} },
    { {0.0f,0.0f,1.0f,1.0f}, {0.0f,1.0f,1.0f,1.0f}, {1.0f,1.0f,1.0f,1.0f}, {1.0f,0.0f,1.0f,1.0f} },
    { {0.0f,0.0f,0.0f,1.0f}, {1.0f,0.0f,0.0f,1.0f}, {1.0f,1.0f,0.0f,1.0f}, {0.0f,1.0f,0.0f,1.0f} }
};

static cv::Vec3f ndcToScreen(const cv::Vec4f& v) {
    return cv::Vec3f{
        (v[0] * 0.5f + 0.5f) * BuildImage::IMG_WIDTH,
        (1.0f - (v[1] * 0.5f + 0.5f)) * BuildImage::IMG_HEIGHT,
        v[2]
    };
}

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

            if (z < zbuf[idx]) {
                zbuf[idx] = z;
                auto& px = rgb.at<cv::Vec3b>(y, x);
                px = color; // white in BGR
            }
        }
    }
}


// light-weight renderer
std::vector<uint8_t> BuildImage::make(const std::span<uint16_t>& buildData) {

    const float aspect = static_cast<float>(IMG_WIDTH) / IMG_HEIGHT;
    const int bs = buildData[1];
    const float fmin = std::numeric_limits<float>::lowest();
    const float fmax = std::numeric_limits<float>::max();

    // get points & bounds
    cv::Vec4f min{fmax,fmax,fmax,1.0f}, max{fmin,fmin,fmin,1.0f};
    cv::Vec3f col;
    std::vector<cv::Vec4f> points;
    std::vector<cv::Vec3f> colors;
    const size_t bdLen = buildData.size();
    int idx = 0;
    for (size_t i = 2; i < bdLen; ++i) {
        if (buildData[i] & 1) {   
            col = ColorLib::getColorAsVec(buildData[i] >> 1);

            cv::Vec4f u(Utils::idxToVec4(idx, bs));
            min = cv::min(min, u);
            max = cv::max(max, u);

            points.push_back(u);
            colors.push_back(col);
            ++idx;
        } else {
            int repeat = buildData[i] >> 1;
            for(int j = 0; j < repeat; ++j){
                cv::Vec4f u(Utils::idxToVec4(idx + j, bs));
                min = cv::min(min, u);
                max = cv::max(max, u);

                points.push_back(u); 
                colors.push_back(col);
            }
            idx += repeat;
        }
    }

    // create view matrix
    cv::Vec3f campos{
        CAMERA_R_SCALAR * std::sinf(CAMERA_PHI) * std::cosf(CAMERA_THETA),
        CAMERA_R_SCALAR * std::cosf(CAMERA_PHI),
        CAMERA_R_SCALAR * std::sinf(CAMERA_PHI)
    };
    cv::Vec3f target{0.0f, 0.0f, 0.0f};
    cv::Vec3f f = cv::normalize(target - campos); // forward
    cv::Vec3f s = cv::normalize(f.cross(UP)); // right
    cv::Vec3f u = s.cross(f); // up
    cv::Matx44f view{
        s[0],  s[1],  s[2], -s.dot(campos),
        u[0],  u[1],  u[2], -u.dot(campos),
       -f[0], -f[1], -f[2], -f.dot(campos),
        0.0f,  0.0f,  0.0f,            1.0f
    };
    
    // create projection matrix
    float g = 1.f / std::tan(FOV * 0.5f);
    cv::Matx44f proj = cv::Matx44f::zeros();
    proj(0,0) = g / aspect;
    proj(1,1) = g;
    proj(2,2) = (FAR + NEAR) / (NEAR - FAR);
    proj(2,3) = (2.f * FAR * NEAR) / (NEAR - FAR);
    proj(3,2) = -1.f;
    
    // apply view matrix
    proj = proj * view;
    
    // buffers
    std::vector<float> zbuf(IMG_WIDTH * IMG_HEIGHT, fmax);
    cv::Mat rgb(IMG_HEIGHT, IMG_WIDTH, CV_8UC3);

    // draw faces for each voxel
    for(const auto& u : points){
        for(size_t i = 0; i < 6; ++i){
            // back face culling
            if(f.dot(FACE_NORMALS[i]) >= 0.0f)
                continue;

            const auto& v = FACE_VERTS[i];

            cv::Vec4f v0(u + v[0]);
            cv::Vec4f v1(u + v[1]);
            cv::Vec4f v2(u + v[2]);
            cv::Vec4f v3(u + v[3]);

            v0 = proj * v0;
            v1 = proj * v1;
            v2 = proj * v2;
            v3 = proj * v3;

            // convert to screen space
            auto vs0 = ndcToScreen(v0 / v0[3]);
            auto vs1 = ndcToScreen(v1 / v1[3]);
            auto vs2 = ndcToScreen(v2 / v2[3]);
            auto vs3 = ndcToScreen(v3 / v3[3]);

            // compute lighting


            rasterize(rgb, zbuf, vs0, vs1, vs2);
            rasterize(rgb, zbuf, vs0, vs2, vs3);
        }
    }

    // jpeg encoding
    std::vector<uint8_t> jpg; 
    cv::imencode(".jpg", rgb, jpg, {cv::IMWRITE_JPEG_QUALITY, 90}); 
    return jpg;
}