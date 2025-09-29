#include <sstream>
#include <fstream>
#include <array>
#include <vector>
#include <utility>
#include <cstdint>

#include <fmt/format.h>

#include "chunk/chunk.hpp"
#include "config/config.hpp"

std::string Chunk::makeIdStr(const uint64_t idl, const uint64_t idr, const bool isLayer){
    return fmt::format("{}{:x}_{:x}", isLayer ? "l":"", idl, idr);
}

std::pair<uint64_t,uint64_t> Chunk::parseIdStr(const std::string& id){

    std::istringstream ss(id);
    std::string idlHex, idrHex;
    getline(ss, idlHex, '_');
    getline(ss, idrHex, '_');

    // for low res chunks remove layer prefix
    if(idlHex[0] == 'l')
        idlHex.erase(0,1);
       
    return std::pair<uint64_t,uint64_t>(stoll(idlHex, nullptr, 16), stoll(idrHex, nullptr, 16));

}

const std::vector<uint32_t>& Chunk::mapFwd(int layer, size_t idx) {

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

uint32_t Chunk::mapBwd(int layer, size_t idx) {

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

uint32_t Chunk::plotIdToPosIdx(uint32_t idx) {

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
