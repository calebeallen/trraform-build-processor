
#include <fstream>
#include <array>
#include <vector>

#include "chunk/chunk_map.hpp"
#include "config/config.hpp"

const std::vector<uint32_t>& ChunkMap::fwd(int layer, size_t idx) {

    if(layer == 0){
        static const std::vector<uint32_t> map = []() {
            std::vector<uint32_t> map;
            map.reserve(VARS::L0_SIZE);
            for(int i = 0; i < VARS::L0_SIZE; ++i)
                map.push_back(i);
            return map;
        }();
        return map;
    }

    if(layer == 1){
        static const std::array<std::vector<uint32_t>, VARS::L0_SIZE> map = []() {
            std::array<std::vector<uint32_t>, VARS::L0_SIZE> map;

            std::ifstream file("static/cmap_l1.dat", std::ios::binary);
            if (!file) 
                throw std::runtime_error("Failed to open cmap_l1.dat");

            uint32_t buf[2];
            while (file.read(reinterpret_cast<char*>(buf), sizeof(buf)))
                map[buf[0]].push_back(buf[1]);

            return map;
        }();
        return map[idx];
    }
    
    if(layer == 2){
        static const std::array<std::vector<uint32_t>, VARS::L1_SIZE> map = []() {
            std::array<std::vector<uint32_t>, VARS::L1_SIZE> map;

            std::ifstream file("static/cmap_l2.dat", std::ios::binary);
            if (!file) 
                throw std::runtime_error("Failed to open cmap_l2.dat");

            uint32_t buf[2];
            int i = 1;
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

uint32_t bwd(int layer, size_t idx) {
    if (layer == 0)
        return 0;
    
    if (layer == 1) {
        static const std::array<uint32_t, VARS::L1_SIZE> map = []() {
            std::array<uint32_t, VARS::L1_SIZE> map;
            std::ifstream file("static/cmap_l1.dat", std::ios::binary);
            if (!file) 
                throw std::runtime_error("Failed to open cmap_l1.dat");

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
            if (!file) 
                throw std::runtime_error("Failed to open cmap_l2.dat");

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

uint32_t plotIdToPosIdx(uint32_t idx) {

    static const std::array<uint32_t, VARS::L2_SIZE> map = []() {
        std::array<uint32_t, VARS::L2_SIZE> map;
        std::ifstream file("static/cmap_l2.dat", std::ios::binary);
        if (!file) 
            throw std::runtime_error("Failed to open cmap_l2.dat");

        uint32_t buf[2];
        size_t i = 0;
        while (file.read(reinterpret_cast<char*>(buf), sizeof(buf))) {
            map[i] = buf[1];
            ++i;
        }

        return map;
    }();

    return map[idx];
}
