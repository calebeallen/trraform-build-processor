#pragma once

#include <vector>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <cstdint>

#include <nlohmann/json.hpp>
#include <opencv2/core.hpp>

namespace Serial {
    std::unordered_map<uint64_t, std::vector<uint8_t>> decode_chunk(
        const std::vector<uint8_t>& data,
        const std::unordered_set<uint64_t>& ignore_keys
    );
    std::vector<uint8_t> encode_chunk(const std::unordered_map<uint64_t, std::vector<uint8_t>>& parts);

    struct PointCloud {
        cv::Mat points;
        std::vector<uint16_t> colors;
    };
    std::unordered_map<uint64_t, PointCloud> decode_point_cloud(const std::vector<uint8_t>& data);
    PointCloud decode_single_point_cloud(
        const std::vector<uint8_t>& data, 
        float sample
    );
    std::vector<uint8_t> encode_point_cloud(const std::unordered_map<uint64_t, PointCloud>&& point_clouds);

    nlohmann::json get_default_json();
    nlohmann::json get_json(const std::vector<uint8_t>& data);
    std::span<const uint8_t> get_default_build_view();
    std::span<const uint8_t> get_build_view(const std::vector<uint8_t>& data);
    std::vector<uint8_t> encode_plot(
        const nlohmann::json& json, 
        const std::span<const uint8_t>& build
    );
    PointCloud decode_build(
        const std::vector<uint8_t>& data,
        float sample
    );
}