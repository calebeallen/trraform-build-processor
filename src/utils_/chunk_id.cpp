
#include <sstream>
#include <fstream>
#include <array>
#include <vector>
#include <utility>
#include <cstdint>
#include <cassert>
#include <stdexcept>
#include <string>
#include <optional>

#include <fmt/format.h>

#include "utils_/chunk_id.hpp"
#include "config/config.hpp"

const std::vector<uint32_t>& ChunkId::map_fwd(size_t layer, size_t idx) {

    if (layer == 0) {
        static const std::vector<uint32_t> map = []() {
            std::vector<uint32_t> map;
            map.reserve(VARS::L0_SIZE);
            for(int i = 0; i < VARS::L0_SIZE; ++i)
                map.push_back(i);
            return map;
        }();
        return map;
    }

    if (layer == 1) {
        static const std::array<std::vector<uint32_t>, VARS::L0_SIZE> map = []() {
            std::array<std::vector<uint32_t>, VARS::L0_SIZE> map;
            std::ifstream file("static/cmap_l1.dat", std::ios::binary);

            assert(file && "Failed to open cmap_l1.dat");

            uint32_t buf[2];
            while (file.read(reinterpret_cast<char*>(buf), sizeof(buf)))
                map[buf[0]].push_back(buf[1]);

            return map;
        }();
        return map[idx];
    }
    
    if (layer == 2) {
        static const std::array<std::vector<uint32_t>, VARS::L1_SIZE> map = []() {
            std::array<std::vector<uint32_t>, VARS::L1_SIZE> map;
            std::ifstream file("static/cmap_l2.dat", std::ios::binary);

            assert(file && "Failed to open cmap_l2.dat");

            uint32_t buf[2];
            uint32_t i = 1;
            while (file.read(reinterpret_cast<char*>(buf), sizeof(buf))) {
                map[buf[0]].push_back(i);
                ++i;
            }

            return map;
        }();
        return map[idx];
    }

    throw std::runtime_error("Invalid layer");
}

uint32_t ChunkId::map_bwd(size_t layer, size_t idx) {

    if (layer == 0)
        return 0;
    
    if (layer == 1) {
        static const std::array<uint32_t, VARS::L1_SIZE> map = []() {
            std::array<uint32_t, VARS::L1_SIZE> map;
            std::ifstream file("static/cmap_l1.dat", std::ios::binary);

            assert(file && "Failed to open cmap_l1.dat");

            uint32_t buf[2];
            while (file.read(reinterpret_cast<char*>(buf), sizeof(buf)))
                map[buf[1]] = buf[0];

            return map;
        }();
        return map[idx];
    }

    if (layer == 2) {
        static const std::array<uint32_t, VARS::L2_SIZE> map = []() {
            std::array<uint32_t, VARS::L2_SIZE> map;
            std::ifstream file("static/cmap_l2.dat", std::ios::binary);

            assert(file && "Failed to open cmap_l2.dat");

            uint32_t buf[2];
            size_t i = 0;
            while (file.read(reinterpret_cast<char*>(buf), sizeof(buf))) {
                map[i] = buf[0];
                ++i;
            }

            return map;
        }();
        return map[idx];
    }

    throw std::runtime_error("Invalid layer");
}

ChunkId::ChunkId(const std::string& chunk_id) : chunk_id_(chunk_id) {
    std::istringstream ss(chunk_id);
    std::string idl_hex, idr_hex;
    getline(ss, idl_hex, '_');
    getline(ss, idr_hex, '_');

    if (idl_hex[0] == 'l') {
        idl_hex.erase(0,1);
        lod_chunk_ = true;
    }

    idl_ = std::stoull(idl_hex, nullptr, 16);
    idr_ = std::stoull(idr_hex, nullptr, 16);
}

const std::string& ChunkId::get_string() {
    return chunk_id_;
}

std::optional<std::string> ChunkId::get_parent() {
    if (idl_ == 0 || !lod_chunk_) // for now
        return std::nullopt;

    const uint32_t next_idr = map_bwd(idl_ - 1, idr_);
    return fmt::format("l{:x}_{:x}",idl_ - 1, next_idr);
}