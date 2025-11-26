#pragma once

#include <vector>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <cstdint>

#include <nlohmann/json.hpp>
#include <opencv2/core.hpp>

#include "utils_/serial.hpp"

std::unordered_map<uint64_t, std::vector<uint8_t>> Serial::decode_chunk(
    const std::vector<uint8_t>& data,
    const std::unordered_set<uint64_t>& ignore_keys
) {
    std::unordered_map<uint64_t, std::vector<uint8_t>> parts;
    const uint8_t* data_ptr = data.data() + 2;
    const uint8_t* const end = data.data() + data.size();
    while (data_ptr < end) {
        // read id (64 bit int little endian)
        uint64_t id;
        std::memcpy(&id, data_ptr, sizeof(uint64_t));
        data_ptr += sizeof(uint64_t);

        // read part len metadata (32 bit int little endian)
        uint32_t part_size;
        std::memcpy(&part_size, data_ptr, sizeof(uint32_t));
        data_ptr += sizeof(uint32_t);

        if (!ignore_keys.contains(id)) {
            std::vector<uint8_t> part(part_size);
            std::memcpy(part.data(), data_ptr, part_size);
            parts.emplace(id, std::move(part));
        }
        data_ptr += part_size;
    }
    return parts;
};

std::vector<uint8_t> Serial::encode_chunk(const std::unordered_map<uint64_t, std::vector<uint8_t>>& parts) {
    // encode parts
    size_t size = 2; // reserve first 2 bytes (version)
    for(auto& [key, part] : parts)
        size += sizeof(uint64_t) + sizeof(uint32_t) + part.size();

    std::vector<uint8_t> data(size);
    data[0] = data[1] = 0;
    uint8_t* data_ptr = data.data() + 2;
    for(auto& [id, part] : parts){
        // set id
        std::memcpy(data_ptr, &id, sizeof(uint64_t));
        data_ptr += sizeof(uint64_t);

        // set part len metadata
        uint32_t partLen = part.size();
        std::memcpy(data_ptr, &partLen, sizeof(uint32_t));
        data_ptr += sizeof(uint32_t);

        // set part
        std::memcpy(data_ptr, part.data(), part.size());
        data_ptr += partLen;
    }
    return data;
};

// std::unordered_map<std::string, PointCloud> decode_point_cloud(const std::vector<uint8_t>& data);
// PointCloud decode_single_point_cloud(const std::vector<uint8_t>& data, float sample);
// std::vector<uint8_t> encode_point_cloud(const std::unordered_map<std::string, PointCloud>&& point_clouds);

// nlohmann::json get_default_json();
// nlohmann::json get_json(const std::vector<uint8_t>& data);
// std::span<const uint8_t> get_default_build_view();
// std::span<const uint8_t> get_build_view(const std::vector<uint8_t>& data);
// std::vector<uint8_t> encode_plot(const nlohmann::json& json, const std::span<const uint8_t>& build);
// PointCloud decode_build(const std::vector<uint8_t>& data, float sample);
