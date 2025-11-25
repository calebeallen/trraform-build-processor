#include <vector>
#include <span>
#include <cstdint>
#include <limits>
#include <optional>
#include <cmath>
#include <iostream>
#include <chrono>

#include <opencv2/core.hpp>
#include <opencv2/imgcodecs.hpp>

#include "utils/build_image.hpp"
#include "config/config.hpp"
#include "utils/color_lib.hpp"
#include "utils/utils.hpp"

static const cv::Vec3f UP{0.0f, 1.0f, 0.0f};

// Included strictly to prevent undefined errors, though your rasterizer
// uses the light vector logic instead.
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

    // OPTIMIZATION: Quick return if out of bounds
    if (minX > maxX || minY > maxY) return;

    // precompute edge function denominator (area * 2)
    float denom = (b[1] - c[1]) * (a[0] - c[0]) + (c[0] - b[0]) * (a[1] - c[1]);
    
    // OPTIMIZATION: Back-face culling 
    // If denom is negative (or near zero), the face is looking away.
    // Note: If you see missing faces, remove the "< 0" check and keep "fabs < 1e-8".
    if (denom <= 0.0f) 
        return;

    float invDenom = 1.0f / denom;

    // OPTIMIZATION: Precompute constant deltas for barycentric steps
    // w0 = (b1 - c1)(x - c0) + (c0 - b0)(y - c1)
    float dy0 = b[1] - c[1];
    float dx0 = c[0] - b[0];
    float c0_const = dy0 * (-c[0]) + dx0 * (-c[1]);

    float dy1 = c[1] - a[1];
    float dx1 = a[0] - c[0];
    float c1_const = dy1 * (-c[0]) + dx1 * (-c[1]);

    // OPTIMIZATION: Use raw pointers for row access
    // This avoids (y * WIDTH) multiplication and bounds checking per pixel
    size_t row_idx = minY * BuildImage::IMG_WIDTH;

    for (int y = minY; y <= maxY; ++y) {
        
        cv::Vec3b* row_ptr = rgb.ptr<cv::Vec3b>(y);
        float* z_ptr = zbuf.data() + row_idx;

        // Precompute Y-part of barycentric coords
        float w0_row = dy0 * minX + dx0 * y + c0_const;
        float w1_row = dy1 * minX + dx1 * y + c1_const;

        for (int x = minX; x <= maxX; ++x) {
            
            // Optimization: use cached row values
            float w0 = w0_row * invDenom;
            float w1 = w1_row * invDenom;
            float w2 = 1.0f - w0 - w1;

            if (w0 >= 0.f && w1 >= 0.f && w2 >= 0.f) {
                float z = w0 * a[2] + w1 * b[2] + w2 * c[2];
                
                // Direct pointer access
                if (z >= 0 && z < z_ptr[x]) {
                    z_ptr[x] = z;
                    row_ptr[x] = color; 
                }
            }

            // Increment X contribution
            w0_row += dy0;
            w1_row += dy1;
        }
        row_idx += BuildImage::IMG_WIDTH;
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

std::vector<std::uint8_t> BuildImage::make(const std::vector<std::uint16_t>& buildData) {
    auto start = std::chrono::high_resolution_clock::now();

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
    cv::Matx44f vp = proj * view; // Combined matrix

    // light vector (Original calculation preserved)
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

    // OPTIMIZATION: Manual Matrix Multiplication
    // Using cv::Mat multiplication inside a tight loop is very slow due to checking overhead.
    // We pre-extract matrix components to floats.
    float m00 = vp(0,0), m01 = vp(0,1), m02 = vp(0,2), m03 = vp(0,3);
    float m10 = vp(1,0), m11 = vp(1,1), m12 = vp(1,2), m13 = vp(1,3);
    float m30 = vp(3,0), m31 = vp(3,1), m32 = vp(3,2), m33 = vp(3,3);

    // Pass 1: Compute bounds
    // Note: We cannot easily skip this pass without changing the centering logic
    for (size_t i = 0; i < points.size(); ++i) {
        auto& p = points[i];
        
        // Manually apply translation (p - target)
        float px = p[0] - target[0];
        float py = p[1] - target[1];
        float pz = p[2] - target[2];

        for (size_t j = 0; j < 8; ++j) {
            // Apply offset + Transform manually
            float vx = px + VERT_OFFSET[j][0];
            float vy = py + VERT_OFFSET[j][1];
            float vz = pz + VERT_OFFSET[j][2];

            // u = vp * v
            float ux = m00*vx + m01*vy + m02*vz + m03;
            float uy = m10*vx + m11*vy + m12*vz + m13;
            float uw = m30*vx + m31*vy + m32*vz + m33; // u[3]

            float invW = 1.0f / uw;
            float x = (ux * invW * 0.5f);
            float y = (uy * invW * 0.5f);

            projMinX = std::min(projMinX, x);
            projMaxX = std::max(projMaxX, x);
            projMinY = std::min(projMinY, y);
            projMaxY = std::max(projMaxY, y);
        }
    }

    float dx = (projMinX + projMaxX) * 0.5f;
    float dy = (projMinY + projMaxY) * 0.5f;
    
    // Pass 2: Draw
    for (size_t i = 0; i < points.size(); ++i) {
        auto& p = points[i];
        
        float px = p[0] - target[0];
        float py = p[1] - target[1];
        float pz = p[2] - target[2];

        cv::Vec3f v[8];
        for (size_t j = 0; j < 8; ++j) {
            float vx = px + VERT_OFFSET[j][0];
            float vy = py + VERT_OFFSET[j][1];
            float vz = pz + VERT_OFFSET[j][2];

            float ux = m00*vx + m01*vy + m02*vz + m03;
            float uy = m10*vx + m11*vy + m12*vz + m13;
            float uz = vp(2,0)*vx + vp(2,1)*vy + vp(2,2)*vz + vp(2,3); // Only need Z now
            float uw = m30*vx + m31*vy + m32*vz + m33;

            float invW = 1.0f / uw;
            
            v[j][0] = (ux * invW * 0.5f + 0.5f - dx) * BuildImage::IMG_WIDTH;
            v[j][1] = (uy * invW * 0.5f + 0.5f - dy) * BuildImage::IMG_HEIGHT;
            v[j][2] = 0.5f * uz * invW + 0.5f;
        }

        // rasterize
        const auto& cf = colors[i];
        
        for (size_t j = 0; j < 6; ++j) {
            // apply lighting (Original calculation preserved)
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

    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
    std::cout << "render time: " << duration.count() << " ms" << std::endl;

    start = std::chrono::high_resolution_clock::now();
    std::vector<uint8_t> png;
    cv::imencode(".png", rgb, png, {cv::IMWRITE_PNG_COMPRESSION, 3}); 
    end = std::chrono::high_resolution_clock::now();
    duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
    std::cout << "compress time: " << duration.count() << " ms" << std::endl;
    return png;
}